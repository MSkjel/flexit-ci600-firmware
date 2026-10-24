// see unit.h
#include "unit.h"
#include "board.h"
#include "clock.h"
#include "mbserver.h"
#include "model.h"
#include "panel.h"
#include "plat.h"
#include "rs485.h"
#include "schedule.h"
#include "settings.h"
#include "ui.h"
#include <string.h>

// CX60 registers, taken from its firmware
#define R_CFG_MODE 0x000  // the mode set, a boost or the week program may run another
#define R_SETPOINT 0x00C
#define R_MASTER_PANEL 0x00F
#define R_WEEK_FIRST 0x01C  // week schedule, 7 days x 4 periods x (start, end, mode, setpoint), times in minutes
#define R_WEEK_LAST 0x08B
#define R_CLOCK_HI 0x094
#define R_CLOCK_LO 0x095
#define R_CX60_VERSION 0x097
#define R_PANEL1_VERSION 0x099  // input register 3 of each panel at discovery, 0x099/0x09B/0x09D. Stock is 7.x
#define R_PANEL3_VERSION 0x09D
#define R_SETTINGS_LAST 0x0BD  // 0x000..0x0BD are the settings the CX60 sends when it starts
#define R_ST_SETPOINT 0x0BE    // 0x0BE..0x112 is the status broadcast. The setpoint set, or the potentiometer's
#define R_ST_MODE 0x0BF        // the mode running
#define R_ST_FILTER_H 0x0C0
#define R_ST_BOOST_S 0x0C1
#define R_ST_SETPOINT_NOW 0x0C2  // the setpoint in effect, the week program's or the home/away input's
#define R_ST_SUPPLY 0x0C3
#define R_ST_EXTRACT 0x0C4
#define R_ST_OUTDOOR 0x0C5
#define R_ST_WATER 0x0C6
#define R_ST_COOL 0x0C7
#define R_ST_ROTOR 0x0C8
#define R_ST_HEAT 0x0C9
#define R_ST_FAN 0x0CA
#define R_ST_TOTAL_H 0x0CB
#define R_FILTER_RESETS 0x0CC
#define R_ALARMS 0x0CD  // active alarms, 10 x (code, since hi, since lo)
#define R_ALARMS_END 0x0EA
#define R_LOG 0x0EB  // alarm log, 5 x (code, start hi, start lo, end hi, end lo)
#define R_LOG_END 0x103
#define R_ST_HEATER 0x10E     // the heater may run: the panels' flags below ORed, and the unit has one
#define R_DS1_ELECTRIC 0x10F  // the DIP switches DS1..DS4
#define R_DS4_FAN_AC 0x112
#define R_CFG2_FIRST 0x113  // 0x113..0x14B, more settings
#define R_CFG_REGULATION 0x114
#define R_CMD_FILTER 0x116
#define R_WEEK_EN 0x118  // schedule period enables, 7 days x 4
#define R_WEEK_EN_END 0x133
#define R_CFG_EXTRACT_SENSOR 0x136  // the extract sensor is in use, extract regulation needs it
#define R_CMD_BOOST_ON 0x13A
#define R_CMD_BOOST_OFF 0x13B
#define R_CMD_CLR_ALARM 0x13C
#define R_CMD_CLR_LOG 0x13D
#define R_CFG_HEATER 0x13F  // heater allowed, one flag per panel address. The CX60 ORs those of the panels present
#define R_CFG_HEATER_LAST 0x141
#define R_CMD_FACTORY 0x142
#define R_CMD_LAST 0x149
#define R_CFG2_LAST 0x14B
#define R_RT_STOP 0x14C  // runtime counters in seconds, hi/lo pairs. The de-icing settings sit among them
#define R_RT_MIN 0x14E
#define R_RT_NORMAL 0x150
#define R_RT_MAX 0x152
#define R_RT_ROTOR 0x154
#define R_RT_HEATER 0x156
#define R_RT_TOTAL 0x15C
#define R_RT_LAST 0x15F

static void bus_write(uint16_t reg, int16_t v) {
  uint32_t s = board_irq_save();
  mb_write(reg, v);
  board_irq_restore(s);
  if (unit_cached(reg))
    settings_touch(true);  // only the panel's own menu writes these
}

static uint16_t U(uint16_t reg) { return (uint16_t) MB.hold[reg]; }  // a register, unsigned

