/* Dreamcatcher, DSP half (platform interface: opengp5/platform/dsp/gp5_dsp.h). The sound is dream.h; the
 * settings are ../params.h. Link commands:
 *   0x7F10 set:  [10 7f][0c 00][id u32][value u32] -> ACK
 *   0x7F11 get:  [11 7f][04 00] -> [11 7f][len][19 settings][memory write index][engine steps]
 *                [cycles last block][cycles worst block][late blocks]   (all u32) */
#include "dream.h"

static dc_t dream;
static dc_large_t dream_large GP5_LARGE;

static void fw_init(void) { dc_init(&dream, &dream_large); }

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    dc_block(&dream, in, out_left, out_right, GP5_BLOCK);
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n)
{
    if (cmd == 0x7F10 && n >= 12) { dc_set(&dream, gp5_get32(p + 4), gp5_get32(p + 8)); gp5_ack(); return 1; }
    if (cmd == 0x7F11) {
        enum { N = DC_NPARAM + 5 };
        uint8_t r[4 + 4 * N] = { 0x11, 0x7F, (uint8_t)sizeof r, 0 };
        uint32_t v[N];
        for (uint32_t i = 0; i < DC_NPARAM; i++) v[i] = dream.p[i];
        v[DC_NPARAM] = dream.mw; v[DC_NPARAM + 1] = dream.steps;
        v[DC_NPARAM + 2] = GP5_CYCLES_LAST; v[DC_NPARAM + 3] = GP5_CYCLES_WORST; v[DC_NPARAM + 4] = GP5_LATE_BLOCKS;
        for (uint32_t i = 0; i < N; i++)
            for (uint32_t k = 0; k < 4; k++) r[4 + 4 * i + k] = (uint8_t)(v[i] >> (8 * k));
        gp5_reply(r, sizeof r);
        return 1;
    }
    return 0;
}
