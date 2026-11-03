/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Rasta UI: black ground, deep green, gold and red, white text. Frames are
 * composed one 16-row band at a time into two small DMA buffers; a band is
 * sent to the LCD only when its pixels changed, so the animated lion costs a
 * few bands per frame, not the whole screen. Audio never waits on the UI. */
#include "rr_ui.h"
#include "rr_app.h"
#include "rr_fx.h"
#include "instrument.h"
#include <string.h>
#include "rr_lion_atlas.h"

enum { W = 240, H = 240, BAND = 16, BANDS = H / BAND, FRAME_MS = 70 };
enum { BLACK = 0x0000, NIGHT = 0x0841, PANEL = 0x10a2, GREEN = 0x0467, LEAF = 0x2589, GOLD = 0xfe23,
       AMBER = 0xfd20, RED = 0xd042, WHITE = 0xffff, GREY = 0x8410, DIM = 0x4208, ICE = 0x969f };
static uint16_t band[2][W * BAND] __attribute__((aligned(4)));
static uint32_t band_hash[BANDS];
static uint16_t *buf;
static int y0;
static unsigned next_band, which, pending_band = 255, frame_ready;
static uint32_t last_frame, pixels_sent;

/* 5x7 glyphs, column-major, bit 0 at the top. Digits and capitals follow the
 * M-VAVE FM1 Open panel font (GPL-3.0, app/screen.c); symbols are new. */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const char sym_chars[] = ".-/:+%><*!?#'()=,_&";
static const uint8_t sym[][5] = {
    {0, 0x60, 0x60, 0, 0}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x60, 0x10, 0x08, 0x04, 0x03}, {0, 0x36, 0x36, 0, 0},
    {0x08, 0x08, 0x3e, 0x08, 0x08}, {0x63, 0x13, 0x08, 0x64, 0x63}, {0x41, 0x22, 0x14, 0x08, 0}, {0, 0x08, 0x14, 0x22, 0x41},
    {0x2a, 0x1c, 0x7f, 0x1c, 0x2a}, {0, 0, 0x5f, 0, 0}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x14, 0x7f, 0x14, 0x7f, 0x14},
    {0, 0, 0x07, 0, 0}, {0, 0x1c, 0x22, 0x41, 0}, {0, 0x41, 0x22, 0x1c, 0}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0, 0x80, 0x60, 0, 0}, {0x40, 0x40, 0x40, 0x40, 0x40}, {0x36, 0x49, 0x55, 0x22, 0x50}};

