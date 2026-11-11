// A debug build's profiler, make -C hw PROF=1, see prof.c. Without PROF it's nothing.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef PROF
void prof_init(void);  // once the clock is set
// Every second, with the pixels LVGL has drawn so far and those of them that went to the LCD
void prof_second(uint32_t drawn, uint32_t sent);
int prof_log(uint8_t *out, int room);  // plat_log()
// A sleep from here on is the wait for the LCD, or isn't. What it was, to put back after.
bool prof_waits(bool lcd);
#else
static inline void prof_init(void) {}
static inline void prof_second(uint32_t drawn, uint32_t sent) { (void) drawn, (void) sent; }
static inline bool prof_waits(bool lcd) { return lcd; }
static inline int prof_log(uint8_t *out, int room) {
  (void) out, (void) room;
  return 0;
}
#endif
