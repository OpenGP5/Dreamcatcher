/* Dreamcatcher: the settings both halves share. Sent as link command 0x7F10 [id u32][value u32].
 * Values are small integers; the DSP clamps anything out of range. */
#ifndef DREAMCATCHER_PARAMS_H
#define DREAMCATCHER_PARAMS_H

enum {
    DC_ON,        /* 0 bypass (exact dry), 1 on                                                   */
    DC_MIX,       /* 0..255: dry only .. both full (128) .. wet only                              */
    DC_CLOCK,     /* 0..10: engine sample rate, 1.4 kHz .. 44.1 kHz (10). Loop length + delay time */
    DC_CATCH,     /* 0 listening (memory records), 1 the micro-loop plays                         */
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
    DC_NPARAM
};

#define DC_SHAPES 7
#define DC_CLOCKS 11

/* defaults: a gentle ambient patch, effect on */
#define DC_DEFAULTS { 1, 128, 10, 0, 0, 255, 160, 200, 1, 150, 150, 128, 70, 60, 0, 0, 128, 85, 110 }
#endif
