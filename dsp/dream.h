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
 * ROUTE sends the input, the loop, or both through the wet channel; LOOP LEVEL sets the loop's level in both places.
 *
 * 1.2: the delay is read by two overlapping GRAINS. Each new grain may be pitched to an interval of SCALE
 * (PITCH), play backwards (REVERSE) or repeat the grain before (STUTTER); the grains are fed back, so pitched
 * repeats stack into a shimmer. TONE darkens the feedback. FREEZE: the delay line repeats what it holds,
 * exactly, while the grains keep reading it. OVERDUB: the playing goes into the caught loop; FADE sets how much
 * of the loop stays each pass. A big TIME change dips the repeats, jumps and comes back (no racing read head).
 *
 * Plain C, no allocation, no C library: the same file runs on the pedal and on the computer
 * (opengp5/tools/run_effect.py, dsp/test/dream_host.c; ./test.sh). Big buffers are the caller's (dc_large_t). */
#ifndef DREAM_H
#define DREAM_H
#include <stdint.h>
#include "gp5_math.h"
#include "../params.h"
#include "sync.h"

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
/* Sizes for the control processor, which runs the engine (1.2: about 236 KB of its 272 KB of RAM). */
#define DC_MEM    65536u                    /* loop memory, engine samples (1.47 s at 44.1 kHz, 47 s at 1.4 kHz); a power of 2 */
#define DC_XF     512u                      /* loop seam crossfade */
#define DC_LFULL  (DC_MEM - DC_XF - 64u)     /* longest loop: leaves the crossfade's samples before its start */
#define DC_DLY    51200u                    /* delay line, engine steps */
#define DC_DMAX   (DC_DLY - 64u)             /* longest delay (1.16 s at 44.1 kHz, 37 s at 1.4 kHz) */
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

typedef struct { float on, mix; } dc_mixer_t;   /* the final mix's ON / MIX ramps (dc_mix) */
typedef struct { float age; float anchor; } dc_rev_t;
typedef struct { float age, len, q, off; } dc_grain_t;   /* read speed q (<0 backwards); offset from the delay */

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
    dc_grain_t gr[2]; uint32_t gnext; float gsince, glen;   /* grains */
    float duck; int jump;                   /* a big TIME change: dip, jump, come back */
    float tlp;                              /* TONE: feedback low-pass state */
    float fz, flen;                         /* freeze: crossfade 0..1, frozen delay length (whole steps) */
    uint32_t opos;                          /* overdub write position in the loop */

    /* chance, glitch, LFO */
    float lfo_ph, lfo, rprev, rnext, env;
    float beat, sync_cycle;                 /* sync: beats since the clock wrapped; beats per LFO cycle */
    uint32_t beat_acc, beat_bpm;            /* the beat clock, exact: + BPM each sample, a beat every DC_BEAT_UNITS */
    float tin, tfb, gin, gfb;               /* gate targets and their ramps */
    float gm, crm;                          /* glitch: engine clock multiplier, crush multiplier */

    /* dissolve, modulation, filter, output */
    float cph, hold[2];
    uint32_t vw;
    float ic1[2], ic2[2], fc;
    float mix;                              /* MIX ramp for the effect's own gain */
    dc_mixer_t mx;                          /* the final mix, one-processor build */

    /* per-block targets */
    float r, dtarget, smear, rate_hz, depth, crush, qlev, rspeed, fc_t; int fmode, rev_on;
    float slice, slice_inv2, tape_len;
    float grain, ppitch, prev, pstut, tone_a, fade;

    uint32_t steps;                         /* engine steps run (statistics) */
} dc_t;

static const float dc_clock_ratio[DC_CLOCKS] = {
    1.0f / 32, 3.0f / 64, 1.0f / 16, 3.0f / 32, 1.0f / 8, 3.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 2, 3.0f / 4, 1.0f
};
static const float dc_tape_speed[8] = { -4.0f, -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f, 4.0f };

