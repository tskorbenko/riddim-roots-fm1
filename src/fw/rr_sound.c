/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Instrument library: original DX7-format voices, Karplus-Strong guitars,
 * 303 basses, PSG noise and eight 808 kit voicings. Every family has its own
 * articulation: velocity-to-brightness on the modulators, key-rate scaling,
 * pluck transients, slow-attack brass modulators, reed vibrato. */
#include "rr_sound.h"
#include "drum808.h"
#include <string.h>

/* One operator pair (carrier + modulator) of DX7 algorithm 5. */
struct pair { uint8_t level, coarse, fine, det, cdecay, cbody, mod, ratio, mdecay, mbody; };
struct voice {
    char name[11];
    uint8_t fb, attack, sustain, release, mattack, mrelease, vel, lfo_speed, pmd, amd, pms;
    struct pair p[3];
};
static const struct voice voices[] = {
    /*   name          fb  att sus rel  matt mrel vel lfo pmd amd pms   pair1 (body)                    pair2 (colour)                   pair3 (transient)       */
    {"E.BASS    ",     3, 99, 62, 72,  99, 80,  5,  30, 0, 0, 0, {{99,1,0,7,38,82,74,1,55,52}, {96,1,0,8,40,78,58,2,62,30}, {62,2,0,5,75,0,70,7,85,0}}},
    {"UPRIGHT   ",     0, 92, 40, 66,  95, 72,  4,  30, 0, 0, 0, {{99,1,0,7,45,70,62,1,60,35}, {94,1,0,7,40,72,40,1,55,20}, {55,1,0,7,80,0,72,3,90,0}}},
    {"SUB BASS  ",     0, 95, 88, 70,  99, 70,  2,  30, 0, 0, 0, {{99,1,0,7,30,92,28,1,60,10}, {97,1,0,7,30,92,0,1,50,0},     {50,2,0,7,80,0,40,1,85,0}}},
    {"DIGI BASS ",     6, 99, 20, 85,  99, 85,  3,  30, 0, 0, 0, {{99,1,0,7,70,40,80,1,70,30}, {95,1,0,8,72,35,70,3,80,10},{0,1,0,7,50,0,0,1,50,0}}},
    {"FM BASS   ",     7, 99, 50, 74,  99, 74,  5,  30, 0, 0, 0, {{99,1,0,7,50,70,78,1,58,40}, {70,1,0,11,50,70,60,2,70,20},{60,1,0,3,55,60,80,1,60,50}}},
    {"E.PIANO   ",     6, 99, 20, 60,  99, 64,  6,  34, 0, 0, 0, {{99,1,0,7,30,75,70,1,45,35}, {90,1,0,10,32,72,55,1,50,20},{68,1,0,4,70,0,80,14,90,0}}},
    {"PIANO     ",     0, 99, 10, 58,  99, 66,  6,  30, 0, 0, 0, {{99,1,0,7,35,70,68,1,50,30}, {92,1,1,9,35,68,60,3,55,15}, {72,2,0,6,55,30,60,5,70,0}}},
    {"CLAVINET  ",     2, 99, 30, 82,  99, 82,  6,  30, 0, 0, 0, {{99,1,0,7,60,60,85,3,60,50}, {80,1,0,9,65,50,75,5,70,40}, {60,2,0,7,80,0,70,7,85,0}}},
    {"MELODICA  ",     3, 70, 90, 70,  64, 70,  4,  33, 6, 0, 3, {{99,1,0,7,30,90,70,2,30,80}, {80,1,0,9,30,90,55,1,30,70}, {50,3,0,7,40,80,40,1,40,60}}},
    {"BRASS SEC ",     7, 80, 92, 72,  70, 70,  5,  32, 4, 0, 2, {{99,1,0,7,25,92,82,1,35,82}, {92,1,0,11,25,92,78,1,35,78},{85,1,0,3,25,92,80,1,35,80}}},
    {"TRUMPET   ",     6, 82, 90, 74,  70, 72,  5,  34, 8, 0, 3, {{99,1,0,7,30,90,86,1,40,84}, {70,2,0,7,30,85,60,1,40,60}, {0,1,0,7,30,0,0,1,40,0}}},
    {"TROMBONE  ",     5, 76, 90, 70,  64, 68,  4,  32, 6, 0, 2, {{99,1,0,7,30,90,74,1,40,70}, {82,1,0,9,30,88,64,1,40,60}, {0,1,0,7,30,0,0,1,40,0}}},
    {"STEEL PAN ",     0, 99, 10, 60,  99, 62,  5,  30, 0, 0, 0, {{99,1,0,7,40,40,65,2,55,30}, {70,2,0,7,50,20,50,3,60,10}, {60,3,0,7,60,0,40,1,70,0}}},
    {"SYNTH BRAS",     7, 90, 94, 75,  75, 74,  4,  30, 3, 0, 2, {{99,1,0,4,20,95,80,1,30,88}, {95,1,0,10,20,95,80,1,30,88},{70,0,0,7,20,95,60,1,30,80}}},
    {"DUB CHORD ",     2, 95, 25, 66,  95, 66,  4,  30, 0, 0, 0, {{99,1,0,6,55,35,55,1,60,25}, {90,1,0,10,55,35,45,2,65,15},{60,2,0,7,60,20,30,1,60,0}}},
    {"DRONE     ",     5, 40, 95, 40,  45, 40,  2,  20, 3, 15, 2,{{99,1,0,5,20,95,40,1,20,60}, {90,1,0,11,20,95,45,2,20,55},{75,0,0,7,20,95,30,3,20,40}}},
    {"GLITCH    ",     7, 99, 0,  85,  99, 85,  6,  30, 0, 0, 0, {{99,1,37,7,70,10,80,3,75,20},{80,2,71,7,75,0,85,7,80,0},  {70,5,13,7,85,0,70,11,85,0}}},
    {"SYNTH LEAD",     4, 92, 92, 72,  90, 72,  4,  35, 7, 0, 3, {{99,1,0,7,25,92,72,2,30,70}, {80,1,0,9,25,92,60,1,30,60}, {0,1,0,7,30,0,0,1,30,0}}},
};
enum { VOICES = sizeof voices / sizeof voices[0] };
/* rr_inst -> DX7 voice index (255 = other engine). */
static const uint8_t fm_index[RR_INSTS] = {
    0, 1, 2, 3, 4, 255, 255, 255, 255, 255, 255, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 255, 17, 255, 255
};
RR_SMALL static void fm_voice(uint8_t p[156], const struct voice *v)
{
    memset(p, 0, 156);
    for (unsigned k = 0; k < 3; ++k) {
        const struct pair *pr = &v->p[k];
        uint8_t *c = p + 21 * (5 - 2 * k), *m = p + 21 * (4 - 2 * k);
        /* Carrier: attack, own decay to body, slow settle, release. */
        c[0] = v->attack; c[1] = pr->cdecay; c[2] = (uint8_t)(pr->cdecay / 2 + 18); c[3] = v->release;
        c[4] = 99; c[5] = pr->cbody; c[6] = (uint8_t)(pr->cbody * v->sustain / 99); c[7] = 0;
        c[8] = 39; c[13] = 2; c[15] = 2; c[16] = pr->level; c[18] = pr->coarse; c[19] = pr->fine; c[20] = pr->det;
        /* Modulator: velocity opens brightness; higher keys decay faster and
         * are slightly darker (right curve -EXP), like real strings and tines. */
        m[0] = v->mattack; m[1] = pr->mdecay; m[2] = (uint8_t)(pr->mdecay / 2 + 10); m[3] = v->mrelease;
        m[4] = 99; m[5] = pr->mbody; m[6] = (uint8_t)(pr->mbody * v->sustain / 99); m[7] = 0;
        m[8] = 39; m[10] = 22; m[12] = 1; m[13] = 3; m[15] = v->vel; m[16] = pr->mod; m[18] = pr->ratio; m[19] = 0; m[20] = 7;
        if (v->amd) c[14] = 2;
    }
    for (unsigned i = 126; i < 130; ++i) p[i] = 99;
    for (unsigned i = 130; i < 134; ++i) p[i] = 50;
    p[134] = 4; p[135] = v->fb; p[136] = 1; p[137] = v->lfo_speed; p[138] = v->pmd ? 40 : 0;
    p[139] = v->pmd; p[140] = v->amd; p[141] = 0; p[142] = 4; p[143] = v->pms; p[144] = 24;
    memcpy(p + 145, v->name, 10);
    p[155] = 63;
}
/* Tonewheel organ: algorithm 32, six drawbar sines 16'/8'/5 1/3'/4'/2 2/3'/2'
 * plus key click; the digital organ is brighter and percussive. */
