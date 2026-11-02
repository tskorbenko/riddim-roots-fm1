/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DUB FX LAB master bus. Audio ISR, internal SRAM, fixed point, no division
 * or allocation per sample. Shared building blocks: one master history ring
 * (stutter, tape stop, rewind, chorus, flanger) and one half-rate tape echo
 * (sound-system voices, master echo, feedback burst). */
#include "rr_fx.h"
#include <string.h>

const char *const rr_fx_names[RR_FX] = {
    "TAPE ECHO", "DIGI DELAY", "SPRING REV", "DUB CHAMBER", "HIGH-PASS", "LOW-PASS", "BAND-PASS", "PHASER",
    "FLANGER", "CHORUS", "SATURATION", "DISTORTION", "BITCRUSH", "TAPE STOP", "REWIND", "DUB SIREN",
    "AIRHORN", "LASER", "NOISE BURST", "STUTTER", "FEEDBACK", "BASS DROP", "MUTE", "ECHO THROW"
};
enum { RING = 16384, ECHO = 16384, CMDS = 16 };
static int16_t ring[RING] __attribute__((aligned(4)));    /* master history, 44.1 kHz */
static int16_t echo[ECHO] __attribute__((aligned(4)));    /* tape echo, 22.05 kHz */
static volatile int32_t param[RR_FXP];
static volatile uint32_t cmd_write;
static uint32_t cmd_read;
static volatile uint16_t cmd_data[CMDS];                   /* fx | on << 7 | velocity << 8 */

/* Sine table filled at init (control task), read in audio from internal RAM. */
static int16_t sintab[256];
static struct {
    uint32_t phase, inc, lfo, lfo_inc, t, len, rate;
    int32_t env, target, z;
    uint8_t active, hold, wave, step;
} v_siren, v_horn, v_laser, v_noise, v_drop;
static struct {
    int32_t lp[2], bp[2], ap[2][4], apx[2][4], hold[2], dc_x[2], dc_y[2], echo_lp, echo_hp, tone;
    uint32_t w, ew, lfo, crush_n, noise, frozen_at, pos_q8, gain, stut, xfade;
    int32_t speed;   /* Q16 read speed for tape stop / rewind, negative = rewind */
    uint8_t mode;    /* 0 live, 1 tape stop, 2 rewind, 3 stutter */
    uint8_t release;
    uint32_t fade;
} st;
static volatile struct rr_fx_status status;

static inline __attribute__((always_inline)) int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
static inline __attribute__((always_inline)) int32_t mul15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline __attribute__((always_inline)) int32_t tri(uint32_t ph) { int32_t v = (int32_t)(ph >> 16) - 32768; return (v < 0 ? -v : v) * 2 - 32768; }
static inline __attribute__((always_inline)) int32_t sin15(uint32_t ph) { return sintab[ph >> 24]; }

void rr_fx_init(void)
{
    /* Bhaskara I sine approximation (error < 0.2 %), no libm needed. */
    for (unsigned i = 0; i < 256; ++i) {
        int32_t p = (int32_t)(i & 127), n = 16 * p * (128 - p), d = 81920 - 4 * p * (128 - p);
        int32_t y = (int32_t)(32767LL * n / d);
        sintab[i] = (int16_t)(i < 128 ? y : -y);
    }
    memset(ring, 0, sizeof ring); memset(echo, 0, sizeof echo);
    memset(&st, 0, sizeof st);
    st.noise = 0x1234567u; st.gain = 32768;
    param[FXP_FILTER] = 64; param[FXP_MASTER] = 110; param[FXP_ECHO_SAMPLES] = 9000; param[FXP_ECHO_FB] = 70;
    param[FXP_WARMTH] = 40; param[FXP_RESONANCE] = 30; param[FXP_SIREN_RATE] = 40; param[FXP_STEP_SAMPLES] = 9187;
    param[FXP_INSERT] = FX_PHASER;
}
void rr_fx_param(unsigned p, int value) { if (p < RR_FXP) param[p] = value; }
void rr_fx_trigger(unsigned fx, unsigned on, unsigned velocity)
{
    uint32_t w = cmd_write;
    if (w - cmd_read >= CMDS) return;                      /* audio will catch up */
    cmd_data[w & (CMDS - 1)] = (uint16_t)((fx & 127U) | (on ? 128U : 0U) | ((velocity & 127U) << 8));
    __asm__ volatile("" ::: "memory");
    cmd_write = w + 1;
}
void rr_fx_status(struct rr_fx_status *s) { s->blocks = status.blocks; s->peak = status.peak; s->reduction = status.reduction; s->voices = status.voices; s->clips = status.clips; }

