/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Generative band: motifs, phrase-aware evolution, selective freeze,
 * arrangement, conductor commands and continuous style transitions. */
#include "rr_core.h"

const char *const rr_section_names[RR_SECTIONS] = {"INTRO", "VERSE", "CHORUS", "DUB", "BREAK", "BUILD", "DROP", "OUTRO"};
const char *const rr_comp_names[RR_COMPS] = {"DRUMS", "PERC", "BASS", "SKANK", "ORGAN", "PIANO", "HORNS", "FX", "HARMONY", "ARRANGE"};
const char *const rr_tmode_names[RR_TMODES] = {"SMOOTH", "DUB MIX", "BREAK", "HARD", "MUTANT"};
const char *const rr_cmd_names[RR_CMDS] = {
    "-", "FILL", "NEW GROOVE", "PERC X2", "NO KICK", "BREAKDOWN",
    "NEW BASS", "BASS SOLO", "BASS OCT", "SYNCOPATE", "BASS HOME",
    "BUBBLE", "STABS", "KEYS VAR", "VOICING",
    "NEW SKANK", "MUTED", "SKANK VAR",
    "MUTE BAND", "ECHO THROW", "SPRING SN", "BASS ONLY", "BIG DROP", "FULL BAND",
    "NEXT VERSION", "BUILD", "DROP", "SOLO", "MUTATE", "RESTORE", "REWIND",
    "KEEP", "REJECT", "REBUILD", "FREEZE", "SIREN"
};
/* count (root included), then positive offsets; index = chord id. */
const uint8_t rr_chord_table[RR_CHORDS][8] = {
    {1}, {3, 4, 7}, {3, 3, 7}, {4, 3, 7, 10}, {4, 4, 7, 11}, {4, 4, 7, 10}, {3, 5, 7}, {3, 7, 12},
    {4, 3, 10, 14}, {2, 12}, {4, 7, 14, 15}, {4, 4, 7, 9}, {4, 4, 7, 12}, {4, 3, 7, 12}, {4, 3, 10, 17}, {3, 3, 6}
};
static const uint8_t scales[4][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10}
};
RR_SMALL unsigned rr_scale_note(unsigned key, unsigned mode, unsigned chord_root, int degree)
{
    int d = (int)chord_root + degree, oct = 0;
    while (d < 0) { d += 7; --oct; }
    while (d >= 7) { d -= 7; ++oct; }
    return (unsigned)rr_clamp((int)key + scales[mode & 3][d] + 12 * oct, 0, 127);
}

/* ---------------- grammar ---------------- */
/* Drum templates per style: two variants of {BD, SD, RS, CH, OH, CP}. */
static const uint16_t drum_tpl[RR_STYLES][2][6] = {
    {{0x0101, 0x1010, 0, 0x5555, 0, 0}, {0x0101, 0x1010, 0, 0xffff, 0x4040, 0}},                 /* ska */
    {{0x0100, 0, 0x1010, 0x5555, 0, 0}, {0x0101, 0, 0x1010, 0x5555, 0x4000, 0}},                 /* rocksteady */
    {{0x0100, 0, 0x0100, 0x5555, 0, 0}, {0x0100, 0, 0x0100, 0x7575, 0, 0}},                      /* roots one drop */
    {{0x1111, 0x0100, 0, 0x4444, 0, 0}, {0x1111, 0x1010, 0, 0x5555, 0x4444, 0}},                 /* steppers */
    {{0x0100, 0, 0x0100, 0x4444, 0, 0}, {0x0100, 0x0100, 0, 0x0404, 0x4000, 0}},                 /* dub */
    {{0x0101, 0x1010, 0, 0x5555, 0, 0}, {0x0501, 0x1010, 0, 0x5555, 0, 0x1000}},                 /* rub-a-dub */
    {{0x1111, 0x4848, 0, 0x5555, 0, 0}, {0x0909, 0x1010, 0, 0x5555, 0, 0x1010}},                 /* dancehall */
    {{0x0c05, 0x9290, 0, 0x5555, 0, 0}, {0x0405, 0x5410, 0, 0x5555, 0x0040, 0}},                 /* jungle */
    {{0x1111, 0, 0x1010, 0, 0x4444, 0}, {0x1111, 0, 0x1210, 0x2222, 0x4444, 0}},                 /* dub techno */
    {{0x0001, 0x0100, 0, 0x5555, 0, 0}, {0x0401, 0x0100, 0, 0x5454, 0x4000, 0}},                 /* dubstep */
    {{0x0109, 0x1000, 0x0440, 0x5555, 0, 0}, {0x2081, 0x0110, 0, 0x9249, 0, 0x8000}},            /* dub noise */
};
/* Bass lines: 32 sixteenths. 1..9 scale degree above the chord root,
 * b/a/l = 7th/6th/5th below, '-' holds, '.' rests. */
static const char *const bass_tpl[RR_STYLES][2] = {
    {"1-3-5-8-6-5-3-2-1-3-5-6-8-6-5-b-", "1---5---8---5---1---3---5---l---"},
    {"1--1..5-8-..5-3-1--1..5-6-5-3-2-", "1-....1-5-..3-..1-....1-5-6-5-3-"},
    {"..1---..5-.3-1--..1---..5-6-5-3-", "1--.1-..5--.3-1-1--.1-..b-..l-1-"},
    {"1-.1-.1-5-.5-.5-3-.3-.3-1-.1-.b-", "1.1.1.1.5.5.3.3.1.1.1.1.6.5.3.2."},
    {"1-------..5-1---1-------..b-l---", "..1-----..5-------1-----..3-1---"},
    {"1-.1-.5-..1-3-5-1-.1-.5-..6-5-3-", "1..1..1.5..5..5.3..3..3.1..1..b."},
    {"1..1..1.....1...1..1..5.....b...", "1.....1.1.....5.1.....1.b.....l."},
    {"1-------5---3-1-1-------b---l---", "1---..1-5-------3---..1-b-------"},
    {"..1...1...1...1...1...1...1...5.", "1-.....1-.......1-.....1-...5..."},
    {"1-------....1---5-------....b---", "1---1-..----5---1---1-..----3---"},
    {"1.1..1.b..1..5..1..1.1..l...1.b.", "1-----..1-..5---b-----..1-..l---"},
};
/* Skank / piano stabs (16 steps): x chord, m muted accent. */
static const char *const skank_tpl[RR_STYLES][2] = {
    {"..x...x...x...x.", "..xm..x...xm..x."}, {"....x.......x...", "....x.x.....x.x."},
    {"....x.......x...", "....xm......xm.."}, {"....x.x.....x.x.", "..x.x...x.x...x."},
    {"....x...........", "............x..."}, {"....x.......x...", "..x.x...x.x...x."},
    {"...x..x....x..x.", "x..x..x...x.x..."}, {"....x.......x...", "....x..m....x..."},
    {"..x.....x.....x.", "...x.....x...x.."}, {".......x........", "........x.....x."},
    {"x..x....x..x.x..", "..x..m..x..m...x"},
};
/* Organ: l low voicing, h high voicing, x chord, '-' hold. */
static const char *const organ_tpl[RR_STYLES][2] = {
    {"x---------------", "..x...x...x...x."}, {"..lh..lh..lh..lh", "x-------x-------"},
    {"..lh..lh..lh..lh", "x---------------"}, {"..lh..lh..lh..lh", "..l...l...l...l."},
    {"x---------------", "........x-------"}, {".l.h.l.h.l.h.l.h", "..lh..lh..lh..lh"},
    {"x.....x.....x...", "..x..x....x..x.."}, {"x---------------", "x-------x-------"},
    {"..x.....x.....x.", "...x.....x...x.."}, {"x---------------", "x-------........"},
    {"x.......x.......", "....x-------...."},
};
/* Horns / melodica / lead lines (32 steps). */
static const char *const horn_tpl[RR_STYLES][2] = {
    {"5-3-1-3-5---8---6-5-3-2-1-------", "8-8-6-5-3---5---6---5-3-1-------"},
    {"....5-6-8---6-5-3-------........", "8-----6-5-------3-5-6-5-3-------"},
    {"........5---3-1-3---------------", "....8---6-5-3---5---------------"},
    {"1---3---5---3---1---------------", "5---5---6---5---3-------1-------"},
    {"........8-----5---3-------------", "............5-------3-----------"},
    {"5.5.3.1.........5.5.6.5.3-------", "8.8.6.5.3.......5.5.3.1.b-------"},
    {"1..1..3.....5...1..1..b.....1...", "5..5..6.....5...3..3..1.....b..."},
    {"8-------5---3---1---------------", "5-------6---5---3---------------"},
    {"1-------------------------------", "5---------------3---------------"},
    {"........5-------3-------1-------", "........8-------6-------5-------"},
    {"8.7.5.3.1.......b.l.1...........", "1-----3-----5-----8-------------"},
};
/* Progression families: chord-root scale degrees for 8 bars. */
static const uint8_t prog_tpl[][RR_PHRASE] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 3, 3, 0, 0, 3, 3}, {0, 0, 6, 6, 0, 0, 6, 6}, {0, 3, 0, 3, 0, 3, 4, 4},
    {0, 3, 4, 3, 0, 3, 4, 4}, {0, 5, 3, 4, 0, 5, 3, 4}, {0, 5, 6, 0, 0, 5, 6, 6}, {0, 6, 5, 6, 0, 6, 5, 6},
    {0, 3, 0, 4, 0, 3, 4, 0}, {0, 4, 5, 3, 0, 4, 5, 3}, {0, 0, 0, 0, 3, 3, 0, 0}, {0, 0, 0, 0, 0, 0, 5, 5},
};
static const uint8_t prog_family[] = {1, 2, 2, 2, 4, 4, 8, 8, 16, 16, 32, 64};