RR_SMALL static void organ_voice(uint8_t p[156], int digital)
{
    static const uint8_t ratio[6] = {0, 1, 1, 2, 3, 4}, fine[6] = {0, 0, 50, 0, 0, 0};
    static const uint8_t level[2][6] = {{72, 78, 66, 62, 48, 44}, {56, 80, 0, 72, 60, 56}};
    memset(p, 0, 156);
    for (unsigned op = 0; op < 6; ++op) {
        uint8_t *v = p + 21 * (5 - op);
        v[0] = 99; v[1] = digital ? 60 : 99; v[2] = 99; v[3] = 82;
        v[4] = 99; v[5] = digital && op > 2 ? 70 : 99; v[6] = digital && op > 2 ? 60 : 99; v[7] = 0;
        v[8] = 39; v[15] = 1; v[16] = level[digital][op]; v[18] = ratio[op]; v[19] = fine[op]; v[20] = op & 1 ? 8 : 6;
    }
    for (unsigned i = 126; i < 130; ++i) p[i] = 99;
    for (unsigned i = 130; i < 134; ++i) p[i] = 50;
    p[134] = 31; p[135] = digital ? 2 : 5; p[136] = 1; p[137] = 36; p[138] = 0; p[139] = 4; p[142] = 4; p[143] = 2; p[144] = 24;
    memcpy(p + 145, digital ? "DIGI ORGAN" : "ORGAN     ", 10);
    p[155] = 63;
}
RR_SMALL static void base(struct groove_preset *p, unsigned type, unsigned level, unsigned sends)
{
    memset(p, 0, sizeof *p);
    p->type = (uint8_t)type; p->volume = (uint8_t)(level > 127 ? 127 : level); p->modified = 1;
    p->controls.envelope[2] = 100;
    p->data[209] = (uint8_t)sends;
}
RR_SMALL void rr_sound_preset(struct groove_preset *p, unsigned inst, unsigned level, unsigned sends)
{
    if (inst >= RR_INSTS) inst = I_ORGAN;
    if (inst == I_ORGAN || inst == I_DIGIORGAN) {
        base(p, GROOVE_DX7, level, sends); organ_voice(p->data, inst == I_DIGIORGAN); p->program = 2;
    } else if (fm_index[inst] != 255) {
        base(p, GROOVE_DX7, level, sends); fm_voice(p->data, &voices[fm_index[inst]]); p->program = 0;
    } else if (inst == I_SKANKGTR || inst == I_MUTEDGTR) {
        /* Karplus-Strong string: DECAY, BRIGHT, TUNE, RELEASE. */
        static const uint8_t g[2][4] = {{62, 108, 24, 30}, {34, 64, 24, 16}};
        base(p, GROOVE_PLUCK, level, sends); memcpy(p->data, g[inst == I_MUTEDGTR], 4);
    } else if (inst == I_WOBBLE || inst == I_ACID) {
        static const uint8_t b[2][12] = {
            {30, 72, 34, 74, 64, 0, 64, 104, 22, 1, 40, 7},     /* wobble: saw, dark, LFO on volume */
            {52, 104, 84, 48, 92, 0, 64, 92, 34, 2, 62, 10}     /* acid */
        };
        base(p, GROOVE_303, level, sends); memcpy(p->data, b[inst == I_ACID], 12);
        p->program = 0; p->controls.mono = 1;
        if (inst == I_WOBBLE) { p->controls.enabled = 1; p->controls.envelope[0] = 44; p->controls.envelope[1] = 70; p->controls.envelope[2] = 0; p->controls.envelope[3] = 0; }
    } else if (inst == I_NOISE || inst == I_SIREN) {
        base(p, GROOVE_PSG, level, sends);
        p->program = p->data[0] = (uint8_t)(inst == I_NOISE ? 3 : 0);
        p->data[1] = inst == I_NOISE ? 22 : 16; p->data[2] = 24; p->data[3] = inst == I_NOISE ? 70 : 48;
    } else rr_sound_kit(p, K_ROOTS, level);
}

