/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* RIDDIM ROOTS firmware glue. Control task only, except rr_midi_receive
 * (routing task, lock-free queue). The audio engine never stops: style,
 * tempo, instrument and memory changes are all applied while it plays. */
#include "rr_app.h"
#include "rr_fx.h"
#include "rr_sound.h"
#include "rr_ui.h"
#include "instrument.h"
#include "fm1_engine_control.h"
#include <string.h>

struct rr_state rr;
const char *const rr_page_names[RR_PAGES] = {"HOME", "STYLE", "EVOLVE", "FREEZE", "INSTRUMENTS", "LIVE", "DUB FX", "CONDUCTOR", "MIDI", "SYSTEM"};
const char *const rr_sys_names[RR_SYS] = {"STORE MEM", "RECALL MEM", "SAVE FLASH", "LOAD FLASH", "PANIC", "SIMPLE/PERF", "DEFAULTS", "SAVE MIDI MAP"};
typedef char rr_cell_layout[sizeof(struct rr_cell) == sizeof(struct groove_step) ? 1 : -1];

enum { BTN_OCTDN, BTN_OCTUP, BTN_FX, BTN_SCL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY, BTN_REC };
enum { ENC_SELECT, ENC_BPM, ENC_K1, ENC_K2, ENC_K3, ENC_K4, ENC_STYLE };
enum { SETTINGS_SLOT = 15, FLASH_FIRST = 8, MQ = 128 };
#define BIT(b) (1U << (b))

/* ---- diagnostics block, found by tests through its magic ---- */
static volatile struct {
    uint32_t magic, ms, playing, style, target, transition, bpm, evolve, freeze, bar_played;
    uint32_t section, live_notes, notes_in, cc_in, midi_errors, page, kmode, mutations, fx_blocks, fx_peak;
    uint32_t underruns, overruns, lion, bank, last_target, last_value, active, energy, dub, bpm_target;
    uint32_t hash[8], inst[8], level[6], dim[7], mem_used, fx_voices, version, save_result, load_result, shed, pixels, polls;
} diag;

/* ---- MIDI queue from the routing task ---- */
static volatile uint32_t mq_write;
static uint32_t mq_read;
static volatile uint32_t mq[MQ];
static inline void barrier(void)
{
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
}
RR_SMALL int rr_midi_receive(unsigned source, const uint8_t *m, size_t n)
{
    (void)source;
    if (!m || !n || n > 3) return 0;
    uint8_t s = m[0];
    if (s == 0xf8 || s == 0xfe || (s >= 0xf0 && s != 0xfa && s != 0xfb && s != 0xfc && s != 0xff)) return 1;
    uint32_t w = mq_write;
    if (w - mq_read >= MQ) { ++rr.midi_errors; return 1; }
    mq[w & (MQ - 1)] = s | (n > 1 ? (uint32_t)m[1] << 8 : 0) | (n > 2 ? (uint32_t)m[2] << 16 : 0) | ((uint32_t)n << 24);
    barrier();
    mq_write = w + 1;
    fm1_control_request();
    return 1;
}

/* ---- sequencer scheduling ---- */
static struct {
    uint32_t written, playing_abs;      /* bars written / bar now playing (since PLAY) */
    uint8_t play_slot, started, fired[4];
    struct rr_bar_info info[4];
    struct rr_cell drums[4][RR_STEPS];
    uint8_t track_style[4][RR_GEN_TRACKS];
    struct rr_engine before;            /* engine state before the queued bar: commands re-render it */
    uint8_t can_rerender;
} seq;
static struct rr_cell cells[RR_GEN_TRACKS][RR_STEPS];
static uint32_t now_ms, tempo_ms, style_ms, toast_ms, diag_ms, midi_ms;
static uint32_t touched[RR_TARGETS];
static uint8_t inst_user[RR_ROLES_UI];
static struct { uint8_t track, send, active; uint32_t until; } throw_state;
static uint32_t spring_until;
static uint8_t base_sends[RR_GEN_TRACKS];

RR_SMALL static void emit(void *ctx, const uint8_t *m, unsigned n)
{
    (void)ctx;
    if (fm1_midi_post(m, n)) ++rr.midi_errors;
}
RR_SMALL void rr_app_toast(const char *text)
{
    unsigned i = 0;
    for (; text[i] && i < sizeof rr.toast - 1; ++i) rr.toast[i] = text[i];
    rr.toast[i] = 0;
    rr.toast_until = now_ms + 1600;
}
static int owned(unsigned target) { return touched[target] && now_ms - touched[target] < (uint32_t)rr.release_s * 1000U; }

static void view(unsigned track, struct groove_view *v) { groove_get_view(track, 0, v); }
RR_SMALL static unsigned style_inst(unsigned style, unsigned role)
{
    const struct rr_style_dna *d = &rr_styles[style];
    switch (role) {
    case RT_BASS: return d->bass_inst;
    case RT_SKANK: return d->skank_inst;
    case RT_ORGAN: return d->organ_inst;
    case RT_HORNS: return d->horn_inst;
    case RT_PIANO: return d->piano_inst;
    default: return I_DRUMKIT;
    }
}
RR_SMALL static unsigned role_sends(unsigned style, unsigned role)
{
    unsigned s = rr_styles[style].sends;
    switch (role) {
    case RT_DRUMS: return 0x21;
    case RT_BASS: return 0x00;
    case RT_SKANK: return s;
    case RT_ORGAN: return (s & 0x0f) | 0x10;
    case RT_HORNS: return s + 0x11 > 0xff ? 0xff : s + 0x11;
    case RT_PIANO: return s & 0x3f;
    default: return 0x22;
    }
}
RR_SMALL static void fill_preset(struct groove_preset *p, unsigned role, unsigned inst, unsigned style)
{
    unsigned level = role < RR_GEN_TRACKS ? rr.mix.level[role] : 104;
    if (role == RT_DRUMS) rr_sound_kit(p, rr.kit, level);
    else rr_sound_preset(p, inst, level, role < RR_GEN_TRACKS ? role_sends(style, role) : 0x22);
}
/* Apply one track's sound while playing. */
RR_SMALL static void apply_timbre(unsigned role, unsigned inst)
{
    struct groove_project *p = groove_project_scratch();
    if (!p) return;
    (void)groove_project_capture(p);
    unsigned style = role < RR_GEN_TRACKS ? seq.track_style[seq.play_slot][role] : rr.e.style;
    if (style >= RR_STYLES) style = rr.e.style;
    fill_preset(&p->preset[role], role, inst, style);
    if (!groove_timbre_apply(role, p)) {
        rr.inst[role] = (uint8_t)inst;
        if (role < RR_GEN_TRACKS) base_sends[role] = p->preset[role].data[209];
    }
}
RR_SMALL void rr_app_instrument(unsigned role, unsigned inst)
{
    if (role >= RR_ROLES_UI) return;
    if (role == RT_DRUMS) { rr.kit = (uint8_t)(inst % RR_KITS); inst = I_DRUMKIT; }
    else inst_user[role] = 1;
    apply_timbre(role, inst);
}
RR_SMALL static void set_volume(unsigned track, unsigned level)
{
    struct groove_view v;
    view(track, &v);
    int delta = (int)level - (int)v.volume;
    if (delta) groove_control_edit(track, 0, delta, 0);
}
RR_SMALL void rr_app_level(unsigned role, unsigned level)
{
    if (role >= RR_ROLES_UI) return;
    if (level > 127) level = 127;
    if (role < RR_GEN_TRACKS) rr.mix.level[role] = (uint8_t)level;
    set_volume(role, level);
}
RR_SMALL static void set_effect(unsigned parameter, unsigned value)
{
    uint8_t now[4];
    groove_effects_get(now);
    int delta = (int)value - (int)now[parameter];
    if (delta) groove_effects_edit(parameter, delta);
}
RR_SMALL static void set_send(unsigned track, unsigned which, unsigned value)
{
    uint8_t now[2];
    groove_sends_get(track, now);
    int delta = (int)(value > 15 ? 15 : value) - (int)now[which];
    if (delta) groove_sends_edit(track, which, delta);
}
RR_SMALL static void apply_sends(void)
{
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) {
        if (throw_state.active && throw_state.track == t) continue;
        unsigned rev = (base_sends[t] & 15U) * rr.mix.reverb_send / 64U, dly = (base_sends[t] >> 4) * rr.mix.delay_send / 64U;
        set_send(t, 0, rev); set_send(t, 1, dly);
    }
}
RR_SMALL static void echo_throw(unsigned track)
{
    if (throw_state.active || track >= RR_GEN_TRACKS) return;
    uint8_t now[2];
    groove_sends_get(track, now);
    throw_state.track = (uint8_t)track; throw_state.send = now[1]; throw_state.active = 1;
    throw_state.until = now_ms + 60000U / (rr.bpm ? rr.bpm : 72) * 2;
    set_send(track, 1, 15);
    rr.lion_event = 3;
}
RR_SMALL static void echo_throw_poll(void)
{
    if (spring_until && (int32_t)(now_ms - spring_until) >= 0) { spring_until = 0; apply_sends(); }
    if (throw_state.active && (int32_t)(now_ms - throw_state.until) >= 0) {
        set_send(throw_state.track, 1, throw_state.send);
        throw_state.active = 0;
    }
}
RR_SMALL static void apply_style_space(unsigned style)
{
    const struct rr_style_dna *d = &rr_styles[style];
    if (!owned(T_DELAY_TIME)) set_effect(1, d->delay);
    if (!owned(T_DELAY_FB)) { set_effect(2, d->feedback); rr.mix.delay_fb = d->feedback; }
    if (!owned(T_REVERB_SIZE)) set_effect(0, d->reverb);
    struct groove_sequence_options o;
    groove_sequence_options_copy(&o);
    o.swing = d->swing;
    groove_sequence_options_set(&o);
}

