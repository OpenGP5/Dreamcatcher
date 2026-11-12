/* Dreamcatcher, control half (platform interface: opengp5/platform/nds32/gp5_firmware.h).
 * The controls are controls.c, the screen ui.c; this file connects them to the platform. */
#include "gp5_firmware.h"
#include "../params.h"
#include "controls.h"
#include "ui.h"

static uint32_t pulse_ph;          /* LFO phase on this side, 0..9999, advanced every 10 ms */
static unsigned pulse_age = 100;   /* 10 ms ticks since the last cycle started */

void fw_init(void) { dc_ui_init(); }
void fw_foot(void) { dc_ui_foot(); }                       /* catch / let go of a micro-loop */
void fw_para_push(void) { dc_ui_push(); }                  /* effect on / off */
void fw_para_turn(int detents) { dc_ui_turn(detents); }    /* choose the setting */
unsigned fw_level(void) { return dc_ui_level(); }
void fw_dsp_ready(void) { dc_ui_resend_all(); }
int fw_take(uint32_t *id, uint32_t *value) { return dc_ui_take(id, value); }
unsigned fw_setting_count(void) { return DC_NPARAM; }
/* LEDs: magenta-violet when on (red and blue full, a touch of green: the LEDs' blue is strong, so the
 * screen's violet linearised looked blue on the pedal); dim red when off. */
unsigned fw_active(void) { return dc_ui_on() ? GP5_LED(32, 2, 32) : 0; }

void fw_knob(unsigned knob)
{
    dc_ui_knob(knob);
    /* The pulse follows the RATE setting (not the DSP's glitch jumps): a flash at each cycle, then a fade. */
    pulse_ph += dc_ui_rate_x100();
    if (pulse_ph >= 10000u) { pulse_ph %= 10000u; pulse_age = 0; }
    else if (pulse_age < 100) pulse_age++;
}

int fw_draw(unsigned knob, int force)
{
    static uint8_t secs[32];
    unsigned i, n = dc_ui_count();
    ui_state_t s;
    for (i = 0; i < n && i < 32; i++) secs[i] = (uint8_t)dc_ui_section(i);
    s.name = dc_ui_name();
    dc_ui_value(s.value);
    s.frac = (uint8_t)dc_ui_frac();
    s.knob = (uint8_t)knob;
    s.live = (uint8_t)dc_ui_live();
    s.on = (uint8_t)dc_ui_on();
    s.caught = (uint8_t)dc_ui_caught();
    s.sel = (uint8_t)dc_ui_sel();
    s.count = (uint8_t)n;
    s.sec = (uint8_t)dc_ui_section(s.sel);
    s.sec_name = dc_ui_section_name(s.sec);
    s.secs = secs;
    s.pulse = (uint8_t)(pulse_age < 6 ? 4 : pulse_age < 10 ? 3 : pulse_age < 15 ? 2 : pulse_age < 22 ? 1 : 0);
    return ui_draw(&s, force);
}

void ui_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px) { gp5_blit(x, y, w, h, px); }
