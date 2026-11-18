/* Dreamcatcher: the settings both halves share. Sent as link command 0x7F10 [id u32][value u32].
 * Values are small integers; the DSP clamps anything out of range. New settings go at the end (preset layout). */
#ifndef DREAMCATCHER_PARAMS_H
#define DREAMCATCHER_PARAMS_H

enum {
    DC_ON,        /* 0 bypass (exact dry), 1 on                                                   */
    DC_MIX,       /* 0..255: dry only .. both full (128) .. wet only                              */
    DC_CLOCK,     /* 0..10: engine sample rate, 1.4 kHz .. 44.1 kHz (10). Loop length + delay time */
    DC_CATCH,     /* 0 listening (memory records), 1 the micro-loop plays,
                     2 caught but silent (the foot is down: plays on release, in time, or turns into freeze) */
    DC_LMODE,     /* 0 TAPE, 1 STRETCH                                                            */
    DC_LENGTH,    /* 0..255: TAPE loop length 1/16 .. all; STRETCH slice size                     */
    DC_MOTION,    /* 0..255: TAPE speed REV 4X .. 4X (8 steps); STRETCH: rev .. frozen (128) .. fwd */
    DC_LOOPLV,    /* 0..255 loop level (direct)                                                   */
    DC_ROUTE,     /* 0 IN, 1 IN+LOOP, 2 LOOP: what the wet channel processes                      */
    DC_TIME,      /* 0..255 delay time (engine steps, exponential)                                */
    DC_SMEAR,     /* 0..255 diffusion + feedback: echo .. reverb                                  */
    DC_CHANCE,    /* 0..255 below noon: chance input/feedback enters; above: input always, feedback chance */
    DC_RATE,      /* 0..255 LFO rate 0.05 .. 12 Hz (also the chance clock)                        */
    DC_DEPTH,     /* 0..255 modulation depth                                                      */
    DC_SHAPE,     /* 0..6 SINE SQUARE RAMP SAW RND SINE RND SQUARE ENVELOPE                       */
    DC_MODTYPE,   /* 0 tremolo, 1 vibrato                                                         */
    DC_DISSOLVE,  /* 0..255 below noon: sample-rate crush; noon clean; above: reverse, slower in halves */
    DC_GLITCH,    /* 0..255 below noon: half / normal / double clock; above: random clock jumps   */
    DC_FILTER,    /* 0..255 below noon low-pass, noon open, above high-pass                       */
    /* 1.2 */
    DC_GRAIN,     /* 0..255 grain size of the delay's read heads (engine steps, exponential)      */
    DC_PITCH,     /* 0..255 chance that a grain is pitched to an interval of SCALE                */
    DC_SCALE,     /* 0..4 OCT UP, OCT DN, OCTAVES, 5TH+OCT, HARMONY                               */
    DC_REVERSE,   /* 0..255 chance that a grain plays backwards                                   */
    DC_STUTTER,   /* 0..255 chance that a grain repeats the one before                            */
    DC_TONE,      /* 0..255 low-pass in the feedback: dark .. open (255)                          */
    DC_FADE,      /* 0..255 overdub: how much of the loop stays each pass (255 all, 0 replaced)   */
    DC_FREEZE,    /* 0/1 the wet channel repeats what it holds, nothing new enters (foot held)    */
    DC_OVERDUB,   /* 0/1 the playing goes into the loop (foot held while a loop plays)            */
    /* 1.2: the hall reverb, on the DSP (dsp/hall.h), on the effect's share of the output           */
    DC_RDECAY,    /* 0..255 RT60 0.3 s .. 27 s; 250..255 HOLD                                      */
    DC_RMIX,      /* 0..255 none .. reverb on top of the effect (128) .. reverb alone             */
    DC_RSIZE,     /* 0..255 room .. big hall                                                      */
    DC_RPREDLY,   /* 0..255 pre-delay 0 .. 200 ms                                                 */
    DC_RMOD,      /* 0..255 drift in the tail                                                     */
    DC_RSHIMMER,  /* 0..255 octave-up feedback into the reverb                                    */
    /* 1.4: sync (dsp/sync.h): TIME, RATE, LENGTH (TAPE) and GRAIN as note values of BPM               */
    DC_SYNC,      /* 0 free, 1 synced to BPM                                                       */
    DC_BPM,       /* 40..240 beats a minute                                                        */
    DC_NPARAM
};

/* OpenGP5 1.0 parameters (opengp5/platform/dsp/gp5_params.h): the same settings, in the same order (so the ids
 * are DC_*), described for the computer and delivered to the DSP by the platform. Dreamcatcher keeps its own presets
 * (nds32/controls.c, store record "DRMC"), so none of them is GP5_P_PRESET / GP5_P_SAVE. The DSP uses ON, MIX and
 * the reverb; the rest is the engine's, on the control processor. 0..255 settings show as numbers on a computer
 * (their texts depend on other settings, e.g. TIME on CLOCK: Launchpad's live editor has its own). */