/* SCALE: the read speeds a pitched grain picks from (2^(semitones/12)) */
#define DC_P5U 1.4983071f   /* +7, a fifth up    */
#define DC_P4D 0.7491535f   /* -5, a fourth down */
static const float dc_scale[DC_SCALES][8] = {
    { 2.0f },                                                                           /* OCT UP  */
    { 0.5f },                                                                           /* OCT DN  */
    { 2.0f, 0.5f },                                                                     /* OCTAVES */
    { DC_P5U, 2.0f, DC_P4D, 0.5f },                                                     /* 5TH+OCT */
    { 1.1892071f, 1.2599210f, 1.3348399f, DC_P5U, 1.7817974f, 2.0f, DC_P4D, 0.5f },    /* HARMONY: m3 M3 4 5 m7 8, -4 -8 */
};
static const uint8_t dc_scale_n[DC_SCALES] = { 1, 1, 2, 4, 8 };

static DC_HOT_INLINE uint32_t dc_rand(dc_t *s) { uint32_t x = s->rnd; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return s->rnd = x; }
static DC_HOT_INLINE float dc_frand(dc_t *s) { return (float)(dc_rand(s) >> 8) * (1.0f / 16777216.0f); }

static __attribute__((unused)) void dc_set(dc_t *s, uint32_t id, uint32_t v)
{
    static const uint8_t max[DC_NPARAM] = DC_MAXES;
    if (id >= DC_NPARAM) return;
    if (v > max[id]) v = max[id];
    if (id == DC_CATCH && v && !s->p[DC_CATCH]) {           /* catch: freeze the memory, start the loop */
        s->catch_w = s->mw;                                  /* (2 -> 1 keeps going: it ran silently, in time) */
        s->lpos = 0.0f;
        s->shead = 0.0f; s->ssince = 1e9f;
    }
    if (id == DC_CATCH && !v && s->p[DC_CATCH]) { s->macc = 0.0f; s->mcnt = 0; }   /* recording again */
    if (id == DC_FREEZE && v && !s->p[DC_FREEZE] && s->fz == 0.0f) {      /* hold the delay at a whole length */
        s->flen = (float)(int32_t)(s->dcur + 0.5f);
        s->dcur = s->flen; s->jump = 0;
    }
    if (id == DC_OVERDUB && v != s->p[DC_OVERDUB]) {         /* write where the loop plays now */
        s->opos = (uint32_t)(s->p[DC_LMODE] ? s->shead : s->lpos);
        s->macc = 0.0f; s->mcnt = 0;
    }
    s->p[id] = v;
}

static __attribute__((unused)) void dc_init(dc_t *s, dc_large_t *L)
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
    s->dcur = 4410.0f; s->gm = 1.0f; s->crm = 1.0f; s->beat = 0.0f; s->beat_acc = 0; s->beat_bpm = 120; s->sync_cycle = 0.0f;
    s->duck = 1.0f; s->tone_a = 1.0f; s->fade = 1.0f;
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

/* a new grain on the free head: maybe a repeat of the last one (STUTTER), else maybe pitched (PITCH, SCALE)
 * and / or backwards (REVERSE). Its read range is kept inside the delay line. */
