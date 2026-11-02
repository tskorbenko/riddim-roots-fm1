/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* RIDDIM ROOTS v0.3.0 "DUB EVOLUTION" — portable music core.
 * No SDK calls: the same code runs on the FM-1 control task and in host tests.
 * The firmware glue asks for one rendered bar at a time (16 sixteenths for six
 * sequencer tracks) and writes it one bar ahead of the playhead. */
#ifndef RR_CORE_H
#define RR_CORE_H
#include <stdint.h>
#include <string.h>

#define RR_VERSION "0.3.0"
/* Control-side code is optimised for size; audio code (rr_fx.c) is not. */
#if defined(__clang__) && !defined(FM1_HOST_TEST)
#define RR_SMALL __attribute__((minsize))
#else
#define RR_SMALL
#endif
enum { RR_STEPS = 16, RR_MOTIF = 32, RR_GEN_TRACKS = 6, RR_PHRASE = 8 };

enum rr_style {
    RS_SKA, RS_ROCKSTEADY, RS_ROOTS, RS_STEPPERS, RS_DUB, RS_RUBADUB,
    RS_DANCEHALL, RS_JUNGLE, RS_DUBTECHNO, RS_DUBSTEP, RS_DUBNOISE, RR_STYLES
};
/* Musical components. Organ, piano and horns are separate band members.
 * HARMONY and ARRANGE are non-sounding components that can also be frozen. */
enum rr_comp {
    RC_DRUMS, RC_PERC, RC_BASS, RC_SKANK, RC_ORGAN, RC_PIANO, RC_HORNS, RC_FX,
    RC_HARMONY, RC_ARRANGE, RR_COMPS
};
enum { RR_MOTIF_COMPS = RC_FX + 1 };
/* Sequencer rows. Rows 6/7 are SDK PLAY tracks for the live keyboard. */
enum rr_track { RT_DRUMS, RT_BASS, RT_SKANK, RT_ORGAN, RT_HORNS, RT_PIANO, RT_LIVE_A, RT_LIVE_B };
enum rr_dim { RD_RHYTHM, RD_BASS, RD_HARMONY, RD_INSTR, RD_ARRANGE, RD_TIMBRE, RD_FX, RR_DIMS };
enum rr_section { SEC_INTRO, SEC_VERSE, SEC_CHORUS, SEC_DUB, SEC_BREAK, SEC_BUILD, SEC_DROP, SEC_OUTRO, RR_SECTIONS };
enum rr_tmode { TM_SMOOTH, TM_DUBMIX, TM_BREAK, TM_HARD, TM_MUTANT, RR_TMODES };

/* 808 kit lanes (bit = SDK drum part). */
enum { DK_BD = 1, DK_SD = 2, DK_LT = 4, DK_MT = 8, DK_HT = 16, DK_RS = 32, DK_CP = 64,
       DK_CB = 128, DK_CY = 256, DK_OH = 512, DK_CH = 1024 };
/* Melodic hit flags. */
enum { MH_NOTE = 1, MH_CHORD = 2, MH_ACCENT = 4, MH_GHOST = 8, MH_TIE = 16, MH_SLIDE = 32 };

/* Chord-table ids written into sequencer cells (SDK chord field). */
enum { CH_SINGLE, CH_MAJ, CH_MIN, CH_MIN7, CH_MAJ7, CH_DOM7, CH_SUS4, CH_POWER,
       CH_MIN9, CH_OCT, CH_MADD9, CH_MAJ6, CH_SKMAJ, CH_SKMIN, CH_MIN11, CH_DIM, RR_CHORDS };

struct rr_cell { uint8_t note, velocity; uint16_t bits; };   /* == struct groove_step */

struct rr_motif {
    uint16_t hit[RR_MOTIF];
    int8_t deg[RR_MOTIF];
    uint8_t vel[RR_MOTIF], gate[RR_MOTIF], len[RR_MOTIF];
    uint8_t style, variant, octave, voicing;
};

