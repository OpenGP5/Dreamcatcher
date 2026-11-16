/* Dreamcatcher, the sound on the control processor (two-processor build). The DSP sends the guitar over the audio
 * link every block (64 samples, 1.45 ms); in the link's interrupt this runs the sound engine (dsp/dream.h, the
 * same code the DSP ran on its own before) and sends back the effect's share of the output, which the DSP mixes
 * with the dry signal. Measured on the pedal: the whole engine at its heaviest (grains pitched, reversed and
 * stuttered, full smear, GLITCH doubling the clock, overdub) takes 64 % of a block here; it took 106-108 % of
 * the DSP. Settings come from the main loop (controls.c via fw_take) through a small queue and are applied
 * between blocks, so the engine never sees one change halfway through a block. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O2")                        /* the platform builds with -Os; the sound wants speed */
#endif
#include <stdint.h>
#include "gp5_firmware.h"
#include "gp5_math.h"                              /* the platform's maths (both processors) */
#include "../dsp/dream.h"
#include "../dsp/link_words.h"
#include "engine.h"

/* The link must carry the words of link_words.h: dsp/firmware.h asks for GP5_LINK_WORDS 136, which the platform
 * (0.5.3 and later) builds both halves with. An older platform has a fixed 128: refuse to build rather than
 * write past its buffers. */
typedef char dc_link_words_check[GP5_LINK_WORDS >= LW_USED ? 1 : -1];

static dc_t eng;
static dc_large_t eng_large;
static float wet_l[64], wet_r[64];
static int32_t dry[64];

#define QN 64u                                     /* settings in flight; the main loop sends one per 20 ms */
static struct { uint32_t id, v; } q[QN];
static volatile uint32_t q_in, q_out;              /* written by the main loop / by the interrupt only */
volatile uint32_t dc_engine_blocks, dc_engine_missed, dc_engine_dropped;
#ifdef GP5_DEBUG_PEEK
/* Debug builds: how long the engine takes, read over USB (7E 0A). Timer 3 (free on the platform) runs freely with
 * a 60 ms period; its count (+0x14) before and after each block gives the block's time in timer counts (the
 * prescaler at +0x10, 144 MHz system clock: (prescaler + 1) / 144 us each). Which way it counts isn't documented:
 * both differences are kept, the one that's small is the time. */
#include "hal/timer.h"
#define BT_BASE 0x4002C000u
#define BT_REG(o) (*(volatile uint32_t *)(BT_BASE + (o)))
volatile uint32_t dc_bt_period, dc_bt_prescale, dc_bt_sum_a, dc_bt_sum_b, dc_bt_max_a, dc_bt_max_b, dc_bt_blocks;
#endif

void dc_engine_set(uint32_t id, uint32_t v)        /* main loop */
{
    uint32_t n = q_in;
    if (n - q_out >= QN) { dc_engine_dropped++; return; }    /* can't happen at 50 a second; never block */
    q[n % QN].id = id; q[n % QN].v = v;
    __asm__ volatile ("" ::: "memory");
    q_in = n + 1u;
}

static int32_t q30(float x)
{
    x *= LW_Q30;
    if (x >= 2147483520.0f) return 2147483520;
    if (x <= -2147483648.0f) return -2147483647 - 1;
    return (int32_t)x;
}

static void on_block(const int32_t *in, int32_t *out)            /* the link's interrupt, once per block */
{
    uint32_t i, o = q_out;
#ifdef GP5_DEBUG_PEEK
    uint32_t c0 = BT_REG(0x14);
#endif
    while (o != q_in) { dc_set(&eng, q[o % QN].id, q[o % QN].v); o++; }
    q_out = o;
    if (in[LW_MAGIC] == (int32_t)LW_MAGIC_D) for (i = 0; i < 64; i++) dry[i] = in[LW_IN + i];
    else { for (i = 0; i < 64; i++) dry[i] = 0; dc_engine_missed++; }
    dc_block_wet(&eng, dry, wet_l, wet_r, 64);
    for (i = 0; i < 64; i++) { out[LW_WL + i] = q30(wet_l[i]); out[LW_WR + i] = q30(wet_r[i]); }
    out[LW_MAGIC] = (int32_t)LW_MAGIC_N;
#ifdef GP5_DEBUG_PEEK
    {
        uint32_t c1 = BT_REG(0x14), P = dc_bt_period, a = (c1 + P - c0) % P, b = (c0 + P - c1) % P;
        dc_bt_sum_a += a; dc_bt_sum_b += b;
        if (a > dc_bt_max_a) dc_bt_max_a = a;
        if (b > dc_bt_max_b) dc_bt_max_b = b;
        dc_bt_blocks++;
    }
#endif
    dc_engine_blocks++;
}

void dc_engine_start(void)
{
    dc_init(&eng, &eng_large);
#ifdef GP5_DEBUG_PEEK
    gp5_timer_config(3, 60000, 0);
    gp5_timer_start(3);
    dc_bt_period = BT_REG(0x0C) + 1u; dc_bt_prescale = BT_REG(0x10);
#endif
    gp5_link_start(on_block);
}

/* the platform's maths, built here too: the control processor has no C library either */
#include "gp5_math.c"
