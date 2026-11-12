/* Dreamcatcher, the sound. Two halves that feed each other:
 *
 *   MICRO-LOOPER  an always-listening memory (the guitar is recorded all the time at the CLOCK rate);
 *                 CATCH plays the last moments back as a loop: TAPE (length, speed / direction in
 *                 octaves) or STRETCH (unity-pitch slices whose start creeps, freezes or runs backwards).
 *   WET CHANNEL   a glitch delay that smears into a reverb. Once per LFO cycle CHANCE decides whether new
 *                 input and / or the old output may enter it; GLITCH jumps its clock between half, normal
 *                 and double speed (octave jumps); DISSOLVE crushes the sample rate (below noon) or plays
 *                 the delay backwards, slower in halves (above noon); the LFO also drives a tremolo or
 *                 vibrato; a state-variable filter ends the chain.
 *
 * CLOCK sets the engine rate of both halves (MOOD's idea): lower = longer loops, longer delays, grittier.
 * ROUTE sends the input, the loop, or both through the wet channel.
 *
 * Plain C, no allocation, no C library: the same file runs on the pedal and on the computer
 * (opengp5/tools/run_effect.py, dsp/test/dream_host.c; ./test.sh). Big buffers are the caller's (dc_large_t). */
#ifndef DREAM_H
#define DREAM_H
#include <stdint.h>
#include "gp5_math.h"
#include "../params.h"

/* The pedal's DSP runs code straight from SPI flash; this loop is too big for its fetch cache and measured
 * ~5 cycles per instruction there (110 % load). Everything that runs per sample is therefore placed in RAM:
 * section .data_hot, which the linker script copies to RAM with .data at start-up; calls to and from flash
 * go through linker veneers. The linker's "RWX segment" warning is expected. */
#if defined(__arm__) && !defined(GP5_HOST)
#define DC_HOT __attribute__((section(".data_hot"), noinline))
#define DC_HOT_INLINE __attribute__((section(".data_hot"), always_inline)) inline
#else
#define DC_HOT
#define DC_HOT_INLINE inline
#endif

#define DC_FS     44100.0f
#define DC_MEM    32768u                    /* loop memory, engine samples (0.74 s at 44.1 kHz, 24 s at 1.4 kHz) */
#define DC_XF     512u                      /* loop seam crossfade */
#define DC_LFULL  (DC_MEM - DC_XF - 64u)     /* longest loop: leaves the crossfade's samples before its start */
#define DC_DLY    24576u                    /* delay line, engine steps */
#define DC_DMAX   (DC_DLY - 64u)             /* longest delay (0.55 s at 44.1 kHz) */
#define DC_RMAX   ((DC_DLY - 64u) / 2u)      /* longest reverse slice: a reverse read reaches back twice its length */
#define DC_VIB    512u                      /* vibrato line, audio samples */

typedef struct {
    int16_t mem[DC_MEM];
    int16_t dly[DC_DLY];
    float vib[2][DC_VIB];
} dc_large_t;                               /* 118784 bytes: fits the 124928-byte large block */

#define DC_AP_IN  4
static const uint16_t dc_ap_len[DC_AP_IN + 2] = { 67, 109, 163, 241, 157, 211 };   /* 4 in the loop, L, R out */
#define DC_AP_TOTAL (67 + 109 + 163 + 241 + 157 + 211)

typedef struct { float age; float anchor; } dc_rev_t;

typedef struct {
    dc_large_t *L;
    uint32_t p[DC_NPARAM];
    uint32_t rnd;

    /* memory / loop */
    uint32_t mw, catch_w;                   /* memory write index; write index when the loop was caught */
    float mph, macc; uint32_t mcnt;         /* memory clock phase, input average */
    float lpos, lgain;                      /* tape position (in the loop), loop fade */
    float shead, sage[2], spos[2];          /* stretch: slice start, two overlapping slices */
    uint32_t snext; float ssince;

    /* delay / reverb */
    uint32_t dw;
    float dph, dacc; uint32_t dcnt; float dx;
    float dcur;                             /* delay length now (glides) */
    float ap[DC_AP_TOTAL]; uint16_t api[DC_AP_IN + 2];
    float oprev[2], ocur[2];                /* step-rate outputs, interpolated to the audio rate */
    dc_rev_t rev[2]; float rsince, rmix;

    /* chance, glitch, LFO */
    float lfo_ph, lfo, rprev, rnext, env;
    float tin, tfb, gin, gfb;               /* gate targets and their ramps */
    float gm, crm;                          /* glitch: engine clock multiplier, crush multiplier */

    /* dissolve, modulation, filter, output */
    float cph, hold[2];
    uint32_t vw;
    float ic1[2], ic2[2], fc;
    float on, mix;

    /* per-block targets */
    float r, dtarget, smear, rate_hz, depth, crush, qlev, rspeed, fc_t; int fmode, rev_on;
    float slice, slice_inv2, tape_len;

    uint32_t steps;                         /* engine steps run (statistics) */
} dc_t;

