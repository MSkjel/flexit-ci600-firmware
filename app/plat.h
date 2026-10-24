// What the app needs from the platform, the firmware's hw/ or a host program.
#pragma once
#include "model.h"

void plat_value_changed(val_t v);  // user or ESP changed v, write it to the bus etc.
void plat_save(bool by_user);      // a saved value changed. by_user = from the panel's keys, save soon
void plat_save_now(void);          // save right away, for a value a restart mustn't lose
void plat_action(int a);           // action_t
void plat_time_set(void);          // M.yy..M.ss were set, set our clock and the CU60's
void plat_period_set(void);        // M.period changed, write it to the unit
bool plat_const(const void *p);    // p is in memory nothing writes, like flash, so a label can use it without a copy
uint32_t plat_entropy(void);       // different on every start, for the ESP link session

// Counters for System > Diagnostics
typedef struct {
  uint32_t uptime_s;                         // since this start
  uint32_t restarts;                         // since the power came on
  uint32_t full_us, full_send_us;            // the last big redraw scaled to the whole screen, and sending
  uint32_t stack_free;                       // bytes of stack never used
  uint32_t saves;                            // settings saves since the flash was erased
  uint32_t heap_size, heap_used, heap_peak;  // LVGL's memory, bytes
} plat_stats_t;
void plat_stats(plat_stats_t *s);
