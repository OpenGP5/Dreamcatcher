#include "ui.h"
#include "font5x7.h"
#include "gp5_font.h"
#include "fonts/sans_16.h"
#include "fonts/sans_24.h"

/* Pixel format: the pedal's panel is BGR 5-6-5, high byte first (UI_PANEL); the computer gets RGB 5-6-5. */
#ifdef UI_PANEL
#define RGB565(r, g, b) ((uint16_t)((((b) >> 3) << 11) | (((g) >> 2) << 5) | ((r) >> 3)))
#define UI_RGB(r, g, b) ((uint16_t)((RGB565(r, g, b) >> 8) | (RGB565(r, g, b) << 8)))
#else
#define UI_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#endif

typedef struct { uint8_t r, g, b; } rgb_t;
static const rgb_t BG = { 18, 9, 30 }, TEXT = { 246, 238, 255 }, DIM = { 150, 124, 182 }, FAINT = { 64, 42, 88 },
                   TRACK = { 44, 26, 64 }, LIVE = { 255, 206, 120 }, VIOLET = { 150, 100, 255 },
                   PINK = { 255, 120, 190 }, LILAC = { 206, 182, 255 };
static const rgb_t SEC[5] = {
    { 206, 182, 255 },   /* MAIN  lilac */
    { 150, 100, 255 },   /* WET   violet */
    { 226, 104, 218 },   /* MOD   orchid */
    { 116, 128, 255 },   /* COLOR indigo */
    { 255, 120, 190 },   /* LOOP  pink */
};

static uint16_t c16(rgb_t c) { return UI_RGB(c.r, c.g, c.b); }
static rgb_t mixc(rgb_t a, rgb_t b, unsigned t, unsigned n)       /* a .. b, t of n */
{
    rgb_t c;
    c.r = (uint8_t)(a.r + ((int)b.r - (int)a.r) * (int)t / (int)n);
    c.g = (uint8_t)(a.g + ((int)b.g - (int)a.g) * (int)t / (int)n);
    c.b = (uint8_t)(a.b + ((int)b.b - (int)a.b) * (int)t / (int)n);
    return c;
}

#define BAND_MAX 25
static uint16_t band[UI_W * BAND_MAX];
static unsigned bx0, by0, bw, bh;

static void px(int x, int y, uint16_t c)
{
    if (x < (int)bx0 || x >= (int)(bx0 + bw) || y < (int)by0 || y >= (int)(by0 + bh)) return;
    band[(unsigned)(y - (int)by0) * bw + (unsigned)(x - (int)bx0)] = c;
}
static void rect(int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) px(x + i, y + j, c);
}
static int in_pill(int i, int j, int w, int h, int r)
{
    int dx = i < r ? r - 1 - i : (i >= w - r ? i - (w - r) : -1);
    int dy = j < r ? r - 1 - j : (j >= h - r ? j - (h - r) : -1);
    return !(dx >= 0 && dy >= 0 && 4 * (dx * dx + dy * dy) > (2 * r - 1) * (2 * r - 1));
}
static void pill(int x, int y, int w, int h, int r, uint16_t c)
{
    int i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) if (in_pill(i, j, w, h, r)) px(x + i, y + j, c);
}
static int text_w(const char *s, int k)
{
    int n = 0;
    while (s[n]) n++;
    return n ? n * 6 * k - k : 0;
}
static void text(int x, int y, const char *s, int k, uint16_t c)
{
    for (; *s; s++, x += 6 * k) {
        unsigned ch = (unsigned char)*s, col, row;
        if (ch < 0x20 || ch > GP5_FONT5X7_LAST) ch = '?';
        for (col = 0; col < 5; col++)
            for (row = 0; row < 7; row++)
                if ((font[ch - 0x20][col] >> row) & 1) rect(x + (int)col * k, y + (int)row * k, k, k, c);
    }
}
/* Large text: the platform's smooth "sans" font (OpenGP5 gp5_font.h), blended into the band. Placed by baseline:
 * gp5_draw_text takes the top of the line, relative to the band. */
