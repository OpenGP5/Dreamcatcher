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
    DC_NPARAM
};

/* Live access from the computer (OpenGP5 0.5.4: USB 7E 10 set / 7E 11 read; nds32/controls.c dc_ui_param_*):
 * ids 0 .. DC_NPARAM - 1 are the settings above (CATCH, FREEZE and OVERDUB are the foot's: read only); and */
#define DC_P_VOLUME  0x100u               /* output level 0..255 (VOLUME on the pedal)                          */
#define DC_P_PRESET  0x101u               /* the active preset 0..7; set: load that slot (if it holds a sound)    */
#define DC_P_USED    0x102u               /* read: which slots hold a sound (bit per slot)                         */
#define DC_P_CHANGED 0x103u               /* read: 1 if the sound differs from the active preset                   */
#define DC_P_SAVE    0x104u               /* set: save the sound into slot 0..7 (as a PARA click does); read: 0     */

#define DC_SHAPES 7
#define DC_CLOCKS 11
#define DC_SCALES 5

/* largest value of each setting */
#define DC_MAXES { 1, 255, DC_CLOCKS - 1, 2, 1, 255, 255, 255, 2, 255, 255, 255, 255, 255, DC_SHAPES - 1, 1, 255, 255, \
                   255, 255, 255, DC_SCALES - 1, 255, 255, 255, 255, 1, 1, 255, 255, 255, 255, 255, 255 }

/* defaults: a gentle ambient patch, effect on */
#define DC_DEFAULTS { 1, 128, 10, 0, 0, 255, 160, 200, 1, 150, 150, 128, 70, 60, 0, 0, 128, 85, 110, \
                      170, 0, 3, 0, 0, 255, 230, 0, 0, 150, 70, 180, 20, 80, 0 }
#endif
