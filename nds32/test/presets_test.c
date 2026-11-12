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

    /* foot: the looper only, at once; holding it does nothing else */
    dc_ui_foot_down(); drain(); assert(dsp[DC_CATCH] == 1);
    dc_ui_foot_up(); ticks(5); foot(5); assert(dsp[DC_CATCH] == 0 && dsp[DC_ON] == 1);
    foot(500); assert(dsp[DC_CATCH] == 1 && dsp[DC_ON] == 1);
    foot(5); assert(dsp[DC_CATCH] == 0);

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

    /* a record from another version is ignored */
    store[0] = 9; dc_ui_init(); dc_ui_restore(); assert(dc_ui_preset() == 0);
    puts("ALL OK: foot = looper only, PARA click save / load, PARA hold on / off, bounces, power cycle");
    return 0;
}