static uint32_t U32(uint16_t hi) {  // a hi/lo pair, interrupts off so the halves match
  uint32_t s = board_irq_save();
  uint32_t v = (uint32_t) U(hi) << 16 | U(hi + 1);
  board_irq_restore(s);
  return v;
}

// The CX60 only accepts the setpoint and NORMAL fan % from the master panel, the lowest panel address.
static bool is_master(void) {
  uint16_t m = U(R_MASTER_PANEL);
  return m == 0 || m == M.bus_addr;
}

static void stamp(alarm_t *a, uint32_t t) {
  uint16_t yy;
  unix_split(t, &yy, &a->mo, &a->dd, &a->hh, &a->mi);
}

static void read_alarms(void) {
  alarm_t a[10];
  int n = 0;
  for (int i = 0; i < 10; i++) {
    uint16_t r = R_ALARMS + 3 * i;
    if (!U(r))
      continue;
    a[n] = (alarm_t) {.code = (uint8_t) U(r), .active = true};
    stamp(&a[n], U32(r + 1));
    n++;
  }
  // most serious first, the filter reminder goes last
  alarm_t sorted[LEN(M.alarms)];
  unsigned k = 0;
  for (int i = 0; i < n; i++)
    if (a[i].code != ALARM_FILTER && k < LEN(sorted))
      sorted[k++] = a[i];
  for (int i = 0; i < n; i++)
    if (a[i].code == ALARM_FILTER && k < LEN(sorted))
      sorted[k++] = a[i];
  if (k == M.n_alarms && !memcmp(sorted, M.alarms, k * sizeof *sorted))
    return;
  memcpy(M.alarms, sorted, k * sizeof *sorted);
  M.n_alarms = (uint8_t) k;
  ui_alarms_changed();
}

static void read_log(void) {
  alarm_t log[LEN(M.history)];
  int n = 0;
  for (unsigned i = 0; i < LEN(M.history); i++) {
    uint16_t r = R_LOG + 5 * i;
    if (!U(r))
      continue;
    alarm_t *a = &log[n++];
    *a = (alarm_t) {.code = (uint8_t) U(r), .active = U32(r + 3) == 0};
    stamp(a, U32(r + 1));
  }
  if (n == M.n_history && !memcmp(log, M.history, n * sizeof *log))
    return;
  memcpy(M.history, log, n * sizeof *log);
  M.n_history = (uint8_t) n;
  ui_alarms_changed();
}

static void read_week(int day) {
  period_t p[4];
  int n = 0;
  for (int i = 0; i < 4; i++) {
    uint16_t r = R_WEEK_FIRST + day * 16 + i * 4;
    uint16_t start = U(r), end = U(r + 1);
    if (!U(R_WEEK_EN + day * 4 + i) || start >= end || end > 1440)
      continue;
    p[n++] = (period_t) {.start_h = (uint8_t) (start / 60),
                         .start_m = (uint8_t) (start % 60),
                         .end_h = (uint8_t) (end / 60),
                         .end_m = (uint8_t) (end % 60),
                         .level = (uint8_t) (U(r + 2) & 3),
                         .idx = (uint8_t) i,
                         .setpoint = (int16_t) U(r + 3)};
  }
  sched_store(day, p, n);
}

// The CX60 relays each register back and read_week picks them up.
void plat_period_set(void) {
  const period_edit_t *p = &M.period;
  uint16_t r = R_WEEK_FIRST + p->day * 16 + p->idx * 4;
  bus_write(r, (int16_t) p->start);
  bus_write(r + 1, (int16_t) p->end);
  bus_write(r + 2, p->level);
  bus_write(r + 3, p->setpoint);
  bus_write(R_WEEK_EN + p->day * 4 + p->idx, 1);
}

static void read_runtime(void) {
  // The CX60 only sends the word that changed, the high words only come when it starts. Until then the
  // counters are the copy kept in flash, see unit_cached().
  uint16_t h16 = U(R_ST_TOTAL_H);  // 16-bit hours, used until the seconds counter is there
  if (!mb_written(R_RT_STOP) || !mb_written(R_RT_TOTAL)) {
    M.h_total = h16;
    return;
  }
  M.h_stop = U32(R_RT_STOP) / 3600;
  M.h_min = U32(R_RT_MIN) / 3600;
  M.h_normal = U32(R_RT_NORMAL) / 3600;
  M.h_max = U32(R_RT_MAX) / 3600;
  M.h_rotor = U32(R_RT_ROTOR) / 3600;
  M.h_heater = U32(R_RT_HEATER) / 3600;
  // A high word that changed while we weren't listening is lost, the CX60's 16-bit hours correct the total.
  uint32_t h = U32(R_RT_TOTAL) / 3600;
  M.h_total = mb_written(R_ST_TOTAL_H) ? h + (uint32_t) (int16_t) (h16 - (uint16_t) h) : h;
}