/* ---- project bootstrap ---- */
RR_SMALL static void build_project(void)
{
    struct groove_project *p = groove_project_scratch();
    const struct rr_style_dna *d = &rr_styles[rr.e.style];
    memset(p, 0, sizeof *p);
    memcpy(p->name, "RIDDIM ROOTS", 12);
    p->tempo = rr.bpm;
    p->effects[0] = d->reverb; p->effects[1] = d->delay; p->effects[2] = d->feedback; p->effects[3] = 92;
    groove_sequence_defaults(&p->sequence);
    p->sequence.swing = d->swing;
    for (unsigned c = 1; c < RR_CHORDS && c <= GROOVE_CHORDS; ++c) memcpy(p->sequence.chord[c - 1], rr_chord_table[c], 8);
    groove_project_arp_defaults(p);
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        unsigned inst = t < RR_GEN_TRACKS ? style_inst(rr.e.style, t) : rr.inst[t];
        if (t < RR_GEN_TRACKS) rr.inst[t] = (uint8_t)inst;
        fill_preset(&p->preset[t], t, inst, rr.e.style);
        if (t < RR_GEN_TRACKS) base_sends[t] = p->preset[t].data[209];
    }
    (void)groove_project_apply(p);
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) groove_sequence_length_set(t, 64);
    groove_track_role_set(RT_LIVE_A, GROOVE_TRACK_PLAY);
    groove_track_role_set(RT_LIVE_B, GROOVE_TRACK_PLAY);
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) groove_midi_input_set(t, 10 + t);  /* band rows: unused channels */
    groove_midi_input_set(RT_LIVE_A, 6);
    groove_midi_input_set(RT_LIVE_B, 9);
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) seq.track_style[0][t] = seq.track_style[1][t] = seq.track_style[2][t] = seq.track_style[3][t] = rr.e.style;
}

