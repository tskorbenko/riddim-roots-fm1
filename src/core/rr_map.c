/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Controller mapping: Vortex defaults, banks, MIDI Learn and soft takeover.
 * Assignments are RIDDIM ROOTS' own preset (docs/VORTEX_MIDI_UA.md), not
 * claims about Alesis factory CC numbers. */
#include "rr_core.h"

const char *const rr_target_names[RR_TARGETS] = {
    "-", "BASS LVL", "DRUMS LVL", "SKANK LVL", "KEYS LVL", "HORNS LVL", "PERC/FX LVL",
    "DELAY SEND", "REVERB SEND",
    "RHYTHM MUT", "BASS MUT", "HARMONY MUT", "INSTR MUT", "ARRANGE MUT", "TIMBRE MUT", "FX MUT", "EVOLVE",
    "FILTER", "DELAY FB", "MORPH", "CHAOS", "MASTER", "BPM", "ENERGY", "DUB", "STYLE",
    "FX SELECT", "FX AMOUNT", "REVERB SIZE", "DELAY TIME", "LIVE LEVEL", "EXPRESSION", "PERC LVL",
    "PAD SIREN", "PAD ECHO", "PAD FILL", "PAD DROP", "PAD REWIND", "PAD VERSION", "PAD MUTATE", "PAD FREEZE",
    "PLAY", "STOP", "BANK", "TAP", "COMMAND"
};
#define ANY 255
static const struct rr_map defaults[] = {
    /* Bank 1 MIX: faders F1..F8 = CC20..27 on channel 1. */
    {MAP_CC, 0, 20, 0, T_LEVEL_BASS, 0, 127, 0}, {MAP_CC, 0, 21, 0, T_LEVEL_DRUMS, 0, 127, 0},
    {MAP_CC, 0, 22, 0, T_LEVEL_SKANK, 0, 127, 0}, {MAP_CC, 0, 23, 0, T_LEVEL_KEYS, 0, 127, 0},
    {MAP_CC, 0, 24, 0, T_LEVEL_HORNS, 0, 127, 0}, {MAP_CC, 0, 25, 0, T_LEVEL_FXPERC, 0, 127, 0},
    {MAP_CC, 0, 26, 0, T_DELAY_SEND, 0, 127, 0}, {MAP_CC, 0, 27, 0, T_REVERB_SEND, 0, 127, 0},
    /* Bank 2 EVOLVE */
    {MAP_CC, 0, 20, 1, T_DIM_RHYTHM, 0, 127, 0}, {MAP_CC, 0, 21, 1, T_DIM_BASS, 0, 127, 0},
    {MAP_CC, 0, 22, 1, T_DIM_HARMONY, 0, 127, 0}, {MAP_CC, 0, 23, 1, T_DIM_INSTR, 0, 127, 0},
    {MAP_CC, 0, 24, 1, T_DIM_ARRANGE, 0, 127, 0}, {MAP_CC, 0, 25, 1, T_DIM_TIMBRE, 0, 127, 0},
    {MAP_CC, 0, 26, 1, T_DIM_FX, 0, 127, 0}, {MAP_CC, 0, 27, 1, T_EVOLVE, 0, 127, 0},
    /* Bank 3 DUB FX */
    {MAP_CC, 0, 20, 2, T_FILTER, 0, 127, 0}, {MAP_CC, 0, 21, 2, T_DELAY_FB, 0, 127, 0},
    {MAP_CC, 0, 22, 2, T_DELAY_TIME, 0, 127, 0}, {MAP_CC, 0, 23, 2, T_REVERB_SIZE, 0, 127, 0},
    {MAP_CC, 0, 24, 2, T_FX_SELECT, 0, 127, 0}, {MAP_CC, 0, 25, 2, T_FX_AMOUNT, 0, 127, 0},
    {MAP_CC, 0, 26, 2, T_ENERGY, 0, 127, 0}, {MAP_CC, 0, 27, 2, T_DUB, 0, 127, 0},
    /* Pads 1..8 = notes 36..43 on channel 10, every bank. */
    {MAP_NOTE, 9, 36, ANY, T_PAD_SIREN, 0, 127, MAPF_MOMENTARY}, {MAP_NOTE, 9, 37, ANY, T_PAD_ECHO, 0, 127, MAPF_MOMENTARY},
    {MAP_NOTE, 9, 38, ANY, T_PAD_FILL, 0, 127, 0}, {MAP_NOTE, 9, 39, ANY, T_PAD_DROP, 0, 127, MAPF_MOMENTARY},
    {MAP_NOTE, 9, 40, ANY, T_PAD_REWIND, 0, 127, 0}, {MAP_NOTE, 9, 41, ANY, T_PAD_VERSION, 0, 127, 0},
    {MAP_NOTE, 9, 42, ANY, T_PAD_MUTATE, 0, 127, 0}, {MAP_NOTE, 9, 43, ANY, T_PAD_FREEZE, 0, 127, MAPF_TOGGLE},
    /* Ribbon banks, tilt, volume slider, aftertouch. */
    {MAP_CC, 0, 74, ANY, T_FILTER, 0, 127, 0}, {MAP_CC, 0, 71, ANY, T_DELAY_FB, 0, 127, 0},
    {MAP_CC, 0, 16, ANY, T_MORPH, 0, 127, 0}, {MAP_CC, 0, 17, ANY, T_CHAOS, 0, 127, 0},
    {MAP_CC, 0, 7, ANY, T_MASTER, 0, 127, 0}, {MAP_AT, 0, 0, ANY, T_EXPRESSION, 0, 127, 0},
    /* Direct performance CCs. */
    {MAP_CC, 0, 3, ANY, T_BPM, 0, 127, 0}, {MAP_CC, 0, 9, ANY, T_STYLE, 0, 127, 0},
    {MAP_CC, 0, 118, ANY, T_PLAY, 0, 127, 0}, {MAP_CC, 0, 117, ANY, T_STOP, 0, 127, 0},
    {MAP_CC, 0, 116, ANY, T_TAP, 0, 127, 0}, {MAP_CC, 0, 119, ANY, T_CMD, 0, 127, 0},
    {MAP_PC, 0, 110, ANY, T_BANK, 0, 0, 0}, {MAP_PC, 0, 111, ANY, T_BANK, 1, 1, 0}, {MAP_PC, 0, 112, ANY, T_BANK, 2, 2, 0},
};
typedef char rr_map_table_fits[sizeof defaults / sizeof defaults[0] <= RR_MAPS ? 1 : -1];