/* ---------------- motif builders ---------------- */
RR_SMALL static int degree_of(char c)
{
    if (c >= '1' && c <= '9') return c - '1';
    return c == 'b' ? -1 : c == 'a' ? -2 : c == 'l' ? -3 : 99;
}
RR_SMALL static unsigned accent_vel(unsigned i, uint32_t *r, unsigned base)
{
    unsigned v = base + (i % 8 == 0 ? 18 : i % 4 == 0 ? 10 : i % 2 == 0 ? 0 : -8);
    return (unsigned)rr_clamp((int)v + (int)rr_range(r, 7) - 3, 20, 127);
}
RR_SMALL static void motif_clear(struct rr_motif *m, unsigned style)
{
    memset(m, 0, sizeof *m);
    m->style = (uint8_t)style;
}
RR_SMALL static void build_line(struct rr_motif *m, const char *t, unsigned n, uint32_t *r, unsigned base_vel)
{
    for (unsigned i = 0; i < RR_MOTIF; ++i) {
        char c = t[i % n];
        int d = degree_of(c);
        if (d == 99) continue;
        unsigned len = 1;
        while (len < RR_MOTIF - i && t[(i + len) % n] == '-') ++len;
        m->hit[i] = MH_NOTE | (i % 8 == 0 ? MH_ACCENT : 0);
        m->deg[i] = (int8_t)d; m->len[i] = (uint8_t)len;
        m->gate[i] = (uint8_t)(len > 1 ? 14 : 9 + rr_range(r, 4));
        m->vel[i] = (uint8_t)accent_vel(i, r, base_vel);
    }
}
RR_SMALL static void build_chords(struct rr_motif *m, const char *t, uint32_t *r, unsigned base_vel)
{
    for (unsigned i = 0; i < RR_MOTIF; ++i) {
        char c = t[i % 16];
        if (c != 'x' && c != 'm' && c != 'l' && c != 'h') continue;
        unsigned len = 1;
        while (len < RR_MOTIF - i && t[(i + len) % 16] == '-') ++len;
        m->hit[i] = MH_CHORD | (c == 'm' ? MH_GHOST : 0);
        m->deg[i] = (int8_t)(c == 'h' ? 7 : c == 'l' ? -7 : 0);
        m->len[i] = (uint8_t)len;
        m->gate[i] = (uint8_t)(len > 1 ? 14 : c == 'm' ? 2 : 4 + rr_range(r, 3));
        m->vel[i] = (uint8_t)(c == 'm' ? 52 + rr_range(r, 10) : accent_vel(i, r, base_vel));
    }
}
RR_SMALL static void build_drums(struct rr_motif *m, unsigned style, unsigned variant, unsigned energy, uint32_t *r)
{
    const uint16_t *d = drum_tpl[style][variant & 1];
    static const uint16_t lanes[6] = {DK_BD, DK_SD, DK_RS, DK_CH, DK_OH, DK_CP};
    for (unsigned i = 0; i < RR_MOTIF; ++i) {
        unsigned s = i & 15, v = 0;
        for (unsigned l = 0; l < 6; ++l) if (d[l] & (1U << s)) m->hit[i] |= lanes[l];
        if ((m->hit[i] & DK_OH) && (m->hit[i] & DK_CH)) m->hit[i] &= (uint16_t)~DK_CH;
        if (m->hit[i] & (DK_BD | DK_SD | DK_RS)) v = 104;
        else if (m->hit[i]) v = s % 4 == 0 ? 86 : s % 2 == 0 ? 78 : 64;
        /* Second bar answers: snare/rim ghost pickup and an open-hat lift. */
        if (i == 30 && style <= 5 && rr_chance(r, 45)) { m->hit[i] |= DK_RS; v = v > 58 ? v : 58; }
        if (i >= 16 && s == 14 && energy > 60 && rr_chance(r, 50)) { m->hit[i] = (uint16_t)((m->hit[i] & ~DK_CH) | DK_OH); v = v ? v : 80; }
        m->vel[i] = (uint8_t)(v ? rr_clamp((int)v + (int)rr_range(r, 9) - 4, 30, 127) : 0);
    }
    m->variant = (uint8_t)variant;
}
RR_SMALL static void build_perc(struct rr_motif *m, unsigned style, unsigned energy, uint32_t *r)
{
    /* Hand percussion: shaker (CP lane voiced as maracas), congas, cowbell, clave. */
    static const uint16_t conga[4][2] = {{0x8840, 0x2080}, {0x0880, 0xa080}, {0x4404, 0x1040}, {0x0220, 0x8808}};
    unsigned c = rr_range(r, 4), dense = energy > 55;
    for (unsigned i = 0; i < RR_MOTIF; ++i) {
        unsigned s = i & 15, h = 0, v = 0;
        if ((conga[c][0] >> s) & 1U) { h |= DK_MT; v = 72; }
        if ((conga[c][1] >> s) & 1U) { h |= DK_LT; v = 80; }
        if (style != RS_DUBTECHNO && style != RS_DUBSTEP && (dense ? 1 : !(s & 1))) { h |= DK_CP; v = v ? v : (s & 1 ? 40 : 54); }
        if ((style == RS_SKA || style == RS_DANCEHALL) && (s == 6 || s == 14)) { h |= DK_CB; v = 70; }
        if (style == RS_ROCKSTEADY && (s == 3 || s == 10) && i >= 16) { h |= DK_HT; v = 74; }
        if (style == RS_DUBNOISE && rr_chance(r, 18)) { h |= DK_CY; v = 60; }
        m->hit[i] = (uint16_t)h;
        m->vel[i] = (uint8_t)(v ? rr_clamp((int)v + (int)rr_range(r, 13) - 6, 20, 120) : 0);
    }
}
/* Embellish a melodic motif: change some pitches to chord tones/neighbours,
 * occasionally add or drop onsets. amount 0..100. */
