/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Musical DNA: a compact, versioned, CRC-protected record of the riddim.
 * Components are written in order of musical importance; one that no
 * longer fits is marked for regeneration instead of being truncated. */
#include "rr_core.h"

uint16_t rr_crc16(const uint8_t *p, unsigned n)
{
    uint16_t c = 0xffff;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (unsigned b = 0; b < 8; ++b) c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}
struct io { uint8_t *p; unsigned at, cap; int ok; };
static void put8(struct io *w, unsigned v) { if (w->at < w->cap) w->p[w->at++] = (uint8_t)v; else w->ok = 0; }
static void put32(struct io *w, uint32_t v) { for (unsigned i = 0; i < 4; ++i) put8(w, v >> (8 * i)); }
static unsigned get8(struct io *r) { if (r->at < r->cap) return r->p[r->at++]; r->ok = 0; return 0; }
static uint32_t get32(struct io *r) { uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)get8(r) << (8 * i); return v; }
static const uint16_t drum_lanes[] = {DK_BD, DK_SD, DK_RS, DK_CH, DK_OH, DK_CP, DK_HT, DK_MT, DK_LT, DK_CB, DK_CY};
enum { LANES = sizeof drum_lanes / sizeof drum_lanes[0], VERSION = 1 };

static unsigned comp_size(const struct rr_motif *m, unsigned c)
{
    unsigned n = 4;
    if (c == RC_DRUMS || c == RC_PERC) return n + 2 + 4 * LANES;
    for (unsigned i = 0; i < RR_MOTIF; ++i) if (m->hit[i]) n += 3;
    return n + 1;
}
static void write_comp(struct io *w, const struct rr_motif *m, unsigned c)
{
    put8(w, m->style); put8(w, m->variant); put8(w, m->octave); put8(w, m->voicing);
    if (c == RC_DRUMS || c == RC_PERC) {
        uint16_t used = 0;
        for (unsigned l = 0; l < LANES; ++l) for (unsigned i = 0; i < RR_MOTIF; ++i) if (m->hit[i] & drum_lanes[l]) used |= (uint16_t)(1U << l);
        put8(w, used); put8(w, used >> 8);
        for (unsigned l = 0; l < LANES; ++l) if (used & (1U << l)) {
            uint32_t bits = 0;
            for (unsigned i = 0; i < RR_MOTIF; ++i) if (m->hit[i] & drum_lanes[l]) bits |= 1UL << i;
            put32(w, bits);
        }
        return;
    }
    unsigned count = 0;
    for (unsigned i = 0; i < RR_MOTIF; ++i) count += m->hit[i] != 0;
    put8(w, count);
    for (unsigned i = 0; i < RR_MOTIF; ++i) if (m->hit[i]) {
        put8(w, i | ((m->hit[i] & MH_CHORD) ? 64 : 0) | ((m->hit[i] & MH_GHOST) ? 128 : 0) | ((m->hit[i] & MH_ACCENT) ? 32 : 0));
        put8(w, (uint8_t)m->deg[i]);
        put8(w, m->len[i] > 15 ? 15 : m->len[i]);
    }
}
static int read_comp(struct io *r, struct rr_motif *m, unsigned c)
{
    memset(m, 0, sizeof *m);
    m->style = (uint8_t)get8(r); m->variant = (uint8_t)get8(r); m->octave = (uint8_t)get8(r); m->voicing = (uint8_t)get8(r);
    if (m->style >= RR_STYLES) return 0;
    if (c == RC_DRUMS || c == RC_PERC) {
        unsigned used = get8(r); used |= get8(r) << 8;
        for (unsigned l = 0; l < LANES; ++l) if (used & (1U << l)) {
            uint32_t bits = get32(r);
            for (unsigned i = 0; i < RR_MOTIF; ++i) if (bits & (1UL << i)) m->hit[i] |= drum_lanes[l];
        }
        for (unsigned i = 0; i < RR_MOTIF; ++i) if (m->hit[i])
            m->vel[i] = (uint8_t)(m->hit[i] & (DK_BD | DK_SD | DK_RS) ? 104 : (i & 3) == 0 ? 86 : (i & 1) ? 62 : 76);
        return r->ok;
    }
    unsigned count = get8(r);
    if (count > RR_MOTIF) return 0;
    for (unsigned k = 0; k < count; ++k) {
        unsigned a = get8(r), i = a & 31U;
        int8_t d = (int8_t)get8(r);
        unsigned b = get8(r);
        m->hit[i] = (uint16_t)((a & 64 ? MH_CHORD : MH_NOTE) | (a & 128 ? MH_GHOST : 0) | (a & 32 ? MH_ACCENT : 0));
        m->deg[i] = d; m->len[i] = (uint8_t)(b & 15 ? b & 15 : 1);
        m->gate[i] = (uint8_t)(m->len[i] > 1 ? 14 : 8);
        m->vel[i] = (uint8_t)(a & 128 ? 54 : i % 8 == 0 ? 104 : i % 4 == 0 ? 94 : 84);
    }
    return r->ok;
}
unsigned rr_dna_pack(const struct rr_engine *e, const struct rr_mix *mix, unsigned bpm, uint8_t out[RR_DNA_BYTES])
{
    struct io w = {out, 0, RR_DNA_BYTES - 2, 1};
    static const uint8_t order[RR_MOTIF_COMPS] = {RC_BASS, RC_DRUMS, RC_SKANK, RC_ORGAN, RC_PIANO, RC_HORNS, RC_PERC, RC_FX};
    memset(out, 0, RR_DNA_BYTES);
    put8(&w, RR_DNA_MAGIC & 255); put8(&w, RR_DNA_MAGIC >> 8); put8(&w, VERSION);
    put8(&w, e->transition ? e->target : e->style); put8(&w, bpm); put8(&w, e->key); put8(&w, e->mode);
    put8(&w, e->energy); put8(&w, e->dub); put8(&w, e->evolve); put8(&w, e->intensity);
    for (unsigned d = 0; d < RR_DIMS; ++d) put8(&w, e->dim[d]);
    put8(&w, e->freeze); put8(&w, e->freeze >> 8); put8(&w, e->section); put8(&w, e->version);
    for (unsigned b = 0; b < RR_PHRASE; ++b) put8(&w, e->prog[b] | (e->prog_q[b] << 3));
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) put8(&w, mix->level[t]);
    put8(&w, mix->delay_send); put8(&w, mix->reverb_send); put8(&w, mix->filter); put8(&w, mix->delay_fb);
    put32(&w, e->seed);
    for (unsigned k = 0; k < RR_MOTIF_COMPS; ++k) {
        unsigned c = order[k];
        const struct rr_motif *m = &e->comp[c].cur;
        unsigned need = 1 + comp_size(m, c);
        if (w.at + need > w.cap) { put8(&w, 0); continue; }   /* regenerate on load */
        put8(&w, 1 + c);
        write_comp(&w, m, c);
    }
    uint16_t crc = rr_crc16(out, RR_DNA_BYTES - 2);
    out[RR_DNA_BYTES - 2] = (uint8_t)crc; out[RR_DNA_BYTES - 1] = (uint8_t)(crc >> 8);
    return w.ok ? w.at : 0;
}
int rr_dna_unpack(struct rr_engine *e, struct rr_mix *mix, unsigned *bpm, const uint8_t in[RR_DNA_BYTES])
{
    uint16_t crc = (uint16_t)(in[RR_DNA_BYTES - 2] | in[RR_DNA_BYTES - 1] << 8);
    if (rr_crc16(in, RR_DNA_BYTES - 2) != crc) return -1;
    struct io r = {(uint8_t *)in, 0, RR_DNA_BYTES - 2, 1};
    if (get8(&r) != (RR_DNA_MAGIC & 255) || get8(&r) != RR_DNA_MAGIC >> 8 || get8(&r) != VERSION) return -2;
    unsigned style = get8(&r);
    if (style >= RR_STYLES) return -3;
    struct rr_engine n;
    rr_engine_init(&n, style, 0);
    *bpm = get8(&r); n.key = (uint8_t)(get8(&r) % 12); n.mode = (uint8_t)(get8(&r) & 3);
    n.energy = (uint8_t)get8(&r); n.dub = (uint8_t)get8(&r); n.evolve = (uint8_t)get8(&r); n.intensity = (uint8_t)get8(&r);
    for (unsigned d = 0; d < RR_DIMS; ++d) n.dim[d] = (uint8_t)rr_clamp((int)get8(&r), 0, 100);
    n.freeze = (uint16_t)get8(&r); n.freeze |= (uint16_t)(get8(&r) << 8);
    n.section = (uint8_t)(get8(&r) % RR_SECTIONS); n.version = (uint8_t)get8(&r);
    for (unsigned b = 0; b < RR_PHRASE; ++b) { unsigned v = get8(&r); n.prog[b] = (uint8_t)((v & 7) % 7); n.prog_q[b] = (uint8_t)((v >> 3) % RR_CHORDS); }
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) mix->level[t] = (uint8_t)(get8(&r) & 127);
    mix->delay_send = (uint8_t)get8(&r); mix->reverb_send = (uint8_t)get8(&r); mix->filter = (uint8_t)get8(&r); mix->delay_fb = (uint8_t)get8(&r);
    n.seed = get32(&r); n.rng = n.seed ^ 0x5a5a5a5au;
    if (n.evolve > 100 || n.energy > 100 || n.dub > 100 || *bpm < 30 || *bpm > 250 || !r.ok) return -4;
    for (unsigned k = 0; k < RR_MOTIF_COMPS && r.ok; ++k) {
        unsigned tag = get8(&r);
        if (!tag) continue;
        if (tag > RR_MOTIF_COMPS) return -5;
        if (!read_comp(&r, &n.comp[tag - 1].cur, tag - 1)) return -6;
    }
    memcpy(n.prog_kept, n.prog, RR_PHRASE); memcpy(n.progq_kept, n.prog_q, RR_PHRASE);
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) n.comp[c].kept = n.comp[c].cur;
    *e = n;
    return 0;
}
