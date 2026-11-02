/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DUB FX LAB: master-bus DSP and sound-system voices (audio ISR), plus the
 * control-side mailbox. SDK delay/reverb sends cover echo throws and spaces. */
#ifndef RR_FX_H
#define RR_FX_H
#include "fm1.h"

enum rr_fxid {
    FX_TAPE_ECHO, FX_DIGI_DELAY, FX_SPRING, FX_CHAMBER, FX_HPF, FX_LPF, FX_BPF, FX_PHASER,
    FX_FLANGER, FX_CHORUS, FX_SATURATE, FX_DISTORT, FX_CRUSH, FX_TAPESTOP, FX_REWIND, FX_SIREN,
    FX_AIRHORN, FX_LASER, FX_NOISE, FX_STUTTER, FX_FEEDBACK, FX_BASSDROP, FX_MUTE, FX_ECHOTHROW, RR_FX
};
extern const char *const rr_fx_names[RR_FX];
/* Continuous parameters (control task). */
enum rr_fxp {
    FXP_INSERT,      /* insert effect id: phaser .. crush, tape echo */
    FXP_AMOUNT,      /* insert wet 0..127 */
    FXP_FILTER,      /* 0..127, 64 open; below = low-pass, above = high-pass */
    FXP_RESONANCE,   /* 0..127 */
    FXP_ECHO_WET, FXP_ECHO_FB, FXP_ECHO_SAMPLES,
    FXP_WARMTH,      /* always-on tape saturation */
    FXP_MASTER,      /* 0..127 */
    FXP_BEND,        /* -8192..8191 siren pitch */
    FXP_EXPRESSION,  /* aftertouch: opens the filter */
    FXP_CHAOS,       /* tilt: modulation depth of the insert */
    FXP_SIREN_RATE, FXP_SIREN_WAVE, FXP_STEP_SAMPLES, FXP_SHED,
    RR_FXP
};
void rr_fx_init(void);
void rr_fx_param(unsigned p, int value);
/* One-shot / momentary voices: FX_TAPESTOP .. FX_BASSDROP. on=0 releases a hold. */
void rr_fx_trigger(unsigned fx, unsigned on, unsigned velocity);
struct rr_fx_status { uint32_t blocks, peak, reduction, voices, clips; };
void rr_fx_status(struct rr_fx_status *s);
FM1_AUDIO_CODE void rr_fx_process(int32_t *output);
#endif
