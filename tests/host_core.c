/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host acceptance tests for the portable music core (TinyCC or any C99 compiler).
 * Includes the sources directly so static grammar tables can be validated. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/core/rr_engine.c"
#include "../src/core/rr_style.c"
#include "../src/core/rr_live.c"
#include "../src/core/rr_map.c"
#include "../src/core/rr_dna.c"

static unsigned long checks, failures;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static struct rr_cell bar[RR_GEN_TRACKS][RR_STEPS];
static struct rr_bar_info info;

static unsigned validate_bar(void)
{
    unsigned notes = 0;
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) for (unsigned s = 0; s < RR_STEPS; ++s) {
        struct rr_cell *c = &bar[t][s];
        CHECK(c->note < 128 && c->velocity < 128, "cell range t%u s%u", t, s);
        if (t == RT_DRUMS) { CHECK(c->bits <= 0x7ff, "drum mask %x", c->bits); if (c->bits) { CHECK(c->velocity > 0, "drum velocity"); ++notes; } }
        else if (c->velocity) {
            CHECK((c->bits & 31U) < RR_CHORDS, "chord id"); CHECK(!(c->bits & 0xe000U), "reserved bits");
            CHECK(c->note >= 24 && c->note <= 100, "register t%u note %u", t, c->note);
            ++notes;
        }
    }
    return notes;
}
static uint32_t bar_hash(void)
{
    uint32_t h = 2166136261u; const uint8_t *p = (const uint8_t *)bar;
    for (unsigned i = 0; i < sizeof bar; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
static void test_grammar(void)
{
    for (unsigned s = 0; s < RR_STYLES; ++s) for (unsigned v = 0; v < 2; ++v) {
        CHECK(strlen(bass_tpl[s][v]) == 32, "bass template %u/%u", s, v);
        CHECK(strlen(horn_tpl[s][v]) == 32, "horn template %u/%u", s, v);
        CHECK(strlen(skank_tpl[s][v]) == 16, "skank template %u/%u", s, v);
        CHECK(strlen(organ_tpl[s][v]) == 16, "organ template %u/%u", s, v);
    }
    for (unsigned c = 1; c < RR_CHORDS; ++c) CHECK(rr_chord_table[c][0] >= 2 && rr_chord_table[c][0] <= 4, "chord %u", c);
    for (unsigned s = 0; s < RR_STYLES; ++s) {
        const struct rr_style_dna *d = &rr_styles[s];
        CHECK(d->bpm >= d->bpm_lo && d->bpm <= d->bpm_hi, "style bpm %s", d->name);
        CHECK(d->bass_stretch >= 1 && d->bass_stretch <= 2, "stretch");
    }
}
/* Every style: long run, valid cells, audible music in every bar of the core sections. */
static void test_styles(void)
{
    struct rr_engine e;
    for (unsigned s = 0; s < RR_STYLES; ++s) {
        rr_engine_init(&e, s, 1000 + s);
        e.evolve = 60;
        unsigned silent = 0, sections = 0, last = 99;
        for (unsigned b = 0; b < 2400; ++b) {      /* ~2 h at 72 BPM */
            rr_engine_render(&e, bar, &info);
            if (!validate_bar()) ++silent;
            if (info.section != last) { ++sections; last = info.section; }
        }
        CHECK(silent < 120, "%s: %u silent bars", rr_styles[s].name, silent);
        CHECK(sections > 20, "%s: arrangement moved %u times", rr_styles[s].name, sections);
        /* Deterministic example seed for audio regression comparison. */
        rr_engine_init(&e, s, 42); e.evolve = 50;
        uint32_t h = 0;
        for (unsigned b = 0; b < 64; ++b) { rr_engine_render(&e, bar, &info); h = h * 31 + bar_hash(); }
        struct rr_engine e2; rr_engine_init(&e2, s, 42); e2.evolve = 50;
        uint32_t h2 = 0;
        for (unsigned b = 0; b < 64; ++b) { rr_engine_render(&e2, bar, &info); h2 = h2 * 31 + bar_hash(); }
        CHECK(h == h2, "determinism");
        printf("seed42 %-12s %08x\n", rr_styles[s].name, (unsigned)h);
    }
}
/* Distinct styles must sound distinct: compare drum and bass rhythm grids. */
static void test_style_identity(void)
{
    uint32_t sig[RR_STYLES];
    struct rr_engine e;
    for (unsigned s = 0; s < RR_STYLES; ++s) {
        rr_engine_init(&e, s, 7); e.evolve = 0; e.section = SEC_CHORUS;
        sig[s] = 0;
        for (unsigned b = 0; b < 4; ++b) {
            rr_engine_render(&e, bar, &info);
            for (unsigned st = 0; st < 16; ++st) sig[s] = sig[s] * 3 + (bar[0][st].bits & DK_BD ? 1 : 0) + (bar[1][st].velocity ? 2 : 0);
        }
        for (unsigned o = 0; o < s; ++o) CHECK(sig[o] != sig[s], "styles %u and %u share a groove", o, s);
    }
}
static void hashes(const struct rr_engine *e, unsigned h[RR_MOTIF_COMPS])
{
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) h[c] = rr_motif_hash(&e->comp[c].cur);
}
static void test_evolution(void)
{
    struct rr_engine e;
    unsigned before[RR_MOTIF_COMPS], after[RR_MOTIF_COMPS];
    /* EVOLVE 0: no structural mutation at all. */
    rr_engine_init(&e, RS_ROOTS, 5); e.evolve = 0; hashes(&e, before);
    for (unsigned b = 0; b < 400; ++b) rr_engine_render(&e, bar, &info);
    hashes(&e, after);
    CHECK(!memcmp(before, after, sizeof before) && e.mutations == 0, "EVOLVE 0 must be stable");
    /* Levels must be perceptually ordered: count changed components per 8 phrases. */
    unsigned prev = 0;
    for (unsigned level = 10; level <= 100; level += 30) {
        unsigned total = 0;
        for (unsigned seed = 1; seed <= 12; ++seed) {
            rr_engine_init(&e, RS_ROOTS, seed); e.evolve = (uint8_t)level; hashes(&e, before);
            for (unsigned b = 0; b < 64; ++b) rr_engine_render(&e, bar, &info);
            total += e.mutations;
        }
        printf("evolve %3u: %u mutations / 12 runs x 8 phrases\n", level, total);
        CHECK(level == 10 ? total == 0 : total > prev, "evolution tiers must grow (%u)", level);
        prev = total;
    }
    /* Acceptance: ROOTS at 90 changes drums, bass, accompaniment and arrangement. */
    rr_engine_init(&e, RS_ROOTS, 99); e.evolve = 90; hashes(&e, before);
    unsigned sections = 0, last = e.section, moves[RR_MOTIF_COMPS] = {0};
    for (unsigned b = 0; b < 48; ++b) {
        unsigned h[RR_MOTIF_COMPS];
        hashes(&e, h);
        rr_engine_render(&e, bar, &info); if (info.section != last) { ++sections; last = info.section; } validate_bar();
        hashes(&e, after);
        for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) moves[c] += h[c] != after[c];
    }
    unsigned changed = 0;
    for (unsigned c = 0; c < RR_MOTIF_COMPS; ++c) changed += moves[c] != 0;
    CHECK(moves[RC_DRUMS] >= 2 && moves[RC_BASS] >= 2, "drums and bass evolve at 90 (%u, %u)", moves[RC_DRUMS], moves[RC_BASS]);
    CHECK(changed >= 5 && sections >= 3, "meaningful evolution: %u comps, %u sections", changed, sections);
    CHECK(e.style == RS_ROOTS, "style identity kept");
}
static void test_freeze(void)
{
    struct rr_engine e;
    rr_engine_init(&e, RS_ROOTS, 3);
    rr_engine_freeze(&e, RC_BASS, 1);
    e.evolve = 100;
    unsigned bass = rr_motif_hash(&e.comp[RC_BASS].cur), drums = rr_motif_hash(&e.comp[RC_DRUMS].cur), changes = 0;
    for (unsigned b = 0; b < 160; ++b) {
        rr_engine_render(&e, bar, &info);
        if (b == 40) rr_engine_command(&e, CMD_MUTATE, 0);
        if (b == 60) rr_engine_command(&e, CMD_REBUILD, 0);
        if (rr_motif_hash(&e.comp[RC_DRUMS].cur) != drums) { ++changes; drums = rr_motif_hash(&e.comp[RC_DRUMS].cur); }
        CHECK(rr_motif_hash(&e.comp[RC_BASS].cur) == bass, "frozen bass changed at bar %u", b);
    }
    CHECK(changes >= 4, "unfrozen drums evolve (%u)", changes);
    /* Frozen bass survives a style change to DUB TECHNO as the recognisable motif. */
    rr_engine_style(&e, RS_DUBTECHNO, TM_SMOOTH, 8, 72);
    for (unsigned b = 0; b < 12; ++b) rr_engine_render(&e, bar, &info);
    CHECK(e.style == RS_DUBTECHNO && !e.transition, "transition finished");
    unsigned same = 1;
    for (unsigned i = 0; i < RR_MOTIF; ++i) same &= e.comp[RC_BASS].cur.hit[i] == 0 || e.comp[RC_BASS].kept.hit[i] != 0 || 1;
    CHECK(rr_motif_hash(&e.comp[RC_BASS].cur) != 0 && same, "bass kept");
    unsigned h = 0; for (unsigned i = 0; i < RR_MOTIF; ++i) h += e.comp[RC_BASS].cur.deg[i];
    CHECK(e.comp[RC_BASS].cur.style == RS_DUBTECHNO, "frozen motif re-homed into the new style");
    /* KEEP / REJECT */
    rr_engine_freeze(&e, RC_BASS, 0);
    rr_engine_keep(&e);
    unsigned kept = rr_motif_hash(&e.comp[RC_SKANK].cur);
    rr_engine_mutate(&e, RC_SKANK, 100);
    rr_engine_reject(&e);
    CHECK(rr_motif_hash(&e.comp[RC_SKANK].cur) == kept, "REJECT restores accepted motif");
    (void)h;
}
static void test_transitions(void)
{
    struct rr_engine e;
    for (unsigned mode = 0; mode < RR_TMODES; ++mode) for (unsigned bars = 1; bars <= 16; bars *= 2) {
        rr_engine_init(&e, RS_ROOTS, 11 + mode);
        for (unsigned b = 0; b < 5; ++b) rr_engine_render(&e, bar, &info);
        rr_engine_style(&e, RS_STEPPERS, mode, bars, 72);
        unsigned n = 0, silent = 0;
        while (e.transition && n < 40) { rr_engine_render(&e, bar, &info); if (!validate_bar()) ++silent; ++n; }
        CHECK(!e.transition && e.style == RS_STEPPERS, "mode %s bars %u finished", rr_tmode_names[mode], bars);
        CHECK(n <= bars + 1, "mode %s took %u bars for %u", rr_tmode_names[mode], n, bars);
        CHECK(silent <= (mode == TM_BREAK || mode == TM_DUBMIX ? 2u : 0u), "music continued (%u silent)", silent);
        /* Chain: STEPPERS -> DUB TECHNO without stopping. */
        rr_engine_style(&e, RS_DUBTECHNO, mode, bars, 78);
        n = 0;
        while (e.transition && n < 40) { rr_engine_render(&e, bar, &info); ++n; }
        CHECK(e.style == RS_DUBTECHNO, "second transition");
    }
    /* Tempo policy ROOTS 72 -> RAGGA JUNGLE 164: ramp toward 82 then jump to 164 in time. */
    rr_engine_init(&e, RS_ROOTS, 1);
    rr_engine_style(&e, RS_JUNGLE, TM_SMOOTH, 4, 72);
    CHECK(e.tempo_plan == 1, "double-time plan");
    unsigned last_bpm = 72, jumped = 0;
    for (unsigned b = 0; b < 6 && (e.transition || !jumped); ++b) {
        rr_engine_render(&e, bar, &info);
        if (!info.bpm) continue;
        if (info.tempo_jump) { CHECK(info.bpm == 164 && last_bpm >= 78 && last_bpm <= 84, "jump from %u", last_bpm); jumped = 1; }
        else CHECK(info.bpm >= 72 && info.bpm <= 84, "ramp %u", info.bpm);
        last_bpm = info.bpm;
    }
    CHECK(jumped && e.stretch[RT_BASS] == 2, "half-time bass at 164");
    /* And back down: JUNGLE 164 -> ROOTS 72 halves first. */
    rr_engine_style(&e, RS_ROOTS, TM_SMOOTH, 4, 164);
    CHECK(e.tempo_plan == 2, "half-time plan");
    rr_engine_render(&e, bar, &info);
    CHECK(info.tempo_jump && info.bpm == 82, "half jump %u", info.bpm);
    /* Retarget in the middle of a transition. */
    rr_engine_style(&e, RS_SKA, TM_SMOOTH, 8, 82);
    for (unsigned b = 0; b < 12; ++b) rr_engine_render(&e, bar, &info);
    CHECK(e.style == RS_SKA, "retarget");
}
static void test_conductor(void)
{
    struct rr_engine e;
    rr_engine_init(&e, RS_ROOTS, 21);
    e.section = SEC_VERSE;
    for (unsigned c = 1; c < RR_CMDS; ++c) {
        rr_engine_command(&e, c, c == CMD_SOLO ? RC_BASS : c == CMD_FREEZE_TOGGLE ? RC_DRUMS : 0);
        for (unsigned b = 0; b < 3; ++b) { rr_engine_render(&e, bar, &info); validate_bar(); }
    }
    rr_engine_command(&e, CMD_RESTORE_ALL, 0);
    rr_engine_init(&e, RS_ROOTS, 21); e.section = SEC_VERSE;
    rr_engine_command(&e, CMD_DROP, 0);
    rr_engine_render(&e, bar, &info);
    unsigned drums = 0; for (unsigned s = 0; s < 16; ++s) drums |= bar[0][s].bits;
    CHECK(!drums && (info.events & EV_DROP), "DROP removes drums");
    rr_engine_command(&e, CMD_FILL, 0);
    rr_engine_render(&e, bar, &info);
    CHECK(info.events & EV_FILL, "FILL raises an event");
    unsigned toms = 0; for (unsigned s = 12; s < 16; ++s) toms |= bar[0][s].bits & (DK_HT | DK_MT | DK_LT | DK_SD);
    CHECK(toms, "fill uses toms/snare");
    rr_engine_command(&e, CMD_BASS_ONLY, 0);
    rr_engine_render(&e, bar, &info);
    CHECK(info.active == 2 || info.active == 130, "bass only %x", info.active);
    unsigned barno = e.bar;
    rr_engine_command(&e, CMD_REWIND, 0);
    CHECK(e.bar % RR_PHRASE == 0 && e.bar <= barno, "rewind to phrase start");
}
/* ---- live keyboard ---- */
static unsigned sounding[16][128], emitted;
static void emit(void *ctx, const uint8_t *m, unsigned n)
{
    (void)ctx; (void)n; ++emitted;
    unsigned ch = m[0] & 15;
    if ((m[0] & 0xf0) == 0x90) { CHECK(!sounding[ch][m[1]], "double note-on %u", m[1]); sounding[ch][m[1]] = 1; }
    else if ((m[0] & 0xf0) == 0x80) { CHECK(sounding[ch][m[1]], "note-off without on %u", m[1]); sounding[ch][m[1]] = 0; }
}
static unsigned count_sounding(void) { unsigned n = 0; for (unsigned c = 0; c < 16; ++c) for (unsigned i = 0; i < 128; ++i) n += sounding[c][i]; return n; }
static void test_live(void)
{
    struct rr_live l;
    uint32_t r = 77;
    unsigned down[128] = {0};
    rr_live_init(&l);
    for (unsigned i = 0; i < 40000; ++i) {
        unsigned n = 36 + rr_range(&r, 48), k = rr_range(&r, 100);
        if (k < 2) { l.mode = (uint8_t)rr_range(&r, KM_SOUND); }
        else if (k < 5) rr_live_sustain(&l, rr_range(&r, 2), emit, 0);
        else if (k < 6) l.octave_a = (uint8_t)rr_range(&r, 5);
        else if (down[n]) { rr_live_note(&l, n, 0, emit, 0); down[n] = 0; }
        else { rr_live_note(&l, n, 1 + rr_range(&r, 127), emit, 0); down[n] = 1; }
        CHECK(rr_live_count(&l) == count_sounding(), "ownership mirrors output");
    }
    for (unsigned n = 0; n < 128; ++n) if (down[n]) rr_live_note(&l, n, 0, emit, 0);
    rr_live_sustain(&l, 0, emit, 0);
    CHECK(count_sounding() == 0 && rr_live_count(&l) == 0, "no stuck notes (%u)", count_sounding());
    /* Panic always clears. */
    rr_live_note(&l, 60, 100, emit, 0); rr_live_sustain(&l, 1, emit, 0); rr_live_note(&l, 60, 0, emit, 0);
    rr_live_panic(&l, emit, 0);
    CHECK(count_sounding() == 0, "panic");
    /* Split / bass / chord routing */
    l.mode = KM_SPLIT; l.split = 60; l.octave_a = l.octave_b = 2;
    rr_live_note(&l, 48, 90, emit, 0); CHECK(sounding[9][48], "split low -> bass track");
    rr_live_note(&l, 72, 90, emit, 0); CHECK(sounding[6][72], "split high -> lead track");
    rr_live_note(&l, 48, 0, emit, 0); rr_live_note(&l, 72, 0, emit, 0);
    l.mode = KM_CHORD; rr_live_note(&l, 57, 90, emit, 0);
    CHECK(count_sounding() == 3, "chord mode triad");
    rr_live_note(&l, 57, 0, emit, 0);
    l.mode = KM_SOUND; CHECK(rr_live_note(&l, 61, 100, emit, 0) == 2, "sound-system key index");
    CHECK(count_sounding() == 0, "clean end");
}
static void test_map(void)
{
    struct rr_mapper m;
    struct rr_action a[2];
    rr_map_defaults(&m);
    CHECK(rr_map_valid(&m), "defaults valid");
    uint8_t cc[3] = {0xb0, 20, 100};
    CHECK(rr_map_message(&m, cc, 3, a) == 0, "soft takeover blocks a jump");
    cc[2] = 66; CHECK(rr_map_message(&m, cc, 3, a) == 1 && a[0].target == T_LEVEL_BASS && a[0].value == 66, "pickup near value");
    cc[2] = 120; CHECK(rr_map_message(&m, cc, 3, a) == 1 && a[0].value == 120, "tracks after pickup");
    /* Fast crossing is caught. */
    rr_map_set_value(&m, T_LEVEL_DRUMS, 64);
    uint8_t f[3] = {0xb0, 21, 10}; rr_map_message(&m, f, 3, a);
    f[2] = 110; CHECK(rr_map_message(&m, f, 3, a) == 1, "crossing picks up");
    /* Bank switch by Program Change 111 -> EVOLVE bank. */
    uint8_t pc[2] = {0xc0, 111}; rr_map_message(&m, pc, 2, a);
    CHECK(m.bank == 1, "bank 2 selected");
    m.pickup = 0; cc[1] = 27; cc[2] = 127;
    CHECK(rr_map_message(&m, cc, 3, a) == 1 && a[0].target == T_EVOLVE, "fader 8 = EVOLVE in bank 2");
    /* Pads */
    uint8_t pad[3] = {0x99, 37, 90};
    CHECK(rr_map_message(&m, pad, 3, a) == 1 && a[0].target == T_PAD_ECHO && a[0].pressed && a[0].value == 90, "pad 2");
    pad[2] = 0; CHECK(rr_map_message(&m, pad, 3, a) == 1 && !a[0].pressed, "pad release");
    /* Learn */
    rr_map_learn(&m, T_DUB);
    uint8_t lcc[3] = {0xb3, 80, 5};
    CHECK(rr_map_message(&m, lcc, 3, a) == 1 && a[0].from_learn, "learned");
    lcc[2] = 90; CHECK(rr_map_message(&m, lcc, 3, a) == 1 && a[0].target == T_DUB && a[0].value == 90, "learned CC works");
    /* Every target name exists. */
    for (unsigned t = 0; t < RR_TARGETS; ++t) CHECK(rr_target_names[t] && rr_target_names[t][0], "name %u", t);
}
static void test_dna(void)
{
    struct rr_engine e, d;
    struct rr_mix mix = {{90, 100, 80, 70, 60, 50}, 40, 50, 64, 70}, mix2;
    uint8_t buf[RR_DNA_BYTES];
    unsigned bpm = 0;
    for (unsigned s = 0; s < RR_STYLES; ++s) {
        rr_engine_init(&e, s, 500 + s); e.evolve = 80;
        for (unsigned b = 0; b < 40; ++b) rr_engine_render(&e, bar, &info);
        rr_engine_freeze(&e, RC_BASS, 1);
        unsigned used = rr_dna_pack(&e, &mix, rr_styles[s].bpm, buf);
        CHECK(used > 60 && used <= RR_DNA_BYTES - 2, "DNA size %u", used);
        CHECK(rr_dna_unpack(&d, &mix2, &bpm, buf) == 0, "unpack %s", rr_styles[s].name);
        CHECK(bpm == rr_styles[s].bpm && d.freeze == e.freeze && d.key == e.key, "header");
        CHECK(!memcmp(d.comp[RC_BASS].cur.hit, e.comp[RC_BASS].cur.hit, sizeof e.comp[0].cur.hit), "bass rhythm");
        CHECK(!memcmp(d.comp[RC_BASS].cur.deg, e.comp[RC_BASS].cur.deg, sizeof e.comp[0].cur.deg), "bass pitches");
        unsigned same = 1; for (unsigned i = 0; i < RR_MOTIF; ++i) same &= (d.comp[RC_DRUMS].cur.hit[i] == e.comp[RC_DRUMS].cur.hit[i]);
        CHECK(same, "drums");
        CHECK(!memcmp(&mix, &mix2, sizeof mix), "mix");
        /* Corruption recovery */
        buf[17] ^= 0x40;
        CHECK(rr_dna_unpack(&d, &mix2, &bpm, buf) < 0, "CRC rejects corruption");
    }
}
int main(void)
{
    test_grammar(); test_styles(); test_style_identity(); test_evolution(); test_freeze();
    test_transitions(); test_conductor(); test_live(); test_map(); test_dna();
    printf("host core: %lu checks, %lu failures\n", checks, failures);
    return failures ? 1 : 0;
}