#define NAME_FONT  (&gp5_font_sans_16)
#define VALUE_FONT (&gp5_font_sans_24)
static int aa_width(const gp5_font_t *f, const char *s) { return gp5_text_width(f, s); }
static void aa_text(int x, int baseline, const char *s, const gp5_font_t *f, rgb_t fg)
{
    gp5_draw_text(band, bw, bh, x - (int)bx0, baseline - (int)f->base - (int)by0, f, s,
                  ((uint32_t)fg.r << 16) | ((uint32_t)fg.g << 8) | fg.b);
}

static void begin(unsigned x0, unsigned y0, unsigned w, unsigned h)
{
    unsigned i;
    bx0 = x0; by0 = y0; bw = w; bh = h;
    for (i = 0; i < w * h; i++) band[i] = c16(BG);
}
static void send(void) { ui_blit(bx0, by0, bw, bh, band); }

static uint32_t hash_str(uint32_t h, const char *s) { while (*s) h = h * 31u + (unsigned char)*s++; return h * 31u + 7u; }

/* the emblem: a ring with an eight-spoke web, a bead in the middle, three feathers */
static void emblem(int on, int caught)
{
    int x, y;
    uint16_t ring = c16(on ? LILAC : DIM), web = c16(on ? mixc(FAINT, LILAC, 1, 2) : FAINT);
    uint16_t bead = c16(on ? (caught ? PINK : VIOLET) : FAINT), feather = c16(on ? PINK : FAINT);
    for (y = 1; y <= 12; y++) for (x = 3; x <= 15; x++) {
        int dx = x - 9, dy = y - 6, d2 = dx * dx + dy * dy;
        if (d2 >= 22 && d2 <= 36) px(x, y, ring);
        else if (d2 < 22 && d2 > 2 && (dx == 0 || dy == 0 || dx == dy || dx == -dy)) px(x, y, web);
        else if (d2 >= 5 && d2 <= 6) px(x, y, web);
    }
    px(9, 6, bead); px(8, 6, bead); px(10, 6, bead); px(9, 5, bead); px(9, 7, bead);
    rect(6, 12, 1, 3, feather); rect(9, 13, 1, 2, feather); rect(12, 12, 1, 3, feather);
}

static void orb(unsigned pulse, int on, int caught)
{
    int i, j;
    rgb_t hot = caught ? PINK : VIOLET;
    uint16_t c = c16(on ? mixc(FAINT, hot, pulse, 4) : FAINT);
    uint16_t halo = c16(on ? mixc(BG, hot, pulse, 10) : BG);
    for (j = 0; j < 9; j++) for (i = 0; i < 9; i++) {
        int dx = i - 4, dy = j - 4, d2 = dx * dx + dy * dy;
        if (d2 <= 6) px(108 + i, 3 + j, c);
        else if (d2 <= 16) px(108 + i, 3 + j, halo);
    }
}

