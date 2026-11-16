/* Dreamcatcher's hall reverb, on the DSP (1.2). It reverberates the effect's share of the output (the wet channel
 * and the loop, as it comes back from the control processor); the dry guitar stays dry.
 *
 * An 8-line feedback delay network: the lines' outputs are mixed by an 8 x 8 Hadamard matrix (orthogonal, so the
 * network keeps its energy), each through its own absorption filter (a one-pole low-pass whose gain is the decay
 * per pass: lows and mids decay in exactly DECAY, the highs 2.5 times faster, so long tails darken as they go),
 * and written back with the new input. Before the network: a pre-delay and four allpass diffusers.
 * Settings (params.h, 0..255):
 *   DECAY    0.3 s .. 27 s (RT60); at the top, HOLD: the tail sustains and new input fades out
 *   MIX      none .. the reverb on top of the effect (noon) .. the reverb alone
 *   SIZE     the lines' lengths, 25 % .. 100 % (a room .. a big hall); turning it glides, like tape
 *   PREDLY   0 .. 200 ms before the reverb starts
 *   MOD      slow drift of the lines' lengths (chorus-like movement in the tail)
 *   SHIMMER  an octave-up copy of the tail fed back in (through a soft limit, so it can't run away)
 * HOLD has no absorption at all (gain 1, pole 0).
 *
 * Cost. The DSP runs about 4-5 cycles per instruction (measured on the pedal: 32 cycles for a trivial loop step),
 * so a full-rate network took more than the whole DSP. This one runs at half rate (22.05 kHz: the input
 * low-passed and halved, the output interpolated back up; a hall's tail has little above 11 kHz, and the dry and
 * wet sounds stay at full rate), updates MOD, SIZE and the pre-delay once per block, reads whole samples when MOD
 * is off, and runs the shimmer's pitch shifter only when SHIMMER is up.
 * Plain C, float; also runs on the computer (dsp/test/hall_test.c). Needs dream.h first (DC_HOT, dc_soft). */
#ifndef DC_HALL_H
#define DC_HALL_H
#include <stdint.h>

#define HL_N      8u
#define HL_FS     22050.0f                 /* the network's rate: half the audio rate */
#define HL_MODMAX 12.0f                    /* MOD's reach, network samples (0.54 ms) at the biggest SIZE */
#define HL_PAD    28u                      /* room past each line's longest length for MOD (2 x HL_MODMAX + 4) */
#define HL_LINES  (751u + 907u + 1069u + 1277u + 1487u + 1709u + 1889u + 2113u + HL_N * HL_PAD)
#define HL_APT    (71u + 53u + 191u + 139u)
#define HL_SH     1024u                    /* shimmer buffer, a power of 2 */
#define HL_SHD    992.0f                   /* shimmer grain: the read heads' span (45 ms) */
#define HL_PD     4416u                    /* pre-delay line: 200 ms at 22.05 kHz + spare */
static const uint16_t hl_len[HL_N] = { 751, 907, 1069, 1277, 1487, 1709, 1889, 2113 };   /* 34 .. 96 ms */
static const uint16_t hl_aplen[4] = { 71, 53, 191, 139 };

typedef struct {                           /* the big buffers (the DSP's GP5_LARGE memory) */
    float line[HL_LINES];
    float ap[HL_APT];
    float sh[HL_SH];
    float pd[HL_PD];
} hl_large_t;

typedef struct {
    hl_large_t *L;
    uint32_t p[6];                         /* DECAY, MIX, SIZE, PREDLY, MOD, SHIMMER as set */
    uint32_t off[HL_N], cap[HL_N], w[HL_N];
    float lp[HL_N], g[HL_N], pole[HL_N];
    float len[HL_N];                       /* each line's length now (glides to SIZE's) */
    float back[HL_N];                      /* this block's read distance (length + MOD's drift) */
    uint32_t apoff[4], api[4];
    uint32_t pdw, pdl;                     /* pre-delay: write index, length now in samples (glides) */
    uint32_t shw; float shph, shhp, shx;   /* shimmer: write index, read-head phase, high-pass state, last out */
    float ox[4], oy[4], oc[4], os[4];      /* MOD: four quadrature oscillators (8 drifts), stepped per block */
    uint32_t onorm;
    float xprev, ol0, or0;                 /* halving: the last input sample; the last network outputs */
    /* per-block targets */
    float size, mod, shim, revg, effg, in_t, in_g;
    uint32_t pd_t;
    int hold;
} hl_t;

static __attribute__((unused)) void hl_set(hl_t *h, uint32_t which, uint32_t v)   /* which 0..5 */
{
    if (which < 6) h->p[which] = v > 255u ? 255u : v;
}