static const float dc_clock_ratio[DC_CLOCKS] = {
    1.0f / 32, 3.0f / 64, 1.0f / 16, 3.0f / 32, 1.0f / 8, 3.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 2, 3.0f / 4, 1.0f
};
static const float dc_tape_speed[8] = { -4.0f, -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f, 4.0f };

static DC_HOT_INLINE uint32_t dc_rand(dc_t *s) { uint32_t x = s->rnd; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return s->rnd = x; }
static DC_HOT_INLINE float dc_frand(dc_t *s) { return (float)(dc_rand(s) >> 8) * (1.0f / 16777216.0f); }

static void dc_set(dc_t *s, uint32_t id, uint32_t v)
{
    static const uint8_t max[DC_NPARAM] = { 1, 255, DC_CLOCKS - 1, 1, 1, 255, 255, 255, 2, 255, 255, 255, 255, 255,
                                            DC_SHAPES - 1, 1, 255, 255, 255 };
    if (id >= DC_NPARAM) return;
    if (v > max[id]) v = max[id];
    if (id == DC_CATCH && v && !s->p[DC_CATCH]) {           /* catch: freeze the memory, start the loop */
        s->catch_w = s->mw;
        s->lpos = 0.0f;
        s->shead = 0.0f; s->ssince = 1e9f;
    }
    s->p[id] = v;
}

static void dc_init(dc_t *s, dc_large_t *L)
{
    static const uint32_t def[DC_NPARAM] = DC_DEFAULTS;
    uint32_t i, k;
    uint8_t *z = (uint8_t *)s;
    for (i = 0; i < sizeof *s; i++) z[i] = 0;              /* no memset: the DSP has no C library */
    s->L = L;
    for (i = 0; i < DC_MEM; i++) L->mem[i] = 0;
    for (i = 0; i < DC_DLY; i++) L->dly[i] = 0;
    for (k = 0; k < 2; k++) for (i = 0; i < DC_VIB; i++) L->vib[k][i] = 0.0f;
    for (i = 0; i < DC_NPARAM; i++) s->p[i] = def[i];
    s->rnd = 0x2545F491u;
    s->sage[0] = s->sage[1] = 1e9f;
    s->dcur = 4410.0f; s->gm = 1.0f; s->crm = 1.0f;
    s->tin = s->gin = 1.0f;
    s->fc = 19000.0f;
}

/* ---------------------------------------------------------------- memory and loop */

static DC_HOT_INLINE int32_t dc_floor(float x) { int32_t i = (int32_t)x; return (float)i > x ? i - 1 : i; }

static DC_HOT_INLINE float dc_mem(const dc_t *s, float idx)               /* idx: absolute, may be outside 0..DC_MEM */
{
    int32_t i = dc_floor(idx);
    float f = idx - (float)i;
    i &= (int32_t)(DC_MEM - 1u);
    float a = (float)s->L->mem[i], b = (float)s->L->mem[(i + 1) & (int32_t)(DC_MEM - 1u)];
    return (a + f * (b - a)) * (1.0f / 32768.0f);
}

/* A loop of length len ending where the memory stopped. Position 0..len; continuous across the wrap:
 * the last DC_XF samples fade into the samples just before the loop's start. */
static DC_HOT_INLINE float dc_loop_read(const dc_t *s, float pos, float len)
{
    float base = (float)s->catch_w - len;
    float v = dc_mem(s, base + pos);
    if (pos > len - (float)DC_XF) {
        float t = (pos - (len - (float)DC_XF)) * (1.0f / (float)DC_XF);
        v += t * (dc_mem(s, base + pos - len) - v);
    }
    return v;
}

static DC_HOT_INLINE float dc_wrapf(float x, float len)                   /* x is at most a few lengths outside */
{
    while (x >= len) x -= len;
    while (x < 0.0f) x += len;
    return x;
}

