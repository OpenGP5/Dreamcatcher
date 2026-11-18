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
    dc_set(&s, DC_SHAPE, 0); dc_set(&s, DC_CHANCE, 128); dc_set(&s, DC_CLOCK, 10); dc_set(&s, DC_DISSOLVE, 128);
    dc_set(&s, DC_FILTER, 128); dc_set(&s, DC_MODTYPE, 0); dc_set(&s, DC_GLITCH, 85); dc_set(&s, DC_TIME, 150);
    dc_set(&s, DC_PITCH, 255); dc_set(&s, DC_SCALE, 3); dc_set(&s, DC_SMEAR, 200);
    check(run(2000, 0, "grains: 5ths + octaves, always"), "pitched grains sound");
    dc_set(&s, DC_SCALE, 4); dc_set(&s, DC_REVERSE, 128); dc_set(&s, DC_GRAIN, 255);
    run(2000, 0, "grains: harmony, half reversed, 186 ms");
    dc_set(&s, DC_PITCH, 0); dc_set(&s, DC_REVERSE, 255); dc_set(&s, DC_GRAIN, 0);
    run(1500, 0, "grains: all reversed, 6 ms");
    dc_set(&s, DC_REVERSE, 0); dc_set(&s, DC_STUTTER, 230); dc_set(&s, DC_GRAIN, 128);
    run(2000, 0, "grains: stutter 90 %");
    dc_set(&s, DC_STUTTER, 0); dc_set(&s, DC_TONE, 0); dc_set(&s, DC_SMEAR, 255); dc_set(&s, DC_CHANCE, 255);
    dc_set(&s, DC_PITCH, 200); dc_set(&s, DC_SCALE, 0);
    run(3000, 0, "shimmer, dark tone, sound on sound");
    dc_set(&s, DC_TONE, 255);
    run(3000, 0, "shimmer, open tone");
    dc_set(&s, DC_PITCH, 0); dc_set(&s, DC_SMEAR, 150); dc_set(&s, DC_CHANCE, 128);

    /* long delay at the lowest clock, then big TIME jumps: no racing read head, the length lands */
    dc_set(&s, DC_CLOCK, 0); dc_set(&s, DC_TIME, 255);
    run(30000, 0, "1.4 kHz, 37 s delay");
    check(s.dcur > (float)DC_DMAX - 100.0f, "long delay reached");
    dc_set(&s, DC_TIME, 60);
    run(3000, 0, "jump down to 60");
    check(s.dcur < 1200.0f && s.duck == 1.0f, "jump landed");
    dc_set(&s, DC_TIME, 250); dc_set(&s, DC_PITCH, 255); dc_set(&s, DC_REVERSE, 128); dc_set(&s, DC_GRAIN, 255);
    run(20000, 0, "1.4 kHz, long, grains pitched + reversed");
    dc_set(&s, DC_PITCH, 0); dc_set(&s, DC_REVERSE, 0); dc_set(&s, DC_CLOCK, 10); dc_set(&s, DC_TIME, 150);

    /* freeze: after the input stops, the line keeps what it held, exactly, for as long as it is held */
    {
        int32_t in[GP5_BLOCK] = { 0 }, l[GP5_BLOCK], r[GP5_BLOCK];
        dc_set(&s, DC_SMEAR, 0);
        run(1000, 0, "before freeze");
        dc_set(&s, DC_FREEZE, 1);
        for (uint32_t k = 0; k < 200; k++) dc_block(&s, in, l, r, GP5_BLOCK);
        double e1 = 0, e2 = 0;
        for (uint32_t k = 0; k < 2000; k++) {                /* silence in: the frozen sound must stay */
            dc_block(&s, in, l, r, GP5_BLOCK);
            for (uint32_t i = 0; i < GP5_BLOCK; i++) { double a = l[i] / 2147483648.0; if (k < 1000) e1 += a * a; else e2 += a * a; }
        }
        printf("freeze: energy first %.3f, later %.3f, length %.0f\n", e1, e2, s.flen);
        check(e1 > 0.01 && e2 > 0.98 * e1 && e2 < 1.02 * e1, "freeze holds the sound unchanged");
        dc_set(&s, DC_FREEZE, 0);
        run(1500, 0, "after freeze");
        dc_set(&s, DC_SMEAR, 150);
    }

    /* overdub: playing goes into the caught loop; FADE 0 replaces it, FADE 255 keeps it */
    dc_set(&s, DC_CATCH, 2); run(20, 0, "caught, silent");
    dc_set(&s, DC_CATCH, 1); dc_set(&s, DC_OVERDUB, 1); dc_set(&s, DC_FADE, 255);
    check(run(3000, 0, "overdub, fade keep"), "overdub sounds");
    dc_set(&s, DC_FADE, 0); run(1000, 0, "overdub, replace");
    dc_set(&s, DC_LMODE, 1); dc_set(&s, DC_FADE, 200); run(1000, 0, "overdub, stretch");
    dc_set(&s, DC_OVERDUB, 0); dc_set(&s, DC_LMODE, 0); dc_set(&s, DC_CATCH, 0);
    run(80000, 1, "random settings, 2 min");

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
    /* the two-chip build: the control processor's dc_block_wet() + the DSP's dc_mix() is the one-processor
     * dc_block() exactly; with the effect's share carried as the link's Q30 words, within rounding */
    {
        static dc_t a, b; static dc_large_t la, lb; static dc_mixer_t m, mq;
        int32_t in[GP5_BLOCK], l[GP5_BLOCK], r[GP5_BLOCK], l2, r2, lq, rq;
        float wl[GP5_BLOCK], wr[GP5_BLOCK];
        uint32_t same = 1, k, i; double worst = 0;
        dc_init(&a, &la); dc_init(&b, &lb);
        m.on = m.mix = mq.on = mq.mix = 0.0f;
        for (k = 0; k < 40000; k++) {
            if (rnd() % 30 == 0) { uint32_t id = rnd() % DC_NPARAM, v = rnd() % 300u; dc_set(&a, id, v); dc_set(&b, id, v); }
            input(in, k);
            dc_block(&a, in, l, r, GP5_BLOCK);
            dc_block_wet(&b, in, wl, wr, GP5_BLOCK);
            for (i = 0; i < GP5_BLOCK; i++) {
                dc_mix(&m, b.p[DC_ON], b.p[DC_MIX], in[i], wl[i], wr[i], &l2, &r2);
                same &= l2 == l[i] && r2 == r[i];
                {   /* through the link: Q30, clamped to +-2 */
                    float x = wl[i] * 1073741824.0f, y = wr[i] * 1073741824.0f;
                    int32_t qx = x >= 2147483520.0f ? 2147483520 : x <= -2147483648.0f ? (-2147483647 - 1) : (int32_t)x;
                    int32_t qy = y >= 2147483520.0f ? 2147483520 : y <= -2147483648.0f ? (-2147483647 - 1) : (int32_t)y;
                    dc_mix(&mq, b.p[DC_ON], b.p[DC_MIX], in[i], (float)qx * (1.0f / 1073741824.0f), (float)qy * (1.0f / 1073741824.0f), &lq, &rq);
                    double d = ((double)lq - l[i]) / 2147483648.0; if (d < 0) d = -d; if (d > worst) worst = d;
                    d = ((double)rq - r[i]) / 2147483648.0; if (d < 0) d = -d; if (d > worst) worst = d;
                }
            }
        }
        printf("two-chip split vs one processor: %s; through the link, worst difference %.2e of full scale\n",
               same ? "identical" : "DIFFERENT", worst);
        check(same, "two-chip split = one processor");
        check(worst < 1e-6, "the link's Q30 words lose nothing audible");
    }
    /* sync (1.4): the delay, the tape loop and the grain land on note values of BPM; the LFO stays in phase with the beat */
    dc_init(&s, &big);                                       /* (a fresh engine: the random run left anything set) */
    dc_set(&s, DC_SYNC, 1); dc_set(&s, DC_BPM, 120); dc_set(&s, DC_CLOCK, 10); dc_set(&s, DC_TIME, 255);
    dc_set(&s, DC_RATE, 140);                                /* 1/4: one cycle a beat */
    dc_set(&s, DC_LMODE, 0); dc_set(&s, DC_LENGTH, 255); dc_set(&s, DC_GRAIN, 255);
    run(3000, 0, "sync: 120 BPM");
    printf("sync: delay %.0f steps, tape %.0f, grain %.1f, LFO %.3f Hz, phase %.4f vs beat %.4f\n",
           s.dcur, s.tape_len, s.grain, s.rate_hz, s.lfo_ph, s.beat - (float)(int)s.beat);
    check(s.dtarget == 44100.0f && fabsf(s.dcur - s.dtarget) < 10.0f, "synced delay = a half note at 120 BPM (1 s, the longest that fits)");
    check(fabsf(s.tape_len - 44100.0f) < 1.0f, "synced tape loop = a half note");
    check(fabsf(s.grain - 5512.5f) < 1.0f, "synced grain = a 1/16 (125 ms)");
    check(fabsf(s.rate_hz - 2.0f) < 1e-4f, "synced LFO: a quarter note at 120 BPM = 2 Hz");
    check(fabsf(s.lfo_ph - (s.beat - (float)(int)s.beat)) < 1e-3f, "synced LFO in phase with the beat");
    dc_set(&s, DC_SYNC, 0);

    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
