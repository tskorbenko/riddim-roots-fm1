/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Style DNA: tempo range, groove grammar families, band line-up, mix and space.
 * Styles differ by grammar and sound, not only by BPM. */
#include "rr_core.h"
#include "rr_inst.h"

const char *const rr_inst_names[RR_INSTS] = {
    "E.BASS", "UPRIGHT", "SUB BASS", "DIGI BASS", "FM BASS", "WOBBLE", "ACID",
    "SKANK GTR", "MUTED GTR", "ORGAN", "DIGI ORGAN", "E.PIANO", "PIANO", "CLAVINET",
    "MELODICA", "BRASS SEC", "TRUMPET", "TROMBONE", "STEEL PAN", "SYNTH BRASS",
    "DUB CHORD", "DRONE", "GLITCH", "NOISE SYN", "SYNTH LEAD", "DUB SIREN", "DRUM KIT"
};

/* Progression families (bit = family in rr_engine.c). */
enum { PF_ONE = 1, PF_TWO = 2, PF_ROCK = 4, PF_MINOR4 = 8, PF_MAJOR4 = 16, PF_DUB = 32, PF_TECH = 64 };

/*             name          bpm lo  hi sw md  drm bas skk key hrn str en dub fx kit        bass        skank       organ        horns        piano        alternates                         progressions               levels: drm bas skk org hrn pno  dly fb rev sends */
const struct rr_style_dna rr_styles[RR_STYLES] = {
    {"SKA",        150,130,175, 0,1,  0,  0,  0,  0,  0,  1,80, 20, 10,K_SKA,     I_UPRIGHT, I_SKANKGTR,I_ORGAN,     I_TRUMPET,   I_PIANO,    {I_DIGIORGAN,I_TROMBONE,I_EPIANO}, PF_MAJOR4|PF_ROCK,        {96,100,78,62,74,60}, 134,40,30,0x31},
    {"ROCKSTEADY",  96, 84,110,14,1,  1,  1,  1,  1,  1,  1,55, 30, 15,K_ROOTS,   I_EBASS,   I_SKANKGTR,I_ORGAN,     I_BRASS,     I_PIANO,    {I_DIGIORGAN,I_MELODICA,I_EPIANO}, PF_MAJOR4|PF_ROCK|PF_TWO, {92,104,76,66,66,64}, 135,45,40,0x42},
    {"ROOTS",       72, 64, 84,22,0,  2,  2,  2,  2,  2,  1,50, 45, 20,K_ROOTS,   I_EBASS,   I_SKANKGTR,I_ORGAN,     I_MELODICA,  I_PIANO,    {I_EPIANO,I_BRASS,I_CLAV},         PF_TWO|PF_MINOR4|PF_ONE,  {94,110,74,68,64,60}, 135,55,45,0x53},
    {"STEPPERS",    78, 70, 92,10,0,  3,  3,  3,  2,  2,  1,70, 55, 30,K_STEPPERS,I_SUBBASS,  I_SKANKGTR,I_ORGAN,    I_BRASS,     I_EPIANO,   {I_DIGIORGAN,I_SYNTHBRASS,I_CLAV}, PF_ONE|PF_TWO|PF_MINOR4,  {100,112,72,64,62,56},135,60,45,0x63},
    {"DUB",         70, 62, 80,20,0,  4,  4,  4,  3,  3,  1,40, 85, 50,K_ROOTS,   I_SUBBASS,  I_MUTEDGTR,I_ORGAN,    I_MELODICA,  I_EPIANO,   {I_DUBCHORD,I_TROMBONE,I_PIANO},   PF_ONE|PF_TWO|PF_DUB,     {96,114,66,60,58,52}, 135,72,60,0x96},
    {"RUB-A-DUB",   84, 76, 96,16,0,  5,  5,  5,  4,  4,  1,60, 40, 20,K_DIGITAL, I_FMBASS,   I_SKANKGTR,I_DIGIORGAN,I_SYNTHBRASS,I_CLAV,     {I_ORGAN,I_BRASS,I_EPIANO},        PF_TWO|PF_MINOR4,         {98,108,70,66,62,62}, 134,40,35,0x42},
    {"DANCEHALL",   96, 88,108, 0,0,  6,  6,  6,  5,  5,  1,80, 25, 40,K_DIGITAL, I_DIGIBASS, I_MUTEDGTR,I_DIGIORGAN,I_LEAD,      I_CLAV,     {I_STEELPAN,I_SYNTHBRASS,I_GLITCH},PF_ONE|PF_TWO,            {104,106,60,62,66,60},134,30,25,0x31},
    {"RAGGA JUNGL",164,155,178, 0,0,  7,  7,  7,  6,  6,  2,95, 50, 60,K_JUNGLE,  I_SUBBASS,  I_SKANKGTR,I_ORGAN,    I_MELODICA,  I_DUBCHORD, {I_DIGIORGAN,I_LEAD,I_EPIANO},     PF_ONE|PF_TWO|PF_DUB,     {100,114,58,56,60,50},134,50,40,0x52},
    {"DUB TECHNO", 122,112,132, 0,0,  8,  8,  8,  7,  7,  1,60, 80, 70,K_TECHNO,  I_SUBBASS,  I_MUTEDGTR,I_DUBCHORD, I_DRONE,     I_EPIANO,   {I_DIGIORGAN,I_GLITCH,I_PIANO},    PF_TECH|PF_ONE,           {98,104,40,84,46,40}, 135,78,70,0xa8},
    {"DEEP DUBSTP",140,130,150, 0,0,  9,  9,  9,  8,  8,  2,70, 70, 80,K_DUBSTEP, I_WOBBLE,   I_MUTEDGTR,I_DUBCHORD, I_MELODICA,  I_EPIANO,   {I_DRONE,I_LEAD,I_PIANO},          PF_ONE|PF_DUB,            {102,116,44,64,56,46}, 136,62,65,0x87},
    {"DUB NOISE",   90, 70,140, 6,0, 10, 10, 10,  9,  9,  1,75, 95,100,K_NOISE,   I_ACID,     I_MUTEDGTR,I_GLITCH,   I_NOISE,     I_DRONE,    {I_DUBCHORD,I_LEAD,I_DIGIORGAN},   PF_ONE|PF_DUB|PF_TECH,    {100,104,56,60,64,56}, 133,88,80,0xbb},
};
