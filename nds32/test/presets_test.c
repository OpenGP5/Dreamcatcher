/* Presets, PARA and foot switch timing with the real controls.c and an in-memory store. Run by ./test.sh. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* a failed check reports and exits (no abort: macOS would show a crash report) */
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#include "../controls.h"
#include "../../params.h"

static uint8_t store[1024]; static unsigned store_len; static int saves;
unsigned dc_persist_load(void *dst, unsigned max) { memcpy(dst, store, store_len < max ? store_len : max); return store_len; }
int dc_persist_save(const void *src, unsigned len) { memcpy(store, src, len); store_len = len; saves++; return 0; }

static uint32_t dsp[DC_NPARAM];
void dc_on_set(unsigned id, uint32_t value) { (void)id; (void)value; }   /* (firmware.c's: the engine and the platform) */
static void drain(void) { uint32_t id, v; while (dc_ui_take(&id, &v)) dsp[id] = v; }
static void ticks(unsigned n) { while (n--) dc_ui_tick(); }
static void knob(unsigned v) { dc_ui_knob(v); dc_ui_knob(v); }
static void go(unsigned item) { while (dc_ui_sel() != item) dc_ui_turn(1); }
static void set_item(unsigned item, unsigned from, unsigned to) { go(item); knob(from); dc_ui_knob(to); dc_ui_knob(to); }
static void para(unsigned n) { dc_ui_para_down(); ticks(n); dc_ui_para_up(); ticks(5); drain(); }
static void foot(unsigned n) { dc_ui_foot_down(); ticks(n); dc_ui_foot_up(); ticks(5); drain(); }
static const char *value(void) { static char v[16]; dc_ui_value(v); return v; }

