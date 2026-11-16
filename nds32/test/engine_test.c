/* Host test of engine.c, the sound on the control processor: the link's words in and out, the settings queue.
 *   (test.sh builds it) */
#include <stdio.h>
#include <stdint.h>
#include "gp5_firmware.h"                     /* (an older platform's copy fixes 128: the build uses 136) */
#undef GP5_LINK_WORDS
#define GP5_LINK_WORDS 136u
static void (*cb)(const int32_t *in, int32_t *out);
void gp5_link_start(void (*f)(const int32_t *in, int32_t *out)) { cb = f; }
#include "../engine.c"

static int fails;
static void check(int ok, const char *what) { if (!ok) { fails++; printf("FAIL: %s\n", what); } }

int main(void)
{
    static dc_t ref; static dc_large_t ref_large;
    int32_t in[GP5_LINK_WORDS], out[GP5_LINK_WORDS], g[64];
    float wl[64], wr[64];
    uint32_t k, i, same = 1, seed = 1;
    dc_engine_start();
    check(cb != 0, "the link is started");
    dc_init(&ref, &ref_large);
    for (k = 0; k < 6000; k++) {
        if (k % 17 == 0) {                                     /* settings from the main loop, a few per block */
            seed = seed * 1664525u + 1013904223u;
            uint32_t id = (seed >> 8) % DC_NPARAM, v = (seed >> 16) % 300u;
            dc_engine_set(id, v); dc_set(&ref, id, v);
        }
        for (i = 0; i < 64; i++) {
            uint32_t t = k * 64u + i;
            g[i] = (int32_t)(0.5f * sinf(2.0f * GP5_PI * 196.0f * (float)t / 44100.0f) * 2147483648.0f * ((t / 22050u) % 2u));
            in[LW_IN + i] = g[i];
        }
        in[LW_MAGIC] = (int32_t)LW_MAGIC_D;
        cb(in, out);
        dc_block_wet(&ref, g, wl, wr, 64);
        same &= out[LW_MAGIC] == (int32_t)LW_MAGIC_N;
        for (i = 0; i < 64; i++) same &= out[LW_WL + i] == q30(wl[i]) && out[LW_WR + i] == q30(wr[i]);
    }
    check(same, "the words back are the engine's effect share, settings applied before each block");
    in[LW_MAGIC] = 0; cb(in, out);
    check(dc_engine_missed == 1, "a block without the DSP's marker is counted (and heard as silence in)");
    for (k = 0; k < QN + 5; k++) dc_engine_set(DC_MIX, 1);
    check(dc_engine_dropped == 5, "a full queue drops instead of blocking");
    cb(in, out);
    check(q_in == q_out, "the queue empties at the next block");
    if (fails) printf("engine: %d FAILED\n", fails); else printf("engine: ALL OK (%u blocks)\n", (unsigned)dc_engine_blocks);
    return fails != 0;
}
