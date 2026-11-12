#ifndef DREAMCATCHER_CONTROLS_H
#define DREAMCATCHER_CONTROLS_H
#include <stdint.h>

/* Dreamcatcher's controls. No hardware in here: the platform feeds knob and switch events in and sends what
 * dc_take() hands out to the DSP. Also built on the computer (test/presets_test.c, test/ui_mock.c).
 *
 *   turn PARA     choose a setting (grouped: MAIN, WET, MOD, COLOR, LOOP)
 *   volume knob   changes the chosen setting once it is turned a little; until then the setting keeps its value
 *   foot switch   catch a micro-loop / let it go (the looper only)
 *   click PARA    save the sound into the active preset; on PRESET (MAIN) with another slot highlighted by the
 *                 volume knob: load that slot (an empty one: save the sound there)
 *   hold PARA 3 s effect on / off (the looper is left as it is)
 * Presets (8 slots, each all settings + VOLUME) and the last-loaded slot are kept with dc_persist_load / _save,
 * which the firmware provides (the platform's flash store on the pedal, memory in tests).
 */
#define DC_SLOTS 8

enum { SEC_MAIN, SEC_WET, SEC_MOD, SEC_COLOR, SEC_LOOP, SEC_N };

void dc_ui_init(void);
void dc_ui_turn(int detents);
void dc_ui_foot_down(void);
void dc_ui_foot_up(void);
void dc_ui_para_down(void);
void dc_ui_para_up(void);
void dc_ui_tick(void);                          /* every 10 ms: debouncing, taps and holds of both switches */
void dc_ui_restore(void);                       /* after dc_ui_init: load the presets, start on the last slot */
extern unsigned dc_persist_load(void *dst, unsigned max);
extern int dc_persist_save(const void *src, unsigned len);
void dc_ui_knob(unsigned knob);                 /* 0..255, every 10 ms */
void dc_ui_resend_all(void);
int dc_ui_take(uint32_t *id, uint32_t *value);  /* next setting for the DSP; 0 = none */
unsigned dc_ui_level(void);                     /* output level 0..255 */

unsigned dc_ui_on(void);
unsigned dc_ui_caught(void);
unsigned dc_ui_live(void);                      /* the chosen setting follows the knob */
unsigned dc_ui_sel(void);
unsigned dc_ui_count(void);
unsigned dc_ui_section(unsigned item);          /* SEC_* of a setting */
const char *dc_ui_section_name(unsigned sec);
const char *dc_ui_name(void);                   /* chosen setting's name, up to 8 characters */
void dc_ui_value(char *v);                      /* its value as text, up to 7 characters + 0 */
unsigned dc_ui_frac(void);                      /* its position in its range, 0..255 */
unsigned dc_ui_rate_x100(void);                 /* LFO rate in 1/100 Hz (for the pulse on the screen) */
unsigned dc_ui_preset(void);                    /* 0..DC_SLOTS-1 */
unsigned dc_ui_overlay(void);                   /* 1 while the screen shows saving (name/value/frac say what) */
#endif