/* ---- bar writing ---- */
RR_SMALL static void write_bar(unsigned slot)
{
    seq.before = rr.e;
    seq.can_rerender = 1;
    rr_engine_render(&rr.e, cells, &seq.info[slot]);
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) {
        groove_sequence_replace(t, slot * RR_STEPS, (const struct groove_step *)cells[t], RR_STEPS);
        static const uint8_t comp_of[RR_GEN_TRACKS] = {RC_DRUMS, RC_BASS, RC_SKANK, RC_ORGAN, RC_HORNS, RC_PIANO};
        seq.track_style[slot][t] = rr.e.comp[comp_of[t]].cur.style;
    }
    memcpy(seq.drums[slot], cells[RT_DRUMS], sizeof seq.drums[slot]);
    seq.fired[slot] = 0;
    ++seq.written;
}
/* A command arrived after the next bar was queued: render that bar again. */
RR_SMALL static void rerender_next(void)
{
    if (!rr.playing || !seq.can_rerender || seq.written != seq.playing_abs + 2) return;
    struct groove_view v;
    view(0, &v);
    if ((v.play_step & 15U) >= 15) return;     /* too close to the boundary */
    struct rr_engine keep = rr.e;
    rr.e = seq.before;
    /* carry the request flags set on the live engine since */
    rr.e.fill_bar = keep.fill_bar; rr.e.drop_bars = keep.drop_bars; rr.e.breakdown_bars = keep.breakdown_bars;
    rr.e.bass_only_bars = keep.bass_only_bars; rr.e.nokick_bars = keep.nokick_bars; rr.e.user_mute = keep.user_mute;
    rr.e.events |= keep.events; rr.e.freeze = keep.freeze; rr.e.evolve = keep.evolve; rr.e.energy = keep.energy;
    rr.e.dub = keep.dub; memcpy(rr.e.dim, keep.dim, sizeof keep.dim); rr.e.perc_double = keep.perc_double;
    rr.e.skank_muted = keep.skank_muted; rr.e.bass_octave = keep.bass_octave; rr.e.section = keep.section;
    rr.e.build_bars = keep.build_bars; rr.e.version = keep.version; rr.e.rewind_bars = keep.rewind_bars;
    memcpy(rr.e.comp, keep.comp, sizeof keep.comp);
    if (keep.transition && keep.tprogress == 0 && !keep.tswitched) {
        /* a fresh style request (possibly retargeting a running transition) */
        rr.e.style = keep.style; rr.e.transition = keep.transition; rr.e.target = keep.target; rr.e.tmode = keep.tmode;
        rr.e.tbars = keep.tbars; rr.e.tprogress = 0; rr.e.tswitched = 0; rr.e.bpm_from = keep.bpm_from;
        rr.e.bpm_target = keep.bpm_target; rr.e.tempo_plan = keep.tempo_plan;
    }
    if (keep.bar < seq.before.bar) rr.e.bar = keep.bar;     /* rewind */
    --seq.written;
    write_bar((seq.play_slot + 1) & 3U);
}
RR_SMALL static void fire_cue(unsigned kind, unsigned arg)
{
    switch (kind) {
    case FXK_ECHO_THROW: if (!owned(T_DELAY_SEND)) echo_throw(arg); break;
    case FXK_SIREN: rr_fx_trigger(FX_SIREN, 1, arg); rr_fx_trigger(FX_SIREN, 0, 0); break;
    case FXK_NOISE: rr_fx_trigger(FX_NOISE, 1, arg); break;
    case FXK_LASER: rr_fx_trigger(FX_LASER, 1, arg); break;
    case FXK_SPRING: if (!owned(T_REVERB_SEND)) { set_send(RT_DRUMS, 0, 15); spring_until = now_ms + 700; } break;
    case FXK_SWEEP: if (!owned(T_FILTER)) rr_fx_param(FXP_FILTER, 96); break;
    case FXK_FEEDBACK: if (!owned(T_DELAY_FB)) rr_fx_trigger(FX_FEEDBACK, 1, arg); break;
    }
}
RR_SMALL static void bar_started(unsigned slot)
{
    struct rr_bar_info *in = &seq.info[slot];
    ++rr.bar_played;
    rr.section = in->section; rr.phrase_pos = in->phrase_pos; rr.chord_root = in->chord_root;
    rr.chord_q = in->chord_q; rr.active = in->active; rr.events = in->events;
    if (in->bpm) {
        rr.bpm_target = in->bpm;
        if (in->tempo_jump) { struct groove_view v; view(0, &v); groove_tempo_edit((int)in->bpm - (int)v.tempo); }
    }
    if (in->events & EV_MUTATE) rr.lion_event = 1;
    if (in->events & EV_TRANSITION) rr.lion_event = 4;
    if (in->events & EV_ECHO_THROW) echo_throw(RT_SKANK);
    if (in->events & EV_SIREN) { rr_fx_trigger(FX_SIREN, 1, 90); rr_fx_trigger(FX_SIREN, 0, 0); }
    if (in->events & EV_REWIND) rr_fx_trigger(FX_REWIND, 0, 0);
    if (in->events & EV_DROP) { rr_fx_trigger(FX_BASSDROP, 1, 100); rr.lion_event = 5; }
    if ((in->events & EV_SPRING) && !owned(T_REVERB_SEND)) fire_cue(FXK_SPRING, 0);
    if (!owned(T_FILTER) && rr_app_target_value(T_FILTER) != 64 && !rr.fx_hold) rr_fx_param(FXP_FILTER, rr.mix.filter);
    /* Instruments follow the style each band member is playing in. */
    for (unsigned t = 1; t < RR_GEN_TRACKS; ++t) {
        unsigned want = style_inst(seq.track_style[slot][t], t);
        if (!inst_user[t] && want != rr.inst[t]) apply_timbre(t, want);
    }
    if (seq.track_style[slot][RT_DRUMS] != seq.track_style[(slot + 3) & 3][RT_DRUMS] && !inst_user[RT_DRUMS]) {
        rr.kit = rr_styles[seq.track_style[slot][RT_DRUMS]].kit; apply_timbre(RT_DRUMS, I_DRUMKIT);
    }
    if (in->events & EV_TRANSITION_DONE) { apply_style_space(rr.e.style); rr.lion_event = 0; rr_app_toast(rr_styles[rr.e.style].name); }
    /* Timbre mutation: swap a keyboard/horn sound at phrase boundaries. */
    unsigned timbre = rr.e.evolve * rr.e.dim[RD_TIMBRE] / 100U;
    if ((in->events & EV_PHRASE) && timbre > 45) {
        static const uint8_t roles[3] = {RT_ORGAN, RT_HORNS, RT_PIANO}, comps[3] = {RC_ORGAN, RC_HORNS, RC_PIANO};
        unsigned k = rr_range(&rr.e.rng, 3);
        if (!(rr.e.freeze & (1U << comps[k])) && !inst_user[roles[k]] && rr_chance(&rr.e.rng, timbre - 30)) {
            const struct rr_style_dna *d = &rr_styles[rr.e.style];
            unsigned alt = d->alt_inst[k];
            apply_timbre(roles[k], rr.inst[roles[k]] == alt ? style_inst(rr.e.style, roles[k]) : alt);
        }
    }
}
RR_SMALL static void sequencer_poll(void)
{
    struct groove_view v;
    view(0, &v);
    rr.bpm = (uint8_t)(v.tempo > 255 ? 255 : v.tempo);
    if (!rr.playing) return;
    if (!v.playing) {
        if (seq.started && now_ms - tempo_ms > 400) { rr.playing = 0; }   /* stopped elsewhere */
        return;
    }
    seq.started = 1;
    unsigned step = v.play_step & 63U, slot = step >> 4, pos = step & 15U;
    rr.step = (uint8_t)pos;
    if (slot != seq.play_slot) {
        seq.play_slot = (uint8_t)slot;
        ++seq.playing_abs;
        seq.can_rerender = 0;
        bar_started(slot);
    }
    if (seq.written == seq.playing_abs + 1 && pos >= 8) write_bar((slot + 1) & 3U);
    struct rr_bar_info *in = &seq.info[slot];
    for (unsigned k = 0; k < in->nfx; ++k)
        if (!(seq.fired[slot] & (1U << k)) && pos >= in->fx_step[k]) { seq.fired[slot] |= (uint8_t)(1U << k); fire_cue(in->fx_kind[k], in->fx_arg[k]); }
    /* Bass onsets drive the lion's bounce. */
    unsigned bass = seq.drums[slot][pos].bits & DK_BD;
    if (bass && pos != rr.bass_hit) rr.bass_hit = (uint8_t)pos;
}
RR_SMALL static void tempo_poll(void)
{
    if (now_ms - tempo_ms < 90) return;
    tempo_ms = now_ms;
    if (!rr.bpm_target || rr.bpm_target == rr.bpm) return;
    int diff = (int)rr.bpm_target - (int)rr.bpm;
    int step = rr.bpm_smooth ? (diff > 0 ? 1 : -1) * (1 + (diff < 0 ? -diff : diff) / 12) : diff;
    groove_tempo_edit(step);
}
RR_SMALL void rr_app_bpm(int bpm, int instant)
{
    bpm = rr_clamp(bpm, 30, 250);
    rr.bpm_target = (uint8_t)bpm;
    if (instant || !rr.bpm_smooth) { struct groove_view v; view(0, &v); groove_tempo_edit(bpm - (int)v.tempo); }
}
RR_SMALL void rr_app_play(int on)
{
    if (on && !rr.playing) {
        seq.written = 0; seq.playing_abs = 0; seq.play_slot = 0; seq.started = 0;
        rr.e.bar -= rr.e.bar % RR_PHRASE;
        write_bar(0); write_bar(1);
        bar_started(0);
        const uint8_t start = 0xfa;
        groove_midi_local(&start, 1);
        rr.playing = 1; tempo_ms = now_ms;
    } else if (!on && rr.playing) {
        const uint8_t stop = 0xfc;
        groove_midi_local(&stop, 1);
        rr.playing = 0;
        rr.lion_event = 0;
    }
}
RR_SMALL void rr_app_style(unsigned style)
{
    if (style >= RR_STYLES) return;
    rr_engine_style(&rr.e, style, rr.tmode, rr.tbars, rr.bpm);
    if (!rr.playing) {
        /* Stopped: switch at once so PLAY starts in the new style. */
        while (rr.e.transition) { rr_engine_render(&rr.e, cells, &seq.info[0]); }
        rr.e.bar = 0;
        rr_app_bpm(rr_styles[style].bpm, 1);
        rr.kit = rr_styles[style].kit;
        for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) { seq.track_style[0][t] = (uint8_t)style; if (!inst_user[t]) apply_timbre(t, t ? style_inst(style, t) : I_DRUMKIT); }
        apply_style_space(style);
    } else rerender_next();
    rr.lion_event = 4;
    rr_app_toast(rr_styles[style].name);
}
RR_SMALL void rr_app_command(unsigned cmd, unsigned arg)
{
    if (cmd >= RR_CMDS) return;
    if (cmd == CMD_FILL && rr.playing) {
        /* Quantised to the next beat inside the bar that is playing now. */
        struct groove_view v;
        view(0, &v);
        unsigned pos = v.play_step & 15U, slot = (v.play_step >> 4) & 3U;
        if (pos < 10) {
            unsigned from = (pos + 4) & ~3U;
            rr_engine_fill(&rr.e, seq.drums[slot], from, 40 + rr.e.intensity / 2);
            groove_sequence_replace(RT_DRUMS, slot * RR_STEPS + from, (const struct groove_step *)&seq.drums[slot][from], RR_STEPS - from);
            rr.lion_event = 6;
            return;
        }
    }
    rr_engine_command(&rr.e, cmd, arg);
    if (cmd == CMD_ECHO_THROW) echo_throw(arg ? arg : RT_SKANK);
    if (cmd == CMD_SPRING_SNARE) fire_cue(FXK_SPRING, 0);
    if (cmd == CMD_SIREN) { rr_fx_trigger(FX_SIREN, 1, 100); rr_fx_trigger(FX_SIREN, 0, 0); }
    if (cmd == CMD_REWIND) rr_fx_trigger(FX_REWIND, 1, 100);
    if (cmd == CMD_DROP || cmd == CMD_BIG_DROP) rr.lion_event = 5;
    if (cmd == CMD_MUTATE || cmd == CMD_REBUILD || cmd == CMD_NEXT_VERSION) rr.lion_event = 1;
    rerender_next();
    rr_app_toast(rr_cmd_names[cmd]);
}
RR_SMALL void rr_app_fx(unsigned fx, unsigned on, unsigned velocity)
{
    rr.fx_hold = (uint8_t)(on ? fx + 1 : 0);
    if (on) rr.lion_event = 3;
    switch (fx) {
    case FX_TAPE_ECHO: case FX_PHASER: case FX_FLANGER: case FX_CHORUS: case FX_SATURATE: case FX_DISTORT: case FX_CRUSH:
    case FX_HPF: case FX_LPF: case FX_BPF:
        if (!on) break;
        rr.fx_on = (uint8_t)(rr.fx_on && rr.fx_sel == fx ? 0 : 1);
        rr.fx_sel = (uint8_t)fx;
        rr_fx_param(FXP_INSERT, fx);
        rr_fx_param(FXP_AMOUNT, rr.fx_on ? rr.fx_amount : 0);
        break;
    case FX_DIGI_DELAY: if (on) { set_effect(1, 134); rr.mix.delay_send = rr.mix.delay_send > 64 ? 40 : 110; apply_sends(); } break;
    case FX_SPRING: if (on) { set_effect(0, 40); set_send(RT_DRUMS, 0, 15); set_send(RT_SKANK, 0, 12); spring_until = now_ms + 2500; } break;
    case FX_CHAMBER: if (on) { set_effect(0, 118); rr.mix.reverb_send = 110; apply_sends(); } break;
    case FX_ECHOTHROW: if (on) echo_throw(RT_SKANK); break;
    case FX_MUTE:
        for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) {
            struct groove_view v; view(t, &v);
            if (!!v.muted != !!on) groove_mix_toggle(t, 0);
        }
        break;
    default:
        rr_fx_trigger(fx, on, velocity ? velocity : 100);
        if (fx == FX_REWIND && !on && rr.playing) rr_app_command(CMD_REWIND, 0);
        break;
    }
}
/* ---- controller targets ---- */
static unsigned scale100(unsigned v) { return v * 100U / 127U; }
RR_SMALL unsigned rr_app_target_value(unsigned t)
{
    return t < RR_TARGETS ? rr.map.value[t] : 0;
}
RR_SMALL void rr_app_target(unsigned t, unsigned v, unsigned pressed)
{
    if (t >= RR_TARGETS) return;
    touched[t] = now_ms ? now_ms : 1;
    diag.last_target = t; diag.last_value = v;
    rr.map.value[t] = (uint8_t)(v > 127 ? 127 : v);
    switch (t) {
    case T_LEVEL_BASS: rr_app_level(RT_BASS, v); break;
    case T_LEVEL_DRUMS: rr_app_level(RT_DRUMS, v); break;
    case T_LEVEL_SKANK: rr_app_level(RT_SKANK, v); break;
    case T_LEVEL_KEYS: rr_app_level(RT_ORGAN, v); rr_app_level(RT_PIANO, v * 7 / 8); break;
    case T_LEVEL_HORNS: rr_app_level(RT_HORNS, v); break;
    case T_LEVEL_FXPERC: case T_PERC_LEVEL: {
        static const uint8_t parts[5] = {2, 3, 4, 6, 7};      /* LT MT HT CP CB */
        for (unsigned i = 0; i < 5; ++i) {
            uint8_t values[GROOVE_TONE_PARAMS];
            if (groove_tone_copy(RT_DRUMS, parts[i], values)) continue;
            int delta = (int)(v * 60 / 127) - (int)values[0];
            if (delta) groove_tone_edit(RT_DRUMS, parts[i], 0, delta);
        }
        break;
    }
    case T_DELAY_SEND: rr.mix.delay_send = (uint8_t)v; apply_sends(); break;
    case T_REVERB_SEND: rr.mix.reverb_send = (uint8_t)v; apply_sends(); break;
    case T_DIM_RHYTHM: case T_DIM_BASS: case T_DIM_HARMONY: case T_DIM_INSTR: case T_DIM_ARRANGE: case T_DIM_TIMBRE: case T_DIM_FX:
        rr.e.dim[t - T_DIM_RHYTHM] = (uint8_t)scale100(v); break;
    case T_EVOLVE: case T_MORPH: rr.e.evolve = (uint8_t)scale100(v); break;
    case T_FILTER: rr.mix.filter = (uint8_t)v; rr_fx_param(FXP_FILTER, v); break;
    case T_DELAY_FB: rr.mix.delay_fb = (uint8_t)v; set_effect(2, v); rr_fx_param(FXP_ECHO_FB, v * 100 / 127); break;
    case T_CHAOS: { int d = (int)v - 64; d = d < 0 ? -d : d; rr_fx_param(FXP_CHAOS, d < 6 ? 0 : (d - 6) * 2); break; }
    case T_MASTER: rr_fx_param(FXP_MASTER, v); break;
    case T_BPM: { unsigned lo = rr.bpm_lo, hi = rr.bpm_hi; rr_app_bpm((int)(lo + (hi - lo) * v / 127U), 0); break; }
    case T_ENERGY: rr.e.energy = (uint8_t)scale100(v); break;
    case T_DUB: rr.e.dub = (uint8_t)scale100(v); break;
    case T_STYLE: { unsigned s = v * RR_STYLES / 128U; if (s != rr.e.target || !rr.e.transition) { if (s != rr.e.style || rr.e.transition) rr_app_style(s); } break; }
    case T_FX_SELECT: rr.fx_sel = (uint8_t)(v * RR_FX / 128U); break;
    case T_FX_AMOUNT: rr.fx_amount = (uint8_t)v; rr.fx_on = v > 0; rr_fx_param(FXP_AMOUNT, v); break;
    case T_REVERB_SIZE: set_effect(0, v); break;
    case T_DELAY_TIME: set_effect(1, 128 + v * 9 / 128); break;
    case T_LIVE_LEVEL: set_volume(RT_LIVE_A, v); break;
    case T_EXPRESSION: rr_fx_param(FXP_EXPRESSION, v); break;
    case T_PAD_SIREN: rr_app_fx(FX_SIREN, pressed, v); break;
    case T_PAD_ECHO: if (pressed) rr_app_command(CMD_ECHO_THROW, RT_SKANK); break;
    case T_PAD_FILL: if (pressed) rr_app_command(CMD_FILL, 0); break;
    case T_PAD_DROP: rr_app_fx(FX_MUTE, pressed, v); if (!pressed) rr_app_command(CMD_FILL, 0); break;
    case T_PAD_REWIND: rr_app_fx(FX_REWIND, pressed, v); break;
    case T_PAD_VERSION: if (pressed) rr_app_command(CMD_NEXT_VERSION, 0); break;
    case T_PAD_MUTATE: if (pressed) rr_app_command(CMD_MUTATE, 0); break;
    case T_PAD_FREEZE: if (pressed || v == 0) { rr_engine_freeze(&rr.e, rr.sel[P_FREEZE] % RR_COMPS, -1); rr.lion_event = 2; } break;
    case T_PLAY: if (v > 63) rr_app_play(1); break;
    case T_STOP: if (v > 63) rr_app_play(0); break;
    case T_TAP: if (v > 63) { int b = groove_tempo_tap(now_ms); if (b > 0) rr.bpm_target = (uint8_t)b; } break;
    case T_CMD: rr_app_command(v % RR_CMDS, 0); break;
    case T_BANK: { static const char *const banks[RR_BANKS] = {"BANK 1 MIX", "BANK 2 EVOLVE", "BANK 3 DUB FX"}; rr_app_toast(banks[rr.map.bank % RR_BANKS]); break; }
    }
}