struct rr_comp_state {
    struct rr_motif cur, kept;
    uint8_t depth, rate;      /* 0..100 per-component mutation depth and rate */
    uint8_t age;              /* phrases since the last mutation */
    uint8_t generation;       /* incremented by every committed change */
    uint8_t switched;         /* style already switched in the running transition */
    uint8_t solo_bars, out_bars;
};

struct rr_style_dna {
    char name[12];
    uint8_t bpm, bpm_lo, bpm_hi, swing, mode;   /* mode: 0 minor, 1 major, 2 dorian, 3 mixolydian */
    uint8_t drum_set, bass_set, skank_set, keys_set, horn_set;
    uint8_t bass_stretch, energy, dub, fx_bias;
    uint8_t kit, bass_inst, skank_inst, organ_inst, horn_inst, piano_inst;
    uint8_t alt_inst[3];      /* timbre-mutation candidates for organ/horns/piano */
    uint8_t prog_mask;        /* allowed progression families */
    uint8_t levels[RR_GEN_TRACKS];
    uint8_t delay, feedback, reverb, sends;
};
extern const struct rr_style_dna rr_styles[RR_STYLES];

/* Conductor commands: band-member actions and dub-engineer actions. */
enum rr_cmd {
    CMD_NONE, CMD_FILL, CMD_SWITCH_GROOVE, CMD_DOUBLE_PERC, CMD_NO_KICK, CMD_BREAKDOWN,
    CMD_NEW_BASS, CMD_BASS_SOLO, CMD_BASS_OCTAVE, CMD_BASS_SYNC, CMD_BASS_HOME,
    CMD_BUBBLE, CMD_STABS, CMD_KEYS_VARIATION, CMD_VOICING,
    CMD_SKANK_CHANGE, CMD_SKANK_MUTED, CMD_SKANK_VARIATION,
    CMD_MUTE_MUSIC, CMD_ECHO_THROW, CMD_SPRING_SNARE, CMD_BASS_ONLY, CMD_BIG_DROP, CMD_RESTORE_ALL,
    CMD_NEXT_VERSION, CMD_BUILD, CMD_DROP, CMD_SOLO, CMD_MUTATE, CMD_RESTORE, CMD_REWIND,
    CMD_KEEP, CMD_REJECT, CMD_REBUILD, CMD_FREEZE_TOGGLE, CMD_SIREN, RR_CMDS
};
extern const char *const rr_cmd_names[RR_CMDS];

/* Engine → glue notifications (FX automation, lion animation, UI flashes). */
enum { EV_BAR = 1, EV_PHRASE = 2, EV_MUTATE = 4, EV_TRANSITION = 8, EV_TRANSITION_DONE = 16,
       EV_ECHO_THROW = 32, EV_SIREN = 64, EV_DROP = 128, EV_REWIND = 256, EV_FILL = 512,
       EV_BUILD = 1024, EV_TEMPO_JUMP = 2048, EV_SPRING = 4096, EV_NOISE = 8192 };

struct rr_engine {
    uint32_t seed, rng, bar;          /* bar = absolute bar being rendered */
    uint8_t style, target, tmode, tbars, tprogress, transition;
    uint16_t tswitched;               /* components already in the target style */
    uint8_t key, mode, energy, dub, evolve, intensity;
    uint8_t dim[RR_DIMS];
    uint16_t freeze;                  /* bit per rr_comp */
    uint8_t section, section_bars, phrase_pos;
    uint8_t prog[RR_PHRASE], prog_q[RR_PHRASE], prog_kept[RR_PHRASE], progq_kept[RR_PHRASE];
    uint8_t bpm_target, bpm_from, tempo_plan;  /* tempo plan: 0 ramp, 1 double, 2 half */
    uint8_t stretch[RR_GEN_TRACKS];
    uint8_t active;                   /* roles audible this bar (bit per track) */
    uint8_t user_mute;                /* engineer/performer mutes (bit per track) */
    uint8_t fill_bar, build_bars, drop_bars, breakdown_bars, bass_only_bars, nokick_bars;
    uint8_t perc_double, skank_muted, bass_octave, rewind_bars, version;
    uint16_t events;                  /* pending EV_* for the glue */
    uint16_t mutations;               /* total committed mutations (diagnostics) */
    struct rr_comp_state comp[RR_MOTIF_COMPS];
};