static DC_HOT_INLINE float dc_loop(dc_t *s)
{
    float r = s->r, m = (float)s->p[DC_MOTION], len;
    if (s->p[DC_LMODE] == 0) {                               /* TAPE */
        len = s->tape_len;
        float v = dc_loop_read(s, dc_wrapf(s->lpos, len), len);
        s->lpos = dc_wrapf(s->lpos + dc_tape_speed[s->p[DC_MOTION] >> 5] * r, len);
        return v;
    }
    /* STRETCH: unity-pitch slices, two overlapping; the slice start moves at "motion" speed */
    len = (float)DC_LFULL;
    float slice = s->slice;                                  /* 256 .. 8192 samples (per block) */
    float d = (m - 128.0f) * (1.0f / 127.0f), v = 0.0f;
    if (d > 1.0f) d = 1.0f;
    float speed = d * fabsf(d);                               /* noon frozen, the ends unstretched */
    if (s->ssince >= 0.5f * slice) {
        uint32_t k = s->snext;
        s->spos[k] = s->shead; s->sage[k] = 0.0f;
        s->snext ^= 1u; s->ssince = 0.0f;
    }
    for (uint32_t k = 0; k < 2; k++) {
        if (s->sage[k] >= slice) continue;
        float t = s->sage[k] * s->slice_inv2;                /* 0..2: triangle, smoothed */
        if (t > 1.0f) t = 2.0f - t;
        v += dc_loop_read(s, s->spos[k], len) * t * t * (3.0f - 2.0f * t);
        s->spos[k] = dc_wrapf(s->spos[k] + r, len);
        s->sage[k] += r;
    }
    s->ssince += r;
    s->shead = dc_wrapf(s->shead + speed * r, len);
    return v;
}

/* ---------------------------------------------------------------- delay / reverb (one engine step) */

static DC_HOT_INLINE float dc_ap(dc_t *s, uint32_t stage, float x, float g)
{
    static const uint16_t off[DC_AP_IN + 2] = { 0, 67, 176, 339, 580, 737 };
    float *b = s->ap + off[stage];
    uint32_t i = s->api[stage];
    float z = b[i], y = z - g * x;
    b[i] = x + g * y;
    s->api[stage] = (uint16_t)(i + 1u == dc_ap_len[stage] ? 0u : i + 1u);
    return y;
}

static DC_HOT_INLINE float dc_dly(const dc_t *s, float back)              /* back steps behind the write index */
{
    float idx = (float)s->dw - back;
    int32_t i = dc_floor(idx);
    float f = idx - (float)i;
    if (i < 0) i += (int32_t)DC_DLY;                        /* back is 1 .. DC_DLY - 1 */
    int32_t j = i + 1 == (int32_t)DC_DLY ? 0 : i + 1;
    float a = (float)s->L->dly[i], b = (float)s->L->dly[j];
    return (a + f * (b - a)) * (1.0f / 32768.0f);
}

static DC_HOT_INLINE float dc_soft(float x)                               /* soft limit, smooth, |result| < 1 */
{
    if (x > 1.5f) x = 1.5f; else if (x < -1.5f) x = -1.5f;
    return x - x * x * x * (4.0f / 27.0f);
}

static DC_HOT_INLINE float dc_knee(float x)                               /* unity below 0.7, then bends smoothly towards 1 */
{
    float a = fabsf(x);
    if (a <= 0.7f) return x;
    float y = 0.7f + 0.3f * tanhf((a - 0.7f) * (1.0f / 0.3f));
    return x < 0.0f ? -y : y;
}