RR_SMALL static void vary_line(struct rr_motif *m, uint32_t *r, unsigned amount, int allow_rhythm)
{
    static const int8_t tones[] = {0, 2, 4, 7, 4, 0, -3, 6, 1, 5};
    unsigned changes = 1 + amount / 18;
    for (unsigned k = 0; k < changes; ++k) {
        unsigned i = rr_range(r, RR_MOTIF);
        if (m->hit[i] & MH_NOTE) {
            if (i % 8 == 0 && rr_chance(r, 70)) continue;          /* keep the anchors */
            m->deg[i] = (int8_t)(rr_chance(r, 60) ? tones[rr_range(r, 10)] : m->deg[i] + (rr_chance(r, 50) ? 1 : -1));
            if (allow_rhythm && rr_chance(r, amount / 2) && m->len[i] > 1) m->len[i] = (uint8_t)(1 + rr_range(r, m->len[i]));
        } else if (allow_rhythm && rr_chance(r, amount)) {
            unsigned covered = 0;
            for (unsigned j = 0; j < RR_MOTIF; ++j)
                if ((m->hit[j] & MH_NOTE) && (i + RR_MOTIF - j) % RR_MOTIF < m->len[j]) covered = 1;
            if (covered) continue;
            m->hit[i] = MH_NOTE | (rr_chance(r, 30) ? MH_GHOST : 0);
            m->deg[i] = tones[rr_range(r, 6)]; m->len[i] = 1;
            m->gate[i] = (uint8_t)(6 + rr_range(r, 6));
            m->vel[i] = (uint8_t)(m->hit[i] & MH_GHOST ? 58 : 84 + rr_range(r, 20));
        }
    }
}
RR_SMALL static void vary_drums(struct rr_motif *m, uint32_t *r, unsigned amount, unsigned style)
{
    unsigned changes = 1 + amount / 15;
    for (unsigned k = 0; k < changes; ++k) {
        unsigned i = rr_range(r, RR_MOTIF), s = i & 15;
        unsigned choice = rr_range(r, 6);
        if (s == 8 && style <= RS_DUB) continue;                     /* never lose the drop */
        if (choice == 0 && s != 0) m->hit[i] ^= DK_BD;
        else if (choice == 1) m->hit[i] ^= (s & 1) ? DK_CH : DK_OH;
        else if (choice == 2) m->hit[i] |= DK_RS;
        else if (choice == 3 && amount > 50) m->hit[i] ^= DK_SD;
        else if (choice == 4) m->hit[i] |= (s & 1) ? DK_CH : 0;
        else m->hit[i] ^= DK_HT;
        if ((m->hit[i] & DK_OH) && (m->hit[i] & DK_CH)) m->hit[i] &= (uint16_t)~DK_CH;
        if (!m->vel[i] && m->hit[i]) m->vel[i] = (uint8_t)(m->hit[i] & (DK_BD | DK_SD) ? 96 : 58 + rr_range(r, 20));
        if (!m->hit[i]) m->vel[i] = 0;
    }
}
RR_SMALL static void vary_chords(struct rr_motif *m, uint32_t *r, unsigned amount)
{
    unsigned changes = 1 + amount / 25;
    for (unsigned k = 0; k < changes; ++k) {
        unsigned i = rr_range(r, RR_MOTIF);
        if (m->hit[i] & MH_CHORD) {
            if ((i & 15) == 4 || (i & 15) == 12) { m->deg[i] = (int8_t)(rr_chance(r, 50) ? 0 : 7); continue; }
            if (rr_chance(r, 50)) { m->hit[i] = 0; m->vel[i] = 0; }
        } else if (rr_chance(r, amount)) {
            m->hit[i] = MH_CHORD | (rr_chance(r, 50) ? MH_GHOST : 0);
            m->len[i] = 1; m->gate[i] = 3; m->deg[i] = 0;
            m->vel[i] = (uint8_t)(m->hit[i] & MH_GHOST ? 50 : 82);
        }
    }
}

RR_SMALL static void generate(struct rr_engine *e, unsigned c, unsigned style, uint32_t seed)
{
    struct rr_comp_state *s = &e->comp[c];
    struct rr_motif *m = &s->cur;
    uint32_t r = seed ? seed : 1;
    unsigned v = rr_range(&r, 2);
    motif_clear(m, style);
    m->variant = (uint8_t)v;
    switch (c) {
    case RC_DRUMS: build_drums(m, style, v, e->energy, &r); break;
    case RC_PERC: build_perc(m, style, e->energy, &r); break;
    case RC_BASS: build_line(m, bass_tpl[style][v], 32, &r, 96); vary_line(m, &r, 20, 0); break;
    case RC_SKANK: build_chords(m, skank_tpl[style][v], &r, 88); if (rr_chance(&r, 50)) vary_chords(m, &r, 20); break;
    case RC_PIANO: build_chords(m, skank_tpl[style][v ^ 1], &r, 78); m->voicing = 1; break;
    case RC_ORGAN: build_chords(m, organ_tpl[style][v], &r, 74); break;
    case RC_HORNS: build_line(m, horn_tpl[style][v], 32, &r, 90); vary_line(m, &r, 10, 0); break;
    case RC_FX: {
        /* Dub-engineer cue sheet: event kinds on chosen sixteenths. */
        static const uint8_t kinds[] = {FXK_ECHO_THROW, FXK_SIREN, FXK_SPRING, FXK_NOISE, FXK_LASER, FXK_SWEEP, FXK_FEEDBACK};
        unsigned count = 1 + rr_range(&r, 3);
        for (unsigned k = 0; k < count; ++k) {
            unsigned i = rr_range(&r, RR_MOTIF);
            m->hit[i] = 1; m->deg[i] = (int8_t)kinds[rr_range(&r, style >= RS_DUBTECHNO ? 7 : 4)];
            m->vel[i] = (uint8_t)(50 + rr_range(&r, 60));
        }
        break;
    }
    }
    s->age = 0;
    ++s->generation;
}