/* ---- MIDI processing (control task) ---- */
RR_SMALL static void midi_message(const uint8_t *m, unsigned n)
{
    rr.midi_flash = 1; midi_ms = now_ms;
    memmove(rr.recent[1], rr.recent[0], sizeof rr.recent - 3);
    rr.recent[0][0] = m[0]; rr.recent[0][1] = n > 1 ? m[1] : 0; rr.recent[0][2] = n > 2 ? m[2] : 0;
    if (rr.recent_n < RR_RECENT) ++rr.recent_n;
    if (m[0] == 0xfa || m[0] == 0xfb) { rr_app_play(1); return; }
    if (m[0] == 0xfc) { rr_app_play(0); return; }
    if (m[0] == 0xff) { rr_live_panic(&rr.live, emit, 0); return; }
    struct rr_action a[2];
    unsigned learning = rr.map.learn, count = rr_map_message(&rr.map, m, n, a);
    unsigned type = m[0] & 0xf0U;
    if (type == 0xb0) ++rr.cc_in;
    for (unsigned i = 0; i < count; ++i) {
        if (a[i].from_learn) { rr_app_toast("LEARNED"); continue; }
        rr_app_target(a[i].target, a[i].value, a[i].pressed);
    }
    if (count || learning) return;
    switch (type) {
    case 0x90: case 0x80: {
        unsigned vel = type == 0x90 ? m[2] : 0;
        if (vel) { ++rr.notes_in; rr.midi_vel = (uint8_t)vel; rr.lion_event = rr.lion_event ? rr.lion_event : 7; }
        int r = rr_live_note(&rr.live, m[1], vel, emit, 0);
        if (r > 0 && vel) {
            if (rr.live.mode == KM_SOUND) {
                static const uint8_t keys[12] = {FX_SIREN, FX_AIRHORN, FX_LASER, FX_NOISE, FX_ECHOTHROW, FX_BASSDROP,
                                                 FX_TAPESTOP, FX_STUTTER, FX_REWIND, FX_FEEDBACK, FX_MUTE, FX_SPRING};
                rr_app_fx(keys[r - 1], 1, vel);
            } else rr_app_command((unsigned)r, 0);
        } else if (r < 0 && rr.live.mode == KM_SOUND) {
            static const uint8_t holds[12] = {FX_SIREN, 0, 0, 0, 0, 0, FX_TAPESTOP, FX_STUTTER, FX_REWIND, 0, FX_MUTE, 0};
            unsigned k = m[1] % 12;
            if (holds[k] || k == 0) rr_app_fx(holds[k], 0, 0);
        }
        break;
    }
    case 0xb0:
        if (m[1] == 64) rr_live_sustain(&rr.live, m[2] >= 64, emit, 0);
        else if (m[1] == 1) rr_app_target(T_EXPRESSION, m[2], 1);
        else if (m[1] == 120 || m[1] == 123) rr_live_panic(&rr.live, emit, 0);
        break;
    case 0xe0: {
        uint8_t a6[3] = {0xe6, m[1], m[2]}, b9[3] = {0xe9, m[1], m[2]};
        emit(0, a6, 3); emit(0, b9, 3);
        rr_fx_param(FXP_BEND, (int)((m[2] << 7) | m[1]) - 8192);
        break;
    }
    case 0xc0: rr_app_instrument(RT_LIVE_A, m[1] % RR_INSTS); rr_app_toast(rr_inst_names[m[1] % RR_INSTS]); break;
    case 0xd0: rr_app_target(T_EXPRESSION, m[1], 1); break;
    }
}
RR_SMALL static void midi_poll(void)
{
    for (unsigned work = 0; work < 48 && mq_read != mq_write; ++work) {
        barrier();
        uint32_t e = mq[mq_read & (MQ - 1)];
        barrier();
        ++mq_read;
        uint8_t m[3] = {(uint8_t)e, (uint8_t)(e >> 8), (uint8_t)(e >> 16)};
        midi_message(m, e >> 24);
    }
}

