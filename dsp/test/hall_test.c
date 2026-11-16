/* Host test of dsp/hall.h, the hall reverb: decay time, stability at the extremes, HOLD, MIX 0, memory.
 *   (test.sh builds it) */
#include <stdio.h>
#include <stdint.h>
#include "../dream.h"
#include "../hall.h"

static hl_t h;
static hl_large_t big;
static int fails;
static void check(int ok, const char *what) { if (!ok) { fails++; printf("FAIL: %s\n", what); } }
static uint32_t seed = 7;
static float white(void) { seed = seed * 1664525u + 1013904223u; return (float)(int32_t)seed * (1.0f / 2147483648.0f); }
static int mids;                                /* 1: noise low-passed to ~500 Hz, for decay times as RT60 means them */
static float noise(void) { static float lp; float w = white(); if (!mids) return w; lp += 0.07f * (w - lp); return 4.0f * lp; }

/* n samples of input x (0: silence); returns the peak of the reverb's output, energy in *e */
static float run(uint32_t n, float x, double *e)
{
    float wl[64], wr[64], pk = 0.0f;
    double en = 0;
    for (uint32_t k = 0; k < n; k += 64) {
        for (uint32_t i = 0; i < 64; i++) { wl[i] = wr[i] = x != 0.0f ? x * noise() : 0.0f; }
        hl_block(&h, wl, wr, 64);
        for (uint32_t i = 0; i < 64; i++) {
            float a = wl[i] < 0 ? -wl[i] : wl[i], b = wr[i] < 0 ? -wr[i] : wr[i];
            if (!(a == a) || !(b == b)) return 1e30f;
            if (a > pk) pk = a;
            if (b > pk) pk = b;
            en += (double)wl[i] * wl[i] + (double)wr[i] * wr[i];
        }
    }
    if (e) *e = en;
    return pk;
}

static void settings(uint32_t decay, uint32_t mix, uint32_t size, uint32_t pd, uint32_t mod, uint32_t shim)
{
    hl_set(&h, 0, decay); hl_set(&h, 1, mix); hl_set(&h, 2, size); hl_set(&h, 3, pd); hl_set(&h, 4, mod); hl_set(&h, 5, shim);
}

/* RT60 from the energy decay: the time from -10 dB to -30 dB, times 3 (after the highs' quicker fall) */
static float rt60(uint32_t decay)
{
    double e0, e;
    hl_init(&h, &big); settings(decay, 255, 200, 0, 0, 0); mids = 1;
    run(44100, 0.5f, 0);                                   /* a second of noise: the tail is built up */
    run(64, 0.0f, &e0);
    float t5 = -1, t25 = -1;
    for (uint32_t k = 1; k < 44100 * 40 / 64; k++) {
        run(64, 0.0f, &e);
        float db = 10.0f * log10f((float)(e / e0) + 1e-30f);
        if (t5 < 0 && db < -10.0f) t5 = k * 64 / 44100.0f;
        if (t25 < 0 && db < -30.0f) { t25 = k * 64 / 44100.0f; break; }
    }
    mids = 0;
    return (t25 - t5) * 3.0f;
}

int main(void)
{
    printf("hall memory: %u bytes (DSP large block: 124928, link 2304)\n", (unsigned)sizeof(hl_large_t));
    check(sizeof(hl_large_t) <= 124928u - 2304u - 512u, "fits the DSP's large memory");

    for (uint32_t d = 60; d <= 240; d += 60) {
        float want = 0.3f * exp2f((float)d * 6.5f / 249.0f), got = rt60(d);
        printf("DECAY %3u: RT60 wanted %5.2f s, measured %5.2f s\n", d, want, got);
        check(got > 0.6f * want && got < 1.5f * want, "decay time follows DECAY");
    }

    /* the extremes: longest decay, full shimmer, full mod, biggest size, bursts for a minute: bounded */
    hl_init(&h, &big); settings(249, 255, 255, 255, 255, 255);
    float pk = 0;
    for (int k = 0; k < 30; k++) { float p = run(44100, k % 3 == 0 ? 0.9f : 0.0f, 0); if (p > pk) pk = p; }
    printf("extremes, 30 s: peak %.2f\n", pk);
    check(pk < 8.0f, "full shimmer + longest decay stays bounded");

    /* HOLD: the tail sustains after the input stops */
    hl_init(&h, &big); settings(255, 255, 220, 0, 0, 0); mids = 1;
    double e1, e2;
    run(44100, 0.5f, 0);
    run(4410, 0.0f, &e1);
    run(44100 * 20, 0.5f, 0);                               /* playing on: HOLD lets nothing new in */
    run(4410, 0.0f, &e2);
    printf("HOLD: energy after 20 s %.0f %% of the start\n", 100.0 * e2 / e1);
    check(e2 > 0.7 * e1 && e2 < 1.5 * e1, "HOLD sustains, and doesn't take new input");

    /* MIX 0: the effect's share passes untouched */
    hl_init(&h, &big); settings(200, 0, 200, 50, 100, 100);
    {
        float wl[64], wr[64], al[64], ar[64]; int same = 1;
        for (int k = 0; k < 500; k++) {
            for (int i = 0; i < 64; i++) { al[i] = wl[i] = noise(); ar[i] = wr[i] = noise(); }
            hl_block(&h, wl, wr, 64);
            for (int i = 0; i < 64; i++) same &= wl[i] == al[i] && wr[i] == ar[i];
        }
        check(same, "MIX 0 leaves the effect untouched");
    }
    /* MIX full: the effect is gone, only the reverb */
    hl_init(&h, &big); settings(10, 255, 30, 0, 0, 0);
    { float wl[64] = { 1.0f }, wr[64] = { 0 }; hl_block(&h, wl, wr, 64); check(wl[0] == 0.0f, "MIX full: the reverb alone (no direct sound)"); }

    printf(fails ? "hall: %d FAILED\n" : "hall: ALL OK\n", fails);
    return fails != 0;
}