/* ---------------- harmony ---------------- */
RR_SMALL static unsigned quality(const struct rr_engine *e, unsigned root, unsigned style)
{
    unsigned mode = e->mode;
    unsigned minor = mode == 1 || mode == 3 ? (root == 1 || root == 2 || root == 5) : (root == 0 || root == 3 || (root == 4 && mode != 2));
    if (mode == 2 && root == 3) minor = 0;                         /* dorian IV */
    if (style == RS_DUBTECHNO) return minor ? CH_MIN9 : CH_MADD9;
    if (style == RS_DUBSTEP || style == RS_DUBNOISE) return minor ? CH_MIN7 : CH_SUS4;
    if (style == RS_SKA && !minor && root == 4) return CH_DOM7;
    if ((style == RS_ROOTS || style == RS_DUB || style == RS_JUNGLE) && minor && root == 0) return CH_MIN7;
    return minor ? CH_MIN : CH_MAJ;
}
RR_SMALL static void new_progression(struct rr_engine *e, unsigned style, uint32_t *r)
{
    unsigned mask = rr_styles[style].prog_mask, n = sizeof prog_family, pick = 0, tries = 0;
    do { pick = rr_range(r, n); } while (!(prog_family[pick] & mask) && ++tries < 40);
    for (unsigned b = 0; b < RR_PHRASE; ++b) {
        e->prog[b] = prog_tpl[pick][b];
        e->prog_q[b] = (uint8_t)quality(e, e->prog[b], style);
    }
}
RR_SMALL static void set_mode_for(struct rr_engine *e, unsigned style)
{
    unsigned mode = rr_styles[style].mode;
    if (mode == e->mode) return;
    /* Keep the pitch collection: relative major/minor shares every note,
     * so frozen motifs stay consonant across the change. */
    if (mode == 1 && e->mode != 1) e->key = (uint8_t)((e->key + 3) % 12);
    else if (mode != 1 && e->mode == 1) e->key = (uint8_t)((e->key + 9) % 12);
    e->mode = (uint8_t)mode;
}

/* ---------------- arrangement ---------------- */
RR_SMALL static unsigned section_mask(const struct rr_engine *e, unsigned sec, unsigned pos)
{
    enum { D = 1, B = 2, S = 4, O = 8, H = 16, P = 32, C = 64, X = 128 };
    unsigned m;
    switch (sec) {
    case SEC_INTRO: m = D | S | O | (pos >= 2 ? B | C : 0) | (pos >= 4 ? H : 0); break;
    case SEC_VERSE: m = D | B | S | O | P | C; break;
    case SEC_CHORUS: m = D | B | S | O | P | H | C | X; break;
    case SEC_DUB: m = D | B | X | ((pos & 3) == 3 ? S | O : 0) | (pos == 5 ? H : 0); break;
    case SEC_BREAK: m = (pos < 2 ? B : pos < 6 ? B | S | C : D | B | S) | X; break;
    case SEC_BUILD: m = D | B | S | O | (pos >= 4 ? P | C : 0); break;
    case SEC_DROP: m = pos < 1 ? B | X : D | B | S | O | P | H | C | X; break;
    default: m = D | B | (pos < 4 ? S | O : 0); break;
    }
    if (e->energy < 35) m &= ~(unsigned)(P | H | C);
    else if (e->energy < 55) m &= ~(unsigned)P;
    if (e->energy > 75) m |= C;
    return m;
}
RR_SMALL static unsigned next_section(struct rr_engine *e)
{
    unsigned sec = e->section, a = e->evolve * e->dim[RD_ARRANGE] / 100, dub = e->dub;
    if (e->freeze & (1U << RC_ARRANGE)) return sec;
    if (!e->evolve) return sec == SEC_INTRO ? SEC_VERSE : sec;   /* EVOLVE 0: stable form */
    if (a < 15) return sec == SEC_VERSE ? SEC_CHORUS : sec == SEC_CHORUS && rr_chance(&e->rng, dub / 2) ? SEC_DUB : SEC_VERSE;
    switch (sec) {
    case SEC_INTRO: return SEC_VERSE;
    case SEC_VERSE: return rr_chance(&e->rng, 20 + dub / 4) ? SEC_DUB : SEC_CHORUS;
    case SEC_CHORUS: return rr_chance(&e->rng, dub) ? SEC_DUB : rr_chance(&e->rng, a / 3) ? SEC_BREAK : SEC_VERSE;
    case SEC_DUB: return rr_chance(&e->rng, 40) ? SEC_BREAK : rr_chance(&e->rng, 50) ? SEC_CHORUS : SEC_VERSE;
    case SEC_BREAK: return rr_chance(&e->rng, 50 + a / 2) ? SEC_BUILD : SEC_CHORUS;
    case SEC_BUILD: return SEC_DROP;
    case SEC_DROP: return rr_chance(&e->rng, dub) ? SEC_DUB : SEC_CHORUS;
    default: return SEC_INTRO;
    }
}