/* ---------- primitives (clipped to the current band) ---------- */
RR_SMALL static void fill(int x, int y, int w, int h, uint16_t c)
{
    int ya = y < y0 ? y0 : y, yb = y + h > y0 + BAND ? y0 + BAND : y + h;
    int xa = x < 0 ? 0 : x, xb = x + w > W ? W : x + w;
    for (int yy = ya; yy < yb; ++yy) { uint16_t *p = buf + (yy - y0) * W; for (int xx = xa; xx < xb; ++xx) p[xx] = c; }
}
static inline void dot(int x, int y, uint16_t c) { if (y >= y0 && y < y0 + BAND && x >= 0 && x < W) buf[(y - y0) * W + x] = c; }
RR_SMALL static const uint8_t *glyph(char ch)
{
    if (ch >= '0' && ch <= '9') return font[ch - '0'];
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return font[10 + ch - 'A'];
    for (unsigned i = 0; sym_chars[i]; ++i) if (sym_chars[i] == ch) return sym[i];
    return 0;
}
RR_SMALL static int text(int x, int y, const char *s, int scale, uint16_t c)
{
    if (y > y0 + BAND || y + 7 * scale < y0) { while (*s) { x += 6 * scale; ++s; } return x; }
    for (; *s; ++s, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int col = 0; col < 5; ++col) for (int row = 0; row < 8; ++row)
            if (g[col] >> row & 1) { if (scale == 1) dot(x + col, y + row, c); else fill(x + col * scale, y + row * scale, scale, scale, c); }
    }
    return x;
}
static int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }
static void frame(int x, int y, int w, int h, uint16_t c) { fill(x, y, w, 1, c); fill(x, y + h - 1, w, 1, c); fill(x, y, 1, h, c); fill(x + w - 1, y, 1, h, c); }
RR_SMALL static void meter(int x, int y, int w, int h, unsigned v, unsigned max, uint16_t c)
{
    int f = (int)(v * (unsigned)(w - 2) / (max ? max : 1));
    fill(x, y, w, h, PANEL); fill(x + 1, y + 1, f, h - 2, c);
}
RR_SMALL static char *num(char *o, int v)
{
    char t[12]; int n = 0, neg = v < 0;
    unsigned u = (unsigned)(neg ? -v : v);
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) *o++ = '-';
    while (n) *o++ = t[--n];
    *o = 0;
    return o;
}
static void cat(char *o, const char *a) { while (*o) ++o; while (*a) *o++ = *a++; *o = 0; }
static const char *const notes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* ---------- lion animation ---------- */
static struct {
    uint8_t mode, eyes, extra, frozen;
    int8_t hdx, hdy, bdy, tail, sx;
    uint8_t flag_amp, mane;
    uint32_t until, flag_phase, last_bass, bounce;
} lion;
enum { EYES_ART, EYES_OPEN, EYES_SLEEP, EYES_BLINK };
static uint8_t lion_ram[RR_LION_H * RR_LION_STRIDE];   /* decoded once from the RLE atlas */
unsigned rr_ui_lion_mode(void) { return lion.mode; }
uint32_t rr_ui_pixels_sent(void) { return pixels_sent; }
RR_SMALL static unsigned lion_px(int x, int y)
{
    if ((unsigned)x >= RR_LION_W || (unsigned)y >= RR_LION_H) return 0;
    return (lion_ram[y * RR_LION_STRIDE + (x >> 1)] >> ((x & 1) * 4)) & 15U;
}
static int head_zone(int x, int y) { return (x >= 60 && y < 60) || (x >= 56 && x <= 94 && y < 28); }
/* Composite source pixel for sprite-space (x, y) with the current pose. */
RR_SMALL static unsigned lion_sample(int x, int y, uint32_t now)
{
    int hx = x - lion.hdx, hy = y - lion.hdy - lion.bdy;
    if (head_zone(hx, hy)) {
        int mx = hx;
        if (lion.mane) { unsigned p = lion_px(hx, hy); if (p == 7 || p == 8 || p == 15) mx += ((hy + (int)(now >> 6)) & 1) ? 1 : -1; }
        unsigned p = lion_px(mx, hy);
        /* expressions */
        if (lion.eyes == EYES_OPEN) {
            int lx = hx - 76, ly = hy - 36, rx = hx - 88, ry = hy - 34;
            if ((lx >= -2 && lx <= 2 && ly >= -2 && ly <= 2) || (rx >= -2 && rx <= 2 && ry >= -2 && ry <= 2))
                return (lx >= 0 && lx <= 1 && ly >= 0 && ly <= 1) || (rx >= 0 && rx <= 1 && ry >= 0 && ry <= 1) ? 1 : 13;
        } else if (lion.eyes == EYES_SLEEP || lion.eyes == EYES_BLINK) {
            if ((hx >= 72 && hx <= 80 && hy >= 34 && hy <= 38) || (hx >= 85 && hx <= 91 && hy >= 32 && hy <= 36))
                return (hy == 37 && hx >= 73 && hx <= 79) || (hy == 35 && hx >= 86 && hx <= 90) ? 1 : 2;
        }
        if (p) return p;
    }
    int by = y - lion.bdy;
    if (head_zone(x, by) && !head_zone(hx, hy)) {
        /* uncovered by the moving head: show the mane/body behind */
        if (by < 28) return 0;
    }
    if (x < 53 && by >= 6 && by <= 40) {
        /* flag cloth waves away from the pole */
        int amp = lion.flag_amp * (53 - x) / 53;
        int phase = (int)((lion.flag_phase >> 4) + (unsigned)x * 6) & 255;
        int w = phase < 128 ? (phase < 64 ? phase : 128 - phase) : -(phase < 192 ? phase - 128 : 256 - phase);
        int sy = by - w * amp / 256;
        unsigned p = lion_px(x, sy);
        if (p == 1 || p == 2 || p == 3 || p == 4 || p == 5 || p == 10 || p == 11) return p;
        p = lion_px(x, by);
        return p == 6 || p == 7 || p == 14 || p == 15 || p == 8 ? p : 0;
    }
    if (x < 32 && by >= 36 && by <= 68) {
        int sx = x - lion.tail * (68 - by) / 32;
        unsigned p = lion_px(sx, by);
        if (p) return p;
    }
    if (head_zone(x, by)) return 0;
    return lion_px(x, by);
}
RR_SMALL static void draw_lion(int ox, int oy, int half, uint32_t now)
{
    int w = half ? RR_LION_W / 2 : RR_LION_W, h = half ? RR_LION_H / 2 : RR_LION_H;
    ox += lion.sx;
    int ya = oy < y0 ? y0 : oy, yb = oy + h > y0 + BAND ? y0 + BAND : oy + h;
    for (int y = ya; y < yb; ++y) for (int x = 0; x < w; ++x) {
        unsigned p = half ? lion_sample(x * 2, (y - oy) * 2, now) : lion_sample(x, y - oy, now);
        if (p) dot(ox + x, y, rr_lion_palette[p]);
    }
    /* overlays */
    if (lion.mode == L_SLEEP) {
        unsigned t = (now >> 9) % 3;
        text(ox + w - 18 + (int)t * 3, oy + 4 - (int)t * 4, "Z", half ? 1 : 2, WHITE);
        if (!half) text(ox + w - 6, oy - 6 + (int)t, "Z", 1, GREY);
    }
    if (lion.mode == L_FREEZE && !half) { text(ox + 2, oy + h - 10, "*", 2, ICE); text(ox + w - 12, oy + 2, "*", 1, ICE); }
    if ((lion.mode == L_FX || lion.extra == 3) && !half) {
        for (int k = 0; k < 3; ++k) { int r = 6 + k * 5 + (int)((now >> 7) & 3); fill(ox + 94 + r / 2, oy + 44 - r / 2, 2, r, k == 1 ? GOLD : GREEN); }
    }
    if (lion.mode == L_MIDI && !half) { text(ox + 70, oy - 2 - (int)rr.midi_vel / 32, "#", 1, GOLD); }
    if (lion.mode == L_MUTATE && !half) text(ox + 96, oy + 18, "!", 2, RED);
}
RR_SMALL static void lion_update(uint32_t now)
{
    unsigned ev = rr.lion_event;
    rr.lion_event = 0;
    if (ev == 1) { lion.mode = L_MUTATE; lion.until = now + 700; }
    else if (ev == 2) { lion.mode = L_FREEZE; lion.until = now + 1800; }
    else if (ev == 3) { lion.mode = L_FX; lion.until = now + 1300; }
    else if (ev == 5) { lion.mode = L_DROP; lion.until = now + 1500; }
    else if (ev == 7 && (lion.mode == L_PLAY || lion.mode == L_SLEEP || lion.mode == L_MIDI)) { lion.mode = L_MIDI; lion.until = now + 350; }
    else if (ev == 6) { lion.bounce = 3; }
    unsigned all = (1U << RR_COMPS) - 1;
    if (rr.fx_hold && lion.mode != L_MUTATE) { lion.mode = L_FX; lion.until = now + 200; }
    if ((int32_t)(now - lion.until) >= 0 && lion.mode != L_PLAY && lion.mode != L_SLEEP && lion.mode != L_DANCE) lion.mode = L_PLAY;
    if (rr.e.transition && (lion.mode == L_PLAY || lion.mode == L_DANCE)) lion.mode = L_DANCE;
    else if (lion.mode == L_DANCE) lion.mode = L_PLAY;
    if (lion.mode == L_PLAY && !rr.playing) lion.mode = L_SLEEP;
    if (lion.mode == L_SLEEP && rr.playing) lion.mode = L_PLAY;
    if ((rr.e.freeze & all) == all && rr.playing) lion.mode = L_FREEZE, lion.until = now + 100;
    if (lion.mode == L_FREEZE) return;                 /* freeze: pose latched exactly */
    unsigned step = rr.step, sub = step & 3U;
    if (rr.bass_hit != lion.last_bass) { lion.last_bass = rr.bass_hit; lion.bounce = 2; }
    lion.hdx = 0; lion.hdy = 0; lion.sx = 0; lion.eyes = EYES_ART; lion.mane = 0; lion.extra = 0;
    lion.flag_amp = rr.playing ? 10 : 4; lion.flag_phase += rr.playing ? 900 : 300;
    lion.tail = rr.playing ? (int8_t)((step & 4) ? 2 : -2) : 0;
    lion.bdy = (int8_t)(lion.bounce ? 1 : 0);
    if (lion.bounce) --lion.bounce;
    switch (lion.mode) {
    case L_PLAY: lion.hdy = (int8_t)(sub == 0 ? 2 : sub == 1 ? 1 : 0); if (((now >> 8) % 23) == 0) lion.eyes = EYES_OPEN; break;
    case L_SLEEP: lion.hdy = 3; lion.eyes = EYES_SLEEP; lion.flag_amp = 2; break;
    case L_MUTATE: lion.hdx = (int8_t)(((now >> 5) & 1) ? 2 : -2); lion.eyes = EYES_OPEN; lion.flag_amp = 16; break;
    case L_FX: lion.eyes = ((now >> 7) & 3) == 0 ? EYES_BLINK : EYES_ART; lion.flag_amp = 24; lion.flag_phase += 1600; lion.mane = 1; lion.extra = 3;
        lion.hdy = (int8_t)(sub == 0 ? 2 : 0); break;
    case L_DROP: lion.hdx = 2; lion.hdy = -2; lion.eyes = EYES_BLINK; lion.flag_amp = 6; break;
    case L_DANCE: lion.sx = (int8_t)((step & 8) ? 3 : -3); lion.hdx = (int8_t)((step & 4) ? 1 : -1); lion.hdy = (int8_t)(sub < 2 ? 2 : 0); lion.flag_amp = 20; lion.flag_phase += 1200; break;
    case L_MIDI: lion.hdy = (int8_t)-(1 + rr.midi_vel / 48); lion.eyes = rr.midi_vel > 100 ? EYES_OPEN : EYES_ART; break;
    }
}