/* ---- memory ---- */
RR_SMALL static void dna_to_rows(const uint8_t *dna, struct groove_step rows[128])
{
    /* 20 bits per cell: note 7, velocity 6 (+64), gate 4, probability 3. */
    memset(rows, 0, sizeof(struct groove_step) * 128);
    for (unsigned c = 0; c < 128; ++c) {
        unsigned bit = c * 20, v = 0;
        for (unsigned b = 0; b < 20; ++b) {
            unsigned at = bit + b;
            if (at / 8 < RR_DNA_BYTES && (dna[at / 8] >> (at % 8)) & 1U) v |= 1U << b;
        }
        rows[c].note = (uint8_t)(v & 127); rows[c].velocity = (uint8_t)(64 + ((v >> 7) & 63));
        rows[c].drums = (uint16_t)((((v >> 13) & 15U) << 5) | (((v >> 17) & 7U) << 9));
    }
}
RR_SMALL static void rows_to_dna(const struct groove_step rows[128], uint8_t *dna)
{
    memset(dna, 0, RR_DNA_BYTES);
    for (unsigned c = 0; c < 128; ++c) {
        unsigned v = (rows[c].note & 127U) | (((unsigned)(rows[c].velocity - 64) & 63U) << 7) |
                     (((rows[c].drums >> 5) & 15U) << 13) | (((rows[c].drums >> 9) & 7U) << 17);
        for (unsigned b = 0; b < 20; ++b) {
            unsigned at = c * 20 + b;
            if (at / 8 < RR_DNA_BYTES && ((v >> b) & 1U)) dna[at / 8] |= (uint8_t)(1U << (at % 8));
        }
    }
}
RR_SMALL static void recall_dna(const uint8_t *dna)
{
    unsigned bpm = 0, was = rr.playing;
    struct rr_engine e;
    struct rr_mix mix;
    if (rr_dna_unpack(&e, &mix, &bpm, dna)) { rr_app_toast("MEMORY EMPTY/BAD"); return; }
    unsigned old = rr.e.style;
    e.bar = rr.e.bar - rr.e.bar % RR_PHRASE + RR_PHRASE;
    unsigned style = e.style;
    if (was && style != old) {
        /* Recall glides there like a style change; the recalled motifs arrive with it. */
        e.style = (uint8_t)old; e.transition = 1; e.target = (uint8_t)style; e.tmode = rr.tmode; e.tbars = rr.tbars;
        e.tprogress = 0; e.tswitched = 0xffff; e.bpm_from = rr.bpm; e.bpm_target = (uint8_t)bpm; e.tempo_plan = 0;
    }
    rr.e = e; rr.mix = mix;
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) rr_app_level(t, mix.level[t]);
    apply_sends();
    rr_fx_param(FXP_FILTER, mix.filter);
    if (!was) rr_app_bpm((int)bpm, 1); else rr.bpm_target = (uint8_t)bpm;
    rerender_next();
    rr_app_toast("RECALLED");
}
RR_SMALL void rr_app_system(unsigned action, unsigned slot)
{
    struct groove_step rows[128];
    switch (action) {
    case SYS_STORE:
        rr_dna_pack(&rr.e, &rr.mix, rr.bpm, rr.memory[slot % RR_MEMORY]);
        rr.memory_used |= (uint8_t)(1U << (slot % RR_MEMORY));
        rr_app_toast("STORED"); break;
    case SYS_RECALL:
        if (!(rr.memory_used & (1U << (slot % RR_MEMORY)))) { rr_app_toast("SLOT EMPTY"); break; }
        recall_dna(rr.memory[slot % RR_MEMORY]); break;
    case SYS_SAVE: {
        static uint8_t dna[RR_DNA_BYTES];
        rr_dna_pack(&rr.e, &rr.mix, rr.bpm, dna);
        dna_to_rows(dna, rows);
        groove_sequence_replace(RT_LIVE_A, 0, rows, 64);
        groove_sequence_replace(RT_LIVE_B, 0, rows + 64, 64);
        int r = groove_project_save_slot(FLASH_FIRST + slot % 7);
        diag.save_result = (uint32_t)r;
        rr_app_toast(r ? "SAVE FAILED" : "SAVED TO FLASH");
        break;
    }
    case SYS_LOAD: {
        static uint8_t dna[RR_DNA_BYTES];
        unsigned was = rr.playing;
        int r = groove_project_load_slot(FLASH_FIRST + slot % 7);
        diag.load_result = (uint32_t)r;
        if (r) { rr_app_toast("LOAD FAILED"); break; }
        groove_sequence_read(RT_LIVE_A, 0, rows, 64);
        groove_sequence_read(RT_LIVE_B, 0, rows + 64, 64);
        rows_to_dna(rows, dna);
        groove_track_role_set(RT_LIVE_A, GROOVE_TRACK_PLAY); groove_track_role_set(RT_LIVE_B, GROOVE_TRACK_PLAY);
        for (unsigned t = 0; t < GROOVE_TRACKS; ++t) groove_sequence_length_set(t, 64);
        struct groove_view v; view(0, &v);
        rr.playing = (uint8_t)(was && v.playing);
        recall_dna(dna);
        if (was && !rr.playing) rr_app_play(1);
        break;
    }
    case SYS_PANIC: rr_live_panic(&rr.live, emit, 0); groove_midi_panic(); rr_app_toast("PANIC"); break;
    case SYS_MODE: rr.simple = !rr.simple; rr.page = P_HOME; rr_app_toast(rr.simple ? "SIMPLE MODE" : "PERFORMANCE MODE"); break;
    case SYS_DEFAULTS: rr_map_defaults(&rr.map); rr_app_toast("MIDI MAP RESET"); break;
    case SYS_MAPSAVE: {
        static uint8_t buf[RR_DNA_BYTES];
        memset(buf, 0, sizeof buf);
        buf[0] = 'R'; buf[1] = 'M'; buf[2] = rr.map.pickup; buf[3] = rr.live.mode; buf[4] = rr.live.split;
        buf[5] = rr.bpm_lo; buf[6] = rr.bpm_hi; buf[7] = rr.bpm_smooth; buf[8] = rr.release_s; buf[9] = rr.tmode; buf[10] = rr.tbars;
        for (unsigned i = 0; i < RR_MAPS; ++i) {
            const struct rr_map *e = &rr.map.map[i];
            uint8_t *o = buf + 12 + i * 6;
            o[0] = (uint8_t)(e->kind | e->channel << 4); o[1] = e->number; o[2] = e->bank; o[3] = e->target; o[4] = e->lo | (e->flags & 1U) << 7; o[5] = e->hi | (e->flags & 6U) << 6 >> 1;
        }
        uint16_t crc = rr_crc16(buf, RR_DNA_BYTES - 2);
        buf[RR_DNA_BYTES - 2] = (uint8_t)crc; buf[RR_DNA_BYTES - 1] = (uint8_t)(crc >> 8);
        dna_to_rows(buf, rows);
        groove_sequence_replace(RT_LIVE_A, 0, rows, 64);
        groove_sequence_replace(RT_LIVE_B, 0, rows + 64, 64);
        int r = groove_project_save_slot(SETTINGS_SLOT);
        rr_app_toast(r ? "SAVE FAILED" : "MIDI MAP SAVED");
        break;
    }
    }
}
RR_SMALL static void load_settings(void)
{
    char name[13];
    if (groove_project_info(SETTINGS_SLOT, name) != 0 || memcmp(name, "RIDDIM ROOTS", 12)) return;
    if (groove_project_load_slot(SETTINGS_SLOT)) return;
    struct groove_step rows[128];
    static uint8_t buf[RR_DNA_BYTES];
    groove_sequence_read(RT_LIVE_A, 0, rows, 64);
    groove_sequence_read(RT_LIVE_B, 0, rows + 64, 64);
    rows_to_dna(rows, buf);
    uint16_t crc = (uint16_t)(buf[RR_DNA_BYTES - 2] | buf[RR_DNA_BYTES - 1] << 8);
    if (buf[0] != 'R' || buf[1] != 'M' || rr_crc16(buf, RR_DNA_BYTES - 2) != crc) return;   /* corrupted: keep defaults */
    struct rr_mapper m;
    rr_map_defaults(&m);
    m.pickup = buf[2] & 1;
    for (unsigned i = 0; i < RR_MAPS; ++i) {
        const uint8_t *o = buf + 12 + i * 6;
        struct rr_map *e = &m.map[i];
        e->kind = o[0] & 15; e->channel = o[0] >> 4; e->number = o[1] & 127; e->bank = o[2]; e->target = o[3];
        e->lo = o[4] & 127; e->hi = o[5] & 127; e->flags = (uint8_t)((o[4] >> 7) | ((o[5] >> 6) & 1U) << 1 | ((o[5] >> 7) & 1U) << 2);
    }
    if (!rr_map_valid(&m)) return;
    rr.map = m;
    if (buf[3] < RR_KMODES) rr.live.mode = buf[3];
    if (buf[4] >= 24 && buf[4] <= 96) rr.live.split = buf[4];
    if (buf[5] >= 30 && buf[6] > buf[5] && buf[6] <= 250) { rr.bpm_lo = buf[5]; rr.bpm_hi = buf[6]; }
    rr.bpm_smooth = buf[7] & 1; if (buf[8] && buf[8] <= 30) rr.release_s = buf[8];
    if (buf[9] < RR_TMODES) rr.tmode = buf[9];
    if (buf[10] && buf[10] <= 16) rr.tbars = buf[10];
}