/* ---------------- evolution ---------------- */
RR_SMALL static unsigned comp_dim(unsigned c)
{
    static const uint8_t map[RR_MOTIF_COMPS] = {RD_RHYTHM, RD_RHYTHM, RD_BASS, RD_INSTR, RD_INSTR, RD_INSTR, RD_INSTR, RD_FX};
    return map[c];
}
static unsigned effective(const struct rr_engine *e, unsigned dim) { return e->evolve * e->dim[dim] / 100U; }
RR_SMALL static int mutate(struct rr_engine *e, unsigned c, unsigned amount)
{
    struct rr_comp_state *s = &e->comp[c];
    uint32_t *r = &e->rng;
    unsigned style = s->cur.style, before = rr_motif_hash(&s->cur);
    amount = amount * s->depth / 100U;
    if (!amount) return 0;
    if (amount > 80 || (amount > 60 && rr_chance(r, 50))) {
        generate(e, c, style, rr_rand(r));          /* new phrase in the same style */
        if (c == RC_BASS || c == RC_HORNS) vary_line(&s->cur, r, amount, 1);
    } else if (amount > 40 && c <= RC_ORGAN && rr_chance(r, 60)) {
        /* Switch to the other grammar variant, keep motif identity elsewhere. */
        struct rr_motif old = s->cur;
        generate(e, c, style, rr_rand(r));
        if (c == RC_BASS) for (unsigned i = 0; i < RR_MOTIF; i += 8) {
            s->cur.hit[i] = old.hit[i]; s->cur.deg[i] = old.deg[i]; s->cur.len[i] = old.len[i];
            s->cur.vel[i] = old.vel[i]; s->cur.gate[i] = old.gate[i];
        }
    } else {
        if (c == RC_DRUMS || c == RC_PERC) vary_drums(&s->cur, r, amount, style);
        else if (c == RC_BASS || c == RC_HORNS) vary_line(&s->cur, r, amount, amount > 25);
        else if (c == RC_FX) generate(e, c, style, rr_rand(r));
        else vary_chords(&s->cur, r, amount);
        ++s->generation;
    }
    if (rr_motif_hash(&s->cur) == before) {
        /* A regenerated phrase can coincide with the old one: force audible change. */
        if (c == RC_DRUMS || c == RC_PERC) vary_drums(&s->cur, r, amount, style);
        else if (c == RC_BASS || c == RC_HORNS) vary_line(&s->cur, r, amount, 1);
        else if (c == RC_FX) s->cur.hit[rr_range(r, RR_MOTIF)] ^= 1;
        else vary_chords(&s->cur, r, 100);
    }
    s->age = 0;
    ++e->mutations;
    e->events |= EV_MUTATE;
    return 1;
}
RR_SMALL static void phrase_evolution(struct rr_engine *e)
{
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) {
        struct rr_comp_state *s = &e->comp[c];
        if (e->freeze & (1U << c)) continue;
        if (s->age < 255) ++s->age;
        unsigned eff = effective(e, comp_dim(c));
        unsigned p = eff * s->rate / 100U;
        if (eff < 30 && s->age > 2 && memcmp(&s->cur, &s->kept, sizeof s->cur)) {
            /* Predictable recovery: drift back to the accepted motif. */
            for (unsigned i = 0; i < RR_MOTIF; i += 2) {
                s->cur.hit[i] = s->kept.hit[i]; s->cur.deg[i] = s->kept.deg[i]; s->cur.vel[i] = s->kept.vel[i];
                s->cur.len[i] = s->kept.len[i]; s->cur.gate[i] = s->kept.gate[i];
            }
            if (s->age > 4) s->cur = s->kept;
            continue;
        }
        if (eff <= 20) continue;                 /* tier 1: humanisation only */
        unsigned amount = eff <= 40 ? eff / 2 : eff <= 60 ? eff - 10 : eff;
        /* Tier 2 changes one phrase in three; tier 5 changes nearly every phrase. */
        if (rr_chance(&e->rng, eff <= 40 ? p / 2 : p)) mutate(e, c, amount);
    }
    unsigned h = effective(e, RD_HARMONY);
    if (!(e->freeze & (1U << RC_HARMONY)) && h > 40 && rr_chance(&e->rng, (h - 40) * 2)) {
        new_progression(e, e->style, &e->rng);
        e->events |= EV_MUTATE;
    }
}
/* ---------------- transitions ---------------- */
static const uint8_t switch_order[RR_TMODES][RR_COMPS] = {
    {RC_FX, RC_PERC, RC_ORGAN, RC_PIANO, RC_HORNS, RC_HARMONY, RC_DRUMS, RC_SKANK, RC_ARRANGE, RC_BASS},
    {RC_FX, RC_HORNS, RC_PIANO, RC_ORGAN, RC_SKANK, RC_PERC, RC_HARMONY, RC_DRUMS, RC_ARRANGE, RC_BASS},
    {RC_FX, RC_PERC, RC_DRUMS, RC_BASS, RC_SKANK, RC_ORGAN, RC_PIANO, RC_HORNS, RC_HARMONY, RC_ARRANGE},
    {RC_FX, RC_PERC, RC_DRUMS, RC_BASS, RC_SKANK, RC_ORGAN, RC_PIANO, RC_HORNS, RC_HARMONY, RC_ARRANGE},
    {RC_DRUMS, RC_FX, RC_SKANK, RC_PERC, RC_HORNS, RC_ORGAN, RC_HARMONY, RC_PIANO, RC_BASS, RC_ARRANGE},
};
RR_SMALL static unsigned switch_bar(const struct rr_engine *e, unsigned index)
{
    unsigned bars = e->tbars ? e->tbars : 1;
    switch (e->tmode) {
    case TM_HARD: return 0;
    case TM_BREAK: return bars - 1;
    case TM_DUBMIX: return index < 2 ? 0 : (bars / 2) + (index - 2) * (bars - bars / 2) / RR_COMPS;
    default: return index * bars / RR_COMPS;
    }
}
RR_SMALL static void switch_comp(struct rr_engine *e, unsigned c)
{
    unsigned t = e->target;
    if (c == RC_HARMONY) {
        if (!(e->freeze & (1U << RC_HARMONY))) { set_mode_for(e, t); new_progression(e, t, &e->rng); }
        else for (unsigned b = 0; b < RR_PHRASE; ++b) e->prog_q[b] = (uint8_t)quality(e, e->prog[b], t);
        return;
    }
    if (c == RC_ARRANGE) return;
    struct rr_comp_state *s = &e->comp[c];
    if (e->freeze & (1U << c)) { s->cur.style = (uint8_t)t; return; }   /* identity kept */
    generate(e, c, t, rr_rand(&e->rng));
    s->kept = s->cur;
}
RR_SMALL static void transition_step(struct rr_engine *e, struct rr_bar_info *info)
{
    unsigned k = e->tprogress, done = 1;
    info->events |= EV_TRANSITION;
    for (unsigned i = 0; i < RR_COMPS; ++i) {
        unsigned c = switch_order[e->tmode][i];
        uint16_t bit = (uint16_t)(1U << c);
        unsigned when = switch_bar(e, i);
        /* MUTANT hybridises: components cross over early at random bars. */
        if (e->tmode == TM_MUTANT && !(e->tswitched & bit) && rr_chance(&e->rng, 30)) when = k;
        if (k >= when && !(e->tswitched & bit)) { switch_comp(e, c); e->tswitched |= bit; }
        if (!(e->tswitched & bit)) done = 0;
    }
    if (e->tmode == TM_DUBMIX && k < e->tbars / 2 + 1) {
        info->nfx = 1; info->fx_step[0] = 12; info->fx_kind[0] = FXK_ECHO_THROW; info->fx_arg[0] = RT_SKANK;
    }
    if (e->tmode == TM_BREAK && k + 1 == e->tbars) e->fill_bar = 1;
    /* Tempo plan. */
    unsigned from = e->bpm_from, to = e->bpm_target, bars = e->tbars ? e->tbars : 1;
    if (e->tempo_plan == 1) {           /* double time: ramp to half target, then jump */
        unsigned half = to / 2;
        if (k + 1 < bars) info->bpm = (uint8_t)(from + ((int)half - (int)from) * (int)(k + 1) / (int)bars);
        else { info->bpm = (uint8_t)to; info->tempo_jump = 1; info->events |= EV_TEMPO_JUMP; }
        for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) e->stretch[t] = (uint8_t)(k + 1 < bars ? 1 : 2);
    } else if (e->tempo_plan == 2) {    /* half time: jump to half, then ramp */
        if (k == 0) { info->bpm = (uint8_t)(from / 2); info->tempo_jump = 1; info->events |= EV_TEMPO_JUMP; }
        else info->bpm = (uint8_t)(from / 2 + ((int)to - (int)(from / 2)) * (int)k / (int)bars);
    } else if (e->tmode == TM_HARD) {
        info->bpm = (uint8_t)to; info->tempo_jump = 1;
    } else info->bpm = (uint8_t)(from + ((int)to - (int)from) * (int)(k + 1) / (int)bars);
    if (done && k + 1 >= bars) {
        e->style = e->target; e->transition = 0; e->tempo_plan = 0;
        info->bpm = (uint8_t)to;
        for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) e->stretch[t] = 1;
        e->stretch[RT_BASS] = rr_styles[e->style].bass_stretch;
        e->stretch[RT_SKANK] = e->stretch[RT_HORNS] = rr_styles[e->style].bass_stretch;
        info->events |= EV_TRANSITION_DONE;
    } else ++e->tprogress;
}

