#ifndef DREAMCATCHER_UI_H
#define DREAMCATCHER_UI_H
#include <stdint.h>

/* Dreamcatcher's screen (160 x 80, purple). No hardware in here: ui_draw() renders bands and hands each band
 * whose content changed to ui_blit() (pedal: the platform's gp5_blit; computer: test/ui_mock.c).
 *
 *   header  dream-catcher emblem, name, a pulse that blinks at the LFO rate, state pill OFF / ON / LOOP
 *   name    section pill (MAIN WET MOD COLOR LOOP, each its own shade) and the chosen setting
 *   value   its value, large; gold while it follows the volume knob
 *   bar     where the value sits in its range (violet to pink); a marker shows the knob while it waits
 *   footer  one dot per setting, grouped by section, the chosen one wider
 */
#define UI_W 160
#define UI_H 80

typedef struct {
    const char *name;
    char value[10];
    uint8_t frac, knob, live, on, caught, sel, count, sec;
    uint8_t pulse;            /* 0..4: brightness of the LFO pulse */
    const uint8_t *secs;      /* section of each setting, count entries */
    const char *sec_name;
} ui_state_t;

/* Draws at most ONE band per call; returns 1 while more is left. force = redraw everything. */
int ui_draw(const ui_state_t *s, int force);
extern void ui_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px);
#endif