/* ---- 808 kit voicings ---- */
struct kitpart { uint8_t level, tune, decay, alt, rev, dly; };
struct kit { uint8_t bd_tone, sd_tone, snappy, drive, dist; struct kitpart part[D8_NUM]; };
/* parts: BD SD LT MT HT RS CP CB CY OH CH; alt selects conga / clave / maracas. */
static const struct kit kits[RR_KITS] = {
    {30, 70, 62, 0, 2, {{97,58,72,0,0,0},{73,74,66,0,22,0},{62,58,62,1,6,0},{59,70,52,1,6,0},{54,82,46,1,6,0},{68,72,44,0,18,10},{40,64,40,1,4,0},{35,64,50,0,4,0},{35,64,66,0,10,0},{49,66,54,0,6,0},{54,72,30,0,4,0}}},
    {40, 74, 82, 18, 2, {{113,52,96,0,0,0},{81,72,80,0,26,0},{62,56,62,1,6,0},{59,68,52,1,6,0},{54,80,46,1,6,0},{68,70,44,0,20,12},{38,64,40,1,4,0},{35,64,50,0,4,0},{40,64,70,0,12,0},{51,66,56,0,6,0},{54,74,28,0,4,0}}},
    {60, 82, 100, 10, 1, {{103,60,52,0,0,0},{84,82,48,0,14,0},{57,62,50,0,4,0},{54,72,44,0,4,0},{51,84,40,0,4,0},{65,80,36,1,10,8},{68,70,46,0,12,0},{46,70,40,0,4,0},{35,70,60,0,8,0},{46,74,46,0,4,0},{59,80,24,0,2,0}}},
    {50, 92, 112, 6, 2, {{100,56,60,0,0,0},{84,82,58,0,18,0},{59,60,56,0,6,0},{57,72,48,0,6,0},{54,84,44,0,6,0},{62,76,40,0,10,0},{40,64,40,1,4,0},{27,64,50,0,4,0},{46,72,72,0,12,0},{54,72,62,0,8,0},{59,82,24,0,4,0}}},
    {25, 64, 70, 8, 2, {{116,48,100,0,0,0},{59,70,60,0,30,0},{54,56,60,1,10,0},{51,66,52,1,10,0},{49,78,46,1,10,0},{70,76,40,0,26,18},{54,66,52,0,20,12},{32,64,50,0,8,0},{30,60,70,0,12,0},{54,84,58,0,16,10},{49,90,20,0,8,0}}},
    {20, 60, 92, 12, 2, {{122,44,110,0,0,0},{84,62,108,0,40,0},{54,52,66,0,10,0},{51,62,56,0,10,0},{49,74,50,0,10,0},{62,70,44,0,24,12},{49,64,60,0,22,0},{30,64,50,0,6,0},{40,62,80,0,20,0},{49,70,60,0,10,0},{54,80,26,0,6,0}}},
    {44, 88, 120, 56, 5, {{108,50,80,0,0,0},{81,86,70,0,24,10},{59,50,70,0,10,0},{57,60,60,0,10,0},{54,72,52,0,10,0},{70,90,50,1,20,16},{59,70,60,0,20,10},{40,90,50,0,10,0},{46,80,90,0,20,10},{49,96,70,0,14,0},{57,100,26,0,8,0}}},
    {36, 84, 92, 0, 2, {{94,62,60,0,0,0},{81,86,54,0,16,0},{59,62,52,1,6,0},{57,72,46,1,6,0},{54,84,42,1,6,0},{65,74,40,0,12,6},{40,64,40,1,4,0},{49,72,46,0,4,0},{40,70,64,0,10,0},{54,76,46,0,6,0},{57,80,24,0,4,0}}},
};
RR_SMALL void rr_sound_kit(struct groove_preset *p, unsigned kit, unsigned level)
{
    if (kit >= RR_KITS) kit = K_ROOTS;
    const struct kit *k = &kits[kit];
    base(p, GROOVE_808, level, 0x21);
    unsigned at = D8_NUM;
    for (unsigned part = 0; part < D8_NUM; ++part) p->data[part] = k->part[part].alt;
    for (unsigned variant = 0; variant < 2; ++variant)
        for (unsigned part = 0; part <= D8_NUM; ++part)
            for (int i = 0; i < drum808_nparams((int)part); ++i) {
                const fm1_param_t *d = drum808_param((int)part, i);
                unsigned value = d->def;
                if (!strcmp(d->name, "Sound")) value = variant;
                else if (part == D8_NUM && !strcmp(d->name, "Level")) value = 124;
                else if (part < D8_NUM) {
                    const struct kitpart *kp = &k->part[part];
                    if (!strcmp(d->name, "Level")) value = kp->level;
                    else if (!strcmp(d->name, "Tune")) value = kp->tune;
                    else if (!strcmp(d->name, "Decay")) value = kp->decay;
                    else if (!strcmp(d->name, "Rev")) value = kp->rev;
                    else if (!strcmp(d->name, "Dly")) value = kp->dly;
                    else if (!strcmp(d->name, "Tone")) value = part == D8_BD ? k->bd_tone : k->sd_tone;
                    else if (!strcmp(d->name, "Snappy")) value = k->snappy;
                    else if (!strcmp(d->name, "Drive")) value = k->drive;
                    else if (!strcmp(d->name, "Dist") && k->drive) value = k->dist;
                }
                if (value > d->max) value = d->max;
                p->data[at++] = (uint8_t)value;
            }
}