static DC_HOT_INLINE void dc_step(dc_t *s)
{
    float sm = s->smear;
    s->dcur += (s->dtarget - s->dcur) * 0.0006f;            /* time changes glide like tape */
    float y = dc_dly(s, s->dcur);
    float g = 0.72f * sm, yd = y;
    for (uint32_t k = 0; k < DC_AP_IN; k++) yd = dc_ap(s, k, yd, g);
    float base = 0.88f * sm, fb = base + s->gfb * (0.985f - base);
    float w = dc_soft(s->gin * s->dx + fb * yd);
    s->L->dly[s->dw] = (int16_t)(w * 32767.0f);
    if (++s->dw == DC_DLY) s->dw = 0;

    /* reverse: two half-overlapping heads read the delay line backwards at rspeed */
    float rv = 0.0f;
    float glen = s->dcur < 1024.0f ? 1024.0f : s->dcur > (float)DC_RMAX ? (float)DC_RMAX : s->dcur;
    if (!s->rev_on && s->rmix == 0.0f) {                    /* idle: keep the heads half a slice apart */
        s->rev[0].age = glen;
        s->rev[1].age = 0.5f * glen; s->rev[1].anchor = (float)s->dw - 2.0f - 0.5f * glen;
    } else {
        float ginv2 = 2.0f / glen;
        for (uint32_t k = 0; k < 2; k++) {
            dc_rev_t *h = &s->rev[k];
            if (h->age >= glen) { h->age = 0.0f; h->anchor = (float)s->dw - 2.0f; }
            float t = h->age * ginv2;
            if (t > 1.0f) t = 2.0f - t;
            float back = (float)s->dw - (h->anchor - s->rspeed * h->age);
            if (back < 1.0f) back += (float)DC_DLY;
            rv += dc_dly(s, back) * t;
            h->age += 1.0f;
        }
    }
    s->rmix += ((s->rev_on ? 1.0f : 0.0f) - s->rmix) * 0.004f;
    if (s->rmix < 1e-4f && !s->rev_on) s->rmix = 0.0f;
    float src = yd + s->rmix * (rv - yd);
    float g2 = 0.6f * sm;
    s->oprev[0] = s->ocur[0]; s->oprev[1] = s->ocur[1];
    s->ocur[0] = dc_ap(s, DC_AP_IN, src, g2);
    s->ocur[1] = dc_ap(s, DC_AP_IN + 1, src, g2);
    s->steps++;
}

/* ---------------------------------------------------------------- LFO, chance, glitch */

static DC_HOT void dc_new_cycle(dc_t *s)
{
    uint32_t c = s->p[DC_CHANCE], g = s->p[DC_GLITCH];
    static const float mult[3] = { 0.5f, 1.0f, 2.0f };
    if (s->p[DC_SHAPE] != 6) {                              /* ENV shape: the envelope gates instead */
        if (c < 128) {                                       /* chance that anything enters; then 50/50 new or old */
            int open = dc_frand(s) < (float)c * (1.0f / 128.0f);
            int fresh = dc_frand(s) < 0.5f;
            s->tin = open && fresh ? 1.0f : 0.0f;
            s->tfb = open && !fresh ? 1.0f : 0.0f;
        } else {                                             /* new always; the old one by chance */
            s->tin = 1.0f;
            s->tfb = dc_frand(s) < (float)(c - 128) * (1.0f / 127.0f) ? 1.0f : 0.0f;
        }
    }
    if (g < 43) { s->gm = 0.5f; s->crm = 1.0f; }
    else if (g < 86) { s->gm = 1.0f; s->crm = 1.0f; }
    else if (g < 128) { s->gm = 2.0f; s->crm = 1.0f; }
    else if (dc_frand(s) < (float)(g - 128) * (1.0f / 127.0f)) {
        s->gm = mult[dc_rand(s) % 3u]; s->crm = mult[dc_rand(s) % 3u];
    } else { s->gm = 1.0f; s->crm = 1.0f; }
    s->rprev = s->rnext; s->rnext = dc_frand(s);
}

static DC_HOT_INLINE float dc_smooth(float t) { return t * t * (3.0f - 2.0f * t); }   /* ~raised cosine, 0..1 */

static DC_HOT_INLINE float dc_lfo_shape(dc_t *s)
{
    float ph = s->lfo_ph;
    switch (s->p[DC_SHAPE]) {
    case 1: return ph < 0.5f ? 1.0f : 0.0f;
    case 2: return ph;
    case 3: return 1.0f - ph;
    case 4: return s->rprev + (s->rnext - s->rprev) * dc_smooth(ph);
    case 5: return s->rnext;
    default: return dc_smooth(ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph);   /* sine-like, no sinf */
    }
}

/* ---------------------------------------------------------------- block */

static DC_HOT_INLINE int32_t dc_pcm(float x)
{
    if (x >= 0.99999994f) return 2147483520;
    if (x <= -1.0f) return (-2147483647 - 1);
    return (int32_t)(x * 2147483648.0f);
}

