// What the app needs from the platform, the firmware's hw/ or a host program.
#pragma once
#include "model.h"

void plat_value_changed(val_t v);                 // user or ESP changed v, write it to the bus etc.
void plat_save(bool by_user);                     // a saved value changed. by_user = from the panel's keys, save soon
void plat_save_now(void);                         // save right away, for a value a restart mustn't lose
bool plat_fresh(void);                            // started with no saved settings, like after a flash
void plat_action(int a);                          // action_t
void plat_trial_end(bool keep);                   // ui_trial() was answered, OK in time if keep
void plat_time_set(void);                         // M.yy..M.ss were set, set our clock and the CX60's
void plat_period_set(void);                       // M.period changed, write it to the unit
int plat_week(int day, period_t *out);            // the day's periods as the unit has them, up to PERIODS_PER_DAY
void plat_cx60_reg(uint16_t reg, int16_t value);  // a CX60 register from the ESP, take it as if the CX60 sent it
bool plat_const(const void *p);  // p is in memory nothing writes, like flash, so a label can use it without a copy
uint32_t plat_entropy(void);     // different on every start, for the ESP link session
// A debug build's next line for the ESP's log, as a K_LOG message in out if it fits in room. Returns its length, 0
// for none.
int plat_log(uint8_t *out, int room);

// Counters for System > Diagnostics
typedef struct {
  uint32_t uptime_s;                         // since this start
  uint32_t cpu_mhz, cpu_busy;                // the processor's clock, and the % of the last second it was awake
  uint32_t fault_pc;                         // where it crashed, when M.reset_cause is RESET_FAULT
  uint32_t bus_ok, bus_err, bus_ours;        // frames from the unit, bad ones, polls to this panel
  uint32_t restarts;                         // since the power came on
  uint32_t full_us, full_send_us;            // the last big redraw scaled to the whole screen, and sending
  uint32_t stack_free;                       // bytes of stack never used
  uint32_t saves;                            // settings saves since the flash was erased
  uint32_t heap_size, heap_used, heap_peak;  // LVGL's memory, bytes
  uint32_t cpuid;                            // the core's CPUID, 411FC231 on an STM32F103
  uint32_t boot_crc;                         // plink_crc32 of Flexit's bootloader, to tell its versions apart
} plat_stats_t;
void plat_stats(plat_stats_t *s);
