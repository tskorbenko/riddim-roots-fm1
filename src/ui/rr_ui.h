/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef RR_UI_H
#define RR_UI_H
#include <stdint.h>
enum rr_lion_mode { L_PLAY, L_SLEEP, L_FREEZE, L_MUTATE, L_FX, L_DROP, L_DANCE, L_MIDI, RR_LION_MODES };
void rr_ui_init(void);
/* Control task; renders at most a few 16-row bands per call and submits only
 * bands whose pixels changed. Never blocks on the LCD DMA. */
void rr_ui_poll(uint32_t now);
unsigned rr_ui_lion_mode(void);
uint32_t rr_ui_pixels_sent(void);
void rr_app_knob(unsigned k, int d);
#endif