static void dc_targets(dc_t *s)
{
    float t = (float)s->p[DC_TIME] * (1.0f / 255.0f);
    s->r = dc_clock_ratio[s->p[DC_CLOCK]];
    s->dtarget = 200.0f * exp2f(t * 6.937f);                 /* 200 .. 24500 steps: 4.5 .. 555 ms at 44.1 kHz */
    if (s->dtarget > (float)DC_DMAX) s->dtarget = (float)DC_DMAX;
    s->smear = (float)s->p[DC_SMEAR] * (1.0f / 255.0f);
    s->slice = exp2f(8.0f + 5.0f * (float)s->p[DC_LENGTH] * (1.0f / 255.0f));
    s->slice_inv2 = 2.0f / s->slice;
    s->tape_len = (float)DC_LFULL * exp2f(-4.0f * (1.0f - (float)s->p[DC_LENGTH] * (1.0f / 255.0f)));
    s->rate_hz = 0.05f * expf((float)s->p[DC_RATE] * (5.4806f / 255.0f));   /* 0.05 .. 12 Hz */
    s->depth = (float)s->p[DC_DEPTH] * (1.0f / 255.0f);
    uint32_t d = s->p[DC_DISSOLVE];
    s->crush = 1.0f; s->qlev = 0.0f; s->rev_on = 0; s->rspeed = 1.0f;
    if (d < 120) {
        float a = (float)(120 - d) * (1.0f / 120.0f);
        s->crush = exp2f(-6.5f * a) * s->crm;
        if (s->crush > 1.0f) s->crush = 1.0f;
        s->qlev = a > 0.5f ? exp2f(16.0f - 18.0f * (a - 0.5f)) : 0.0f;   /* bits fall in the lower half */
    } else if (d > 135) {
        uint32_t stage = (d - 136) / 30u;                    /* 4 stages: 1, 1/2, 1/4, 1/8 */
        if (stage > 3) stage = 3;
        s->rev_on = 1; s->rspeed = 1.0f / (float)(1u << stage);
    }
    uint32_t f = s->p[DC_FILTER];
    if (f < 120) { s->fmode = 0; s->fc_t = 150.0f * exp2f((float)f * (6.44f / 120.0f)); }       /* 150 Hz .. 13 kHz */
    else if (f <= 135) { s->fmode = 0; s->fc_t = 19000.0f; }                                   /* open */
    else { s->fmode = 1; s->fc_t = 20.0f * exp2f((float)(f - 136) * (6.97f / 119.0f)); }       /* 20 Hz .. 2.5 kHz */
}