static void fan_outputs(void) { M.fan_ext = M.mode_now >= MODE_MIN ? M.fan_pct[M.mode_now - 1][1] : 0; }

// The CX60 relays the clock's two words in separate exchanges, a few ms apart. A new high word with the old low one
// is 18 hours off, so the clock is only read once the words have stopped changing for CLOCK_SETTLE_MS.
#define CLOCK_SETTLE_MS 500
#define CLOCK_SLACK_S 2        // a clock this close to ours isn't followed
#define CLOCK_UNDONE_S 5       // a panel writing its clock this soon after the keys set ours keeps its own
static bool clock_news;        // a clock word changed
static uint32_t clock_ms;      // when the last one did
static uint32_t clock_minute;  // the minute the CX60 last got from us, or sent us, see minute_of()
static uint8_t clock_keys_s;   // seconds left of CLOCK_UNDONE_S after the clock was set on the keys

// The CX60 starts a week program period once its clock is past the start, not at it. Minutes count from :01
// and go out then, as the stock panel's every-second writes do.
static uint32_t minute_of(uint32_t t) { return (t - 1) / 60; }

// A stock panel on the bus, the CX60 keeps what each one said at discovery
_Static_assert(FW_MAJOR != 7, "we'd pass for a stock panel");
static bool stock_panel(void) {
  for (uint16_t r = R_PANEL1_VERSION; r <= R_PANEL3_VERSION; r += 2)
    if (U(r) >> 8 == 7)
      return true;
  return false;
}