static __attribute__((unused)) void hl_init(hl_t *h, hl_large_t *L)
{
    static const float rate[4] = { 0.13f, 0.17f, 0.23f, 0.29f };       /* Hz */
    uint8_t *z = (uint8_t *)h;
    uint32_t i, o = 0;
    for (i = 0; i < sizeof *h; i++) z[i] = 0;
    h->L = L;
    for (i = 0; i < HL_LINES; i++) L->line[i] = 0.0f;
    for (i = 0; i < HL_APT; i++) L->ap[i] = 0.0f;
    for (i = 0; i < HL_SH; i++) L->sh[i] = 0.0f;
    for (i = 0; i < HL_PD; i++) L->pd[i] = 0.0f;
    for (i = 0; i < HL_N; i++) { h->off[i] = o; h->cap[i] = hl_len[i] + HL_PAD; o += h->cap[i]; h->len[i] = (float)hl_len[i]; }
    for (i = 0, o = 0; i < 4; i++) { h->apoff[i] = o; o += hl_aplen[i]; }
    for (i = 0; i < 4; i++) {               /* one step = one block of 32 network samples */
        float w = 2.0f * GP5_PI * rate[i] * 32.0f / HL_FS;
        h->oc[i] = cosf(w); h->os[i] = sinf(w); h->ox[i] = 1.0f; h->oy[i] = 0.0f;
    }
    h->p[0] = 150; h->p[1] = 0; h->p[2] = 180; h->p[3] = 20; h->p[4] = 80; h->p[5] = 0;
    h->in_g = 1.0f;
}

/* Once per block: the settings into gains and targets; MOD, SIZE and the pre-delay take one step. */
static __attribute__((unused)) void hl_targets(hl_t *h, uint32_t steps)
{
    uint32_t i, d = h->p[0];
    float x, drift[HL_N];
    h->hold = d >= 250u;
    float rt = 0.3f * exp2f((float)(d > 249u ? 249u : d) * (6.5f / 249.0f));     /* 0.3 .. 27 s */
    h->size = 0.25f + 0.75f * (float)h->p[2] * (1.0f / 255.0f);
    x = (float)h->p[1] * (1.0f / 255.0f);
    h->revg = x < 0.5f ? 2.0f * x : 1.0f;
    h->effg = x > 0.5f ? 2.0f - 2.0f * x : 1.0f;
    h->mod = (float)h->p[4] * (1.0f / 255.0f) * HL_MODMAX * h->size;
    h->shim = (float)h->p[5] * (1.0f / 255.0f) * 0.6f;
    h->in_t = h->hold ? 0.0f : 1.0f;

    /* pre-delay: towards its target by at most one sample per sample, in whole samples */
    h->pd_t = (uint32_t)((float)h->p[3] * (4410.0f / 255.0f) + 0.5f);
    if (h->pdl + steps < h->pd_t) h->pdl += steps; else if (h->pdl > h->pd_t + steps) h->pdl -= steps; else h->pdl = h->pd_t;

    /* MOD's oscillators, one step */
    for (i = 0; i < 4; i++) {
        float nx = h->ox[i] * h->oc[i] - h->oy[i] * h->os[i];
        h->oy[i] = h->ox[i] * h->os[i] + h->oy[i] * h->oc[i];
        h->ox[i] = nx;
        drift[i] = h->ox[i]; drift[i + 4] = h->oy[i];
    }
    if (++h->onorm == 64u) {
        h->onorm = 0;
        for (i = 0; i < 4; i++) { float m = 1.5f - 0.5f * (h->ox[i] * h->ox[i] + h->oy[i] * h->oy[i]); h->ox[i] *= m; h->oy[i] *= m; }
    }

    for (i = 0; i < HL_N; i++) {
        /* SIZE glides (at most 1.6 samples per block: like tape), whole samples at rest */
        float t = (float)(uint32_t)((float)hl_len[i] * h->size + 0.5f), dl = t - h->len[i];
        h->len[i] += dl > 1.6f ? 1.6f : dl < -1.6f ? -1.6f : dl;
        float b = h->len[i] + h->mod * (1.0f + drift[i]);
        if (b > (float)(h->cap[i] - 2u)) b = (float)(h->cap[i] - 2u);
        h->back[i] = b;
        /* gain per pass at DC, and the pole that makes Nyquist's g^2.5 */
        float g = h->hold ? 1.0f : expf(-6.9078f * h->len[i] / (rt * HL_FS));
        float k = h->hold ? 1.0f : expf(-6.9078f * 1.5f * h->len[i] / (rt * HL_FS));
        h->g[i] = g;
        h->pole[i] = (1.0f - k) / (1.0f + k);
    }
}

