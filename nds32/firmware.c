/* Dreamcatcher, control half (platform interface: opengp5/platform/nds32/gp5_firmware.h).
 * The controls are controls.c, the screen ui.c, the sound engine.c; this file connects them to the platform. */
#include "gp5_firmware.h"
#include "../params.h"
#include "controls.h"
#include "ui.h"
#include "engine.h"

static uint32_t pulse_ph;          /* LFO phase on this side, 0..9999, advanced every 10 ms */
static unsigned pulse_age = 100;   /* 10 ms ticks since the last cycle started */

#define STORE_ID 0x434D5244u                               /* "DRMC" */
unsigned dc_persist_load(void *dst, unsigned max) { return gp5_store_load(STORE_ID, dst, max); }
int dc_persist_save(const void *src, unsigned len) { return gp5_store_save(STORE_ID, src, len); }
const char *fw_name(void) { return "dreamcatcher"; }   /* the name Launchpad and the storage owner mark see */
/* The computer (OpenGP5 Launchpad) rewrote the presets: load them again, starting on its "last" slot. */
int fw_store_changed(uint32_t id) { if (id != STORE_ID) return 0; dc_ui_restore(); return 1; }

void fw_init(void) { dc_engine_start(); dc_ui_init(); dc_ui_restore(); }   /* start on the last-used preset */
void fw_foot(void) { dc_ui_foot_down(); }                  /* catch / let go of a micro-loop */
void fw_foot_release(void) { dc_ui_foot_up(); }
void fw_para_push(void) { dc_ui_para_down(); }             /* click: save / load a preset; hold 3 s: on / off */
void fw_para_release(void) { dc_ui_para_up(); }
void fw_para_turn(int detents) { dc_ui_turn(detents); }    /* choose the setting */
unsigned fw_level(void) { return dc_ui_level(); }
void fw_dsp_ready(void) { dc_ui_resend_all(); }
/* Each setting goes to the sound engine here (engine.c) and, as before, to the DSP, which uses ON and MIX for the
 * final mix and acknowledges all of them (the platform waits for that at start-up). */
int fw_take(uint32_t *id, uint32_t *value)
{
    if (!dc_ui_take(id, value)) return 0;
    dc_engine_set(*id, *value);
    return 1;
}
unsigned fw_setting_count(void) { return DC_NPARAM; }
/* live access from the computer (OpenGP5 0.5.4, USB 7E 10 / 7E 11): params.h, DC_P_* */
int fw_param_get(uint32_t id, uint32_t *value) { return dc_ui_param_get(id, value); }
int fw_param_set(uint32_t id, uint32_t value) { return dc_ui_param_set(id, value); }
/* LEDs: magenta-violet when on (red and blue full, a touch of green: the LEDs' blue is strong, so the
 * screen's violet linearised looked blue on the pedal); dim red when off. */
unsigned fw_active(void) { return dc_ui_on() ? GP5_LED(32, 2, 32) : 0; }

void fw_knob(unsigned knob)
{
    dc_ui_knob(knob);
    dc_ui_tick();
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
    s.live = (uint8_t)(dc_ui_live() || dc_ui_overlay());     /* gold while saving, too */
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