static DC_HOT void dc_block(dc_t *s, const int32_t *in, int32_t *out_l, int32_t *out_r, uint32_t n)
{
    dc_targets(s);
    s->fc += (s->fc_t - s->fc) * 0.25f;
    float gf = tanf(GP5_PI * s->fc / DC_FS), kf = 1.3f;
    float a1 = 1.0f / (1.0f + gf * (gf + kf)), a2 = gf * a1, a3 = gf * a2;
    float on_t = s->p[DC_ON] ? 1.0f : 0.0f;
    float mix_t = (float)s->p[DC_MIX] * (1.0f / 255.0f);
    float lp_t = s->p[DC_CATCH] && s->p[DC_ON] ? 1.0f : 0.0f;
    float looplv = (float)s->p[DC_LOOPLV] * (1.0f / 255.0f);
    float env_thr = 0.25f * exp2f(-(float)s->p[DC_CHANCE] * (8.0f / 255.0f));

    for (uint32_t i = 0; i < n; i++) {
        float dry = (float)in[i] * (1.0f / 2147483648.0f);
        float r = s->r;

        /* memory: always listening until a loop is caught */
        if (!s->p[DC_CATCH]) {
            s->macc += dry; s->mcnt++;
            s->mph += r;
            if (s->mph >= 1.0f) {
                s->mph -= 1.0f;
                float v = s->macc / (float)s->mcnt;
                s->L->mem[s->mw] = (int16_t)((v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v) * 32767.0f);
                s->mw = (s->mw + 1u) & (DC_MEM - 1u);
                s->macc = 0.0f; s->mcnt = 0;
            }
        }
        float loop = 0.0f;
        s->lgain += (lp_t - s->lgain) * 0.0012f;
        if (lp_t == 0.0f && s->lgain < 1e-4f) s->lgain = 0.0f;
        if (s->lgain > 0.0f) loop = dc_loop(s) * s->lgain;

        /* LFO + chance */
        float a = fabsf(dry);
        s->env += (a - s->env) * (a > s->env ? 0.01f : 0.0003f);
        s->lfo_ph += s->rate_hz * s->gm * (1.0f / DC_FS);
        if (s->lfo_ph >= 1.0f) { s->lfo_ph -= 1.0f; dc_new_cycle(s); }
        if (s->p[DC_SHAPE] == 6) { s->tin = s->env > env_thr ? 1.0f : 0.0f; s->tfb = 0.0f; }
        s->lfo += (dc_lfo_shape(s) - s->lfo) * 0.02f;
        s->gin += (s->tin - s->gin) * 0.005f;
        s->gfb += (s->tfb - s->gfb) * 0.005f;

        /* wet channel input by ROUTE */
        float win = s->p[DC_ROUTE] == 0 ? dry : s->p[DC_ROUTE] == 1 ? dry + loop : (s->lgain > 0.5f ? loop : dry);
        s->dacc += win; s->dcnt++;
        s->dph += r * s->gm;
        while (s->dph >= 1.0f) {
            s->dph -= 1.0f;
            if (s->dcnt) { s->dx = s->dacc / (float)s->dcnt; s->dacc = 0.0f; s->dcnt = 0; }
            dc_step(s);
        }
        float wl = s->oprev[0] + s->dph * (s->ocur[0] - s->oprev[0]);
        float wr = s->oprev[1] + s->dph * (s->ocur[1] - s->oprev[1]);

        /* dissolve: sample-rate crush */
        if (s->crush < 1.0f) {
            s->cph += s->crush;
            if (s->cph >= 1.0f) {
                s->cph -= 1.0f;
                s->hold[0] = wl; s->hold[1] = wr;
                if (s->qlev > 0.0f) {
                    s->hold[0] = (float)dc_floor(wl * s->qlev + 0.5f) / s->qlev;
                    s->hold[1] = (float)dc_floor(wr * s->qlev + 0.5f) / s->qlev;
                }
            }
            wl = s->hold[0]; wr = s->hold[1];
        }

        /* modulation: tremolo or vibrato */
        if (s->p[DC_MODTYPE] == 0) {
            float g = 1.0f - s->depth * s->lfo;
            wl *= g; wr *= g;
        } else {
            s->L->vib[0][s->vw] = wl; s->L->vib[1][s->vw] = wr;
            float back = 22.0f + s->depth * 330.0f * s->lfo;
            float idx = (float)s->vw - back;
            if (idx < 0.0f) idx += (float)DC_VIB;
            uint32_t k = (uint32_t)idx, k2 = (k + 1u) & (DC_VIB - 1u);
            float f = idx - (float)k;
            wl = s->L->vib[0][k] + f * (s->L->vib[0][k2] - s->L->vib[0][k]);
            wr = s->L->vib[1][k] + f * (s->L->vib[1][k2] - s->L->vib[1][k]);
            s->vw = (s->vw + 1u) & (DC_VIB - 1u);
        }

        /* filter: state-variable, low-pass or high-pass */
        float w2[2] = { wl, wr };
        for (uint32_t c = 0; c < 2; c++) {
            float v3 = w2[c] - s->ic2[c];
            float v1 = a1 * s->ic1[c] + a2 * v3;
            float v2 = s->ic2[c] + a2 * s->ic1[c] + a3 * v3;
            s->ic1[c] = 2.0f * v1 - s->ic1[c];
            s->ic2[c] = 2.0f * v2 - s->ic2[c];
            w2[c] = s->fmode ? w2[c] - kf * v1 - v2 : v2;
        }

        /* mix: MIX between dry and the wet channel (noon: both full); the loop on top; ON fades all of it */
        s->on += (on_t - s->on) * 0.003f;
        s->mix += (mix_t - s->mix) * 0.003f;
        if (on_t == 0.0f && s->on < 1e-4f) s->on = 0.0f;
        if (s->on == 0.0f) { out_l[i] = out_r[i] = in[i]; continue; }        /* exact bypass */
        float dg = s->mix < 0.5f ? 1.0f : 2.0f - 2.0f * s->mix;
        float wg = s->mix > 0.5f ? 1.0f : 2.0f * s->mix;
        float direct = loop * looplv;
        float ol = dg * dry + wg * dc_soft(w2[0]) + direct;
        float or_ = dg * dry + wg * dc_soft(w2[1]) + direct;
        out_l[i] = dc_pcm(dc_knee(dry + s->on * (ol - dry)));
        out_r[i] = dc_pcm(dc_knee(dry + s->on * (or_ - dry)));
    }
}
#endif
