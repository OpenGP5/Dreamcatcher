/* Dreamcatcher, DSP half (platform interface: opengp5/platform/dsp/gp5_dsp.h). Two processors: the sound
 * (dsp/dream.h) runs on the control processor, which has the memory and the speed for it; this side sends it the
 * guitar over the audio link and mixes what comes back with the dry signal (dc_mix): the dry path and the exact
 * bypass stay here, with no added delay. The hall reverb (hall.h) runs here too, on the effect's share, before
 * that mix. Words: link_words.h. Link commands:
 *   0x7F10 set:  [10 7f][0c 00][id u32][value u32] -> ACK   (ON, MIX and the reverb's six matter here; all are
 *                acknowledged)
 *   0x7F11 get:  [11 7f][len][ON][MIX][cycles last block][cycles worst block][late blocks][blocks without the
 *                control processor's words]   (all u32) */
#define GP5_AUDIO_LINK
#define GP5_LINK_WORDS 136u                /* 129 used (link_words.h); a multiple of 8 */
#include "dream.h"
#include "link_words.h"
#include "hall.h"

static dc_mixer_t mixer;
static hl_t hall;
static hl_large_t hall_large GP5_LARGE;
static float wl[GP5_BLOCK], wr[GP5_BLOCK];
static uint32_t p_on = 1, p_mix = 128, missed;

static void fw_init(void)
{
    static const uint32_t def[DC_NPARAM] = DC_DEFAULTS;
    mixer.on = 0.0f; mixer.mix = 0.0f;
    p_on = def[DC_ON]; p_mix = def[DC_MIX]; missed = 0;
    hl_init(&hall, &hall_large);
    for (uint32_t i = 0; i < 6; i++) hl_set(&hall, i, def[DC_RDECAY + i]);
}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    const int32_t *li = gp5_link_in();
    int32_t *lo = gp5_link_out();
    int ok = li[LW_MAGIC] == (int32_t)LW_MAGIC_N;
    uint32_t i;
    if (!ok) missed++;                     /* the control processor isn't there (yet): the dry signal alone */
    for (i = 0; i < GP5_BLOCK; i++) {
        wl[i] = ok ? (float)li[LW_WL + i] * (1.0f / LW_Q30) : 0.0f;
        wr[i] = ok ? (float)li[LW_WR + i] * (1.0f / LW_Q30) : 0.0f;
        lo[LW_IN + i] = in[i];
    }
    hl_block(&hall, wl, wr, GP5_BLOCK);
    for (i = 0; i < GP5_BLOCK; i++) dc_mix(&mixer, p_on, p_mix, in[i], wl[i], wr[i], &out_left[i], &out_right[i]);
    lo[LW_MAGIC] = (int32_t)LW_MAGIC_D;
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n)
{
    static const uint8_t max[DC_NPARAM] = DC_MAXES;
    if (cmd == 0x7F10 && n >= 12) {
        uint32_t id = gp5_get32(p + 4), v = gp5_get32(p + 8);
        if (id < DC_NPARAM && v > max[id]) v = max[id];
        if (id == DC_ON) p_on = v;
        if (id == DC_MIX) p_mix = v;
        if (id >= DC_RDECAY && id <= DC_RSHIMMER) hl_set(&hall, id - DC_RDECAY, v);
        gp5_ack();
        return 1;
    }
    if (cmd == 0x7F11) {
        uint32_t v[6] = { p_on, p_mix, GP5_CYCLES_LAST, GP5_CYCLES_WORST, GP5_LATE_BLOCKS, missed };
        uint8_t r[4 + 4 * 6] = { 0x11, 0x7F, (uint8_t)sizeof r, 0 };
        for (uint32_t i = 0; i < 6; i++)
            for (uint32_t k = 0; k < 4; k++) r[4 + 4 * i + k] = (uint8_t)(v[i] >> (8 * k));
        gp5_reply(r, sizeof r);
        return 1;
    }
    return 0;
}