int ui_draw(const ui_state_t *s, int force)
{
    static uint32_t last[6];
    static uint8_t valid[6];
    rgb_t accent = SEC[s->sec < 5 ? s->sec : 0];
    uint32_t h;
    int x, i;

    if (force) for (i = 0; i < 6; i++) valid[i] = 0;

    /* header, rows 0..14 */
    h = 1u + s->on + 2u * s->caught;
    if (!valid[0] || h != last[0]) {
        const char *st = !s->on ? "OFF" : s->caught ? "LOOP" : "ON";
        rgb_t pc = !s->on ? FAINT : s->caught ? PINK : VIOLET;
        int w = text_w(st, 1) + 10;
        last[0] = h; valid[0] = 1; valid[5] = 0;
        begin(0, 0, UI_W, 15);
        emblem(s->on, s->caught);
        text(20, 4, "DREAMCATCHER", 1, c16(s->on ? TEXT : DIM));
        pill(155 - w, 2, w, 11, 5, c16(pc));
        text(160 - w, 4, st, 1, c16(s->on ? BG : DIM));
        send();
        return 1;
    }

    /* section pill + setting name, rows 15..31 */
    h = hash_str(hash_str(s->on + 2u * s->sec, s->name), s->sec_name);
    if (!valid[1] || h != last[1]) {
        int w = text_w(s->sec_name, 1) + 8;
        last[1] = h; valid[1] = 1;
        begin(0, 15, UI_W, 17);
        pill(6, 18, w, 11, 5, c16(s->on ? accent : FAINT));
        text(10, 20, s->sec_name, 1, c16(s->on ? BG : DIM));
        aa_text(6 + w + 6, 28, s->name, NAME_FONT, s->on ? TEXT : DIM);
        send();
        return 1;
    }

    /* value, rows 32..56 */
    h = hash_str(s->on + 2u * s->live + 4u * s->sec, s->value);
    if (!valid[2] || h != last[2]) {
        last[2] = h; valid[2] = 1;
        begin(0, 32, UI_W, 25);
        x = 154 - aa_width(VALUE_FONT, s->value);
        if (x >= 12) rect(6, 35, 2, 19, c16(s->on ? accent : FAINT));    /* section stripe, when the value leaves room */
        aa_text(x, 52, s->value, VALUE_FONT, s->live ? LIVE : (s->on ? TEXT : DIM));
        send();
        return 1;
    }

    /* bar, rows 57..68: violet-to-pink fill; knob marker below while the setting waits for the knob */
    h = s->frac + 256u * s->knob + 65536u * (s->live + 2u * s->on);
    if (!valid[3] || h != last[3]) {
        int fill = 6 + (int)(s->frac * 142u + 127u) / 255;
        int kx = 6 + 3 + (int)(s->knob * 142u + 127u) / 255, j;
        last[3] = h; valid[3] = 1;
        begin(0, 57, UI_W, 12);
        pill(6, 59, 148, 6, 3, c16(TRACK));
        for (x = 0; x < fill; x++) {
            uint16_t c = c16(s->live ? LIVE : (s->on ? mixc(VIOLET, PINK, (unsigned)x, 147) : DIM));
            for (j = 0; j < 6; j++) if (in_pill(x, j, fill, 6, 3)) px(6 + x, 59 + j, c);
        }
        if (!s->live) { rect(kx - 1, 67, 3, 1, c16(DIM)); rect(kx, 66, 1, 1, c16(DIM)); }
        send();
        return 1;
    }

    /* footer, rows 69..79: dots per setting, a gap between sections */
    h = s->sel + 256u * s->count + 65536u * s->on;
    if (!valid[4] || h != last[4]) {
        int total = 0;
        last[4] = h; valid[4] = 1;
        begin(0, 69, UI_W, 11);
        for (i = 0; i < (int)s->count; i++) {
            total += i == (int)s->sel ? 12 : 6;
            if (i + 1 < (int)s->count && s->secs[i + 1] != s->secs[i]) total += 5;
        }
        x = (UI_W - total + 2) / 2;
        for (i = 0; i < (int)s->count; i++) {
            rgb_t c = SEC[s->secs[i] < 5 ? s->secs[i] : 0];
            if (i == (int)s->sel) { pill(x, 72, 10, 4, 2, c16(s->on ? c : DIM)); x += 12; }
            else { pill(x, 72, 4, 4, 2, c16(s->on ? mixc(BG, c, 2, 5) : FAINT)); x += 6; }
            if (i + 1 < (int)s->count && s->secs[i + 1] != s->secs[i]) x += 5;
        }
        send();
        return 1;
    }

    /* the pulse, its own 9 x 9 square inside the header */
    h = s->pulse + 8u * s->on + 16u * s->caught;
    if (!valid[5] || h != last[5]) {
        last[5] = h; valid[5] = 1;
        begin(108, 3, 9, 9);
        orb(s->pulse, s->on, s->caught);
        send();
        return 1;
    }
    return 0;
}