static DC_HOT void dc_grain_start(dc_t *s)
{
    uint32_t k = s->gnext, j = k ^ 1u;
    dc_grain_t *g = &s->gr[k], *pv = &s->gr[j];
    float G = s->grain, q = 1.0f, off = 0.0f;
    int rep = 0;
    if (s->pstut > 0.0f && pv->len > 0.0f && dc_frand(s) < s->pstut) {
        float span;
        G = pv->len; q = pv->q; off = pv->off + pv->age;    /* the same audio again, from its start */
        span = G * (1.0f - q);
        rep = s->dcur + off + (span > 0.0f ? span : 0.0f) <= (float)DC_DLY - 4.0f;
    }
    if (!rep) {
        float a;
        G = s->grain; q = 1.0f; off = 0.0f;
        if (s->ppitch > 0.0f && dc_frand(s) < s->ppitch) {
            uint32_t sc = s->p[DC_SCALE];
            q = dc_scale[sc][dc_rand(s) % dc_scale_n[sc]];
        }
        if (s->prev > 0.0f && dc_frand(s) < s->prev) q = -q;
        a = fabsf(1.0f - q);
        if (a > 0.0f) {                                      /* centred on the delay time, then kept inside */
            float b0, b1, lo, hi;
            if (G * a > (float)DC_DLY - 16.0f) G = ((float)DC_DLY - 16.0f) / a;
            off = -0.5f * G * (1.0f - q);
            b0 = s->dcur + off; b1 = b0 + G * (1.0f - q);
            lo = b0 < b1 ? b0 : b1; hi = b0 < b1 ? b1 : b0;
            if (lo < 4.0f) off += 4.0f - lo;
            else if (hi > (float)DC_DLY - 4.0f) off -= hi - ((float)DC_DLY - 4.0f);
        }
    }
    g->age = 0.0f; g->len = G; g->q = q; g->off = off;
    s->gnext = j; s->gsince = 0.0f; s->glen = G;
}