enum { I_PRESET, I_MIX, I_CLOCK, I_SYNC, I_BPM, I_VOLUME, I_TIME, I_GRAIN = 11, I_RATE = 16 };   /* (as nds32/controls.c lists them) */

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
      para(5); assert(store[0] == 5 && store_len == 4 + 8 * (DC_NPARAM + 1) + 8 * DC_NAME_LEN); }  /* saved again as version 5 */

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
      assert(dc_ui_param_set(DC_P_SAVE, 3) && store[0] == 5 && dc_ui_param_get(DC_P_USED, &v) && v == 8);
      assert(dc_ui_param_get(DC_P_PRESET, &v) && v == 3 && dc_ui_param_get(DC_P_CHANGED, &v) && v == 0);
      dc_ui_param_set(DC_TIME, 10); assert(dc_ui_param_get(DC_P_CHANGED, &v) && v == 1);
      assert(dc_ui_param_set(DC_P_PRESET, 3) && (drain(), dsp[DC_TIME] == 77));                   /* load brings it back */
      assert(!dc_ui_param_get(0x200, &v)); }

    /* names (version 4 and 5): shown for the slot; "PRESET P1" / "LOAD P2" above; kept when saving on the pedal; a rename
     * from the computer keeps the sound playing (unsaved changes included) */
    { const unsigned SN = 4 + 8 * (DC_NPARAM + 1), NM = SN;   /* names follow the slots */
      dc_ui_init(); memset(store, 0, sizeof store); store_len = 0; drain();
      dc_ui_param_set(DC_TIME, 33); assert(dc_ui_param_set(DC_P_SAVE, 0) && dc_ui_param_set(DC_P_SAVE, 1));
      assert(store[0] == 5 && store_len == SN + 8 * DC_NAME_LEN && dc_ui_preset() == 1);
      memcpy(store + NM, "Glass Pad", 9); memcpy(store + NM + DC_NAME_LEN, "Long name XY", 12);   /* P1 + P2 (12 characters: the most) */
      dc_ui_param_set(DC_TIME, 77); drain();                  /* unsaved change on P2 */
      dc_ui_reload(); drain();
      assert(dc_ui_preset() == 1 && dsp[DC_TIME] == 77);      /* same sound in P2: still playing, change kept */
      go(I_PRESET); knob(255); dc_ui_knob(255 / 8 * 1 + 2); dc_ui_knob(255 / 8 * 1 + 2);   /* highlight P2 (current) */
      assert(strcmp(dc_ui_name(), "PRESET P2") == 0 && strcmp(value(), "Long name XY*") == 0);
      dc_ui_knob(2); dc_ui_knob(2);                           /* highlight P1 */
      assert(strcmp(dc_ui_name(), "LOAD P1") == 0 && strcmp(value(), "Glass Pad") == 0);
      dc_ui_param_set(DC_P_SAVE, 1);                          /* saving keeps the name */
      assert(memcmp(store + NM + DC_NAME_LEN, "Long name XY", 12) == 0);
      store[4 + (DC_NPARAM + 1) * 1 + DC_TIME] = 99;          /* the computer changed P2's sound: it loads */
      dc_ui_reload(); drain(); assert(dsp[DC_TIME] == 99);
      { uint32_t v; assert(dc_ui_param_get(DC_P_FORMAT, &v) && v == 5); }
      /* version 4 (1.2 / 1.3: 34 settings + VOLUME, then names) and 3 (the same without names) load: SYNC off, 120 BPM */
      { static uint8_t v5[sizeof store], o[sizeof store]; unsigned k, i, n4 = 4 + 8 * 35;
        memcpy(v5, store, SN + 8 * DC_NAME_LEN);
        memcpy(o, v5, 4); o[0] = 4;
        for (k = 0; k < 8; k++) { for (i = 0; i < 34; i++) o[4 + k * 35 + i] = v5[4 + k * (DC_NPARAM + 1) + i];
                                  o[4 + k * 35 + 34] = v5[4 + k * (DC_NPARAM + 1) + DC_NPARAM]; }
        memcpy(o + n4, v5 + SN, 8 * DC_NAME_LEN);
        memcpy(store, o, n4 + 8 * DC_NAME_LEN); store_len = n4 + 8 * DC_NAME_LEN;
        dc_ui_init(); dc_ui_restore(); drain(); go(I_PRESET);
        assert(dc_ui_preset() == 1 && dsp[DC_TIME] == 99 && dsp[DC_SYNC] == 0 && dsp[DC_BPM] == 120);
        assert(strcmp(dc_ui_name(), "PRESET P2") == 0 && strcmp(value(), "Long name XY") == 0);   /* names kept */
        o[0] = 3; memcpy(store, o, n4); store_len = n4;                                 /* version 3: no names */
        dc_ui_init(); dc_ui_restore(); go(I_PRESET);
        assert(strcmp(dc_ui_name(), "PRESET") == 0 && value()[0] == 'P' && dsp[DC_BPM] == 120); } }

    /* sync (1.4): SYNC and BPM on MAIN; TIME / RATE / LENGTH / GRAIN show note values, only those that fit; saved per preset */
    { dc_ui_init(); memset(store, 0, sizeof store); store_len = 0; drain();
      go(I_SYNC); assert(strcmp(dc_ui_name(), "SYNC") == 0 && strcmp(value(), "FREE") == 0);
      go(I_BPM); assert(strcmp(value(), "120 BPM") == 0);
      knob(200); knob(0); assert(strcmp(value(), "40 BPM") == 0);   /* (the knob takes over once moved) */
      knob(255); drain(); assert(strcmp(value(), "240 BPM") == 0 && dsp[DC_BPM] == 240);
      dc_ui_param_set(DC_BPM, 120); dc_ui_param_set(DC_SYNC, 1);
      go(I_TIME); dc_ui_param_set(DC_TIME, 0); assert(strcmp(value(), "1/32") == 0);
      dc_ui_param_set(DC_TIME, 255); assert(strcmp(value(), "1/2") == 0);        /* 120 BPM, CLOCK 44K: 1 s is the longest that fits (1.16 s) */
      dc_ui_param_set(DC_CLOCK, 8); assert(strcmp(value(), "1 BAR") == 0);      /* 22K: the delay lasts twice as long */
      dc_ui_param_set(DC_CLOCK, 10);
      go(I_RATE); dc_ui_param_set(DC_RATE, 255); assert(strcmp(value(), "4 BARS") == 0 && dc_ui_rate_x100() == 13);   /* 0.125 Hz */
      dc_ui_param_set(DC_RATE, 0); assert(strcmp(value(), "1/32") == 0 && dc_ui_rate_x100() == 1600);
      go(I_GRAIN); dc_ui_param_set(DC_GRAIN, 0); assert(strcmp(value(), "1/64") == 0);
      dc_ui_param_set(DC_GRAIN, 255); assert(strcmp(value(), "1/16") == 0);     /* 125 ms: a grain can be up to 186 ms */
      dc_ui_param_set(DC_BPM, 40); assert(strcmp(value(), "1/32T") == 0);      /* at 40 BPM even a 1/32 (188 ms) is too long */
      dc_ui_param_set(DC_BPM, 120);
      dc_ui_param_set(DC_SYNC, 0);                                               /* off: free values again */
      go(I_TIME); assert(value()[strlen(value()) - 1] == 'S');                   /* "...MS" / "...S" */
      dc_ui_param_set(DC_SYNC, 1); dc_ui_param_set(DC_BPM, 90); dc_ui_param_set(DC_P_SAVE, 2);
      dc_ui_param_set(DC_SYNC, 0); dc_ui_param_set(DC_BPM, 150); dc_ui_param_set(DC_P_PRESET, 2); drain();
      assert(dsp[DC_SYNC] == 1 && dsp[DC_BPM] == 90); }                          /* per preset */

    /* a record from another version is ignored */
    store[0] = 9; dc_ui_init(); dc_ui_restore(); assert(dc_ui_preset() == 0);
    puts("ALL OK: foot tap / hold (catch, freeze, overdub), PARA click save / load, PARA hold on / off, bounces, power cycle, 1.1 and early-1.2 presets, live access, names, sync");
    return 0;
}
