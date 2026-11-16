/* Presets, PARA and foot switch timing with the real controls.c and an in-memory store. Run by ./test.sh. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../controls.h"
#include "../../params.h"

static uint8_t store[1024]; static unsigned store_len; static int saves;
unsigned dc_persist_load(void *dst, unsigned max) { memcpy(dst, store, store_len < max ? store_len : max); return store_len; }
int dc_persist_save(const void *src, unsigned len) { memcpy(store, src, len); store_len = len; saves++; return 0; }

static uint32_t dsp[DC_NPARAM];
static void drain(void) { uint32_t id, v; while (dc_ui_take(&id, &v)) dsp[id] = v; }
static void ticks(unsigned n) { while (n--) dc_ui_tick(); }
static void knob(unsigned v) { dc_ui_knob(v); dc_ui_knob(v); }
static void go(unsigned item) { while (dc_ui_sel() != item) dc_ui_turn(1); }
static void set_item(unsigned item, unsigned from, unsigned to) { go(item); knob(from); dc_ui_knob(to); dc_ui_knob(to); }
static void para(unsigned n) { dc_ui_para_down(); ticks(n); dc_ui_para_up(); ticks(5); drain(); }
static void foot(unsigned n) { dc_ui_foot_down(); ticks(n); dc_ui_foot_up(); ticks(5); drain(); }
static const char *value(void) { static char v[16]; dc_ui_value(v); return v; }

enum { I_PRESET, I_MIX, I_CLOCK, I_VOLUME, I_TIME };

int main(void)
{
    dc_ui_init(); dc_ui_restore(); dc_ui_resend_all(); drain();
    assert(dc_ui_sel() == I_PRESET && dc_ui_preset() == 0 && store_len == 0 && dsp[DC_ON] == 1);
    assert(strcmp(value(), "1 EMPTY") == 0);

    /* foot: a press catches at once (silent while down), a tap plays it, the next tap lets it go */
    dc_ui_foot_down(); drain(); assert(dsp[DC_CATCH] == 2 && dc_ui_caught() == 0);
    dc_ui_foot_up(); ticks(5); drain(); assert(dsp[DC_CATCH] == 1 && dc_ui_caught() == 1);
    foot(5); assert(dsp[DC_CATCH] == 0 && dsp[DC_ON] == 1);
    /* held while listening: freeze until release, listening again (no loop) */
    dc_ui_foot_down(); ticks(50); drain(); assert(dsp[DC_FREEZE] == 1 && dsp[DC_CATCH] == 0 && dc_ui_caught() == 3);
    ticks(400); dc_ui_foot_up(); ticks(5); drain(); assert(dsp[DC_FREEZE] == 0 && dsp[DC_CATCH] == 0 && dsp[DC_ON] == 1);
    /* held while a loop plays: overdub until release, the loop stays */
    foot(5); assert(dsp[DC_CATCH] == 1);
    dc_ui_foot_down(); ticks(50); drain(); assert(dsp[DC_OVERDUB] == 1 && dsp[DC_CATCH] == 1 && dc_ui_caught() == 2);
    dc_ui_foot_up(); ticks(5); drain(); assert(dsp[DC_OVERDUB] == 0 && dsp[DC_CATCH] == 1);
    foot(5); assert(dsp[DC_CATCH] == 0 && dsp[DC_FREEZE] == 0);

    /* PARA held 3 s: on / off, the looper untouched (caught stays caught) */
    foot(5); assert(dsp[DC_CATCH] == 1);
    dc_ui_para_down(); ticks(150); assert(strcmp(dc_ui_name(), "OFF IN") == 0 && strcmp(value(), "BYPASS") == 0);
    ticks(200); dc_ui_para_up(); ticks(5); drain();
    assert(dsp[DC_ON] == 0 && dsp[DC_CATCH] == 1 && saves == 0);      /* off, no save */
    foot(5); assert(dsp[DC_ON] == 0 && dsp[DC_CATCH] == 0);             /* foot still loops while off */
    para(350); assert(dsp[DC_ON] == 1 && saves == 0);
    para(150); assert(saves == 0);                                     /* 1.5 s: neither click nor hold */

    /* bounces: one catch, not three; a very short click does not stick */
    dc_ui_foot_down(); ticks(1); dc_ui_foot_up(); ticks(1); dc_ui_foot_down(); ticks(8); dc_ui_foot_up();
    ticks(1); dc_ui_foot_down(); ticks(1); dc_ui_foot_up(); ticks(10); drain();
    assert(dsp[DC_CATCH] == 1);
    foot(5);
    dc_ui_para_down(); ticks(1); dc_ui_para_up(); ticks(400); drain(); assert(dsp[DC_ON] == 1);

    /* click: saves the active preset (slot 1), on any page */
    { int base = saves;
      set_item(I_TIME, 150, 40); set_item(I_VOLUME, 255, 200); para(5);
      assert(saves == base + 1 && strcmp(dc_ui_name(), "SAVED") == 0 && strcmp(value(), "P1") == 0 && dsp[DC_ON] == 1); }
    go(I_PRESET); assert(strcmp(value(), "P1") == 0);

    /* PRESET: the knob only highlights; a click on an empty highlighted slot saves the sound there */
    knob(0); dc_ui_knob(40); dc_ui_knob(40); drain();
    assert(dc_ui_preset() == 0 && strcmp(value(), "2 EMPTY") == 0 && dsp[DC_TIME] == 40);
    set_item(I_TIME, 40, 90); go(I_PRESET); assert(strcmp(value(), "P1*") == 0);
    knob(10); dc_ui_knob(40); dc_ui_knob(40); para(5);
    assert(dc_ui_preset() == 1 && strcmp(value(), "P2") == 0);

    /* an accidental bump highlights P1 but keeps the sound; a click loads it */
    set_item(I_TIME, 90, 120); go(I_PRESET); knob(40); dc_ui_knob(10); dc_ui_knob(10); drain();
    assert(strcmp(value(), "LOAD P1") == 0 && dc_ui_preset() == 1 && dsp[DC_TIME] == 120);
    { int base = saves; para(5);
      assert(dc_ui_preset() == 0 && dsp[DC_TIME] == 40 && dc_ui_level() == 200 && strcmp(dc_ui_name(), "LOADED") == 0);
      assert(saves == base + 1); }                                     /* remembers the slot to start on */
    ticks(200); assert(strcmp(value(), "P1") == 0);
    /* slot 2 still holds TIME 90 (the 120 was never saved) */
    knob(10); dc_ui_knob(40); dc_ui_knob(40); para(5); drain(); assert(dc_ui_preset() == 1 && dsp[DC_TIME] == 90);

    /* power cycle: starts on the last slot; on / off and the loop are not part of a preset */
    foot(5); drain(); assert(dsp[DC_CATCH] == 1);
    dc_ui_init(); dc_ui_restore(); dc_ui_resend_all(); drain();
    assert(dc_ui_preset() == 1 && dsp[DC_TIME] == 90 && dsp[DC_ON] == 1 && dsp[DC_CATCH] == 0);

    /* 1.0 / 1.1 presets (version 1, 19 settings + VOLUME per slot) load, the new settings at their defaults */
    { static const uint32_t def[DC_NPARAM] = DC_DEFAULTS;
      memset(store, 0, sizeof store); store[0] = 1; store[1] = 1; store[2] = 3;   /* last = slot 2, slots 1 + 2 used */
      for (unsigned k = 0; k < 2; k++) { for (unsigned i = 0; i < 19; i++) store[4 + k * 20 + i] = (uint8_t)def[i];
                                         store[4 + k * 20 + DC_TIME] = (uint8_t)(70 + k); store[4 + k * 20 + 19] = 180; }
      store_len = 4 + 8 * 20;
      dc_ui_init(); dc_ui_restore(); dc_ui_resend_all(); drain();
      assert(dc_ui_preset() == 1 && dsp[DC_TIME] == 71 && dc_ui_level() == 180 && dsp[DC_GRAIN] == def[DC_GRAIN]);
      assert(strcmp(value(), "P2") == 0);
      assert(dsp[DC_RMIX] == 0 && dsp[DC_RDECAY] == def[DC_RDECAY]);           /* no reverb: they sound as saved */
      para(5); assert(store[0] == 3 && store_len == 4 + 8 * (DC_NPARAM + 1)); }  /* saved again as version 3 */

    /* 1.2 presets from before the reverb (version 2, 28 settings + VOLUME) load, the reverb off */
    { static const uint32_t def[DC_NPARAM] = DC_DEFAULTS;
      memset(store, 0, sizeof store); store[0] = 2; store[1] = 0; store[2] = 1;
      for (unsigned i = 0; i < 28; i++) store[4 + i] = (uint8_t)def[i];
      store[4 + DC_GRAIN] = 33; store[4 + 28] = 150;
      store_len = 4 + 8 * 29;
      dc_ui_init(); dc_ui_restore(); dc_ui_resend_all(); drain();
      assert(dc_ui_preset() == 0 && dsp[DC_GRAIN] == 33 && dc_ui_level() == 150 && dsp[DC_RMIX] == 0);
      assert(dsp[DC_RSIZE] == def[DC_RSIZE] && dsp[DC_RSHIMMER] == def[DC_RSHIMMER]); }

    /* live access from the computer (7E 10 / 7E 11) */
    { uint32_t v;
      dc_ui_init(); memset(store, 0, sizeof store); store_len = 0; drain();
      assert(dc_ui_param_set(DC_TIME, 77) && (drain(), dsp[DC_TIME] == 77) && dc_ui_param_get(DC_TIME, &v) && v == 77);
      assert(dc_ui_param_set(DC_RMIX, 999) && dc_ui_param_get(DC_RMIX, &v) && v == 255);          /* clamped */
      assert(!dc_ui_param_set(DC_CATCH, 1) && !dc_ui_param_set(DC_FREEZE, 1));                  /* the foot's */
      assert(dc_ui_param_set(DC_P_VOLUME, 123) && dc_ui_level() == 123);
      assert(dc_ui_param_get(DC_P_USED, &v) && v == 0 && !dc_ui_param_set(DC_P_PRESET, 3));      /* empty: no */
      assert(dc_ui_param_set(DC_P_SAVE, 3) && store[0] == 3 && dc_ui_param_get(DC_P_USED, &v) && v == 8);
      assert(dc_ui_param_get(DC_P_PRESET, &v) && v == 3 && dc_ui_param_get(DC_P_CHANGED, &v) && v == 0);
      dc_ui_param_set(DC_TIME, 10); assert(dc_ui_param_get(DC_P_CHANGED, &v) && v == 1);
      assert(dc_ui_param_set(DC_P_PRESET, 3) && (drain(), dsp[DC_TIME] == 77));                   /* load brings it back */
      assert(!dc_ui_param_get(0x200, &v)); }

    /* a record from another version is ignored */
    store[0] = 9; dc_ui_init(); dc_ui_restore(); assert(dc_ui_preset() == 0);
    puts("ALL OK: foot tap / hold (catch, freeze, overdub), PARA click save / load, PARA hold on / off, bounces, power cycle, 1.1 and early-1.2 presets, live access");
    return 0;
}
