/* Dreamcatcher on two processors: the words of the audio link (OpenGP5 gp5_dsp.h / gp5_firmware.h,
 * GP5_AUDIO_LINK), shared by both halves. One exchange each way per block of 64 samples.
 *   DSP -> control processor:  [LW_MAGIC] LW_MAGIC_D, [LW_IN + i] the guitar, sample i (Q31, as the DSP has it)
 *   control processor -> DSP:  [LW_MAGIC] LW_MAGIC_N, [LW_WL + i] / [LW_WR + i] the effect's share of the left /
 *                              right output for sample i (Q30: +-2.0 full scale), which the DSP adds to the dry
 *                              signal with dc_mix() (dsp/dream.h). It arrives three blocks after the guitar it
 *                              was made from (about 4.4 ms): the dry signal stays on the DSP, without that delay. */
#ifndef DC_LINK_WORDS_H
#define DC_LINK_WORDS_H
#define LW_MAGIC    0u
#define LW_IN       1u                    /* 64 words */
#define LW_WL       1u                    /* 64 words */
#define LW_WR       65u                   /* 64 words */
#define LW_USED     129u
#define LW_MAGIC_D  0x44434D44            /* "DMCD" */
#define LW_MAGIC_N  0x4E434D44            /* "DMCN" */
#define LW_Q30      1073741824.0f
#endif