static const uint32_t HZ = 97391;   /* phase increment per Hz at 44.1 kHz */
static FM1_AUDIO_CODE void command(unsigned fx, unsigned on, unsigned vel)
{
    int32_t level = 9000 + (int32_t)vel * 160;
    switch (fx) {
    case FX_SIREN:
        if (on) { v_siren.active = 1; v_siren.hold = 1; v_siren.target = level; v_siren.wave = (uint8_t)param[FXP_SIREN_WAVE]; }
        else v_siren.hold = 0;
        break;
    case FX_AIRHORN: if (on) { v_horn.active = 1; v_horn.t = 0; v_horn.target = level; v_horn.phase = 0; v_horn.lfo = 0x40000000u; v_horn.z = 0; } break;
    case FX_LASER: if (on) { v_laser.active = 1; v_laser.t = 0; v_laser.step = 0; v_laser.inc = 2200 * HZ; v_laser.target = level * 3 / 4; } break;
    case FX_NOISE: if (on) { v_noise.active = 1; v_noise.env = level; v_noise.z = 0; v_noise.inc = 26000; } break;
    case FX_BASSDROP: if (on) { v_drop.active = 1; v_drop.env = level + 6000; v_drop.inc = 110 * HZ; v_drop.phase = 0; v_drop.t = 0; } break;
    case FX_TAPESTOP:
    case FX_REWIND:
    case FX_STUTTER:
        if (on && !st.mode) {
            st.mode = (uint8_t)(fx == FX_TAPESTOP ? 1 : fx == FX_REWIND ? 2 : 3);
            st.frozen_at = st.w; st.pos_q8 = 0; st.stut = 0; st.xfade = 0;
            st.speed = fx == FX_REWIND ? -65536 : 65536; st.release = 0;
        } else if (!on && st.mode) st.release = 1;
        break;
    case FX_FEEDBACK: if (on) st.fade = 44100; break;
    }
}
static FM1_AUDIO_CODE int32_t voices(void)
{
    int32_t out = 0;
    unsigned count = 0;
    if (v_siren.active) {
        ++count;
        int32_t bend = param[FXP_BEND];
        uint32_t lfo_inc = (uint32_t)(param[FXP_SIREN_RATE] + 4) * 9000u;
        v_siren.lfo += lfo_inc;
        int32_t mod;
        switch (v_siren.wave & 3) {
        case 0: mod = tri(v_siren.lfo) / 3; break;                          /* wail */
        case 1: mod = (v_siren.lfo & 0x80000000u) ? 16384 : 0; break;        /* two-tone */
        case 2: mod = 16384 - (int32_t)(v_siren.lfo >> 17); break;           /* whoop */
        default: mod = sin15(v_siren.lfo) / 2; break;
        }
        uint32_t base = (uint32_t)(520 + bend / 24) * HZ;
        v_siren.phase += base + (uint32_t)(((int64_t)base * mod) >> 15);
        int32_t osc = (v_siren.phase & 0x80000000u) ? 12000 : -12000;      /* square, softened below */
        osc = (osc + tri(v_siren.phase) / 3);
        v_siren.z += (osc - v_siren.z) >> 2;
        int32_t goal = v_siren.hold ? v_siren.target : 0;
        v_siren.env += (goal - v_siren.env) >> 9;
        out += mul15(v_siren.z, v_siren.env);
        if (!v_siren.hold && v_siren.env < 16) v_siren.active = 0;
    }
    if (v_horn.active) {
        /* "bwaap bwaap bwaaaap": 140 ms on, 70 off, 140 on, 70 off, 520 on. */
        static FM1_AUDIO_DATA const uint16_t edges[6] = {6174, 9261, 15435, 18522, 41454, 47000};
        uint32_t t = v_horn.t++;
        unsigned seg = 0;
        while (seg < 6 && t >= edges[seg]) ++seg;
        if (seg >= 6) v_horn.active = 0;
        else {
            ++count;
            int32_t goal = (seg & 1) ? 0 : v_horn.target;
            v_horn.env += (goal - v_horn.env) >> 6;
            uint32_t inc = 372 * HZ - (seg == 4 ? (t - 18522) * 3 : 0);
            v_horn.phase += inc; v_horn.lfo += inc + inc / 97; v_horn.inc += inc - inc / 89;
            int32_t saw = ((int32_t)(v_horn.phase >> 17) + (int32_t)(v_horn.lfo >> 17) + (int32_t)(v_horn.inc >> 17)) - 49152;
            v_horn.z += ((saw / 2) - v_horn.z) >> 1;
            out += mul15(v_horn.z, v_horn.env);
        }
    }
    if (v_laser.active) {
        ++count;
        v_laser.inc = (uint32_t)(((uint64_t)v_laser.inc * 65520u) >> 16);
        v_laser.phase += v_laser.inc;
        int32_t sq = (v_laser.phase & 0x80000000u) ? 9000 : -9000;
        out += mul15(sq, v_laser.target);
        if (++v_laser.t > 7900) {
            v_laser.t = 0; v_laser.inc = 2200 * HZ;
            if (++v_laser.step >= 3) v_laser.active = 0;
        }
    }
    if (v_noise.active) {
        ++count;
        st.noise ^= st.noise << 13; st.noise ^= st.noise >> 17; st.noise ^= st.noise << 5;
        int32_t n = (int32_t)(st.noise >> 17) - 16384;
        v_noise.z += mul15(n - v_noise.z, (int32_t)v_noise.inc);
        if (v_noise.inc > 1500) v_noise.inc -= 1;                            /* darkening sweep */
        out += mul15(v_noise.z, v_noise.env);
        v_noise.env = (int32_t)(((int64_t)v_noise.env * 65531) >> 16);
        if (v_noise.env < 8) v_noise.active = 0;
    }
    if (v_drop.active) {
        ++count;
        v_drop.inc = (uint32_t)(((uint64_t)v_drop.inc * (16777216u - 294u)) >> 24);
        if (v_drop.inc < 30 * HZ) v_drop.inc = 30 * HZ;
        v_drop.phase += v_drop.inc;
        out += mul15(sin15(v_drop.phase), v_drop.env) / 2;
        if (++v_drop.t > 2000) v_drop.env = (int32_t)(((int64_t)v_drop.env * 65532) >> 16);
        if (v_drop.env < 8) v_drop.active = 0;
    }
    status.voices = count;
    return out;
}
static FM1_AUDIO_CODE int32_t ring_read(uint32_t pos_q8)
{
    uint32_t i = pos_q8 >> 8, f = pos_q8 & 255;
    int32_t a = ring[i & (RING - 1)], b = ring[(i + 1) & (RING - 1)];
    return a + (((b - a) * (int32_t)f) >> 8);
}
static FM1_AUDIO_CODE void svf(int ch, int32_t *x, int32_t f, int32_t q, unsigned type)
{
    int32_t low = st.lp[ch], band = st.bp[ch], high = 0;
    for (unsigned k = 0; k < 2; ++k) {                       /* 2x oversampled Chamberlin */
        low += mul15(f, band);
        high = *x - low - mul15(q, band);
        band += mul15(f, high);
        band = sat16(band * 1);
        low = low > 98304 ? 98304 : low < -98304 ? -98304 : low;
    }
    st.lp[ch] = low; st.bp[ch] = band;
    *x = type == 0 ? low : type == 1 ? high : band;
}
FM1_AUDIO_CODE void rr_fx_process(int32_t *out)
{
    if (!out) return;
    while (cmd_read != cmd_write) {
        uint16_t c = cmd_data[cmd_read & (CMDS - 1)];
        __asm__ volatile("" ::: "memory");
        ++cmd_read;
        command(c & 127U, c & 128U, c >> 8);
    }
    ++status.blocks;
    const int32_t filter = param[FXP_FILTER], insert = param[FXP_INSERT], shed = param[FXP_SHED];
    int32_t amount = param[FXP_AMOUNT] * 258;                            /* 0..32766 */
    int32_t chaos = param[FXP_CHAOS];
    /* Filter: 0..63 low-pass 180 Hz..11 kHz, 65..127 high-pass 40 Hz..2.8 kHz. */
    int32_t expression = param[FXP_EXPRESSION];
    int32_t fpos = filter < 64 ? filter + expression / 4 : filter;
    if (filter < 64 && fpos > 63) fpos = 63;
    unsigned ftype = filter < 64 ? 0 : 1, fon = filter < 62 || filter > 66;
    if (insert == FX_HPF || insert == FX_LPF || insert == FX_BPF) {
        if (amount) { fon = 1; ftype = insert == FX_LPF ? 0 : insert == FX_HPF ? 1 : 2; fpos = ftype == 1 ? 64 + param[FXP_AMOUNT] / 2 : 63 - param[FXP_AMOUNT] / 2; }
    }
    int32_t fc = ftype == 1 ? 40 + (fpos - 64) * (fpos - 64) * 2 / 3 : 180 + fpos * fpos * 3;
    int32_t fcoef = (int32_t)(fc * 2334 / 1000);                         /* ~2*pi*fc/88200 in Q15 */
    if (fcoef > 26000) fcoef = 26000;
    int32_t q = 32768 - param[FXP_RESONANCE] * 200;
    if (q < 9000) q = 9000;
    const int32_t warm = param[FXP_WARMTH], master = param[FXP_MASTER] * 258;
    const uint32_t echo_len = (uint32_t)param[FXP_ECHO_SAMPLES] / 2U;      /* half-rate line */
    int32_t fb = param[FXP_ECHO_FB] * 236;                               /* <= 0.92 */
    if (st.fade) { fb = 31500; st.fade = st.fade > FM1_BLOCK_FRAMES ? st.fade - FM1_BLOCK_FRAMES : 0; }
    const int32_t echo_wet = param[FXP_ECHO_WET] * 258;
    uint32_t peak = 0;
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        int32_t l = out[2 * i] >> 8, r = out[2 * i + 1] >> 8;            /* Q15 working range */
        int32_t mono = (l + r) >> 1;
        if (st.mode < 2) ring[st.w++ & (RING - 1)] = (int16_t)sat16(mono);   /* rewind/stutter freeze */
        st.lfo += 18000u + (uint32_t)chaos * 900u;
        if (fon) { svf(0, &l, fcoef, q, ftype); svf(1, &r, fcoef, q, ftype); }
        if (amount && !shed) {
            int32_t wl = l, wr = r;
            switch (insert) {
            case FX_PHASER: {
                int32_t a = 20000 + tri(st.lfo) / 3;
                for (unsigned s = 0; s < 4; ++s) {
                    int32_t yl = mul15(-a, wl) + st.apx[0][s] + mul15(a, st.ap[0][s]);
                    st.apx[0][s] = wl; st.ap[0][s] = sat16(yl); wl = st.ap[0][s];
                    int32_t yr = mul15(-a, wr) + st.apx[1][s] + mul15(a, st.ap[1][s]);
                    st.apx[1][s] = wr; st.ap[1][s] = sat16(yr); wr = st.ap[1][s];
                }
                wl = (l + wl) >> 1; wr = (r + wr) >> 1;
                break;
            }
            case FX_FLANGER: case FX_CHORUS: {
                uint32_t base = insert == FX_FLANGER ? 80 : 900, depth = insert == FX_FLANGER ? 70 : 260;
                uint32_t dl = base + (uint32_t)(((int64_t)(tri(st.lfo) + 32768) * depth) >> 16);
                uint32_t dr = base + (uint32_t)(((int64_t)(tri(st.lfo + 0x40000000u) + 32768) * depth) >> 16);
                wl = (l + ring_read((st.w - dl) << 8)) >> 1;
                wr = (r + ring_read((st.w - dr) << 8)) >> 1;
                break;
            }
            case FX_SATURATE: wl = sat16(l * 2); wl = wl - mul15(mul15(wl, wl), wl) / 3; wr = sat16(r * 2); wr = wr - mul15(mul15(wr, wr), wr) / 3; break;
            case FX_DISTORT: wl = sat16(l * 6) / 2; wr = sat16(r * 6) / 2; break;
            case FX_CRUSH: {
                unsigned hold = 1 + (unsigned)param[FXP_AMOUNT] / 10, bits = 6 + (127 - (unsigned)param[FXP_AMOUNT]) / 14;
                if (++st.crush_n >= hold) { st.crush_n = 0; st.hold[0] = l; st.hold[1] = r; }
                int32_t m = ~((1 << (16 - bits)) - 1);
                wl = st.hold[0] & m; wr = st.hold[1] & m;
                break;
            }
            case FX_TAPE_ECHO: wl = l; wr = r; break;                    /* routed via the echo send */
            default: break;
            }
            l = l + mul15(wl - l, amount); r = r + mul15(wr - r, amount);
        }
        /* Sound-system voices and the half-rate tape echo. */
        int32_t fx = voices();
        int32_t send = fx + (insert == FX_TAPE_ECHO ? mul15(mono, amount) : 0) + mul15(mono, echo_wet);
        if (i & 1) {
            uint32_t rd = st.ew - echo_len - (uint32_t)(tri(st.lfo >> 3) >> 12);
            int32_t e = echo[rd & (ECHO - 1)];
            st.echo_lp += (e - st.echo_lp) >> 1;                         /* tape darkening */
            st.echo_hp += (st.echo_lp - st.echo_hp) >> 7;                /* keep lows from piling up */
            int32_t back = st.echo_lp - st.echo_hp;
            int32_t rec = send + mul15(back, fb);
            rec = rec > 24000 ? 24000 + ((rec - 24000) >> 2) : rec < -24000 ? -24000 + ((rec + 24000) >> 2) : rec;
            echo[st.ew++ & (ECHO - 1)] = (int16_t)sat16(rec);
            st.tone = back;
        }
        l += fx + st.tone; r += fx + st.tone;
        /* Tape stop / rewind / stutter read the frozen history. */
        if (st.mode) {
            int32_t h;
            if (st.mode == 3) {                                      /* stutter: loop one step */
                uint32_t len = (uint32_t)param[FXP_STEP_SAMPLES];
                if (len > RING - 64) len = RING - 64;
                if (len < 64) len = 64;
                h = ring[(st.frozen_at - len + st.stut) & (RING - 1)];
                if (++st.stut >= len) st.stut = 0;
            } else if (st.mode == 1) {                               /* tape stop: lag grows */
                if (st.speed > 0) st.speed -= 3;                     /* ~0.5 s to a standstill */
                if (st.speed < 0) st.speed = 0;
                st.pos_q8 += (uint32_t)((65536 - st.speed) >> 8);
                h = st.speed && st.pos_q8 < ((RING - 4) << 8) ? ring_read((st.w << 8) - st.pos_q8) : 0;
            } else {                                                 /* rewind: read backwards */
                if (st.speed > -150000) st.speed -= 6;
                st.pos_q8 += (uint32_t)((-st.speed) >> 8);
                h = st.pos_q8 < ((RING - 4) << 8) ? ring_read((st.frozen_at << 8) - st.pos_q8) : 0;
            }
            int32_t mix = st.release ? (int32_t)(32768 - (st.xfade >> 1)) : 32768;
            l = mul15(h, mix) + mul15(l, 32768 - mix); r = mul15(h, mix) + mul15(r, 32768 - mix);
            if (st.release && (st.xfade += 256) >= 65536) { st.mode = 0; st.release = 0; st.xfade = 0; }
        }
        /* Back to Q23, tape warmth only above -6 dB, DC blocker. */
        int32_t ol = l << 8, or_ = r << 8;
        for (int ch = 0; ch < 2; ++ch) {
            int32_t x = ch ? or_ : ol;
            int32_t y = x - st.dc_x[ch] + (int32_t)(((int64_t)st.dc_y[ch] * 32604) >> 15);
            st.dc_x[ch] = x; st.dc_y[ch] = y;
            if (warm) {
                int32_t t = 4194304, a = y < 0 ? -y : y;
                if (a > t) { int32_t over = a - t; a = t + over - (int32_t)(((int64_t)over * over) >> 24) * warm / 160; y = y < 0 ? -a : a; }
            }
            y = (int32_t)(((int64_t)y * master) >> 15);
            uint32_t m = (uint32_t)(y < 0 ? -y : y);
            if (m > peak) peak = m;
            out[2 * i + ch] = y;
        }
    }
    /* Block limiter with one-block look-ahead gain ramp; ceiling 0.95 FS. */
    const uint32_t ceiling = 7969177u;
    uint32_t target = peak > ceiling ? (uint32_t)(((uint64_t)ceiling << 15) / peak) : 32768u;
    uint32_t g = st.gain ? st.gain : 32768u;
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        if (g > target) g = target;                                          /* instant attack */
        else if (g < target) g += (target - g + 255) >> 8;                   /* smooth release */
        for (int ch = 0; ch < 2; ++ch) {
            int64_t y = ((int64_t)out[2 * i + ch] * g) >> 15;
            if (y > 8388607) { y = 8388607; ++status.clips; } else if (y < -8388608) { y = -8388608; ++status.clips; }
            out[2 * i + ch] = (int32_t)y;
        }
    }
    st.gain = g;
    status.peak = peak; status.reduction = 32768u - target;
}