/* ---------- page content ---------- */
RR_SMALL static void knob_text(unsigned k, char *label, char *value)
{
    static const char *const labels[RR_PAGES][4] = {
        {"EVOLVE", "ENERGY", "DUB", "FILTER"}, {"STYLE", "MODE", "BARS", "BPM RAMP"}, {"DIMENSION", "AMOUNT", "PART", "RATE"},
        {"PART", "LOCK", "ACTION", "DEPTH"}, {"ROLE", "SOUND", "LEVEL", "MUTE"}, {"KEY MODE", "SPLIT", "LIVE A", "LIVE B"},
        {"EFFECT", "AMOUNT", "FEEDBACK", "FILTER"}, {"COMMAND", "INTENSITY", "DLY SEND", "REV SEND"},
        {"LEARN", "BANK", "PICKUP", "HUMAN SEC"}, {"SLOT", "ACTION", "BPM MAX", "BPM MIN"}};
    static const char *const actions[6] = {"MUTATE", "KEEP", "REJECT", "REBUILD", "VERSION", "RESTORE"};
    static const char *const roles[RR_ROLES_UI] = {"DRUMS", "BASS", "SKANK", "ORGAN", "HORNS", "PIANO", "LIVE A", "LIVE B"};
    static const char *const dims[RR_DIMS + 1] = {"RHYTHM", "BASS", "HARMONY", "INSTR", "ARRANGE", "TIMBRE", "FX", "GLOBAL"};
    static const char *const kits[RR_KITS] = {"ROOTS KIT", "STEPPERS", "DIGITAL", "JUNGLE", "TECHNO", "DUBSTEP", "NOISE KIT", "SKA KIT"};
    unsigned p = rr.page;
    strcpy(label, labels[p][k]);
    value[0] = 0;
    struct rr_engine *e = &rr.e;
    switch (p * 4 + k) {
    case 0: num(value, e->evolve); break;
    case 1: num(value, e->energy); break;
    case 2: num(value, e->dub); break;
    case 3: case 27: strcpy(value, rr.mix.filter < 62 ? "LP " : rr.mix.filter > 66 ? "HP " : "OPEN"); if (rr.mix.filter < 62 || rr.mix.filter > 66) num(value + 3, rr.mix.filter); break;
    case 4: strcpy(value, rr_styles[rr.pending_style].name); value[8] = 0; break;
    case 5: strcpy(value, rr_tmode_names[rr.tmode]); break;
    case 6: num(value, rr.tbars); break;
    case 7: strcpy(value, rr.bpm_smooth ? "SMOOTH" : "INSTANT"); break;
    case 8: strcpy(value, dims[rr.sel[P_EVOLVE] % (RR_DIMS + 1)]); break;
    case 9: num(value, rr.sel[P_EVOLVE] == RR_DIMS ? e->evolve : e->dim[rr.sel[P_EVOLVE] % RR_DIMS]); break;
    case 10: case 12: strcpy(value, rr_comp_names[rr.sel[P_FREEZE] % RR_COMPS]); break;
    case 11: num(value, e->comp[rr.sel[P_FREEZE] % RR_MOTIF_COMPS].rate); break;
    case 13: strcpy(value, e->freeze & (1U << (rr.sel[P_FREEZE] % RR_COMPS)) ? "FROZEN" : "FREE"); break;
    case 14: strcpy(value, actions[rr.freeze_act % 6]); break;
    case 15: num(value, e->comp[rr.sel[P_FREEZE] % RR_MOTIF_COMPS].depth); break;
    case 16: strcpy(value, roles[rr.sel[P_INSTR] % RR_ROLES_UI]); break;
    case 17: strcpy(value, rr.sel[P_INSTR] == 0 ? kits[rr.kit % RR_KITS] : rr_inst_names[rr.inst[rr.sel[P_INSTR]] % RR_INSTS]); value[9] = 0; break;
    case 18: num(value, rr.sel[P_INSTR] < RR_GEN_TRACKS ? rr.mix.level[rr.sel[P_INSTR]] : 0); break;
    case 19: strcpy(value, rr.sel[P_INSTR] < RR_GEN_TRACKS && (e->user_mute >> rr.sel[P_INSTR] & 1) ? "MUTED" : "ON"); break;
    case 20: strcpy(value, rr_kmode_names[rr.live.mode % RR_KMODES]); value[9] = 0; break;
    case 21: strcpy(value, notes[rr.live.split % 12]); num(value + strlen(value), rr.live.split / 12 - 1); break;
    case 22: strcpy(value, rr_inst_names[rr.inst[RT_LIVE_A] % RR_INSTS]); value[9] = 0; break;
    case 23: strcpy(value, rr_inst_names[rr.inst[RT_LIVE_B] % RR_INSTS]); value[9] = 0; break;
    case 24: strcpy(value, rr_fx_names[rr.fx_sel % RR_FX]); value[9] = 0; break;
    case 25: num(value, rr.fx_amount); break;
    case 26: num(value, rr.mix.delay_fb); break;
    case 28: strcpy(value, rr_cmd_names[rr.cmd_sel % RR_CMDS]); value[9] = 0; break;
    case 29: num(value, e->intensity); break;
    case 30: num(value, rr.mix.delay_send); break;
    case 31: num(value, rr.mix.reverb_send); break;
    case 32: strcpy(value, rr_target_names[rr.learn_sel % RR_TARGETS]); value[9] = 0; break;
    case 33: num(value, rr.map.bank + 1); break;
    case 34: strcpy(value, rr.map.pickup ? "PICKUP" : "JUMP"); break;
    case 35: num(value, rr.release_s); break;
    case 36: strcpy(value, rr.mem_sel < RR_MEMORY ? "MEM " : "FLASH "); num(value + strlen(value), rr.mem_sel < RR_MEMORY ? rr.mem_sel + 1 : rr.mem_sel - RR_MEMORY + 1); break;
    case 37: strcpy(value, rr_sys_names[rr.sys_action % RR_SYS]); value[10] = 0; break;
    case 38: num(value, rr.bpm_hi); break;
    case 39: num(value, rr.bpm_lo); break;
    }
}
RR_SMALL static void header(const char *title, uint32_t now, int lion_small)
{
    fill(0, 0, W, 2, RED); fill(0, 2, W, 2, GOLD); fill(0, 4, W, 2, GREEN);
    if (strlen(title) > 9) text(6, 14, title, 1, GOLD); else text(6, 10, title, 2, GOLD);
    char b[8];
    num(b, rr.bpm);
    int x = lion_small ? 132 : 172;
    text(x, 10, b, 2, WHITE);
    text(x + text_w(b, 2) + 2, 17, "BPM", 1, GREY);
    if (rr.playing) { for (int i = 0; i < 7; ++i) fill(x - 14 + i, 11 + i, 1, 14 - 2 * i, (rr.step & 3) == 0 ? LEAF : GREEN); }
    else fill(x - 14, 12, 9, 9, RED);
    if (lion_small) draw_lion(186, 6, 1, now);
}
RR_SMALL static void knobs(void)
{
    static const uint16_t colors[4] = {RED, GOLD, LEAF, WHITE};
    fill(0, 194, W, 1, DIM);
    for (unsigned k = 0; k < 4; ++k) {
        char label[16], value[24];
        knob_text(k, label, value);
        int x = (int)k * 60;
        fill(x + 2, 198, 56, 2, colors[k]);
        text(x + 3, 204, label, 1, GREY);
        value[9] = 0;
        text(x + 3, 218, value, (int)strlen(value) <= 4 ? 2 : 1, WHITE);
    }
}
RR_SMALL static void chip(int x, int y, const char *name, int active, int frozen)
{
    uint16_t bg = frozen ? ICE : active ? GREEN : PANEL;
    fill(x, y, 30, 14, bg);
    if (frozen) frame(x, y, 30, 14, WHITE);
    text(x + 3, y + 4, name, 1, frozen ? BLACK : active ? WHITE : GREY);
}
RR_SMALL static void page_home(uint32_t now)
{
    struct rr_engine *e = &rr.e;
    fill(0, 0, W, 2, RED); fill(0, 2, W, 2, GOLD); fill(0, 4, W, 2, GREEN);
    text(6, 10, rr_styles[e->style].name, 2, GOLD);
    if (e->transition) { text(6, 28, ">", 1, RED); text(14, 28, rr_styles[e->target].name, 1, AMBER); text(90, 28, rr_tmode_names[e->tmode], 1, GREY); }
    else if (rr.style_wait) { text(6, 28, "?", 1, GOLD); text(14, 28, rr_styles[rr.pending_style].name, 1, GREY); }
    char b[24];
    num(b, rr.bpm);
    text(6, 44, b, 4, WHITE);
    text(8 + text_w(b, 4), 44, "BPM", 1, GREY);
    if (rr.bpm_target && rr.bpm_target != rr.bpm) { char t[8]; t[0] = '>'; num(t + 1, rr.bpm_target); text(8 + text_w(b, 4), 56, t, 1, AMBER); }
    strcpy(b, notes[e->key % 12]); cat(b, e->mode == 1 ? " MAJ" : e->mode == 3 ? " MIX" : e->mode == 2 ? " DOR" : " MIN");
    text(6, 80, b, 2, LEAF);
    strcpy(b, rr_section_names[rr.section % RR_SECTIONS]); cat(b, " "); num(b + strlen(b), rr.phrase_pos + 1); cat(b, "/8");
    text(6, 100, b, 1, GOLD);
    if (e->version) { char v[10] = "DUB V"; num(v + 5, e->version); text(84, 100, v, 1, RED); }
    static const char *const ml[3] = {"EVO", "NRG", "DUB"};
    const unsigned mv[3] = {e->evolve, e->energy, e->dub};
    static const uint16_t mc[3] = {RED, GOLD, LEAF};
    for (int i = 0; i < 3; ++i) { text(6, 114 + i * 13, ml[i], 1, GREY); meter(28, 113 + i * 13, 96, 9, mv[i], 100, mc[i]); }
    /* band: active this bar / frozen */
    static const char *const cn[RR_COMPS] = {"DRM", "PRC", "BAS", "SKA", "ORG", "PNO", "HRN", "FX", "HAR", "ARR"};
    static const uint8_t bit[RR_COMPS] = {1, 64, 2, 4, 8, 32, 16, 128, 0, 0};
    for (int i = 0; i < RR_COMPS; ++i) {
        int x = 4 + (i % 5) * 32, y = 156 + (i / 5) * 17;
        chip(x, y, cn[i], rr.playing && (bit[i] ? (rr.active & bit[i]) != 0 : 1), (e->freeze >> i) & 1);
    }
    draw_lion(136, 36, 0, now);
    /* live + MIDI activity */
    fill(166, 132, 8, 8, rr.midi_flash ? GOLD : PANEL);
    text(178, 133, "MIDI", 1, rr.midi_flash ? WHITE : GREY);
    text(166, 146, rr_kmode_names[rr.live.mode % RR_KMODES], 1, LEAF);
    strcpy(b, rr_inst_names[rr.inst[RT_LIVE_A] % RR_INSTS]); b[11] = 0;
    text(166, 158, b, 1, WHITE);
    if (rr.simple) text(166, 172, "SIMPLE", 1, GOLD);
    knobs();
}
RR_SMALL static void row_bar(int y, const char *name, unsigned v, unsigned max, int sel, uint16_t c)
{
    if (sel) fill(2, y - 2, 236, 12, PANEL);
    text(8, y, name, 1, sel ? GOLD : WHITE);
    meter(110, y - 1, 110, 9, v, max, c);
}
RR_SMALL static void page_body(void)
{
    struct rr_engine *e = &rr.e;
    char b[32];
    switch (rr.page) {
    case P_STYLE:
        for (unsigned s = 0; s < RR_STYLES; ++s) {
            int y = 54 + (int)s * 12;
            unsigned cur = s == e->style, tgt = e->transition && s == e->target, sel = s == rr.pending_style;
            if (sel) fill(2, y - 2, 236, 11, PANEL);
            text(8, y, cur ? "*" : tgt ? ">" : " ", 1, cur ? LEAF : RED);
            text(18, y, rr_styles[s].name, 1, cur ? LEAF : tgt ? AMBER : sel ? GOLD : WHITE);
            num(b, rr_styles[s].bpm); text(110, y, b, 1, GREY);
        }
        if (e->transition) { strcpy(b, "BAR "); num(b + 4, e->tprogress + 1); cat(b, "/"); num(b + strlen(b), e->tbars); text(150, 54, b, 1, AMBER); }
        text(150, 66, "PRESS SEQ", 1, GREY); text(150, 76, "TO GO", 1, GREY);
        break;
    case P_EVOLVE: {
        static const char *const dims[RR_DIMS] = {"RHYTHM", "BASS", "HARMONY", "INSTRUMENT", "ARRANGEMENT", "TIMBRE", "EFFECTS"};
        for (unsigned d = 0; d < RR_DIMS; ++d) row_bar(56 + (int)d * 15, dims[d], e->dim[d], 100, rr.sel[P_EVOLVE] == d, LEAF);
        row_bar(56 + RR_DIMS * 15, "GLOBAL EVOLVE", e->evolve, 100, rr.sel[P_EVOLVE] == RR_DIMS, RED);
        strcpy(b, "MUTATIONS "); num(b + 10, e->mutations); text(8, 182, b, 1, GREY);
        break;
    }
    case P_FREEZE: {
        for (unsigned c = 0; c < RR_COMPS; ++c) {
            int x = 4 + (int)(c / 5) * 118, y = 56 + (int)(c % 5) * 24;
            unsigned fz = (e->freeze >> c) & 1, sel = rr.sel[P_FREEZE] == c;
            fill(x, y, 114, 20, fz ? ICE : sel ? PANEL : NIGHT);
            if (sel) frame(x, y, 114, 20, GOLD);
            text(x + 4, y + 6, rr_comp_names[c], 1, fz ? BLACK : WHITE);
            if (fz) text(x + 92, y + 4, "*", 2, BLACK);
            else if (c < RR_MOTIF_COMPS) { num(b, e->comp[c].generation); text(x + 90, y + 6, b, 1, GREY); }
        }
        text(8, 182, "LFO AGAIN = DO ACTION", 1, GREY);
        break;
    }
    case P_INSTR: {
        static const char *const roles[RR_ROLES_UI] = {"DRUMS", "BASS", "SKANK", "ORGAN", "HORNS", "PIANO", "LIVE A", "LIVE B"};
        for (unsigned r = 0; r < RR_ROLES_UI; ++r) {
            int y = 54 + (int)r * 16;
            unsigned sel = rr.sel[P_INSTR] == r;
            if (sel) fill(2, y - 3, 236, 14, PANEL);
            text(8, y, roles[r], 1, sel ? GOLD : GREY);
            text(64, y, r == 0 ? "808 KIT" : rr_inst_names[rr.inst[r] % RR_INSTS], 1, (r < 6 && (e->user_mute >> r & 1)) ? RED : WHITE);
            if (r < RR_GEN_TRACKS) meter(150, y - 1, 80, 9, rr.mix.level[r], 127, (rr.active >> r) & 1 ? LEAF : GREEN);
        }
        break;
    }
    case P_LIVE:
        text(8, 56, rr_kmode_names[rr.live.mode % RR_KMODES], 3, GOLD);
        strcpy(b, "OCTAVE "); num(b + 7, (int)rr.live.octave_a - 2); text(8, 88, b, 1, WHITE);
        strcpy(b, "HELD "); num(b + 5, (int)rr_live_count(&rr.live)); text(100, 88, b, 1, WHITE);
        strcpy(b, "VEL "); num(b + 4, rr.midi_vel); text(160, 88, b, 1, WHITE);
        text(8, 104, "A:", 1, GREY); text(24, 104, rr_inst_names[rr.inst[RT_LIVE_A] % RR_INSTS], 2, LEAF);
        text(8, 128, "B:", 1, GREY); text(24, 128, rr_inst_names[rr.inst[RT_LIVE_B] % RR_INSTS], 2, LEAF);
        text(8, 154, rr.playing ? "BAND + LIVE" : "LIVE ONLY (STOP)", 1, rr.playing ? LEAF : AMBER);
        text(8, 168, "STOP NEVER MUTES THE KEYS", 1, GREY);
        break;
    case P_FX:
        for (unsigned f = 0; f < RR_FX; ++f) {
            int x = 2 + (int)(f / 8) * 79, y = 54 + (int)(f % 8) * 15;
            unsigned sel = rr.fx_sel == f, on = rr.fx_on && f == rr.fx_sel && f <= FX_CRUSH;
            fill(x, y - 3, 76, 13, sel ? (on ? GREEN : PANEL) : NIGHT);
            if (sel) frame(x, y - 3, 76, 13, GOLD);
            strcpy(b, rr_fx_names[f]); b[12] = 0;
            text(x + 3, y, b, 1, rr.fx_hold == f + 1 ? GOLD : WHITE);
        }
        text(4, 176, "FX AGAIN=FIRE  HOLD FX+KEYS", 1, GREY);
        break;
    case P_CONDUCT: {
        for (int i = -2; i <= 2; ++i) {
            int c = (int)rr.cmd_sel + i;
            if (c < 1 || c >= RR_CMDS) continue;
            text(8, 92 + i * 18, rr_cmd_names[c], i ? 1 : 2, i ? GREY : GOLD);
        }
        meter(8, 150, 220, 9, e->intensity, 100, RED);
        text(8, 164, "ARP AGAIN = GO   HOLD ARP+KEYS", 1, GREY);
        text(8, 178, rr_section_names[rr.section % RR_SECTIONS], 1, LEAF);
        break;
    }
    case P_MIDI: {
        static const char *const banks[RR_BANKS] = {"BANK 1 MIX", "BANK 2 EVOLVE", "BANK 3 DUB FX"};
        text(8, 54, banks[rr.map.bank % RR_BANKS], 2, GOLD);
        static const char hex[] = "0123456789ABCDEF";
        for (unsigned i = 0; i < rr.recent_n && i < RR_RECENT; ++i) {
            char m[12];
            for (unsigned j = 0; j < 3; ++j) { m[j * 3] = hex[rr.recent[i][j] >> 4]; m[j * 3 + 1] = hex[rr.recent[i][j] & 15]; m[j * 3 + 2] = ' '; }
            m[8] = 0;
            text(8, 76 + (int)i * 11, m, 1, i ? GREY : WHITE);
        }
        strcpy(b, "NOTES "); num(b + 6, (int)rr.notes_in); text(120, 76, b, 1, WHITE);
        strcpy(b, "CC "); num(b + 3, (int)rr.cc_in); text(120, 88, b, 1, WHITE);
        strcpy(b, "LOST "); num(b + 5, (int)rr.midi_errors); text(120, 100, b, 1, rr.midi_errors ? RED : GREY);
        if (rr.map.learn) { text(120, 116, "LEARN:", 1, RED); text(120, 128, rr_target_names[rr.map.learn_target % RR_TARGETS], 1, GOLD); }
        text(8, 150, "DIN MIDI IN  CH1 KEYS  CH10 PADS", 1, GREY);
        text(8, 162, "GLO AGAIN = LEARN SELECTED", 1, GREY);
        text(8, 176, "PC 110-112 SWITCH BANKS", 1, GREY);
        break;
    }
    case P_SYSTEM:
        text(8, 54, rr.simple ? "SIMPLE MODE" : "PERFORMANCE MODE", 1, GOLD);
        for (unsigned m = 0; m < RR_MEMORY; ++m) {
            char s[4] = {'M', (char)('1' + m), 0, 0};
            int x = 8 + (int)m * 40;
            fill(x, 70, 34, 18, (rr.memory_used >> m & 1) ? GREEN : PANEL);
            if (rr.mem_sel == m) frame(x, 70, 34, 18, GOLD);
            text(x + 11, 76, s, 1, WHITE);
        }
        strcpy(b, "FLASH SLOTS 9-15 VIA SAVE/LOAD"); text(8, 96, b, 1, GREY);
        text(8, 112, "SAVE AGAIN = EXECUTE ACTION", 1, GREY);
        strcpy(b, "BPM RANGE "); num(b + 10, rr.bpm_lo); cat(b, "-"); num(b + strlen(b), rr.bpm_hi); text(8, 128, b, 1, WHITE);
        text(8, 146, "RIDDIM ROOTS " RR_VERSION " DUB EVOLUTION", 1, LEAF);
        strcpy(b, "CPU SHED "); cat(b, rr.overloads || rr.underruns ? "ON" : "OFF"); text(8, 160, b, 1, GREY);
        break;
    }
    knobs();
}
RR_SMALL static void draw_page(uint32_t now)
{
    fill(0, y0, W, BAND, BLACK);
    if (rr.page == P_HOME) page_home(now);
    else { header(rr_page_names[rr.page % RR_PAGES], now, 1); page_body(); }
    if (rr.toast[0] && (int32_t)(rr.toast_until - now) > 0) {
        int w = text_w(rr.toast, 2) + 12;
        if (w > 236) w = 236;
        fill((W - w) / 2, 170, w, 22, BLACK); frame((W - w) / 2, 170, w, 22, GOLD);
        text((W - w) / 2 + 6, 174, rr.toast, w < 236 ? 2 : 1, WHITE);
    }
}
RR_SMALL void rr_ui_init(void)
{
    memset(band_hash, 0xff, sizeof band_hash);
    memset(&lion, 0, sizeof lion);
    memset(lion_ram, 0, sizeof lion_ram);
    unsigned at = 0;
    for (unsigned r = 0; r < RR_LION_RLE; ++r) {
        unsigned n = (rr_lion_rle[r] >> 4) + 1U, c = rr_lion_rle[r] & 15U;
        for (; n; --n, ++at) {
            unsigned x = at % RR_LION_W, y = at / RR_LION_W;
            if (y < RR_LION_H) lion_ram[y * RR_LION_STRIDE + (x >> 1)] |= (uint8_t)(c << ((x & 1) * 4));
        }
    }
    next_band = BANDS;
}
RR_SMALL static uint32_t hash_band(const uint16_t *p)
{
    uint32_t h = 2166136261u;
    const uint32_t *w = (const uint32_t *)p;
    for (unsigned i = 0; i < W * BAND / 2; ++i) h = (h ^ w[i]) * 16777619u;
    return h;
}
RR_SMALL void rr_ui_poll(uint32_t now)
{
    if (pending_band != 255) {
        int r = fm1_screen_pending();
        if (r > 0) return;                                   /* DMA still borrowing a buffer */
        pending_band = 255;
    }
    if (next_band >= BANDS) {
        if (now - last_frame < FRAME_MS) return;
        last_frame = now; next_band = 0;
        lion_update(now);
        frame_ready = 1;
    }
    /* One changed band per call keeps each control iteration short, so
     * short button taps and MIDI are serviced between bands. */
    for (unsigned work = 0; work < 2 && next_band < BANDS; ++work) {
        buf = band[which];
        y0 = (int)(next_band * BAND);
        draw_page(now);
        uint32_t h = hash_band(buf);
        if (h != band_hash[next_band]) {
            if (fm1_screen_submit(0, (unsigned)y0, W, BAND, buf, W * BAND) != 0) return;   /* busy: retry this band */
            band_hash[next_band] = h; pending_band = next_band; pixels_sent += W * BAND;
            which ^= 1; ++next_band;
            return;
        }
        ++next_band;
    }
}