void rr_engine_init(struct rr_engine *e, unsigned style, uint32_t seed);
/* Render the bar e->bar into six tracks of 16 cells, then advance e->bar.
 * Phrase-level mutation, arrangement and transitions are evaluated here. */
struct rr_bar_info {
    uint16_t events;          /* EV_* raised by this bar */
    uint8_t bpm, tempo_jump;  /* tempo wanted when this bar starts */
    uint8_t section, phrase_pos, chord_root, chord_q, active;
    uint8_t nfx, fx_step[4], fx_kind[4], fx_arg[4];
};
enum { FXK_ECHO_THROW, FXK_SIREN, FXK_NOISE, FXK_SPRING, FXK_SWEEP, FXK_LASER, FXK_FEEDBACK, FXK_DROP };
void rr_engine_render(struct rr_engine *e, struct rr_cell out[RR_GEN_TRACKS][RR_STEPS], struct rr_bar_info *info);
/* Drum fill for the remaining steps of the bar that is playing now. */
void rr_engine_fill(const struct rr_engine *e, struct rr_cell drums[RR_STEPS], unsigned from, unsigned intensity);
/* Queue a style change; playback never stops. bars: 0 = next boundary. */
void rr_engine_style(struct rr_engine *e, unsigned style, unsigned mode, unsigned bars, unsigned current_bpm);
void rr_engine_command(struct rr_engine *e, unsigned cmd, unsigned arg);
void rr_engine_freeze(struct rr_engine *e, unsigned comp, int on); /* on<0 toggles */
void rr_engine_mutate(struct rr_engine *e, unsigned comp, unsigned depth);
void rr_engine_keep(struct rr_engine *e);
void rr_engine_reject(struct rr_engine *e);
/* Musical tempo the engine wants now (bpm), given the current one. Follows the
 * transition tempo plan; the glue applies it smoothly or instantly. */
unsigned rr_engine_tempo(const struct rr_engine *e, unsigned current);
unsigned rr_motif_hash(const struct rr_motif *m);
unsigned rr_scale_note(unsigned key, unsigned mode, unsigned chord_root, int degree);
extern const char *const rr_section_names[RR_SECTIONS];
extern const char *const rr_comp_names[RR_COMPS];
extern const char *const rr_tmode_names[RR_TMODES];
extern const uint8_t rr_chord_table[RR_CHORDS][8];

static inline uint32_t rr_rand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static inline unsigned rr_range(uint32_t *s, unsigned n) { return n ? (unsigned)((rr_rand(s) >> 8) % n) : 0; }
static inline int rr_chance(uint32_t *s, unsigned percent) { return rr_range(s, 100) < percent; }
static inline int rr_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- Live keyboard (rr_live.c) ---- */
enum rr_kmode { KM_NORMAL, KM_CHORD, KM_SMART, KM_SKANK, KM_BASS, KM_SPLIT, KM_SOUND, KM_CONDUCT, RR_KMODES };
extern const char *const rr_kmode_names[RR_KMODES];
struct rr_live {
    uint8_t mode, split, octave_a, octave_b, chord_q, sustain;
    uint8_t held[128][4];     /* output notes owned by each input key; 0 = none */
    uint8_t held_ch[128];     /* output channel + 1 */
    uint8_t sustained[2][16]; /* note bitmap per output channel waiting for pedal release */
    uint8_t key, scale_mode, chord_root, chord_minor;
};
/* Output callback receives complete short MIDI messages. */
typedef void (*rr_emit_fn)(void *ctx, const uint8_t *m, unsigned n);
void rr_live_init(struct rr_live *l);
/* Returns: 0 handled as notes, >0 = 1 + sound/command index for SOUND/CONDUCTOR. */
int rr_live_note(struct rr_live *l, unsigned note, unsigned velocity, rr_emit_fn emit, void *ctx);
void rr_live_sustain(struct rr_live *l, unsigned on, rr_emit_fn emit, void *ctx);
void rr_live_panic(struct rr_live *l, rr_emit_fn emit, void *ctx);
unsigned rr_live_count(const struct rr_live *l);