/* ---------------- rendering ---------------- */
RR_SMALL static unsigned reg_base(unsigned track, unsigned key)
{
    switch (track) {
    case RT_BASS: return 28 + (key + 8) % 12;        /* lowest note >= E1 */
    case RT_SKANK: return 55 + (key + 5) % 12;       /* G3..F#4 */
    case RT_ORGAN: return 48 + key % 12;
    case RT_PIANO: return 55 + (key + 5) % 12;
    default: return 60 + key % 12;
    }
}
RR_SMALL static void put(struct rr_cell *c, unsigned note, unsigned vel, unsigned chord, unsigned gate, unsigned tie)
{
    c->note = (uint8_t)rr_clamp((int)note, 0, 127);
    c->velocity = (uint8_t)rr_clamp((int)vel, 1, 127);
    c->bits = (uint16_t)((chord & 31U) | ((gate & 15U) << 5) | ((tie & 1U) << 12));
}
RR_SMALL static unsigned chord_for(const struct rr_engine *e, unsigned c, unsigned q, unsigned style)
{
    if (c == RC_SKANK) return q == CH_MIN || q == CH_MIN7 || q == CH_MIN9 ? CH_SKMIN : q == CH_MADD9 ? CH_MADD9 : CH_SKMAJ;
    if (c == RC_ORGAN && style <= RS_RUBADUB) return q == CH_MIN7 ? CH_MIN : q;
    if (c == RC_PIANO && e->comp[c].cur.voicing) return q == CH_MIN || q == CH_MIN7 ? CH_MIN7 : q == CH_MAJ ? CH_MAJ6 : q;
    return q;
}
RR_SMALL static unsigned line_note(const struct rr_engine *e, const struct rr_motif *m, unsigned j, unsigned track, unsigned root, int shift)
{
    unsigned base = reg_base(track, e->key);
    if (m->hit[j] & MH_CHORD) {
        int off = (int)rr_scale_note(0, e->mode, root, 0);
        if (off > 7 && track != RT_ORGAN) off -= 12;
        return (unsigned)rr_clamp((int)base + off + m->deg[j], 0, 127);
    }
    return base + rr_scale_note(0, e->mode, root, m->deg[j]) + (unsigned)shift;
}
RR_SMALL static void render_line(struct rr_engine *e, unsigned c, unsigned track, struct rr_cell out[RR_STEPS], unsigned pos, unsigned human)
{
    const struct rr_motif *m = &e->comp[c].cur;
    unsigned k = e->stretch[track] ? e->stretch[track] : 1, cycle = RR_MOTIF * k;
    unsigned root = e->prog[pos], q = e->prog_q[pos], was_active = (e->active >> track) & 1U;
    int shift = c == RC_BASS && e->bass_octave ? 12 : 0;
    for (unsigned s = 0; s < RR_STEPS; ++s) {
        uint32_t abs = e->bar * RR_STEPS + s, p = abs % cycle;
        /* Note ends first so an onset on the same step wins. */
        for (unsigned j = 0; j < RR_MOTIF; ++j) {
            if (!m->hit[j] || m->len[j] * k <= 1) continue;
            if ((j * k + m->len[j] * k - 1) % cycle != p) continue;
            if (s + 1 < m->len[j] * k && !was_active) continue;   /* onset was not played */
            put(&out[s], line_note(e, m, j, track, root, shift), m->vel[j], m->hit[j] & MH_CHORD ? chord_for(e, c, q, m->style) : 0, 14, 0);
        }
        if (p % k) continue;
        unsigned j = p / k, h = m->hit[j];
        if (!h) continue;
        if ((h & MH_GHOST) && c == RC_SKANK && !e->skank_muted && rr_chance(&e->rng, 30)) continue;
        int vel = m->vel[j];
        if (human) vel += (int)rr_range(&e->rng, human * 2 + 1) - (int)human;
        unsigned note = line_note(e, m, j, track, root, shift), chord = 0;
        if (h & MH_CHORD) {
            chord = chord_for(e, c, q, m->style);
            if (c == RC_SKANK && e->skank_muted) vel = vel * 3 / 4;
        }
        unsigned len = m->len[j] * k, gate = len > 1 ? 15 : m->gate[j];
        if (c == RC_SKANK && e->skank_muted) gate = 2;
        put(&out[s], note, (unsigned)vel, chord, gate, len > 1);
    }
}
RR_SMALL static void fill_steps(const struct rr_engine *e, struct rr_cell *cells, unsigned from, unsigned intensity, uint32_t r)
{
    unsigned style = e->comp[RC_DRUMS].cur.style;
    static const uint16_t toms[] = {DK_HT, DK_HT, DK_MT, DK_MT, DK_LT, DK_LT, DK_SD, DK_SD};
    for (unsigned s = from; s < RR_STEPS; ++s) {
        unsigned n = s - from, roll = style == RS_JUNGLE || style == RS_DUBNOISE || intensity > 70 ? 1 : 2;
        if (n % roll) { cells[s].bits &= (uint16_t)DK_BD; continue; }
        uint16_t hit = style == RS_DANCEHALL || style == RS_DUBTECHNO ? (uint16_t)(n & 2 ? DK_SD : DK_RS)
                                                                      : toms[(n * 8) / (RR_STEPS - from + 1) & 7];
        if (rr_range(&r, 100) < intensity / 3) hit |= DK_SD;
        cells[s].bits = (uint16_t)(hit | (s == 15 && intensity > 50 ? DK_BD : 0));
        cells[s].velocity = (uint8_t)rr_clamp((int)(70 + n * 50 / (RR_STEPS - from + 1)), 40, 127);
        cells[s].note = 0;
    }
}
RR_SMALL void rr_engine_fill(const struct rr_engine *e, struct rr_cell drums[RR_STEPS], unsigned from, unsigned intensity)
{
    if (from < RR_STEPS) fill_steps(e, drums, from, intensity, e->rng ^ e->bar);
}
RR_SMALL static void render_drums(struct rr_engine *e, struct rr_cell out[RR_STEPS], unsigned active, unsigned human)
{
    const struct rr_motif *d = &e->comp[RC_DRUMS].cur, *p = &e->comp[RC_PERC].cur;
    unsigned light = e->section == SEC_INTRO && e->phrase_pos < 2, build = e->section == SEC_BUILD;
    for (unsigned s = 0; s < RR_STEPS; ++s) {
        unsigned j = (e->bar * RR_STEPS + s) % RR_MOTIF, hit = 0, vel = 0;
        if (active & 1) {
            hit = d->hit[j]; vel = d->vel[j];
            if (light) hit &= (uint16_t)(DK_CH | DK_OH | DK_RS);
            if (e->nokick_bars) hit &= (uint16_t)~DK_BD;
            /* Tier 1+ humanisation: ghost hats and snare drags. */
            if (human && !(hit & DK_CH) && (s & 1) && rr_chance(&e->rng, human * 3)) { hit |= DK_CH; vel = vel ? vel : 44; }
            if (human > 6 && s == 7 && rr_chance(&e->rng, human * 2)) { hit |= DK_RS; vel = vel ? vel : 50; }
            if (build && e->phrase_pos >= 4 && (s & (e->phrase_pos >= 6 ? 1 : 3)) == 0) { hit |= DK_SD; vel = 60 + e->phrase_pos * 8; }
        }
        if (active & 64) {
            unsigned ph = p->hit[j];
            if (!e->perc_double && !(s & 1)) ph &= (uint16_t)~DK_CP;
            if (ph) { hit |= ph; vel = vel > p->vel[j] ? vel : p->vel[j]; }
        }
        if (!hit) continue;
        if (s == 0 && e->phrase_pos == 0 && e->section == SEC_DROP) hit |= DK_CY;
        if (human) vel = (unsigned)rr_clamp((int)vel + (int)rr_range(&e->rng, human * 2 + 1) - (int)human, 20, 127);
        out[s].note = 0; out[s].velocity = (uint8_t)(vel ? vel : 90); out[s].bits = (uint16_t)hit;
    }
    if (e->fill_bar && ((active & 1) || e->drop_bars || e->breakdown_bars)) {
        unsigned intensity = 40 + e->intensity / 2;
        fill_steps(e, out, e->fill_bar > 1 ? 8 : 12, intensity, e->rng);
        e->events |= EV_FILL;
    }
}