/* ---- panel ---- */
static uint32_t prev_keys, prev_buttons;
static const uint8_t page_button[RR_PAGES] = {BTN_HOME, BTN_SEQ, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_SCL, BTN_FX, BTN_ARP, BTN_GLO, BTN_SAVE};
RR_SMALL static void key_event(unsigned key, unsigned down, uint32_t buttons)
{
    unsigned note = 53 + key;                                /* F3..G5 */
    if (buttons & BIT(BTN_FX)) {                             /* hold FX: keys = sound system */
        static const uint8_t keys[14] = {FX_SIREN, FX_AIRHORN, FX_LASER, FX_NOISE, FX_ECHOTHROW, FX_BASSDROP, FX_TAPESTOP,
                                         FX_STUTTER, FX_REWIND, FX_FEEDBACK, FX_MUTE, FX_SPRING, FX_CHAMBER, FX_DIGI_DELAY};
        unsigned fx = keys[key % 14];
        rr_app_fx(fx, down, 100);
        return;
    }
    if (buttons & BIT(BTN_ARP)) { if (down) rr_app_command(1 + key % (RR_CMDS - 1), 0); return; }
    if (buttons & BIT(BTN_LFO)) { if (down && key < RR_COMPS) { rr_engine_freeze(&rr.e, key, -1); rr.lion_event = 2; rr.sel[P_FREEZE] = (uint8_t)key; } return; }
    if (down) { rr.midi_vel = 100; rr.lion_event = rr.lion_event ? rr.lion_event : 7; }
    (void)rr_live_note(&rr.live, note, down ? 100 : 0, emit, 0);
}
static int adj(int v, int d, int lo, int hi) { return rr_clamp(v + d, lo, hi); }
RR_SMALL void rr_app_knob(unsigned k, int d)
{
    struct rr_engine *e = &rr.e;
    unsigned page = rr.page;
    switch (page) {
    case P_HOME:
        if (k == 0) { e->evolve = (uint8_t)adj(e->evolve, d * 2, 0, 100); touched[T_EVOLVE] = now_ms; }
        if (k == 1) e->energy = (uint8_t)adj(e->energy, d * 2, 0, 100);
        if (k == 2) e->dub = (uint8_t)adj(e->dub, d * 2, 0, 100);
        if (k == 3) rr_app_target(T_FILTER, (unsigned)adj(rr.mix.filter, d * 2, 0, 127), 1);
        break;
    case P_STYLE:
        if (k == 0) { rr.pending_style = (uint8_t)adj(rr.pending_style, d, 0, RR_STYLES - 1); style_ms = now_ms; rr.style_wait = 1; }
        if (k == 1) rr.tmode = (uint8_t)adj(rr.tmode, d, 0, RR_TMODES - 1);
        if (k == 2) { static const uint8_t bars[5] = {1, 2, 4, 8, 16}; unsigned i = 0; while (i < 4 && bars[i] < rr.tbars) ++i; rr.tbars = bars[adj((int)i, d, 0, 4)]; }
        if (k == 3) rr.bpm_smooth = d > 0;
        break;
    case P_EVOLVE:
        if (k == 0) rr.sel[P_EVOLVE] = (uint8_t)adj(rr.sel[P_EVOLVE], d, 0, RR_DIMS);
        if (k == 1) { if (rr.sel[P_EVOLVE] == RR_DIMS) e->evolve = (uint8_t)adj(e->evolve, d * 2, 0, 100); else e->dim[rr.sel[P_EVOLVE]] = (uint8_t)adj(e->dim[rr.sel[P_EVOLVE]], d * 2, 0, 100); }
        if (k == 2) rr.sel[P_FREEZE] = (uint8_t)adj(rr.sel[P_FREEZE], d, 0, RR_MOTIF_COMPS - 1);
        if (k == 3) { struct rr_comp_state *c = &e->comp[rr.sel[P_FREEZE] % RR_MOTIF_COMPS]; c->rate = (uint8_t)adj(c->rate, d * 5, 0, 100); }
        break;
    case P_FREEZE:
        if (k == 0) rr.sel[P_FREEZE] = (uint8_t)adj(rr.sel[P_FREEZE], d, 0, RR_COMPS - 1);
        if (k == 1) { rr_engine_freeze(e, rr.sel[P_FREEZE], d > 0); rr.lion_event = 2; }
        if (k == 2) rr.freeze_act = (uint8_t)adj(rr.freeze_act, d, 0, 5);
        if (k == 3) { struct rr_comp_state *c = &e->comp[rr.sel[P_FREEZE] % RR_MOTIF_COMPS]; c->depth = (uint8_t)adj(c->depth, d * 5, 0, 100); }
        break;
    case P_INSTR:
        if (k == 0) rr.sel[P_INSTR] = (uint8_t)adj(rr.sel[P_INSTR], d, 0, RR_ROLES_UI - 1);
        if (k == 1) {
            unsigned role = rr.sel[P_INSTR];
            if (role == RT_DRUMS) rr_app_instrument(RT_DRUMS, (unsigned)adj(rr.kit, d, 0, RR_KITS - 1));
            else rr_app_instrument(role, (unsigned)adj(rr.inst[role], d, 0, RR_INSTS - 2));
        }
        if (k == 2) { unsigned role = rr.sel[P_INSTR]; struct groove_view v; view(role, &v); rr_app_level(role, (unsigned)adj(v.volume, d * 3, 0, 127)); }
        if (k == 3) { unsigned role = rr.sel[P_INSTR]; if (role < RR_GEN_TRACKS) { e->user_mute ^= (uint8_t)(1U << role); rerender_next(); } else inst_user[role] = 1; }
        break;
    case P_LIVE:
        if (k == 0) { rr_live_panic(&rr.live, emit, 0); rr.live.mode = (uint8_t)adj(rr.live.mode, d, 0, RR_KMODES - 1); }
        if (k == 1) rr.live.split = (uint8_t)adj(rr.live.split, d, 36, 84);
        if (k == 2) rr_app_instrument(RT_LIVE_A, (unsigned)adj(rr.inst[RT_LIVE_A], d, 0, RR_INSTS - 1));
        if (k == 3) rr_app_instrument(RT_LIVE_B, (unsigned)adj(rr.inst[RT_LIVE_B], d, 0, RR_INSTS - 1));
        break;
    case P_FX:
        if (k == 0) rr.fx_sel = (uint8_t)adj(rr.fx_sel, d, 0, RR_FX - 1);
        if (k == 1) { rr.fx_amount = (uint8_t)adj(rr.fx_amount, d * 3, 0, 127); rr_fx_param(FXP_INSERT, rr.fx_sel); if (rr.fx_on) rr_fx_param(FXP_AMOUNT, rr.fx_amount); touched[T_FX_AMOUNT] = now_ms; }
        if (k == 2) rr_app_target(T_DELAY_FB, (unsigned)adj(rr.mix.delay_fb, d * 2, 0, 127), 1);
        if (k == 3) rr_app_target(T_FILTER, (unsigned)adj(rr.mix.filter, d * 2, 0, 127), 1);
        break;
    case P_CONDUCT:
        if (k == 0) rr.cmd_sel = (uint8_t)adj(rr.cmd_sel, d, 1, RR_CMDS - 1);
        if (k == 1) e->intensity = (uint8_t)adj(e->intensity, d * 3, 0, 100);
        if (k == 2) rr_app_target(T_DELAY_SEND, (unsigned)adj(rr.mix.delay_send, d * 3, 0, 127), 1);
        if (k == 3) rr_app_target(T_REVERB_SEND, (unsigned)adj(rr.mix.reverb_send, d * 3, 0, 127), 1);
        break;
    case P_MIDI:
        if (k == 0) rr.learn_sel = (uint8_t)adj(rr.learn_sel, d, 1, RR_TARGETS - 1);
        if (k == 1) { rr.map.bank = (uint8_t)adj(rr.map.bank, d, 0, RR_BANKS - 1); memset(rr.map.picked, 0, sizeof rr.map.picked); }
        if (k == 2) rr.map.pickup = d > 0;
        if (k == 3) rr.release_s = (uint8_t)adj(rr.release_s, d, 1, 30);
        break;
    case P_SYSTEM:
        if (k == 0) rr.mem_sel = (uint8_t)adj(rr.mem_sel, d, 0, 6);
        if (k == 1) rr.sys_action = (uint8_t)adj(rr.sys_action, d, 0, RR_SYS - 1);
        if (k == 2) rr.bpm_hi = (uint8_t)adj(rr.bpm_hi, d * 5, 120, 250);
        if (k == 3) rr.bpm_lo = (uint8_t)adj(rr.bpm_lo, d * 5, 30, 100);
        break;
    }
}
/* Pressing the page's own button again executes its selected action. */
RR_SMALL static void page_action(void)
{
    switch (rr.page) {
    case P_STYLE: rr_app_style(rr.pending_style); rr.style_wait = 0; break;
    case P_FREEZE: {
        static const uint8_t acts[6] = {CMD_MUTATE, CMD_KEEP, CMD_REJECT, CMD_REBUILD, CMD_NEXT_VERSION, CMD_RESTORE};
        unsigned a = acts[rr.freeze_act % 6];
        if (a == CMD_MUTATE) { rr_engine_mutate(&rr.e, rr.sel[P_FREEZE] < RR_MOTIF_COMPS ? rr.sel[P_FREEZE] : 255, 70); rr.lion_event = 1; rerender_next(); }
        else rr_app_command(a, 0);
        break;
    }
    case P_FX: rr_app_fx(rr.fx_sel, 1, 100); break;
    case P_CONDUCT: rr_app_command(rr.cmd_sel, rr.cmd_sel == CMD_SOLO ? RC_BASS : 0); break;
    case P_MIDI: rr_map_learn(&rr.map, rr.learn_sel); rr_app_toast("MOVE A CONTROL"); break;
    case P_SYSTEM: rr_app_system(rr.sys_action, rr.mem_sel); break;
    default: break;
    }
}
RR_SMALL static void panel(const struct fm1_controls *c)
{
    uint32_t pressed = c->buttons & ~prev_buttons, released = prev_buttons & ~c->buttons;
    if (groove_master_update(c->knob)) {}
    if (pressed & BIT(BTN_PLAY)) rr_app_play(!rr.playing);
    if (pressed & BIT(BTN_REC)) { int b = groove_tempo_tap(now_ms); if (b > 0) { rr.bpm_target = (uint8_t)b; rr_app_toast("TAP TEMPO"); } }
    if (pressed & BIT(BTN_OCTDN)) rr.live.octave_a = (uint8_t)adj(rr.live.octave_a, -1, 0, 4), rr.live.octave_b = rr.live.octave_a;
    if (pressed & BIT(BTN_OCTUP)) rr.live.octave_a = (uint8_t)adj(rr.live.octave_a, 1, 0, 4), rr.live.octave_b = rr.live.octave_a;
    for (unsigned p = 0; p < RR_PAGES; ++p) if (pressed & BIT(page_button[p])) {
        if (rr.simple && p > P_STYLE && p != P_SYSTEM) break;
        if (rr.page == p && p != P_HOME) page_action();
        else rr.page = (uint8_t)p;
    }
    /* Hold-and-play gestures keep the page; releasing FX ends held voices. */
    if ((released & BIT(BTN_FX)) && rr.fx_hold) rr_app_fx(rr.fx_hold - 1U, 0, 0);
    uint32_t keys = c->keys & 0x7ffffffU, changed = keys ^ prev_keys;
    while (changed) {
        unsigned k = (unsigned)__builtin_ctz(changed);
        changed &= changed - 1;
        key_event(k, (keys >> k) & 1U, c->buttons);
    }
    prev_keys = keys; prev_buttons = c->buttons;
    /* Encoders. ALGORITHM = BPM anywhere (HOME held: x5). PRESET = style anywhere. */
    if (c->encoders[ENC_BPM]) {
        int d = c->encoders[ENC_BPM] * ((c->buttons & BIT(BTN_HOME)) ? 5 : 1);
        rr_app_bpm((int)(rr.bpm_target ? rr.bpm_target : rr.bpm) + d, !rr.bpm_smooth);
        touched[T_BPM] = now_ms;
    }
    if (c->encoders[ENC_STYLE]) {
        rr.pending_style = (uint8_t)((rr.pending_style + RR_STYLES * 4 + c->encoders[ENC_STYLE]) % RR_STYLES);
        style_ms = now_ms; rr.style_wait = 1;
    }
    if (c->encoders[ENC_SELECT]) {
        unsigned count = rr.simple ? 2 : RR_PAGES;
        rr.page = (uint8_t)((rr.page + count * 4 + (unsigned)c->encoders[ENC_SELECT]) % count);
    }
    for (unsigned k = 0; k < 4; ++k) if (c->encoders[ENC_K1 + k]) rr_app_knob(k, c->encoders[ENC_K1 + k]);
    /* A turned style selector commits after 700 ms so scrolling never queues every style. */
    if (rr.style_wait && now_ms - style_ms > 700) {
        rr.style_wait = 0;
        if (rr.pending_style != (rr.e.transition ? rr.e.target : rr.e.style)) rr_app_style(rr.pending_style);
    }
    fm1_led_set(BTN_PLAY, rr.playing && (rr.step & 3) == 0);
}
RR_SMALL static void health_poll(void)
{
    struct fm1_engine_stats es;
    struct fm1_audio_stats as;
    fm1_engine_stats_read(&es);
    fm1_audio_stats_read(&as);
    if (es.underruns > rr.underruns || as.overruns > rr.overloads) {
        rr_fx_param(FXP_SHED, 1);                              /* graceful: drop heavy inserts first */
        if (!diag.shed) rr_app_toast("CPU: FX REDUCED");
        diag.shed = 1;
    }
    rr.underruns = es.underruns; rr.overloads = as.overruns;
}
RR_SMALL static void update_diag(void)
{
    struct rr_fx_status fs;
    rr_fx_status(&fs);
    diag.magic = 0x33445252u; diag.ms = now_ms; diag.playing = rr.playing; diag.style = rr.e.style; diag.target = rr.e.target;
    diag.transition = rr.e.transition; diag.bpm = rr.bpm; diag.evolve = rr.e.evolve; diag.freeze = rr.e.freeze;
    diag.bar_played = rr.bar_played; diag.section = rr.section; diag.live_notes = rr_live_count(&rr.live);
    diag.notes_in = rr.notes_in; diag.cc_in = rr.cc_in; diag.midi_errors = rr.midi_errors; diag.page = rr.page;
    diag.kmode = rr.live.mode; diag.mutations = rr.e.mutations; diag.fx_blocks = fs.blocks; diag.fx_peak = fs.peak;
    diag.underruns = rr.underruns; diag.overruns = rr.overloads; diag.lion = rr_ui_lion_mode(); diag.bank = rr.map.bank;
    diag.active = rr.active; diag.energy = rr.e.energy; diag.dub = rr.e.dub; diag.bpm_target = rr.bpm_target;
    for (unsigned c = 0; c < 8; ++c) diag.hash[c] = rr_motif_hash(&rr.e.comp[c].cur);
    for (unsigned t = 0; t < 8; ++t) diag.inst[t] = rr.inst[t];
    for (unsigned t = 0; t < 6; ++t) diag.level[t] = rr.mix.level[t];
    for (unsigned d = 0; d < RR_DIMS; ++d) diag.dim[d] = rr.e.dim[d];
    diag.mem_used = rr.memory_used; diag.fx_voices = fs.voices; diag.version = rr.e.version; diag.pixels = rr_ui_pixels_sent();
}
RR_SMALL void rr_app_init(void)
{
    memset(&rr, 0, sizeof rr);
    memset(&seq, 0, sizeof seq);
    rr_fx_init();
    rr_map_defaults(&rr.map);
    rr_live_init(&rr.live);
    rr.live.octave_a = rr.live.octave_b = 2;
    rr.bpm_lo = 50; rr.bpm_hi = 200; rr.bpm_smooth = 1; rr.release_s = 4; rr.tmode = TM_SMOOTH; rr.tbars = 4;
    rr.fx_amount = 80; rr.fx_sel = FX_PHASER; rr.cmd_sel = CMD_FILL; rr.learn_sel = T_FILTER;
    rr.mix.delay_send = 64; rr.mix.reverb_send = 64; rr.mix.filter = 64; rr.mix.delay_fb = 60;
    load_settings();
    rr_engine_init(&rr.e, RS_ROOTS, 0x52525233u ^ fm1_millis());
    for (unsigned t = 0; t < RR_GEN_TRACKS; ++t) rr.mix.level[t] = rr_styles[RS_ROOTS].levels[t];
    rr.inst[RT_LIVE_A] = I_ORGAN; rr.inst[RT_LIVE_B] = I_EBASS;
    rr.kit = rr_styles[RS_ROOTS].kit;
    rr.bpm = rr.bpm_target = rr_styles[RS_ROOTS].bpm;
    rr.pending_style = RS_ROOTS;
    for (unsigned t = 0; t < RR_TARGETS; ++t) rr.map.value[t] = 64;
    rr.map.value[T_EVOLVE] = (uint8_t)(rr.e.evolve * 127 / 100);
    build_project();
    for (unsigned t = 0; t < 5; ++t) rr.map.value[T_LEVEL_BASS + (t == 0 ? 1 : t == 1 ? 0 : t)] = rr.mix.level[t];
    apply_style_space(RS_ROOTS);
    rr.live.key = rr.e.key; rr.live.scale_mode = rr.e.mode;
    rr_app_toast("RIDDIM ROOTS " RR_VERSION);
    update_diag();
}
RR_SMALL void rr_app_poll(const struct fm1_controls *c)
{
    now_ms = fm1_millis();
    midi_poll();
    panel(c);
    sequencer_poll();
    tempo_poll();
    echo_throw_poll();
    if (now_ms - toast_ms > 250) { toast_ms = now_ms; health_poll(); }
    rr.live.key = rr.e.key; rr.live.scale_mode = rr.e.mode;
    if (rr.midi_flash && now_ms - midi_ms > 150) rr.midi_flash = 0;
    ++diag.polls;
    if (now_ms - diag_ms >= 40) { diag_ms = now_ms; update_diag(); }
}