RR_SMALL static int continuous(unsigned t)
{
    return (t >= T_LEVEL_BASS && t <= T_PERC_LEVEL && t != T_STYLE && t != T_BPM && t != T_FX_SELECT) ;
}
RR_SMALL void rr_map_defaults(struct rr_mapper *m)
{
    memset(m, 0, sizeof *m);
    memcpy(m->map, defaults, sizeof defaults);
    m->pickup = 1;
    for (unsigned t = 0; t < RR_TARGETS; ++t) { m->value[t] = 64; m->last[t] = 255; }
}
RR_SMALL void rr_map_set_value(struct rr_mapper *m, unsigned target, unsigned value)
{
    if (target >= RR_TARGETS) return;
    if (m->value[target] != value) { m->picked[target] = 0; m->last[target] = 255; }
    m->value[target] = (uint8_t)(value > 127 ? 127 : value);
}
RR_SMALL void rr_map_learn(struct rr_mapper *m, unsigned target)
{
    m->learn = target != T_NONE; m->learn_target = (uint8_t)target; m->learn_bank = m->bank;
}
RR_SMALL int rr_map_valid(const struct rr_mapper *m)
{
    if (m->bank >= RR_BANKS) return 0;
    for (unsigned i = 0; i < RR_MAPS; ++i) {
        const struct rr_map *e = &m->map[i];
        if (e->target >= RR_TARGETS || e->channel > 15 || e->number > 127 || e->kind > MAP_AT) return 0;
        if (e->bank != ANY && e->bank >= RR_BANKS) return 0;
    }
    return 1;
}
RR_SMALL static unsigned learn(struct rr_mapper *m, unsigned kind, unsigned ch, unsigned num, struct rr_action *out)
{
    unsigned slot = RR_MAPS;
    for (unsigned i = 0; i < RR_MAPS; ++i) {
        struct rr_map *e = &m->map[i];
        if (e->target == m->learn_target && (e->bank == m->learn_bank || e->bank == ANY)) { slot = i; break; }
    }
    if (slot == RR_MAPS) for (unsigned i = 0; i < RR_MAPS; ++i) if (!m->map[i].target) { slot = i; break; }
    if (slot == RR_MAPS) slot = RR_MAPS - 1;
    /* A control has one meaning per bank: release a previous assignment. */
    for (unsigned i = 0; i < RR_MAPS; ++i) {
        struct rr_map *e = &m->map[i];
        if (i != slot && e->target && e->kind == kind && e->channel == ch && e->number == num &&
            (e->bank == m->learn_bank || e->bank == ANY)) e->target = T_NONE;
    }
    struct rr_map *e = &m->map[slot];
    e->kind = (uint8_t)kind; e->channel = (uint8_t)ch; e->number = (uint8_t)num;
    e->bank = kind == MAP_NOTE || kind == MAP_PC ? ANY : m->learn_bank;
    e->target = m->learn_target; e->lo = 0; e->hi = 127;
    e->flags = kind == MAP_NOTE ? MAPF_MOMENTARY : 0;
    m->learn = 0;
    out->target = e->target; out->value = 0; out->pressed = 0; out->from_learn = 1;
    return 1;
}
RR_SMALL unsigned rr_map_message(struct rr_mapper *m, const uint8_t *msg, unsigned n, struct rr_action out[2])
{
    if (!n || msg[0] < 0x80 || msg[0] >= 0xf0) return 0;
    unsigned type = msg[0] & 0xf0U, ch = msg[0] & 15U, kind, num = 0, raw = 0, pressed = 1;
    switch (type) {
    case 0xb0: if (n < 3) return 0; kind = MAP_CC; num = msg[1]; raw = msg[2]; break;
    case 0x90: case 0x80: if (n < 3) return 0; kind = MAP_NOTE; num = msg[1];
        raw = type == 0x90 ? msg[2] : 0; pressed = raw != 0; break;
    case 0xc0: kind = MAP_PC; num = msg[1]; raw = 127; break;
    case 0xe0: if (n < 3) return 0; kind = MAP_BEND; raw = msg[2]; break;
    case 0xd0: kind = MAP_AT; raw = msg[1]; break;
    case 0xa0: if (n < 3) return 0; kind = MAP_AT; raw = msg[2]; break;
    default: return 0;
    }
    if (m->learn && pressed && !(kind == MAP_CC && num == 64)) return learn(m, kind, ch, num, out);
    unsigned count = 0;
    for (unsigned i = 0; i < RR_MAPS && count < 2; ++i) {
        const struct rr_map *e = &m->map[i];
        if (!e->target || e->kind != kind || e->channel != ch) continue;
        if ((kind == MAP_CC || kind == MAP_NOTE || kind == MAP_PC) && e->number != num) continue;
        if (e->bank != ANY && e->bank != m->bank) continue;
        unsigned t = e->target, v = e->flags & MAPF_INVERT ? 127 - raw : raw;
        v = e->lo + (unsigned)((int)(e->hi - e->lo) * (int)v / 127);
        if (t == T_BANK) {
            m->bank = (uint8_t)(e->lo < RR_BANKS ? e->lo : 0);
            memset(m->picked, 0, sizeof m->picked); memset(m->last, 255, sizeof m->last);
        }
        if (e->flags & MAPF_TOGGLE) {
            if (!pressed) continue;
            m->toggled[t] ^= 1; v = m->toggled[t] ? 127 : 0;
        }
        if (kind == MAP_CC && continuous(t) && m->pickup && !m->picked[t]) {
            /* Soft takeover: accept once the control reaches or crosses the
             * internal value, so a fast move cannot jump over the pickup. */
            int d = (int)v - (int)m->value[t], p = m->last[t] == 255 ? d : (int)m->last[t] - (int)m->value[t];
            m->last[t] = (uint8_t)v;
            if ((d < -3 || d > 3) && !((p < 0 && d > 0) || (p > 0 && d < 0))) continue;
            m->picked[t] = 1;
        }
        if (continuous(t)) m->value[t] = (uint8_t)v;
        out[count].target = (uint8_t)t; out[count].value = (uint8_t)v;
        out[count].pressed = (uint8_t)pressed; out[count].from_learn = 0; ++count;
    }
    return count;
}