/* ---- MIDI mapping (rr_map.c) ---- */
enum rr_target {
    T_NONE, T_LEVEL_BASS, T_LEVEL_DRUMS, T_LEVEL_SKANK, T_LEVEL_KEYS, T_LEVEL_HORNS, T_LEVEL_FXPERC,
    T_DELAY_SEND, T_REVERB_SEND,
    T_DIM_RHYTHM, T_DIM_BASS, T_DIM_HARMONY, T_DIM_INSTR, T_DIM_ARRANGE, T_DIM_TIMBRE, T_DIM_FX, T_EVOLVE,
    T_FILTER, T_DELAY_FB, T_MORPH, T_CHAOS, T_MASTER, T_BPM, T_ENERGY, T_DUB, T_STYLE,
    T_FX_SELECT, T_FX_AMOUNT, T_REVERB_SIZE, T_DELAY_TIME, T_LIVE_LEVEL, T_EXPRESSION, T_PERC_LEVEL,
    T_PAD_SIREN, T_PAD_ECHO, T_PAD_FILL, T_PAD_DROP, T_PAD_REWIND, T_PAD_VERSION, T_PAD_MUTATE, T_PAD_FREEZE,
    T_PLAY, T_STOP, T_BANK, T_TAP, T_CMD, RR_TARGETS
};
extern const char *const rr_target_names[RR_TARGETS];
enum { MAP_CC, MAP_NOTE, MAP_PC, MAP_BEND, MAP_AT };
enum { MAPF_INVERT = 1, MAPF_TOGGLE = 2, MAPF_MOMENTARY = 4 };
struct rr_map { uint8_t kind, channel, number, bank, target, lo, hi, flags; };
enum { RR_MAPS = 48, RR_BANKS = 3 };
struct rr_mapper {
    struct rr_map map[RR_MAPS];
    uint8_t bank, pickup, learn, learn_target, learn_bank;
    uint8_t value[RR_TARGETS];       /* current 0..127 of continuous targets */
    uint8_t picked[RR_TARGETS];      /* soft takeover reached */
    uint8_t toggled[RR_TARGETS];
    uint8_t last[RR_TARGETS];        /* last raw controller value, 255 = none */
    uint16_t touched[RR_TARGETS];    /* human-priority timestamp (ms >> 4) */
};
struct rr_action { uint8_t target, value, pressed, from_learn; };
void rr_map_defaults(struct rr_mapper *m);
/* Translate one message; returns number of actions written (0..2). */
unsigned rr_map_message(struct rr_mapper *m, const uint8_t *msg, unsigned n, struct rr_action out[2]);
/* Internal value change (UI/automation): pickup must be crossed again. */
void rr_map_set_value(struct rr_mapper *m, unsigned target, unsigned value);
void rr_map_learn(struct rr_mapper *m, unsigned target);
int rr_map_valid(const struct rr_mapper *m);

/* ---- DNA memory (rr_dna.c) ---- */
enum { RR_DNA_BYTES = 312, RR_DNA_MAGIC = 0x5244 };
struct rr_mix { uint8_t level[RR_GEN_TRACKS], delay_send, reverb_send, filter, delay_fb; };
unsigned rr_dna_pack(const struct rr_engine *e, const struct rr_mix *mix, unsigned bpm, uint8_t out[RR_DNA_BYTES]);
int rr_dna_unpack(struct rr_engine *e, struct rr_mix *mix, unsigned *bpm, const uint8_t in[RR_DNA_BYTES]);
uint16_t rr_crc16(const uint8_t *p, unsigned n);
#endif
