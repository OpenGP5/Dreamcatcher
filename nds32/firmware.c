/* Dreamcatcher, control half (platform interface: opengp5/platform/nds32/gp5_firmware.h).
 * The controls are controls.c, the screen ui.c, the sound engine.c; this file connects them to the platform. */
#include "gp5_firmware.h"
#include "gp5_params.h"                                    /* (includes ../params.h: GP5_PARAMS, the DC_* ids) */
#include "../params.h"
#include "controls.h"
#include "ui.h"
#include "engine.h"

static uint32_t pulse_ph;          /* LFO phase on this side, 0..9999, advanced every 10 ms */
static unsigned pulse_age = 100;   /* 10 ms ticks since the last cycle started */
static unsigned ticks;             /* 10 ms ticks since start-up (the screen's scrolling) */

#define STORE_ID 0x434D5244u                               /* "DRMC" */
unsigned dc_persist_load(void *dst, unsigned max) { return gp5_store_load(STORE_ID, dst, max); }
int dc_persist_save(const void *src, unsigned len) { return gp5_store_save(STORE_ID, src, len); }
/* (its id, "dreamcatcher", which owns the storage, is in ../firmware.json) */
/* The computer (OpenGP5 Launchpad) rewrote the presets: load them again, starting on its "last" slot. */
void fw_storage_changed(void) { dc_ui_reload(); }

/* A setting changed in controls.c: the engine (here) and the platform's parameters (the DSP's go to the DSP) */
void dc_on_set(unsigned id, uint32_t value)
{
    dc_engine_set(id, value);
    gp5_param_set(id, (int32_t)value);
}
/* The computer set a parameter (OpenGP5 1.0, 7E 10): through the controls, as from the pedal */
void fw_param_changed(unsigned id) { dc_ui_param_set(id, (uint32_t)gp5_param(id)); }

void fw_init(void)
{
    unsigned i;
    uint32_t v;
    dc_engine_start(); dc_ui_init(); dc_ui_restore();      /* start on the last-used preset */
    for (i = 0; i < DC_NPARAM; i++) if (dc_ui_param_get(i, &v)) dc_on_set(i, v);   /* everything, once */
    gp5_level_own(1);                                       /* VOLUME is part of a preset: the knob takes it over */
}
void fw_foot_down(void) { dc_ui_foot_down(); }             /* catch / let go of a micro-loop (its own timing) */
void fw_foot_up(void) { dc_ui_foot_up(); }
void fw_para_down(void) { dc_ui_para_down(); }             /* click: save / load a preset; hold 3 s: on / off */
void fw_para_up(void) { dc_ui_para_up(); }
void fw_para_turn(int detents) { dc_ui_turn(detents); }    /* choose the setting */
/* live access from the computer beyond the parameters (Launchpad's live editor): VOLUME, PRESET, USED, CHANGED,
 * SAVE, FORMAT (params.h, DC_P_*, ids 0x100..). The parameters themselves (ids 0..33) are the platform's. */
int fw_param_get(uint32_t id, uint32_t *value) { return dc_ui_param_get(id, value); }
int fw_param_set(uint32_t id, uint32_t value) { return dc_ui_param_set(id, value); }
/* LEDs: magenta-violet when on (red and blue full, a touch of green: the LEDs' blue is strong, so the
 * screen's violet linearised looked blue on the pedal); dim red when off. */
static uint32_t led_now = 99;
static void led_update(void) { uint32_t c = dc_ui_on() ? GP5_LED_RGB(32, 2, 32) : GP5_LED_BYPASS; if (c != led_now) { led_now = c; gp5_led(c); } }

void fw_tick(void)                                         /* every 10 ms */
{
    dc_ui_knob(gp5_knob());
    dc_ui_tick();
    gp5_level_set(dc_ui_level());
    led_update();
    ticks++;
    /* The pulse follows the RATE setting (not the DSP's glitch jumps): a flash at each cycle, then a fade. */
    pulse_ph += dc_ui_rate_x100();
    if (pulse_ph >= 10000u) { pulse_ph %= 10000u; pulse_age = 0; }
    else if (pulse_age < 100) pulse_age++;
}

int fw_draw(int force)
{
    static uint8_t secs[32];
    unsigned i, n = dc_ui_count();
    ui_state_t s;
    for (i = 0; i < n && i < 32; i++) secs[i] = (uint8_t)dc_ui_section(i);
    s.name = dc_ui_name();
    dc_ui_value(s.value);
    s.frac = (uint8_t)dc_ui_frac();
    s.knob = (uint8_t)gp5_knob();
    s.live = (uint8_t)(dc_ui_live() || dc_ui_overlay());     /* gold while saving, too */
    s.on = (uint8_t)dc_ui_on();
    s.caught = (uint8_t)dc_ui_caught();
    s.sel = (uint8_t)dc_ui_sel();
    s.count = (uint8_t)n;
    s.sec = (uint8_t)dc_ui_section(s.sel);
    s.sec_name = dc_ui_section_name(s.sec);
    s.secs = secs;
    s.ticks = ticks;
    s.pulse = (uint8_t)(pulse_age < 6 ? 4 : pulse_age < 10 ? 3 : pulse_age < 15 ? 2 : pulse_age < 22 ? 1 : 0);
    return ui_draw(&s, force);
}

void ui_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px) { gp5_blit(x, y, w, h, px); }
