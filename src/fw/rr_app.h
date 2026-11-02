/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Firmware glue between the portable core, the FM1 SDK and the UI. */
#ifndef RR_APP_H
#define RR_APP_H
#include "fm1.h"
#include "rr_core.h"
#include "rr_inst.h"

enum rr_page { P_HOME, P_STYLE, P_EVOLVE, P_FREEZE, P_INSTR, P_LIVE, P_FX, P_CONDUCT, P_MIDI, P_SYSTEM, RR_PAGES };
extern const char *const rr_page_names[RR_PAGES];
enum { RR_ROLES_UI = 8, RR_MEMORY = 4, RR_RECENT = 6 };

struct rr_state {
    struct rr_engine e;
    struct rr_live live;
    struct rr_mapper map;
    struct rr_mix mix;
    uint8_t playing, page, simple, bpm, bpm_target, bpm_smooth, bpm_lo, bpm_hi;
    uint8_t pending_style, style_wait, tmode, tbars;
    uint8_t inst[RR_ROLES_UI];          /* instrument per role: 6 band rows + LIVE A + LIVE B */
    uint8_t kit, muted, level_keys;
    uint8_t sel[RR_PAGES];              /* per-page selection (K1 cursor) */
    uint8_t fx_sel, fx_amount, fx_on, cmd_sel, mem_sel, sys_action, learn_sel, freeze_act;
    uint8_t release_s;                  /* human-priority release, seconds */
    uint8_t section, phrase_pos, chord_root, chord_q, active, step, bass_hit;
    uint8_t midi_flash, midi_vel, lion_event, fx_hold, freeze_view;
    uint8_t memory_used;
    uint16_t events;
    uint32_t bar_played, notes_in, cc_in, midi_errors, overloads, underruns;
    uint8_t recent[RR_RECENT][3];
    uint8_t recent_n;
    char toast[22];
    uint32_t toast_until;
    uint8_t memory[RR_MEMORY][RR_DNA_BYTES];
};
extern struct rr_state rr;

void rr_app_init(void);
void rr_app_poll(const struct fm1_controls *c);
/* Routing-task hook: returns 1 when the message was taken by RIDDIM ROOTS. */
int rr_midi_receive(unsigned source, const uint8_t *m, size_t n);
/* UI-facing actions */
void rr_app_play(int on);
void rr_app_style(unsigned style);
void rr_app_command(unsigned cmd, unsigned arg);
void rr_app_fx(unsigned fx, unsigned on, unsigned velocity);
void rr_app_instrument(unsigned role, unsigned inst);
void rr_app_level(unsigned role, unsigned level);
void rr_app_bpm(int bpm, int instant);
void rr_app_toast(const char *text);
void rr_app_target(unsigned target, unsigned value, unsigned pressed);
unsigned rr_app_target_value(unsigned target);
void rr_app_system(unsigned action, unsigned slot);
enum { SYS_STORE, SYS_RECALL, SYS_SAVE, SYS_LOAD, SYS_PANIC, SYS_MODE, SYS_DEFAULTS, SYS_MAPSAVE, RR_SYS };
extern const char *const rr_sys_names[RR_SYS];
#endif