/* ---------------- public API ---------------- */
RR_SMALL void rr_engine_init(struct rr_engine *e, unsigned style, uint32_t seed)
{
    static const uint8_t keys[] = {9, 2, 4, 7, 0, 5, 11, 9};
    memset(e, 0, sizeof *e);
    if (style >= RR_STYLES) style = RS_ROOTS;
    e->seed = seed ? seed : 0x52696464u;
    e->rng = e->seed;
    e->style = e->target = (uint8_t)style;
    e->mode = rr_styles[style].mode;
    e->key = keys[rr_range(&e->rng, 8)];
    if (e->mode == 1) e->key = (uint8_t)((e->key + 3) % 12);
    e->energy = rr_styles[style].energy; e->dub = rr_styles[style].dub;
    e->evolve = 35; e->intensity = 60; e->tbars = 4;
    for (unsigned d = 0; d < RR_DIMS; ++d) e->dim[d] = 100;
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) e->stretch[t] = 1;
    e->stretch[RT_BASS] = e->stretch[RT_SKANK] = e->stretch[RT_HORNS] = rr_styles[style].bass_stretch;
    new_progression(e, style, &e->rng);
    memcpy(e->prog_kept, e->prog, RR_PHRASE); memcpy(e->progq_kept, e->prog_q, RR_PHRASE);
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) {
        e->comp[c].depth = 100; e->comp[c].rate = 80;
        generate(e, c, style, rr_rand(&e->rng));
        e->comp[c].kept = e->comp[c].cur;
    }
    e->section = SEC_INTRO;
}
RR_SMALL void rr_engine_render(struct rr_engine *e, struct rr_cell out[RR_GEN_TRACKS][RR_STEPS], struct rr_bar_info *info)
{
    memset(out, 0, sizeof(struct rr_cell) * RR_GEN_TRACKS * RR_STEPS);
    memset(info, 0, sizeof *info);
    e->phrase_pos = (uint8_t)(e->bar % RR_PHRASE);
    if (e->phrase_pos == 0 && e->bar) {
        e->section = (uint8_t)next_section(e);
        phrase_evolution(e);
        info->events |= EV_PHRASE;
        if (e->section == SEC_DUB) e->events |= EV_ECHO_THROW;
    }
    if (e->phrase_pos == RR_PHRASE - 1 && effective(e, RD_RHYTHM) > 20 && !(e->freeze & 1U) &&
        rr_chance(&e->rng, 30 + effective(e, RD_RHYTHM) / 2)) e->fill_bar = 1;
    if (e->section == SEC_BUILD && e->phrase_pos == RR_PHRASE - 1) e->fill_bar = 2;
    info->bpm = 0;
    if (e->transition) transition_step(e, info);
    unsigned active = section_mask(e, e->section, e->phrase_pos);
    if (e->breakdown_bars) { active &= ~1U; active |= 2U; --e->breakdown_bars; if (!e->breakdown_bars) e->fill_bar = 1; }
    if (e->bass_only_bars) { active &= 2U | 128U; --e->bass_only_bars; }
    if (e->drop_bars) { active &= (e->drop_bars > 1 ? 2U : 3U) | 128U; --e->drop_bars; info->events |= EV_DROP; }
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) if (e->comp[c].solo_bars) {
        static const uint8_t solo_mask[RR_MOTIF_COMPS] = {1, 64, 3, 5, 9, 33, 17, 128};
        active &= solo_mask[c] | 128U; --e->comp[c].solo_bars;
    }
    active &= ~(unsigned)e->user_mute;
    if (e->build_bars) { --e->build_bars; info->events |= EV_BUILD; }
    if (e->nokick_bars) --e->nokick_bars;
    unsigned human = effective(e, RD_RHYTHM) ? 2 + effective(e, RD_RHYTHM) / 12 : 0;
    render_drums(e, out[RT_DRUMS], active, human);
    if (active & 2) render_line(e, RC_BASS, RT_BASS, out[RT_BASS], e->phrase_pos, human / 2);
    if (active & 4) render_line(e, RC_SKANK, RT_SKANK, out[RT_SKANK], e->phrase_pos, human);
    if (active & 8) render_line(e, RC_ORGAN, RT_ORGAN, out[RT_ORGAN], e->phrase_pos, human);
    if (active & 16) render_line(e, RC_HORNS, RT_HORNS, out[RT_HORNS], e->phrase_pos, human);
    if (active & 32) render_line(e, RC_PIANO, RT_PIANO, out[RT_PIANO], e->phrase_pos, human);
    /* Dub-engineer cues from the FX motif, scaled by DUB. */
    if ((active & 128) && e->dub) {
        const struct rr_motif *f = &e->comp[RC_FX].cur;
        for (unsigned s = 0; s < RR_STEPS && info->nfx < 4; ++s) {
            unsigned j = (e->bar * RR_STEPS + s) % RR_MOTIF;
            if (!f->hit[j] || !rr_chance(&e->rng, e->dub)) continue;
            info->fx_step[info->nfx] = (uint8_t)s; info->fx_kind[info->nfx] = (uint8_t)f->deg[j];
            info->fx_arg[info->nfx++] = f->deg[j] == FXK_ECHO_THROW ? (uint8_t)(rr_chance(&e->rng, 50) ? RT_SKANK : RT_DRUMS) : f->vel[j];
        }
    }
    if (e->rewind_bars) --e->rewind_bars;
    if (e->fill_bar) e->fill_bar = 0;
    e->active = (uint8_t)active;
    info->events |= (uint16_t)(e->events | EV_BAR);
    e->events = 0;
    info->section = e->section; info->phrase_pos = e->phrase_pos; info->active = (uint8_t)active;
    info->chord_root = (uint8_t)rr_scale_note(e->key, e->mode, e->prog[e->phrase_pos], 0);
    info->chord_q = e->prog_q[e->phrase_pos];
    ++e->bar;
}
RR_SMALL void rr_engine_style(struct rr_engine *e, unsigned style, unsigned mode, unsigned bars, unsigned bpm)
{
    if (style >= RR_STYLES) return;
    if (e->transition && style == e->target) return;
    if (!e->transition && style == e->style) return;
    if (e->transition) e->style = e->target;   /* retarget from where we are */
    e->target = (uint8_t)style;
    e->tmode = (uint8_t)(mode < RR_TMODES ? mode : TM_SMOOTH);
    e->tbars = (uint8_t)(bars ? bars : 1);
    if (e->tmode == TM_HARD) e->tbars = 1;
    e->tprogress = 0; e->transition = 1; e->tswitched = 0;
    e->bpm_from = (uint8_t)(bpm ? bpm : rr_styles[e->style].bpm);
    e->bpm_target = rr_styles[style].bpm;
    unsigned from = e->bpm_from, to = e->bpm_target;
    e->tempo_plan = e->tmode == TM_HARD ? 0 : to * 10 >= from * 16 ? 1 : to * 16 <= from * 10 ? 2 : 0;
    if (e->tempo_plan == 1 && e->tbars < 2) e->tbars = 2;
    e->events |= EV_TRANSITION;
}
RR_SMALL void rr_engine_freeze(struct rr_engine *e, unsigned comp, int on)
{
    if (comp >= RR_COMPS) return;
    uint16_t bit = (uint16_t)(1U << comp);
    if (on < 0) on = !(e->freeze & bit);
    if (on) e->freeze |= bit; else e->freeze &= (uint16_t)~bit;
}
RR_SMALL void rr_engine_mutate(struct rr_engine *e, unsigned comp, unsigned depth)
{
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c)
        if ((comp == 255 || comp == c) && !(e->freeze & (1U << c))) mutate(e, c, depth);
    if ((comp == 255 || comp == RC_HARMONY) && !(e->freeze & (1U << RC_HARMONY)) && depth > 50) new_progression(e, e->style, &e->rng);
}
RR_SMALL void rr_engine_keep(struct rr_engine *e)
{
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) e->comp[c].kept = e->comp[c].cur;
    memcpy(e->prog_kept, e->prog, RR_PHRASE); memcpy(e->progq_kept, e->prog_q, RR_PHRASE);
}
RR_SMALL void rr_engine_reject(struct rr_engine *e)
{
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) if (!(e->freeze & (1U << c))) {
        e->comp[c].cur = e->comp[c].kept; e->comp[c].age = 0;
    }
    if (!(e->freeze & (1U << RC_HARMONY))) { memcpy(e->prog, e->prog_kept, RR_PHRASE); memcpy(e->prog_q, e->progq_kept, RR_PHRASE); }
}
RR_SMALL void rr_engine_command(struct rr_engine *e, unsigned cmd, unsigned arg)
{
    unsigned bars = 1 + e->intensity / 34;              /* 1..3 bars at default intensity */
    struct rr_comp_state *b = &e->comp[RC_BASS];
    switch (cmd) {
    case CMD_FILL: e->fill_bar = e->intensity > 70 ? 2 : 1; e->events |= EV_FILL; break;
    case CMD_SWITCH_GROOVE: if (!(e->freeze & 1U)) { generate(e, RC_DRUMS, e->comp[RC_DRUMS].cur.style, rr_rand(&e->rng)); e->events |= EV_MUTATE; } break;
    case CMD_DOUBLE_PERC: e->perc_double = !e->perc_double; break;
    case CMD_NO_KICK: e->nokick_bars = (uint8_t)(bars * 2); break;
    case CMD_BREAKDOWN: e->breakdown_bars = (uint8_t)(bars + 1); break;
    case CMD_NEW_BASS: if (!(e->freeze & (1U << RC_BASS))) { generate(e, RC_BASS, b->cur.style, rr_rand(&e->rng)); e->events |= EV_MUTATE; } break;
    case CMD_BASS_SOLO: if (!(e->freeze & (1U << RC_BASS))) vary_line(&b->cur, &e->rng, 60, 1); b->solo_bars = (uint8_t)(bars + 1); break;
    case CMD_BASS_OCTAVE: e->bass_octave = !e->bass_octave; break;
    case CMD_BASS_SYNC:
        if (!(e->freeze & (1U << RC_BASS))) for (unsigned i = 0; i < RR_MOTIF; ++i)
            if ((b->cur.hit[i] & MH_NOTE) && i % 4 == 0 && i % 8 && i + 1 < RR_MOTIF && !b->cur.hit[i + 1]) {
                b->cur.hit[i + 1] = b->cur.hit[i]; b->cur.deg[i + 1] = b->cur.deg[i]; b->cur.len[i + 1] = 1;
                b->cur.gate[i + 1] = 8; b->cur.vel[i + 1] = b->cur.vel[i]; b->cur.hit[i] = 0;
            }
        break;
    case CMD_BASS_HOME: b->cur = b->kept; e->bass_octave = 0; break;
    case CMD_BUBBLE:
        if (!(e->freeze & (1U << RC_ORGAN))) { motif_clear(&e->comp[RC_ORGAN].cur, e->comp[RC_ORGAN].cur.style);
            build_chords(&e->comp[RC_ORGAN].cur, "..lh..lh..lh..lh", &e->rng, 74); }
        break;
    case CMD_STABS:
        if (!(e->freeze & (1U << RC_ORGAN))) { motif_clear(&e->comp[RC_ORGAN].cur, e->comp[RC_ORGAN].cur.style);
            build_chords(&e->comp[RC_ORGAN].cur, "....x.......x..x", &e->rng, 92); }
        break;
    case CMD_KEYS_VARIATION: if (!(e->freeze & (1U << RC_PIANO))) mutate(e, RC_PIANO, 50); if (!(e->freeze & (1U << RC_ORGAN))) mutate(e, RC_ORGAN, 35); break;
    case CMD_VOICING: e->comp[RC_PIANO].cur.voicing ^= 1; for (unsigned i = 0; i < RR_MOTIF; ++i) if (e->comp[RC_ORGAN].cur.hit[i]) e->comp[RC_ORGAN].cur.deg[i] = (int8_t)(e->comp[RC_ORGAN].cur.deg[i] ? 0 : 7); break;
    case CMD_SKANK_CHANGE: if (!(e->freeze & (1U << RC_SKANK))) generate(e, RC_SKANK, e->comp[RC_SKANK].cur.style, rr_rand(&e->rng)); break;
    case CMD_SKANK_MUTED: e->skank_muted = !e->skank_muted; break;
    case CMD_SKANK_VARIATION: if (!(e->freeze & (1U << RC_SKANK))) mutate(e, RC_SKANK, 45); break;
    case CMD_MUTE_MUSIC: e->user_mute ^= (uint8_t)(arg ? arg : (1U << RT_SKANK) | (1U << RT_ORGAN) | (1U << RT_PIANO) | (1U << RT_HORNS)); break;
    case CMD_ECHO_THROW: e->events |= EV_ECHO_THROW; break;
    case CMD_SPRING_SNARE: e->events |= EV_SPRING; break;
    case CMD_BASS_ONLY: e->bass_only_bars = (uint8_t)(bars + 1); e->events |= EV_ECHO_THROW; break;
    case CMD_BIG_DROP: case CMD_DROP: e->drop_bars = (uint8_t)(cmd == CMD_BIG_DROP ? bars + 2 : bars + 1); e->events |= EV_DROP; break;
    case CMD_RESTORE_ALL: e->user_mute = 0; e->drop_bars = e->bass_only_bars = e->breakdown_bars = e->nokick_bars = 0;
        for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) e->comp[c].solo_bars = 0; break;
    case CMD_NEXT_VERSION:
        /* Same riddim (bass + harmony), new dub version around it. */
        ++e->version;
        for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c)
            if (c != RC_BASS && !(e->freeze & (1U << c))) mutate(e, c, 55 + e->intensity / 3);
        e->section = SEC_DUB; e->events |= EV_ECHO_THROW | EV_MUTATE;
        break;
    case CMD_BUILD: e->section = SEC_BUILD; e->build_bars = 4; e->events |= EV_BUILD; break;
    case CMD_SOLO: if (arg < RR_MOTIF_COMPS) e->comp[arg].solo_bars = (uint8_t)(bars + 1); break;
    case CMD_MUTATE: rr_engine_mutate(e, 255, 40 + e->intensity / 2); break;
    case CMD_RESTORE: rr_engine_reject(e); e->user_mute = 0; break;
    case CMD_REWIND: e->rewind_bars = 1; e->bar -= e->bar % RR_PHRASE; e->events |= EV_REWIND; break;
    case CMD_KEEP: rr_engine_keep(e); break;
    case CMD_REJECT: rr_engine_reject(e); break;
    case CMD_REBUILD:
        for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) if (!(e->freeze & (1U << c))) generate(e, c, e->comp[c].cur.style, rr_rand(&e->rng));
        if (!(e->freeze & (1U << RC_HARMONY))) new_progression(e, e->style, &e->rng);
        if (!(e->freeze & (1U << RC_ARRANGE))) e->section = SEC_INTRO;
        e->events |= EV_MUTATE; ++e->mutations;
        break;
    case CMD_FREEZE_TOGGLE: rr_engine_freeze(e, arg, -1); break;
    case CMD_SIREN: e->events |= EV_SIREN; break;
    }
}
RR_SMALL unsigned rr_engine_tempo(const struct rr_engine *e, unsigned current)
{
    return e->transition ? e->bpm_target : current;
}
RR_SMALL unsigned rr_motif_hash(const struct rr_motif *m)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < RR_MOTIF; ++i) {
        h = (h ^ m->hit[i]) * 16777619u; h = (h ^ (uint8_t)m->deg[i]) * 16777619u; h = (h ^ m->len[i]) * 16777619u;
    }
    return h;
}