#define GP5_PARAMS(P, C)                                                                                         \
    C(ON,       "Effect",   "Main",   2, "Off|On", 1,                     GP5_P_DSP)                              \
    P(MIX,      "Mix",      "Main",   0, 255, 128, GP5_U_NONE,            GP5_P_DSP)                              \
    P(CLOCK,    "Clock",    "Main",   0, 10, 10, GP5_U_NONE,              0)                                      \
    C(CATCH,    "Catch",    "Loop",   3, "Listening|Looping|Caught", 0,   GP5_P_READONLY)                         \
    C(LMODE,    "Mode",     "Loop",   2, "Tape|Stretch", 0,               0)                                      \
    P(LENGTH,   "Length",   "Loop",   0, 255, 255, GP5_U_NONE,            0)                                      \
    P(MOTION,   "Speed",    "Loop",   0, 255, 160, GP5_U_NONE,            0)                                      \
    P(LOOPLV,   "Loop level", "Loop", 0, 255, 200, GP5_U_NONE,            0)                                      \
    C(ROUTE,    "Route",    "Loop",   3, "In|In+Loop|Loop", 1,            0)                                      \
    P(TIME,     "Time",     "Wet",    0, 255, 150, GP5_U_NONE,            0)                                      \
    P(SMEAR,    "Smear",    "Wet",    0, 255, 150, GP5_U_NONE,            0)                                      \
    P(CHANCE,   "Chance",   "Wet",    0, 255, 128, GP5_U_NONE,            0)                                      \
    P(RATE,     "Rate",     "Mod",    0, 255, 70, GP5_U_NONE,             0)                                      \
    P(DEPTH,    "Depth",    "Mod",    0, 255, 60, GP5_U_NONE,             0)                                      \
    C(SHAPE,    "Shape",    "Mod",    7, "Sine|Square|Ramp|Saw|Rnd sin|Rnd sqr|Env", 0, 0)              \
    C(MODTYPE,  "Mod type", "Mod",    2, "Tremolo|Vibrato", 0,            0)                                      \
    P(DISSOLVE, "Dissolve", "Color",  0, 255, 128, GP5_U_NONE,            0)                                      \
    P(GLITCH,   "Glitch",   "Wet",    0, 255, 85, GP5_U_NONE,             0)                                      \
    P(FILTER,   "Filter",   "Color",  0, 255, 110, GP5_U_NONE,            0)                                      \
    P(GRAIN,    "Grain",    "Grain",  0, 255, 170, GP5_U_NONE,            0)                                      \
    P(PITCH,    "Pitch",    "Grain",  0, 255, 0, GP5_U_NONE,              0)                                      \
    C(SCALE,    "Scale",    "Grain",  5, "Oct up|Oct down|Octaves|5th+oct|Harmony", 3, 0)                         \
    P(REVERSE,  "Reverse",  "Grain",  0, 255, 0, GP5_U_NONE,              0)                                      \
    P(STUTTER,  "Stutter",  "Grain",  0, 255, 0, GP5_U_NONE,              0)                                      \
    P(TONE,     "Tone",     "Wet",    0, 255, 255, GP5_U_NONE,            0)                                      \
    P(FADE,     "Fade",     "Loop",   0, 255, 230, GP5_U_NONE,            0)                                      \
    C(FREEZE,   "Freeze",   "Loop",   2, "Off|On", 0,                     GP5_P_READONLY)                         \
    C(OVERDUB,  "Overdub",  "Loop",   2, "Off|On", 0,                     GP5_P_READONLY)                         \
    P(RDECAY,   "Decay",    "Reverb", 0, 255, 150, GP5_U_NONE,            GP5_P_DSP)                              \
    P(RMIX,     "Reverb mix", "Reverb", 0, 255, 70, GP5_U_NONE,           GP5_P_DSP)                              \
    P(RSIZE,    "Size",     "Reverb", 0, 255, 180, GP5_U_NONE,            GP5_P_DSP)                              \
    P(RPREDLY,  "Pre-delay", "Reverb", 0, 255, 20, GP5_U_NONE,            GP5_P_DSP)                              \
    P(RMOD,     "Reverb mod", "Reverb", 0, 255, 80, GP5_U_NONE,           GP5_P_DSP)                              \
    P(RSHIMMER, "Shimmer",  "Reverb", 0, 255, 0, GP5_U_NONE,              GP5_P_DSP)                              \
    C(SYNC,     "Sync",     "Main",   2, "Free|Sync", 0,                  0)                                      \
    P(BPM,      "BPM",      "Main",   40, 240, 120, GP5_U_NONE,           0)

/* Live access from the computer (OpenGP5 0.5.4: USB 7E 10 set / 7E 11 read; nds32/controls.c dc_ui_param_*):
 * ids 0 .. DC_NPARAM - 1 are the settings above (CATCH, FREEZE and OVERDUB are the foot's: read only); and */
#define DC_P_VOLUME  0x100u               /* output level 0..255 (VOLUME on the pedal)                          */
#define DC_P_PRESET  0x101u               /* the active preset 0..7; set: load that slot (if it holds a sound)    */
#define DC_P_USED    0x102u               /* read: which slots hold a sound (bit per slot)                         */
#define DC_P_CHANGED 0x103u               /* read: 1 if the sound differs from the active preset                   */
#define DC_P_SAVE    0x104u               /* set: save the sound into slot 0..7 (as a PARA click does); read: 0     */
#define DC_P_FORMAT  0x106u               /* read: the preset record's version (4: with names)                      */
#define DC_NAME_LEN  12u                  /* a preset's name: up to 12 printable ASCII characters                   */

#define DC_SHAPES 7
#define DC_CLOCKS 11
#define DC_SCALES 5

/* largest value of each setting */
#define DC_MAXES { 1, 255, DC_CLOCKS - 1, 2, 1, 255, 255, 255, 2, 255, 255, 255, 255, 255, DC_SHAPES - 1, 1, 255, 255, \
                   255, 255, 255, DC_SCALES - 1, 255, 255, 255, 255, 1, 1, 255, 255, 255, 255, 255, 255, 1, 240 }

/* defaults: a gentle ambient patch, effect on */
#define DC_DEFAULTS { 1, 128, 10, 0, 0, 255, 160, 200, 1, 150, 150, 128, 70, 60, 0, 0, 128, 85, 110, \
                      170, 0, 3, 0, 0, 255, 230, 0, 0, 150, 70, 180, 20, 80, 0, 0, 120 }
#endif