/* One network sample: x in (mono, half rate) -> *ol, *orr out */
static DC_HOT_INLINE void hl_step(hl_t *h, float x, float *ol, float *orr, int interp)
{
    hl_large_t *L = h->L;
    float r[HL_N], y, a, b, c, d;
    uint32_t i;

    /* pre-delay */
    L->pd[h->pdw] = x;
    {
        int32_t k = (int32_t)h->pdw - (int32_t)h->pdl;
        if (k < 0) k += (int32_t)HL_PD;
        y = L->pd[k];
    }
    if (++h->pdw == HL_PD) h->pdw = 0;

    /* four allpass diffusers */
    for (i = 0; i < 4; i++) {
        float *ap = L->ap + h->apoff[i];
        float v = ap[h->api[i]];
        float u = y + 0.6f * v;
        ap[h->api[i]] = u;
        y = v - 0.6f * u;
        if (++h->api[i] == hl_aplen[i]) h->api[i] = 0;
    }

    /* shimmer: the last output an octave up, high-passed, soft-limited, added to the input */
    if (h->shim > 0.0f) y += h->shim * dc_soft(h->shx) * h->in_g;

    /* the lines' outputs */
    for (i = 0; i < HL_N; i++) {
        const float *ln = L->line + h->off[i];
        uint32_t cap = h->cap[i];
        float pos = (float)h->w[i] - h->back[i];
        if (pos < 0.0f) pos += (float)cap;
        uint32_t k = (uint32_t)pos;
        if (k >= cap) k -= cap;
        if (interp) {
            float f = pos - (float)(uint32_t)pos;
            uint32_t j = k + 1u == cap ? 0u : k + 1u;
            r[i] = ln[k] + f * (ln[j] - ln[k]);
        } else r[i] = ln[k];
    }
    *ol = 0.35f * (r[0] + r[2] + r[4] + r[6]);
    *orr = 0.35f * (r[1] + r[3] + r[5] + r[7]);

    /* Hadamard 8 x 8 (three butterfly stages), scaled by 1 / sqrt(8) */
    for (i = 0; i < 8; i += 2) { a = r[i]; b = r[i + 1]; r[i] = a + b; r[i + 1] = a - b; }
    for (i = 0; i < 8; i += 4) {
        a = r[i]; b = r[i + 1]; c = r[i + 2]; d = r[i + 3];
        r[i] = a + c; r[i + 1] = b + d; r[i + 2] = a - c; r[i + 3] = b - d;
    }
    for (i = 0; i < 4; i++) { a = r[i]; b = r[i + 4]; r[i] = a + b; r[i + 4] = a - b; }

    /* decay, absorption, new input; write */
    for (i = 0; i < HL_N; i++) {
        float v = r[i] * 0.35355339f * h->g[i];
        h->lp[i] = (1.0f - h->pole[i]) * v + h->pole[i] * h->lp[i];      /* HOLD: pole 0, nothing lost */
        L->line[h->off[i] + h->w[i]] = h->lp[i] + ((i & 1u) ? -0.5f : 0.5f) * y;
        if (++h->w[i] == h->cap[i]) h->w[i] = 0;
    }

    /* shimmer's pitch shifter (only when SHIMMER is up): two heads read the output at twice the speed */
    if (h->shim > 0.0f) {
        float s = 0.0f;
        L->sh[h->shw] = 0.5f * (*ol + *orr);
        h->shph += 1.0f / HL_SHD;
        if (h->shph >= 1.0f) h->shph -= 1.0f;
        for (i = 0; i < 2; i++) {
            float ph = h->shph + 0.5f * (float)i;
            if (ph >= 1.0f) ph -= 1.0f;
            float back = 2.0f + (1.0f - ph) * HL_SHD;          /* the delay shrinks 1 sample per sample: 2x speed */
            float pos = (float)h->shw - back;
            if (pos < 0.0f) pos += (float)HL_SH;
            uint32_t k = (uint32_t)pos & (HL_SH - 1u), k2 = (k + 1u) & (HL_SH - 1u);
            float f = pos - (float)(uint32_t)pos;
            float win = ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph;
            s += win * (L->sh[k] + f * (L->sh[k2] - L->sh[k]));
        }
        h->shhp += 0.06f * (s - h->shhp);                      /* high-pass ~200 Hz: no mud */
        h->shx = s - h->shhp;
        h->shw = (h->shw + 1u) & (HL_SH - 1u);
    }
}

/* A block of the effect's share (wl, wr; n even), in place, with the reverb. The input is low-passed (1 2 1) and
 * halved; the network's outputs are interpolated back up (half a network sample later). At MIX 0 the effect's
 * share is unchanged; the network always runs, so turning MIX up brings in the current tail, not an old one. */
static __attribute__((unused)) DC_HOT void hl_block(hl_t *h, float *wl, float *wr, uint32_t n)
{
    uint32_t i;
    int interp = h->mod > 0.0f;
    hl_targets(h, n / 2u);
    interp |= h->len[0] != (float)(uint32_t)h->len[0];        /* SIZE gliding: fractional lengths */
    for (i = 0; i < n; i += 2) {
        float x0 = 0.5f * (wl[i] + wr[i]), x1 = 0.5f * (wl[i + 1] + wr[i + 1]);
        float x = 0.25f * h->xprev + 0.5f * x0 + 0.25f * x1;
        h->xprev = x1;
        h->in_g += (h->in_t - h->in_g) * 0.001f;
        float ol, orr;
        hl_step(h, x * h->in_g, &ol, &orr, interp);
        wl[i] = h->effg * wl[i] + h->revg * 0.5f * (h->ol0 + ol);
        wr[i] = h->effg * wr[i] + h->revg * 0.5f * (h->or0 + orr);
        wl[i + 1] = h->effg * wl[i + 1] + h->revg * ol;
        wr[i + 1] = h->effg * wr[i + 1] + h->revg * orr;
        h->ol0 = ol; h->or0 = orr;
    }
}
#endif
