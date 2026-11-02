/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Instrument library ids, shared by the core (style rules) and firmware recipes. */
#ifndef RR_INST_H
#define RR_INST_H
enum rr_inst {
    I_EBASS, I_UPRIGHT, I_SUBBASS, I_DIGIBASS, I_FMBASS, I_WOBBLE, I_ACID,
    I_SKANKGTR, I_MUTEDGTR, I_ORGAN, I_DIGIORGAN, I_EPIANO, I_PIANO, I_CLAV,
    I_MELODICA, I_BRASS, I_TRUMPET, I_TROMBONE, I_STEELPAN, I_SYNTHBRASS,
    I_DUBCHORD, I_DRONE, I_GLITCH, I_NOISE, I_LEAD, I_SIREN, I_DRUMKIT, RR_INSTS
};
extern const char *const rr_inst_names[RR_INSTS];
/* 808 kit voicings */
enum rr_kit { K_ROOTS, K_STEPPERS, K_DIGITAL, K_JUNGLE, K_TECHNO, K_DUBSTEP, K_NOISE, K_SKA, RR_KITS };
#endif
