/* Sync: TIME, RATE, LENGTH (TAPE) and GRAIN as note values of the preset's tempo (SYNC on, BPM 40..240).
 * Shared by the engine (dream.h), the pedal's screen (nds32/controls.c) and the tests, so they always agree.
 *
 * Each control offers only the note values that fit at the current tempo and CLOCK (the delay and the loop memory
 * last longer at a lower CLOCK); its 0..255 setting is spread evenly over them. Lengths are in beats (a quarter note
 * is one beat). */
#ifndef DC_SYNC_H
#define DC_SYNC_H
#include <stdint.h>

#define DC_SYNC_NOTES 19
static const struct { uint8_t num, den; const char *name; } dc_note[DC_SYNC_NOTES] = {
    { 1, 16, "1/64" },  { 1, 12, "1/32T" }, { 1, 8, "1/32" },  { 1, 6, "1/16T" }, { 3, 16, "1/32." },
    { 1, 4, "1/16" },   { 1, 3, "1/8T" },   { 3, 8, "1/16." }, { 1, 2, "1/8" },   { 2, 3, "1/4T" },
    { 3, 4, "1/8." },   { 1, 1, "1/4" },    { 4, 3, "1/2T" },  { 3, 2, "1/4." },  { 2, 1, "1/2" },
    { 3, 1, "1/2." },   { 4, 1, "1 BAR" },  { 8, 1, "2 BARS" }, { 16, 1, "4 BARS" },
};
#define DC_BEAT_UNITS (60u * 44100u)          /* beat clock counts per beat (it adds BPM each sample) */
#define DC_BEAT_WRAP (48u * DC_BEAT_UNITS)     /* = DC_SYNC_WRAP beats: 127,008,000, fits in 32 bits */
#define DC_SYNC_WRAP 48.0f                    /* beats: every note value above divides it, so the beat clock can wrap */

/* what syncs; for each, the note values it may take and the engine-step range it must land in */
enum { DC_SYNC_TIME, DC_SYNC_RATE, DC_SYNC_LENGTH, DC_SYNC_GRAIN };
static const struct { uint8_t lo, hi; float smin, smax; } dc_sync_kind[4] = {
    { 2, 16, 200.0f, 51136.0f },              /* TIME: 1/32 .. 1 bar; the delay's steps (DC_DMAX) */
    { 2, 18, 0.0f, 0.0f },                    /* RATE: one cycle per 1/32 .. 4 bars; no memory limit (Hz <= 20) */
    { 8, 18, 1024.0f, 64960.0f },             /* LENGTH: 1/8 .. 4 bars; the loop memory (DC_LFULL) */
    { 0, 5, 256.0f, 8192.0f },                /* GRAIN: 1/64 .. 1/16; the grain sizes the engine has */
};

static inline float dc_note_beats(unsigned i) { return (float)dc_note[i].num / (float)dc_note[i].den; }

/* engine steps of note i at bpm, with the engine running at 44100 * clock_ratio steps a second */
static inline float dc_note_steps(unsigned i, unsigned bpm, float clock_ratio)
{
    return dc_note_beats(i) * 60.0f / (float)bpm * 44100.0f * clock_ratio;
}

static inline int dc_note_fits(unsigned kind, unsigned i, unsigned bpm, float clock_ratio)
{
    if (kind == DC_SYNC_RATE) return (float)bpm / 60.0f / dc_note_beats(i) <= 20.0f;
    float st = dc_note_steps(i, bpm, clock_ratio);
    return st >= dc_sync_kind[kind].smin && st <= dc_sync_kind[kind].smax;
}

/* the note value a 0..255 setting picks: the values that fit are spread evenly over 0..255. If none fits (an extreme
 * tempo and CLOCK), the end of the range nearest to fitting. */
static inline unsigned dc_sync_note(unsigned kind, uint32_t x, unsigned bpm, float clock_ratio)
{
    unsigned lo = dc_sync_kind[kind].lo, hi = dc_sync_kind[kind].hi, first = 99u, n = 0, i;
    for (i = lo; i <= hi; i++) if (dc_note_fits(kind, i, bpm, clock_ratio)) { if (first == 99u) first = i; n++; }
    if (!n) return kind != DC_SYNC_RATE && dc_note_steps(lo, bpm, clock_ratio) > dc_sync_kind[kind].smax ? lo : hi;
    if (x > 255u) x = 255u;
    return first + (unsigned)(x * n / 256u);  /* (the fitting values are contiguous: lengths rise with i) */
}
#endif
