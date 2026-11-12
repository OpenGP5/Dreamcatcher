/* Screen mock-ups on the computer: the real controls.c + ui.c + firmware.c render states into a PNG sheet.
 * Run by ./test.sh (writes build/screens.png). Needs zlib. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include "gp5_firmware.h"
#include "../controls.h"

#define W 160
#define H 80
#define K 3
#define PAD 12
#define COLS 2
#define MAXSHOTS 16

static uint16_t fb[W * H];
static uint8_t shots[MAXSHOTS][W * H * 3];
static int nshots;

void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px)
{
    for (unsigned j = 0; j < h; j++) for (unsigned i = 0; i < w; i++) fb[(y + j) * W + x + i] = px[j * w + i];
}
void gp5_text(unsigned line, const char *s) { (void)line; (void)s; }

static void shot(unsigned knob)
{
    for (int f = 1; fw_draw(knob, f); f = 0) {}
    for (unsigned i = 0; i < W * H; i++) {
        unsigned v = fb[i], r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
        shots[nshots][3 * i] = (uint8_t)(r | r >> 5); shots[nshots][3 * i + 1] = (uint8_t)(g | g >> 6);
        shots[nshots][3 * i + 2] = (uint8_t)(b | b >> 5);
    }
    nshots++;
}

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = { v >> 24, v >> 16, v >> 8, v }; fwrite(b, 1, 4, f); }
static void chunk(FILE *f, const char *t, const uint8_t *d, uint32_t n)
{
    put32(f, n); fwrite(t, 1, 4, f); if (n) fwrite(d, 1, n, f);
    uint32_t c = crc32(0, (const uint8_t *)t, 4); c = crc32(c, d, n); put32(f, c);
}

static void knob_to(unsigned v) { fw_knob(v); fw_knob(v); }
static void turn_to(unsigned item) { while (dc_ui_sel() != item) fw_para_turn(1); }

int main(int argc, char **argv)
{
    fw_init();
    knob_to(200);
    shot(200);                                          /* on, MIX chosen, knob waiting */
    fw_knob(150); shot(150);                            /* MIX follows the knob (gold) */
    turn_to(3); knob_to(150); fw_knob(210); shot(210);  /* TIME */
    turn_to(5); knob_to(210); fw_knob(90); shot(90);    /* CHANCE below noon */
    turn_to(7); for (int i = 0; i < 300; i++) fw_knob(90); shot(90);   /* RATE, pulse fading */
    turn_to(9); shot(90);                               /* SHAPE, waiting */
    turn_to(11); knob_to(90); fw_knob(230); shot(230);  /* DISSOLVE reverse */
    turn_to(12); knob_to(230); fw_knob(60); shot(60);   /* FILTER low-pass */
    fw_foot(); turn_to(15); shot(60);                   /* loop caught, SPEED */
    turn_to(13); knob_to(60); fw_knob(240); turn_to(15); knob_to(240); fw_knob(100); shot(100);  /* STRETCH, DRIFT */
    fw_para_push(); turn_to(1); shot(100);              /* off, CLOCK */

    int rows = (nshots + COLS - 1) / COLS, sw = COLS * (W * K + PAD) + PAD, sh = rows * (H * K + PAD) + PAD;
    static uint8_t img[(COLS * (W * K + PAD) + PAD) * 3 * ((MAXSHOTS / COLS) * (H * K + PAD) + PAD)];
    static uint8_t raw[sizeof img + 4096];
    for (int i = 0; i < sw * sh; i++) { img[3 * i] = 34; img[3 * i + 1] = 30; img[3 * i + 2] = 42; }
    for (int n = 0; n < nshots; n++) {
        int ox = PAD + (n % COLS) * (W * K + PAD), oy = PAD + (n / COLS) * (H * K + PAD);
        for (int y = 0; y < H * K; y++) for (int x = 0; x < W * K; x++)
            memcpy(img + 3 * ((oy + y) * sw + ox + x), shots[n] + 3 * ((y / K) * W + x / K), 3);
    }
    uLong rn = 0;
    for (int y = 0; y < sh; y++) { raw[rn++] = 0; memcpy(raw + rn, img + 3 * y * sw, 3 * sw); rn += 3 * sw; }
    static uint8_t z[sizeof raw + 65536]; uLongf zn = sizeof z;
    compress2(z, &zn, raw, rn, 9);
    FILE *f = fopen(argc > 1 ? argv[1] : "dc_mock.png", "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13] = { 0 };
    ih[0] = sw >> 24; ih[1] = sw >> 16; ih[2] = sw >> 8; ih[3] = sw; ih[4] = sh >> 24; ih[5] = sh >> 16; ih[6] = sh >> 8; ih[7] = sh;
    ih[8] = 8; ih[9] = 2;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, zn); chunk(f, "IEND", NULL, 0);
    fclose(f);
    printf("%d screens\n", nshots);
    return 0;
}