static void reg_changed(uint16_t r) {
  int16_t v = MB.hold[r];
  if (r >= R_DS1_ELECTRIC && r <= R_DS4_FAN_AC) {
    uint8_t bit = (uint8_t) (1u << (r - R_DS1_ELECTRIC));
    M.dip = v ? M.dip | bit : M.dip & ~bit;
  }
  switch (r) {
    case R_CFG_MODE:
      val_set_bus(V_MODE, v & 3);
      break;
    case R_CFG_REGULATION:
      v = v != 0;  // the CX60 takes anything but 0 as supply air
      break;
    case R_ST_MODE:
      M.mode_now = (uint8_t) (v & 3);
      fan_outputs();
      ui_value_changed(V_NONE);
      break;
    case R_ST_FILTER_H:
      M.filter_hours = (uint16_t) v;
      break;
    case R_FILTER_RESETS:
      M.filter_resets = (uint16_t) v;
      break;
    case R_ST_BOOST_S:
      M.boost_left = (uint16_t) ((v + 59) / 60);
      val_set_bus(V_BOOST, v > 0);
      break;
    case R_ST_SUPPLY:
      M.t_supply = v;
      break;
    case R_ST_EXTRACT:
      M.t_extract = v;
      break;
    case R_ST_OUTDOOR:
      M.t_outdoor = v;
      break;
    case R_ST_WATER:
      M.t_water = HAS(FEAT_WATER_HEATER) ? v : T_NONE;
      break;
    case R_ST_COOL:
      M.cool_pct = (uint8_t) v;
      break;
    case R_ST_ROTOR:
      M.rotor_pct = (uint8_t) v;
      break;
    case R_ST_HEAT:
      M.heat_pct = (uint8_t) v;
      break;
    case R_ST_FAN:
      M.fan_sup = (uint8_t) v;
      fan_outputs();
      break;
    case R_ST_SETPOINT:
      if (!mb_written(R_SETPOINT))
        M.setpoint = v;  // a starting value for the editor until the real setpoint arrives
      break;
    case R_ST_SETPOINT_NOW:
      // In extract regulation it's min + 6 or max - 6 while the supply air is out of bounds, not a target
      if (M.regulation != REG_EXTRACT || (v != M.supply_min + 6 && v != M.supply_max - 6))
        M.setpoint_now = v;
      break;
    case R_ST_HEATER:
      val_set_bus(V_HEATER, v != 0);  // what the unit made of the panels' flags, see plat_value_changed()
      break;
    case R_ST_TOTAL_H:
      read_runtime();
      break;
    case R_CX60_VERSION:
      M.cx60_version = (uint16_t) v;
      break;
    case R_DS1_ELECTRIC:
      val_set_bus(V_FEAT_FIRST + FEAT_WATER_HEATER, v == 0);
      break;
    case R_DS4_FAN_AC:
      val_set_bus(V_FEAT_FIRST + FEAT_EC_FANS, v == 0);
      break;
    case R_CLOCK_HI:
    case R_CLOCK_LO:
      // The settings block the CX60 sends when it starts has the clock it saved at its last power cut. Only a
      // panel's write is worth following, unless we have no clock at all. Otherwise ours goes out right away.
      if (mb_relayed(r) || clock_to_unix() <= SECS_1970_TO_2000) {
        clock_news = true;
        clock_ms = board_ms();
      } else
        clock_minute = 0;
      break;
    default:
      if (r >= R_ALARMS && r <= R_ALARMS_END)
        read_alarms();
      else if (r >= R_LOG && r <= R_LOG_END)
        read_log();
      else if (r >= R_WEEK_FIRST && r <= R_WEEK_LAST)
        read_week((r - R_WEEK_FIRST) / 16);
      else if (r >= R_WEEK_EN && r <= R_WEEK_EN_END)
        read_week((r - R_WEEK_EN) / 4);
      else if (r >= R_RT_STOP && r <= R_RT_LAST)
        read_runtime();
      else if (r >= R_PANEL1_VERSION && r <= R_PANEL3_VERSION && !((r - R_PANEL1_VERSION) & 1))
        M.panel_version[(r - R_PANEL1_VERSION) / 2] = (uint16_t) v;
      break;
  }
  // settings kept in the CX60 like the fan % and setpoint, see values.h
  val_t id = val_by_cx60(r);
  // Our heater flag is one vote of three, R_ST_HEATER has the result. A stock panel writes 0x117 as each of its
  // periods starts and ends, so with one on the bus a relay of it isn't a change of our setting.
  if (id == V_HEATER || (id == V_WEEK_PROGRAM && mb_relayed(r) && stock_panel()))
    id = V_NONE;
  if (id != V_NONE) {
    val_set_bus(id, v / val_def(id).scale);
    if (id >= V_FAN_MIN_SUP && id <= V_FAN_MAX_EXT)
      fan_outputs();
  }
}

// The CX60 sends its clock at start-up and whenever a panel writes it, which every panel does once a minute. We
// follow it, unless it's our own write coming back or another panel's that's only a little off. Following those
// would just make the clocks fall behind.
static void clock_changed(void) {
  uint32_t t = U32(R_CLOCK_HI);
  uint32_t ours = clock_to_unix();
  if (t <= SECS_1970_TO_2000 || (t + CLOCK_SLACK_S >= ours && t <= ours + CLOCK_SLACK_S))
    return;
  board_rtc_set(t);
  clock_from_unix(t);
  clock_minute = minute_of(t);  // no need to send it back
  if (t > ours + 60 || t + 60 < ours)
    clock_follow(t, ours);  // a real change, not clocks drifting apart
  if (clock_keys_s) {
    clock_keys_s = 0;
    ui_toast(IC_CLOCK, S_TIME_ON_OTHER_PANEL);  // a stock panel never follows ours
  }
}

// The CX60 has no clock of its own and keeps what the panels write. Stock writes it every second, the week program
// only needs minutes.
static void clock_write(uint32_t t) {
  clock_minute = minute_of(t);
  bus_write(R_CLOCK_HI, (int16_t) (t >> 16));
  bus_write(R_CLOCK_LO, (int16_t) (t & 0xFFFF));
}

void unit_init(void) {
  mb_init(M.bus_addr, FW_VERSION, FW_PATCH);
  M.fw_version = (uint32_t) FW_MAJOR << 16 | FW_MINOR << 8 | FW_PATCH;
}