static DC_HOT_INLINE void dc_step(dc_t *s)
{
    float sm = s->smear;
    int hold = s->p[DC_FREEZE] || s->fz > 0.0f;             /* frozen (or fading in / out): the length stays */
    if (!hold) {
        float d = s->dtarget - s->dcur;
        if (s->jump || fabsf(d) > 0.25f * s->dcur + 256.0f) {
            s->jump = 1;                                     /* big change: dip out, jump, come back */
            s->duck -= 1.0f / 128.0f;
            if (s->duck <= 0.0f) { s->duck = 0.0f; s->dcur = s->dtarget; s->jump = 0; }
        } else {                                             /* small change: glide like tape, at most +-50 % speed */
            d *= 0.0006f;
            s->dcur += d > 0.5f ? 0.5f : d < -0.5f ? -0.5f : d;
        }
    }
    if (!s->jump && s->duck < 1.0f) { s->duck += 1.0f / 128.0f; if (s->duck > 1.0f) s->duck = 1.0f; }

    /* the grains: two half-overlapping read heads */
    if (s->gsince >= 0.5f * s->glen) dc_grain_start(s);
    float y = 0.0f;
    int plain = 1;
    for (uint32_t k = 0; k < 2; k++)
        if (s->gr[k].age < s->gr[k].len && (s->gr[k].q != 1.0f || s->gr[k].off != 0.0f)) plain = 0;
    if (plain) y = dc_dly(s, s->dcur);                       /* plain delay: one read, exact */
    else for (uint32_t k = 0; k < 2; k++) {
        dc_grain_t *gk = &s->gr[k];
        if (gk->age >= gk->len) continue;
        float t = gk->age * 2.0f / gk->len;
        if (t > 1.0f) t = 2.0f - t;
        float back = s->dcur + gk->off + gk->age * (1.0f - gk->q);
        back = back < 1.0f ? 1.0f : back > (float)DC_DLY - 2.0f ? (float)DC_DLY - 2.0f : back;
        y += dc_dly(s, back) * t;
    }
    for (uint32_t k = 0; k < 2; k++) if (s->gr[k].age < s->gr[k].len) s->gr[k].age += 1.0f;
    s->gsince += 1.0f;
    y *= s->duck;

    float yd = y;
    float g = 0.72f * sm;
    for (uint32_t k = 0; k < DC_AP_IN; k++) yd = dc_ap(s, k, yd, g);
    s->tlp += s->tone_a * (yd - s->tlp);                     /* TONE: 1 = open */
    float base = 0.88f * sm, fb = base + s->gfb * (0.985f - base);
    float w = dc_soft(s->gin * s->dx + fb * s->tlp);

    /* freeze: the line copies itself at the frozen length (crossfaded in and out over 256 steps) */
    if (s->p[DC_FREEZE]) { s->fz += 1.0f / 256.0f; if (s->fz > 1.0f) s->fz = 1.0f; }
    else if (s->fz > 0.0f) { s->fz -= 1.0f / 256.0f; if (s->fz < 0.0f) s->fz = 0.0f; }
    int32_t fi = (int32_t)s->dw - (int32_t)s->flen;
    if (fi < 0) fi += (int32_t)DC_DLY;
    if (s->fz >= 1.0f) s->L->dly[s->dw] = s->L->dly[fi];    /* exact: repeats forever */
    else {
        if (s->fz > 0.0f) w += s->fz * ((float)s->L->dly[fi] * (1.0f / 32768.0f) - w);
        s->L->dly[s->dw] = (int16_t)(w * 32767.0f);
    }
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

/* ---------------------------------------------------------------- overdub */

static DC_HOT_INLINE void dc_dub_put(dc_t *s, uint32_t i, float v)
{
    float o = (float)s->L->mem[i] * (1.0f / 32768.0f);
    s->L->mem[i] = (int16_t)(dc_knee(o * s->fade + v) * 32767.0f);
}

/* one memory step of new playing into the caught loop, at a 1x write head that started where the loop played;
 * the samples the loop's seam fades into (just before its start) get the same, so the seam stays smooth */
static DC_HOT_INLINE void dc_dub(dc_t *s, float v)
{
    uint32_t len = (uint32_t)(s->p[DC_LMODE] ? (float)DC_LFULL : s->tape_len);
    if (s->opos >= len) s->opos = 0;
    uint32_t i = (s->catch_w - len + s->opos) & (DC_MEM - 1u);
    dc_dub_put(s, i, v);
    if (s->opos + DC_XF >= len) dc_dub_put(s, (i - len) & (DC_MEM - 1u), v);
    s->opos++;
}

/* ---------------------------------------------------------------- block */

static DC_HOT_INLINE int32_t dc_pcm(float x)
{
    if (x >= 0.99999994f) return 2147483520;
    if (x <= -1.0f) return (-2147483647 - 1);
    return (int32_t)(x * 2147483648.0f);
}

/* ---------------------------------------------------------------- the final mix */

/* MIX between dry and the effect (noon: both full); ON fades all of it; off is the exact dry signal. On one
 * processor dc_run() calls it per sample; in the two-chip build the DSP does, with the effect's share from the
 * control processor (dc_block_wet). Its own copy of the ON / MIX ramps, so it needs nothing else of dc_t. */
static DC_HOT_INLINE void dc_mix(dc_mixer_t *m, uint32_t on, uint32_t mix, int32_t in, float el, float er,
                                 int32_t *out_l, int32_t *out_r)
{
    float on_t = on ? 1.0f : 0.0f, mix_t = (float)mix * (1.0f / 255.0f);
    m->on += (on_t - m->on) * 0.003f;
    m->mix += (mix_t - m->mix) * 0.003f;
    if (on_t == 0.0f && m->on < 1e-4f) m->on = 0.0f;
    if (m->on == 0.0f) { *out_l = *out_r = in; return; }       /* exact bypass */
    float dry = (float)in * (1.0f / 2147483648.0f);
    float dg = m->mix < 0.5f ? 1.0f : 2.0f - 2.0f * m->mix;
    *out_l = dc_pcm(dc_knee(dry + m->on * (dg * dry + el - dry)));
    *out_r = dc_pcm(dc_knee(dry + m->on * (dg * dry + er - dry)));
}


static void dc_targets(dc_t *s)
{
    float t = (float)s->p[DC_TIME] * (1.0f / 255.0f);
    s->r = dc_clock_ratio[s->p[DC_CLOCK]];
    s->dtarget = 200.0f * exp2f(t * 8.0f);                   /* 200 .. 51136 steps: 4.5 ms .. 1.16 s at 44.1 kHz */
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
    s->grain = exp2f(8.0f + 5.0f * (float)s->p[DC_GRAIN] * (1.0f / 255.0f));   /* 256 .. 8192 steps */
    s->sync_cycle = 0.0f;
    if (s->p[DC_SYNC]) {                                     /* 1.4: note values of BPM (sync.h), what fits only */
        unsigned bpm = s->p[DC_BPM] < 40u ? 40u : s->p[DC_BPM];
        s->dtarget = dc_note_steps(dc_sync_note(DC_SYNC_TIME, s->p[DC_TIME], bpm, s->r), bpm, s->r);
        if (s->dtarget > (float)DC_DMAX) s->dtarget = (float)DC_DMAX;
        if (!s->p[DC_LMODE]) {
            s->tape_len = dc_note_steps(dc_sync_note(DC_SYNC_LENGTH, s->p[DC_LENGTH], bpm, s->r), bpm, s->r);
            if (s->tape_len > (float)DC_LFULL) s->tape_len = (float)DC_LFULL;
        }
        s->grain = dc_note_steps(dc_sync_note(DC_SYNC_GRAIN, s->p[DC_GRAIN], bpm, s->r), bpm, s->r);
        if (s->grain < 256.0f) s->grain = 256.0f;
        if (s->grain > 8192.0f) s->grain = 8192.0f;
        s->sync_cycle = dc_note_beats(dc_sync_note(DC_SYNC_RATE, s->p[DC_RATE], bpm, s->r));
        s->beat_bpm = bpm;
        s->rate_hz = (float)bpm / 60.0f / s->sync_cycle;
    }
    s->ppitch = (float)s->p[DC_PITCH] * (1.0f / 255.0f);
    s->prev = (float)s->p[DC_REVERSE] * (1.0f / 255.0f);
    s->pstut = (float)s->p[DC_STUTTER] * (1.0f / 255.0f);
    s->tone_a = s->p[DC_TONE] >= 255 ? 1.0f : 0.02f * exp2f((float)s->p[DC_TONE] * (5.6f / 255.0f));
    s->fade = (float)s->p[DC_FADE] * (1.0f / 255.0f);
}

static DC_HOT_INLINE void dc_run(dc_t *s, const int32_t *in, int32_t *out_l, int32_t *out_r, float *wet_l, float *wet_r,
                                 uint32_t n)
{
    dc_targets(s);
    s->fc += (s->fc_t - s->fc) * 0.25f;
    float gf = tanf(GP5_PI * s->fc / DC_FS), kf = 1.3f;
    float a1 = 1.0f / (1.0f + gf * (gf + kf)), a2 = gf * a1, a3 = gf * a2;
    float mix_t = (float)s->p[DC_MIX] * (1.0f / 255.0f);
    uint32_t cs = s->p[DC_CATCH];
    int dub = cs == 1 && s->p[DC_OVERDUB];
    float lp_t = cs == 1 && s->p[DC_ON] ? 1.0f : 0.0f;
    float looplv = (float)s->p[DC_LOOPLV] * (1.0f / 255.0f);
    float env_thr = 0.25f * exp2f(-(float)s->p[DC_CHANCE] * (8.0f / 255.0f));

    for (uint32_t i = 0; i < n; i++) {
        float dry = (float)in[i] * (1.0f / 2147483648.0f);
        float r = s->r;

        /* memory: always listening until a loop is caught; overdub writes into the caught loop */
        if (!cs || dub) {
            s->macc += dry; s->mcnt++;
            s->mph += r;
            if (s->mph >= 1.0f) {
                s->mph -= 1.0f;
                float v = s->macc / (float)s->mcnt;
                v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
                if (!cs) { s->L->mem[s->mw] = (int16_t)(v * 32767.0f); s->mw = (s->mw + 1u) & (DC_MEM - 1u); }
                else dc_dub(s, v);
                s->macc = 0.0f; s->mcnt = 0;
            }
        }
        float loop = 0.0f;
        s->lgain += (lp_t - s->lgain) * 0.0012f;
        if (lp_t == 0.0f && s->lgain < 1e-4f) s->lgain = 0.0f;
        if (s->lgain > 0.0f) loop = dc_loop(s) * s->lgain;
        else if (cs == 2) (void)dc_loop(s);                  /* caught, foot still down: runs silently, in time */

        /* LFO + chance */
        float a = fabsf(dry);
        s->env += (a - s->env) * (a > s->env ? 0.01f : 0.0003f);
        if (s->sync_cycle > 0.0f) {                          /* sync: the cycle follows the beat clock, in phase */
            float ph;
            /* Counted in whole numbers: a float adding BPM / 60 / 44100 each sample rounded every step (near 48 beats
             * its resolution is ~4 % of the step) and ran 0.5..0.9 % off the tempo (+0.9 beat a minute at 120 BPM). */
            s->beat_acc += s->beat_bpm;
            if (s->beat_acc >= DC_BEAT_WRAP) s->beat_acc -= DC_BEAT_WRAP;
            s->beat = (float)s->beat_acc * (1.0f / (float)DC_BEAT_UNITS);
            ph = s->beat / s->sync_cycle;
            ph -= (float)(int32_t)ph;
            if (ph < s->lfo_ph) dc_new_cycle(s);
            s->lfo_ph = ph;
        } else {
            s->lfo_ph += s->rate_hz * s->gm * (1.0f / DC_FS);
            if (s->lfo_ph >= 1.0f) { s->lfo_ph -= 1.0f; dc_new_cycle(s); }
        }
        if (s->p[DC_SHAPE] == 6) { s->tin = s->env > env_thr ? 1.0f : 0.0f; s->tfb = 0.0f; }
        s->lfo += (dc_lfo_shape(s) - s->lfo) * 0.02f;
        s->gin += (s->tin - s->gin) * 0.005f;
        s->gfb += (s->tfb - s->gfb) * 0.005f;

        /* wet channel input by ROUTE */
        /* the loop at LOOP LEVEL, here as on the output: turned down, it feeds the effects less too (owner, 2026-11-19) */
        float lsend = loop * looplv;
        float win = s->p[DC_ROUTE] == 0 ? dry : s->p[DC_ROUTE] == 1 ? dry + lsend : (s->lgain > 0.5f ? lsend : dry);
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
            uint32_t k = (uint32_t)idx;
            float f = idx - (float)k;
            k &= DC_VIB - 1u;                                /* a tiny negative idx + DC_VIB can round to DC_VIB itself */
            uint32_t k2 = (k + 1u) & (DC_VIB - 1u);
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

        /* the effect's share of the output: the wet channel at its MIX gain, the loop on top */
        s->mix += (mix_t - s->mix) * 0.003f;
        float wg = s->mix > 0.5f ? 1.0f : 2.0f * s->mix;
        float direct = lsend;
        float el = wg * dc_soft(w2[0]) + direct, er = wg * dc_soft(w2[1]) + direct;
        if (wet_l) { wet_l[i] = el; wet_r[i] = er; }           /* two-chip build: the DSP mixes (dc_mix) */
        else dc_mix(&s->mx, s->p[DC_ON], s->p[DC_MIX], in[i], el, er, &out_l[i], &out_r[i]);
    }
}

/* One block, all on one processor: in -> out. */
static DC_HOT __attribute__((unused)) void dc_block(dc_t *s, const int32_t *in, int32_t *out_l, int32_t *out_r, uint32_t n)
{
    dc_run(s, in, out_l, out_r, 0, 0, n);
}

/* One block of the effect alone (the two-chip build's control processor): in -> the effect's share of the
 * output, which dc_mix() on the DSP adds to the dry signal. */
static DC_HOT __attribute__((unused)) void dc_block_wet(dc_t *s, const int32_t *in, float *wet_l, float *wet_r, uint32_t n)
{
    dc_run(s, in, 0, 0, wet_l, wet_r, n);
}
#endif
