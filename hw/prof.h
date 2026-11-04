// A debug build's profiler, make -C hw PROF=1, see prof.c. Without PROF it's nothing.
#pragma once
#include <stdint.h>

#ifdef PROF
void prof_init(void);                  // once the clock is set
void prof_second(void);                // every second
int prof_log(uint8_t *out, int room);  // plat_log()
#else
static inline void prof_init(void) {}
static inline void prof_second(void) {}
static inline int prof_log(uint8_t *out, int room) {
  (void) out, (void) room;
  return 0;
}
#endif
