/* Dreamcatcher's sound on the control processor (engine.c). */
#ifndef DC_ENGINE_H
#define DC_ENGINE_H
#include <stdint.h>
void dc_engine_start(void);                        /* once, in fw_init: the engine with its defaults, the link on */
void dc_engine_set(uint32_t id, uint32_t v);       /* a setting, from the main loop: applied before the next block */
#endif