// The settings, and the runtime counters since the CX60 only sends their high words when it starts
bool unit_cached(uint16_t r) {
  if (r == R_CLOCK_HI || r == R_CLOCK_LO)
    return false;
  bool keep = r <= R_SETTINGS_LAST || (r >= R_CFG2_FIRST && r <= R_RT_LAST);
  bool cmd =
      r == R_CMD_FILTER || (r >= R_CMD_BOOST_ON && r <= R_CMD_CLR_LOG) || (r >= R_CMD_FACTORY && r <= R_CMD_LAST);
  return keep && !cmd && mb_written(r);
}

void unit_cache_load(uint16_t r, int16_t v) { mb_load(r, v); }

// Nobody sends these again when we restart: a counter's high word, which the CX60 sends once, and another
// device's week program change. Without an ESP every relay is someone on another panel, so those too.
static bool save_soon(uint16_t r) {
  if (r >= R_RT_STOP)
    return !((r - R_RT_STOP) & 1);
  bool week = (r >= R_WEEK_FIRST && r <= R_WEEK_LAST) || (r >= R_WEEK_EN && r <= R_WEEK_EN_END);
  return mb_relayed(r) && (week || !HAS(FEAT_ESP));
}

void unit_start(void) {
  MB.addr = M.bus_addr;  // the saved address, now that the settings are loaded
}

void unit_poll(void) {
  uint16_t r;
  for (;;) {
    uint32_t s = board_irq_save();
    bool got = mb_take_changed(&r);
    board_irq_restore(s);
    if (!got)
      break;
    reg_changed(r);
    if (unit_cached(r))
      settings_touch(save_soon(r));
  }
  if (clock_news && board_ms() - clock_ms >= CLOCK_SETTLE_MS) {
    clock_news = false;
    clock_changed();
  }
  M.bus_ok = MB.frames;
  M.bus_err = MB.errors;
  if (MB.enter_bootloader) {
    for (uint32_t t = board_ms(); rs485_busy() && board_ms() - t < 20;) {
    }  // let the reply go out first
    settings_flush();
    board_bootloader();
  }
}

// The manual tests in Advanced > Test. The CX60 only uses a test value while its enable register is set, and clears
// that after 60 s.
#define TEST_S 60
static const struct {
  val_t v;
  uint16_t enable;
} TESTS[] = {{V_TEST_SPEED, 0x143},
             {V_TEST_FANS, 0x144},
             {V_TEST_HEATER, 0x146},
             {V_TEST_ROTOR, 0x148},
             {V_TEST_COOLING, 0x149}};
static uint8_t test_s[LEN(TESTS)];  // seconds left of each test

static bool test_changed(val_t v) {
  for (unsigned i = 0; i < LEN(TESTS); i++)
    if (TESTS[i].v == v) {
      int32_t x = val_get(v);
      if (x)
        bus_write(val_def(v).cx60, (int16_t) (v == V_TEST_SPEED ? x - 1 : x));
      bus_write(TESTS[i].enable, x != 0);
      test_s[i] = x ? TEST_S : 0;
      return true;
    }
  return false;
}

static void tests_second(void) {
  for (unsigned i = 0; i < LEN(TESTS); i++)
    if (test_s[i] && !--test_s[i])
      val_set_bus(TESTS[i].v, 0);  // over, the CX60 has cleared its enable too
}

// The CX60 only counts a boost down while something answers at address 1. One whose seconds don't fall is
// stopped, rather than shown as running for good.
#define BOOST_STUCK_S 10
static uint16_t boost_seen;  // R_ST_BOOST_S a second ago
static uint8_t boost_stuck_s;
#define HEATER_SETTLE_MS 2000  // for the CX60 to pick our flag up and say what it made of it
static uint32_t heater_ms;     // when the heater was last switched on the panel
static bool heater_asked;      // switched on there, and the unit hasn't said yet

