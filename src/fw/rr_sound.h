/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef RR_SOUND_H
#define RR_SOUND_H
#include "instrument.h"
#include "rr_core.h"
#include "rr_inst.h"
/* sends: reverb in the low nibble, delay in the high nibble (0..15 each). */
void rr_sound_preset(struct groove_preset *p, unsigned inst, unsigned level, unsigned sends);
void rr_sound_kit(struct groove_preset *p, unsigned kit, unsigned level);
#endif
