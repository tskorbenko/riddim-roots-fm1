/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Live keyboard modes with strict note ownership: every output note is
 * reference counted per input key, so chords, splits, sustain and panic can
 * never leave a stuck note. Output goes to the two SDK PLAY tracks. */
#include "rr_core.h"

const char *const rr_kmode_names[RR_KMODES] = {"NORMAL", "CHORD", "SMART CHORD", "SKANK", "BASS", "SPLIT", "SOUND SYS", "CONDUCTOR"};
enum { CH_A = 6, CH_B = 9 };
static uint8_t refs[2][128];

static unsigned lane(unsigned ch) { return ch == CH_B; }
RR_SMALL static void out_on(rr_emit_fn emit, void *ctx, unsigned ch, unsigned note, unsigned vel)
{
    uint8_t *r = &refs[lane(ch)][note];
    if ((*r)++) return;            /* already sounding: share it */
    uint8_t m[3] = {(uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)(vel ? vel : 1)};
    emit(ctx, m, 3);
}
RR_SMALL static void out_off(struct rr_live *l, rr_emit_fn emit, void *ctx, unsigned ch, unsigned note)
{
    uint8_t *r = &refs[lane(ch)][note];
    if (!*r || --*r) return;
    if (l->sustain) { l->sustained[lane(ch)][note >> 3] |= (uint8_t)(1U << (note & 7)); ++*r; return; }
    uint8_t m[3] = {(uint8_t)(0x80 | ch), (uint8_t)note, 0};
    emit(ctx, m, 3);
}
RR_SMALL void rr_live_init(struct rr_live *l)
{
    memset(l, 0, sizeof *l);
    memset(refs, 0, sizeof refs);
    l->split = 60; l->key = 9; l->chord_minor = 1;
}
RR_SMALL static unsigned snap(const struct rr_live *l, unsigned note, unsigned *degree)
{
    static const uint8_t minor[7] = {0, 2, 3, 5, 7, 8, 10}, major[7] = {0, 2, 4, 5, 7, 9, 11};
    const uint8_t *sc = l->scale_mode == 1 || l->scale_mode == 3 ? major : minor;
    int rel = ((int)note - (int)l->key) % 12;
    if (rel < 0) rel += 12;
    unsigned best = 0;
    for (unsigned d = 0; d < 7; ++d) if (sc[d] <= (unsigned)rel) best = d;
    *degree = best;
    return note - (unsigned)rel + sc[best];
}
RR_SMALL static unsigned build_chord(const struct rr_live *l, unsigned note, uint8_t out[4], unsigned smart)
{
    static const uint8_t minor[7] = {0, 2, 3, 5, 7, 8, 10}, major[7] = {0, 2, 4, 5, 7, 9, 11};
    const uint8_t *sc = l->scale_mode == 1 || l->scale_mode == 3 ? major : minor;
    unsigned d, root = snap(l, note, &d), n = smart ? 4 : 3;
    for (unsigned i = 0; i < n; ++i) {
        unsigned step = d + 2 * i, oct = step / 7;
        int v = (int)root - sc[d] + sc[step % 7] + 12 * (int)oct;
        if (smart) while (v > 74) v -= 12;      /* voice-lead into the C4 region */
        out[i] = (uint8_t)rr_clamp(v, 0, 127);
    }
    return n;
}
RR_SMALL int rr_live_note(struct rr_live *l, unsigned note, unsigned velocity, rr_emit_fn emit, void *ctx)
{
    note &= 127;
    if (velocity) {
        if (l->held[note][0]) rr_live_note(l, note, 0, emit, ctx);  /* repeated Note On */
        uint8_t notes[4] = {0};
        unsigned count = 1, ch = CH_A;
        switch (l->mode) {
        case KM_SOUND: return 1 + (note % 12);
        case KM_CONDUCT: return 1 + (note % 24);
        case KM_BASS: ch = CH_B; notes[0] = (uint8_t)(note >= 12 ? note - 12 : note); break;
        case KM_SPLIT: ch = note < l->split ? CH_B : CH_A; notes[0] = (uint8_t)note; break;
        case KM_CHORD: count = build_chord(l, note, notes, 0); break;
        case KM_SMART: count = build_chord(l, note, notes, 1); break;
        case KM_SKANK: count = build_chord(l, note, notes, 0); velocity = velocity * 7 / 8 + 8; break;
        default: notes[0] = (uint8_t)note; break;
        }
        int oct = (ch == CH_A ? (int)l->octave_a : (int)l->octave_b) - 2;
        for (unsigned i = 0; i < count; ++i) {
            unsigned o = (unsigned)rr_clamp((int)notes[i] + 12 * oct, 0, 127);
            l->held[note][i] = (uint8_t)(o + 1);
            out_on(emit, ctx, ch, o, velocity);
        }
        l->held_ch[note] = (uint8_t)(ch + 1);
        return 0;
    }
    if (l->mode == KM_SOUND || l->mode == KM_CONDUCT) { if (!l->held[note][0]) return -1; }
    unsigned ch = l->held_ch[note] ? l->held_ch[note] - 1U : CH_A;
    for (unsigned i = 0; i < 4; ++i) if (l->held[note][i]) {
        out_off(l, emit, ctx, ch, l->held[note][i] - 1U);
        l->held[note][i] = 0;
    }
    l->held_ch[note] = 0;
    return 0;
}
RR_SMALL void rr_live_sustain(struct rr_live *l, unsigned on, rr_emit_fn emit, void *ctx)
{
    l->sustain = (uint8_t)!!on;
    if (on) return;
    for (unsigned k = 0; k < 2; ++k) for (unsigned n = 0; n < 128; ++n)
        if (l->sustained[k][n >> 3] & (1U << (n & 7))) {
            l->sustained[k][n >> 3] &= (uint8_t)~(1U << (n & 7));
            out_off(l, emit, ctx, k ? CH_B : CH_A, n);
        }
}
RR_SMALL void rr_live_panic(struct rr_live *l, rr_emit_fn emit, void *ctx)
{
    for (unsigned k = 0; k < 2; ++k) for (unsigned n = 0; n < 128; ++n) if (refs[k][n]) {
        uint8_t m[3] = {(uint8_t)(0x80 | (k ? CH_B : CH_A)), (uint8_t)n, 0};
        emit(ctx, m, 3);
        refs[k][n] = 0;
    }
    memset(l->held, 0, sizeof l->held); memset(l->held_ch, 0, sizeof l->held_ch);
    memset(l->sustained, 0, sizeof l->sustained); l->sustain = 0;
}
RR_SMALL unsigned rr_live_count(const struct rr_live *l)
{
    unsigned n = 0;
    (void)l;
    for (unsigned k = 0; k < 2; ++k) for (unsigned i = 0; i < 128; ++i) n += refs[k][i] != 0;
    return n;
}
