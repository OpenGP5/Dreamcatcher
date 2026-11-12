/* Host test of dream.h: every setting at its extremes and at random, catching and releasing loops,
 * maximum feedback; the output must stay finite and inside full scale, bypass must be exact.
 * Run by ./test.sh. */
#include <stdio.h>
#include <stdint.h>
#define GP5_BLOCK 64u
#include "../dream.h"

static dc_t s;
static dc_large_t big;

static uint32_t seed = 12345u;
static uint32_t rnd(void) { seed = seed * 1664525u + 1013904223u; return seed >> 8; }

static int fails;
static void check(int ok, const char *what) { if (!ok) { fails++; printf("FAIL: %s\n", what); } }

/* plucked notes with silence between, like playing */
static void input(int32_t *in, uint32_t block)
{
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        uint32_t t = block * GP5_BLOCK + i, ph = t % 66150u;
        float env = ph < 44100u ? expf(-(float)ph / 9000.0f) : 0.0f;
        float f = 110.0f * (1.0f + (float)((t / 66150u) % 4u) * 0.25f);
        in[i] = (int32_t)(0.6f * env * sinf(2.0f * GP5_PI * f * (float)t / 44100.0f) * 2147483648.0f);
    }
}

static int run(uint32_t blocks, int fuzz, const char *name)
{
    int32_t in[GP5_BLOCK], l[GP5_BLOCK], r[GP5_BLOCK];
    double peak = 0, energy = 0;
    static uint32_t b = 0;
    for (uint32_t k = 0; k < blocks; k++, b++) {
        if (fuzz && rnd() % 40 == 0) {
            uint32_t id = rnd() % DC_NPARAM;
            if (id != DC_ON) dc_set(&s, id, rnd() % 300u);
        }
        input(in, b);
        dc_block(&s, in, l, r, GP5_BLOCK);
        for (uint32_t i = 0; i < GP5_BLOCK; i++) {
            double a = (double)l[i] / 2147483648.0, c = (double)r[i] / 2147483648.0;
            if (a < 0) a = -a;
            if (c < 0) c = -c;
            if (a > peak) peak = a;
            if (c > peak) peak = c;
            energy += a * a;
        }
    }
    printf("%-34s peak %.3f  rms %.4f  dcur %.0f  gm %.1f  steps %u\n", name, peak,
           energy > 0 ? __builtin_sqrt(energy / (blocks * GP5_BLOCK)) : 0.0, s.dcur, s.gm, s.steps);
    for (uint32_t i = 0; i < DC_AP_TOTAL; i++) check(s.ap[i] == s.ap[i] && s.ap[i] < 1e6f && s.ap[i] > -1e6f, "allpass state finite");
    check(s.lfo == s.lfo && s.ic1[0] == s.ic1[0] && s.ic2[1] == s.ic2[1], "state finite");
    return peak > 0.0001;
}

int main(void)
{
    dc_init(&s, &big);
    check(run(700, 0, "defaults"), "defaults make sound");
    dc_set(&s, DC_CATCH, 1);
    check(run(700, 0, "caught, tape 1x"), "loop sounds");
    dc_set(&s, DC_LMODE, 1); dc_set(&s, DC_MOTION, 128);
    check(run(700, 0, "stretch frozen"), "stretch sounds");
    dc_set(&s, DC_MOTION, 0); dc_set(&s, DC_LENGTH, 0);
    run(700, 0, "stretch reverse, tiny slices");
    dc_set(&s, DC_LMODE, 0); dc_set(&s, DC_MOTION, 0); dc_set(&s, DC_LENGTH, 0);
    run(700, 0, "tape rev 4x shortest");
    dc_set(&s, DC_CATCH, 0);
    dc_set(&s, DC_SMEAR, 255); dc_set(&s, DC_CHANCE, 255); dc_set(&s, DC_TIME, 255);
    run(3000, 0, "max smear + max chance (sound on sound)");
    dc_set(&s, DC_GLITCH, 255); dc_set(&s, DC_RATE, 255);
    run(1500, 0, "random glitch, fast LFO");
    dc_set(&s, DC_DISSOLVE, 255);
    run(1500, 0, "reverse 1/8");
    dc_set(&s, DC_DISSOLVE, 140);
    run(1500, 0, "reverse 1x");
    dc_set(&s, DC_DISSOLVE, 0); dc_set(&s, DC_FILTER, 255);
    run(1500, 0, "crush max, high-pass max");
    dc_set(&s, DC_FILTER, 0); dc_set(&s, DC_MODTYPE, 1); dc_set(&s, DC_DEPTH, 255);
    run(1500, 0, "low-pass min, vibrato full");
    dc_set(&s, DC_CLOCK, 0);
    run(3000, 0, "clock 1.4 kHz");
    dc_set(&s, DC_SHAPE, 6); dc_set(&s, DC_CHANCE, 200);
    run(1500, 0, "envelope gate");
    run(40000, 1, "random settings, 60 s");

    /* exact bypass */
    dc_set(&s, DC_ON, 0);
    {
        int32_t in[GP5_BLOCK], l[GP5_BLOCK], r[GP5_BLOCK]; int same = 1;
        for (uint32_t k = 0; k < 400; k++) {
            input(in, k); dc_block(&s, in, l, r, GP5_BLOCK);
            if (k > 100) for (uint32_t i = 0; i < GP5_BLOCK; i++) same &= l[i] == in[i] && r[i] == in[i];
        }
        check(same, "bypass is exact");
    }
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