void unit_second(void) {
  static uint32_t frames_seen;
  static uint8_t quiet_s;
  // The CX60 sends something every ~110 ms. After 5 s of silence, what's on screen is out of date.
  if (MB.frames != frames_seen) {
    frames_seen = MB.frames;
    quiet_s = 0;
  } else if (quiet_s < 255)
    quiet_s++;
  bool offline = quiet_s >= 5;
  if (offline != M.bus_offline) {
    M.bus_offline = offline;
    ui_value_changed(V_NONE);
  }
  M.bus_ours = MB.ours;
  if (!offline && M.boost_left && U(R_ST_BOOST_S) == boost_seen) {
    if (++boost_stuck_s == BOOST_STUCK_S) {
      bus_write(R_CMD_BOOST_OFF, 1);
      ui_toast(IC_TIMER, S_BOOST_NEEDS_ADDR_1);
    }
  } else
    boost_stuck_s = 0;
  boost_seen = U(R_ST_BOOST_S);
  // Our heater flag may make no difference, the unit has no heater say. The toggle shows what it decided.
  if (mb_written(R_ST_HEATER) && board_ms() - heater_ms > HEATER_SETTLE_MS) {
    if (heater_asked && !U(R_ST_HEATER))
      ui_toast(IC_FIRE, S_HEATER_KEPT_OFF);
    heater_asked = false;
    val_set_bus(V_HEATER, U(R_ST_HEATER) != 0);
  }
  // Each new minute, whatever our address. The ESP never writes the clock and there may be no stock panel.
  uint32_t t = clock_to_unix();
  if (t > SECS_1970_TO_2000 && minute_of(t) != clock_minute)
    clock_write(t);
  if (clock_keys_s)
    clock_keys_s--;
  tests_second();
}

// ---------------------------------------------------------------- changes made on the panel
void plat_value_changed(val_t v) {
  val_def_t d = val_def(v);
  if (test_changed(v))
    return;
  if (d.flags & VF_CLOCK) {
    clock_keys_s = CLOCK_UNDONE_S;
    plat_time_set();
    return;
  }
  if (v == V_BUS_ADDR)
    MB.addr = M.bus_addr;  // the CX60 only notices after its next restart
  if (v == V_ROOM_OFFSET)
    M.t_room = T_NONE;  // start over with the new offset instead of smoothing toward it
  if (!(d.flags & VF_CX60))
    return;
  if (v == V_BOOST) {
    bus_write(M.boost_on ? R_CMD_BOOST_ON : R_CMD_BOOST_OFF, 1);
    return;
  }
  if (v == V_HEATER) {
    // The CX60 ORs the flags of the panels present, so off has to clear every panel's
    heater_ms = board_ms();
    heater_asked = M.heater_enabled;
    if (M.heater_enabled)
      bus_write(val_cx60_reg(v), 1);
    else
      for (uint16_t r = R_CFG_HEATER; r <= R_CFG_HEATER_LAST; r++)
        bus_write(r, 0);
    return;
  }
  // We're not the master, so the CX60 would just undo it. The ESP is, and gets the change over the link. While
  // it's offline we write it anyway, and the CX60's relay puts the value back on screen.
  if ((d.flags & VF_MASTER) && !is_master() && HAS(FEAT_ESP) && M.esp_online)
    return;
  bus_write(val_cx60_reg(v), (int16_t) (val_get(v) * d.scale));
  if (v >= V_FAN_MIN_SUP && v <= V_FAN_MAX_EXT)
    fan_outputs();
  if (v == V_REGULATION && M.regulation == REG_EXTRACT)
    bus_write(R_CFG_EXTRACT_SENSOR, 1);  // like the stock panel, the regulation does nothing without it
}

void plat_save(bool by_user) { settings_touch(by_user); }

void plat_save_now(void) {
  settings_touch(true);
  settings_flush();
}

void plat_action(int a) {
  switch (a) {
    case A_RESET_FILTER:
      bus_write(R_CMD_FILTER, 1);
      break;
    case A_RESET_ALARMS:
      bus_write(R_CMD_CLR_ALARM, 1);
      break;
    case A_CLEAR_HISTORY:
      bus_write(R_CMD_CLR_LOG, 1);
      break;
    case A_FACTORY_RESET:
      bus_write(R_CMD_FACTORY, 1);
      break;
    case A_RESTART:
      settings_flush();
      board_reset();
      break;
    case A_REMOVE_PERIOD:
      bus_write(R_WEEK_EN + M.period.day * 4 + M.period.idx, 0);
      break;
    case A_RESET_PANEL:
      panel_defaults();
      settings_touch(true);
      settings_flush();
      board_reset();
      break;
    default:
      break;
  }
}

// The clock was set on the panel or by the ESP. Write it to the CX60 so it and the other panels follow.
void plat_time_set(void) {
  uint32_t t = clock_to_unix(), from = board_rtc_get();
  clock_from_unix(t);
  board_rtc_set(t);
  clock_follow(t, from);
  clock_write(t);
}
