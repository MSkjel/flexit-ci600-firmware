// app/clock.c, schedule.c and the processor's clock: the time, summer time, the week program and Processor speed
#include "unit_test.h"
#include "lcd.h"

// ---------------------------------------------------------------- the clock and summer time
static void test_clock(void) {
  model_defaults();
  uint32_t t = 1792328640u;  // 2026-10-18 13:04
  clock_from_unix(t);
  CHECK_EQ(M.yy, 2026);
  CHECK_EQ(M.mo, 10);
  CHECK_EQ(M.dd, 18);
  CHECK_EQ(M.hh, 13);
  CHECK_EQ(M.mi, 4);
  CHECK_EQ(clock_to_unix(), t);
  // summer time 2026 is 29 March 02:00 to 25 October 03:00 summer time, which is 02:00 standard
  CHECK(!clock_summer(1774749600u - 1) && clock_summer(1774749600u));  // 2026-03-29 02:00
  CHECK(clock_summer(1792893600u - 1) && !clock_summer(1792893600u));  // 2026-10-25 02:00 standard
  // without an ESP the panel changes to and from summer time itself
  M.features = 0;
  M.auto_dst = true;
  M.dst_now = true;
  clock_from_unix(1792893600u + 3600 - 1);  // 02:59:59 summer time
  stub_rtc = clock_to_unix();
  clock_from_unix(stub_rtc + 1);
  int erases = stub_flash_erases;
  clock_second();
  CHECK(!M.dst_now);
  CHECK_EQ(M.hh, 2);
  CHECK_EQ(M.mi, 0);                        // 03:00 summer is 02:00 standard
  CHECK_EQ(stub_flash_erases, erases + 1);  // saved right away
  M.dst_now = true;                         // restart with the old flag: 02:xx comes twice, it stands
  clock_follow(clock_to_unix(), clock_to_unix());
  clock_second();
  CHECK(M.dst_now);
  CHECK_EQ(M.hh, 2);
  CHECK_EQ(M.mi, 0);
  M.dst_now = false;
  clock_from_unix(1774749600u - 1);  // 01:59:59 standard time on 29 March
  clock_from_unix(clock_to_unix() + 1);
  clock_second();
  CHECK(M.dst_now);
  CHECK_EQ(M.hh, 3);
  CHECK_EQ(M.mi, 0);
  CHECK_EQ(stub_rtc, 1774749600u + 3600);
  // an ESP that's there sends the time, so the panel leaves it. One that's only switched on doesn't.
  M.features = FBIT(FEAT_ESP);
  M.esp_online = true;
  M.dst_now = false;
  clock_from_unix(1774749600u);
  clock_second();
  CHECK(!M.dst_now);
  CHECK_EQ(M.hh, 2);
  M.esp_online = false;
  clock_second();
  CHECK(M.dst_now);
  CHECK_EQ(M.hh, 3);
  M.features = 0;
  // a stale flag never moves the clock outside the hour of a switch: the settings wiped in summer
  M.dst_now = false;
  t = 1784124000u;  // 2026-07-15 14:00
  clock_from_unix(t);
  clock_second();
  CHECK(!M.dst_now);
  CHECK_EQ(clock_to_unix(), t);
  clock_follow(t, t);  // from the RTC at boot, or the CX60
  CHECK(M.dst_now);
  CHECK_EQ(clock_to_unix(), t);
  M.dst_now = true;  // and the other way, a summer flag in winter
  clock_from_unix(1792328640u);
  clock_second();
  CHECK(M.dst_now);
  CHECK_EQ(clock_to_unix(), 1792328640u);
  clock_follow(1792897200u, 1792897200u);  // 03:00 on 25 October is standard time
  CHECK(!M.dst_now);
  clock_follow(1792893600u + 3600 + 1800, 1792893600u);  // 03:30 that day: still is
  CHECK(!M.dst_now);
  clock_follow(1792895400u, 1792895400u);  // 02:30 that day could be either, the flag stays
  CHECK(!M.dst_now);
  M.dst_now = true;
  clock_follow(1792895400u, 1792895400u);
  CHECK(M.dst_now);
  clock_follow(1792895400u, 1792895400u + 3600);  // unless the clock just moved back an hour
  CHECK(!M.dst_now);
  clock_follow(1774751400u, 1774751400u);  // 02:30 on 29 March doesn't exist, the flag stays either way
  CHECK(!M.dst_now);
  M.dst_now = true;
  clock_follow(1774751400u, 1774751400u);
  CHECK(M.dst_now);
  // the day stays in its month, so setting the day before the month doesn't roll the date over
  M.yy = 2026, M.mo = 2, M.dd = 15;
  val_set(V_DAY, 30);
  CHECK_EQ(M.yy, 2026);
  CHECK_EQ(M.mo, 2);
  CHECK_EQ(M.dd, 28);
  M.mo = 3, M.dd = 31;
  val_set(V_MONTH, 4);
  CHECK_EQ(M.mo, 4);
  CHECK_EQ(M.dd, 30);
  M.yy = 2028, M.mo = 2, M.dd = 29;
  val_set(V_YEAR, 2027);
  CHECK_EQ(M.yy, 2027);
  CHECK_EQ(M.mo, 2);
  CHECK_EQ(M.dd, 28);
  M.features = FBIT(FEAT_ESP) | FBIT(FEAT_EC_FANS);
}

// ---------------------------------------------------------------- Processor speed
// The clock at a start, at each speed. The mark has the speed the last run was at, and the one this start tries.
static void test_cpu_clock(void) {
  static const int MHZ[] = {72, 80, 88, 96, 128};
  CHECK_EQ(LEN(MHZ), N_CPU_SPEEDS);
  bool revert;
  for (int s = 0; s < N_CPU_SPEEDS; s++) {
    CHECK_EQ(CPU_MHZ[s], MHZ[s]);
    // after a power cut, as the setting says
    CHECK_EQ(cpu_clock_choice(s, 0, RESET_POWER, &revert), s);
    CHECK(!revert);
    // a crash or the watchdog over 72 MHz sets it back to 72, at 72 they don't count
    CHECK_EQ(cpu_clock_choice(s, CLOCK_MARK(s, 0), RESET_FAULT, &revert), 0);
    CHECK_EQ(revert, s > 0);
    CHECK_EQ(cpu_clock_choice(s, CLOCK_MARK(s, 0), RESET_WATCHDOG, &revert), 0);
    CHECK_EQ(revert, s > 0);
    // nor do the panel's own restarts, the reset pin or a crash at 72 before
    CHECK_EQ(cpu_clock_choice(s, CLOCK_MARK(s, 0), RESET_SOFTWARE, &revert), s);
    CHECK(!revert);
    CHECK_EQ(cpu_clock_choice(s, CLOCK_MARK(s, 0), RESET_PIN, &revert), s);
    CHECK(!revert);
    CHECK_EQ(cpu_clock_choice(s, CLOCK_MARK(0, 0), RESET_FAULT, &revert), s);
    CHECK(!revert);
    // set back to 72 at this speed
    CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(s, 0), RESET_SOFTWARE, &revert), 0);
    CHECK(!revert);
  }
  // a mark with speeds there aren't, from other firmware
  CHECK_EQ(cpu_clock_choice(1, CLOCK_MARK(7, 9), RESET_FAULT, &revert), 1);
  CHECK(!revert);
}

// A new speed over 72 starts as a trial of it, with the setting still saved as 72
static void test_cpu_trial(void) {
  bool revert;
  for (int t = 1; t < N_CPU_SPEEDS; t++) {
    for (int from = 0; from < N_CPU_SPEEDS; from++) {  // picked at any speed
      CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(from, t), RESET_SOFTWARE, &revert), t);
      CHECK(!revert);
    }
    // a restart during it carries on with it, a crash ends it
    CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(t, t), RESET_SOFTWARE, &revert), t);
    CHECK(!revert);
    CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(t, t), RESET_FAULT, &revert), 0);
    CHECK(revert);
    CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(t, t), RESET_WATCHDOG, &revert), 0);
    CHECK(revert);
    // a hang before the restart counts at the speed it ran at, over 72
    CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(0, t), RESET_WATCHDOG, &revert), t);
    CHECK(!revert);
    CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(4, t), RESET_WATCHDOG, &revert), 0);
    CHECK(revert);
    // OK keeps it. BACK or no answer restart at 72, and a power cut loses the mark, see hw/main.c
    CHECK_EQ(cpu_clock_choice(t, CLOCK_MARK(t, 0), RESET_SOFTWARE, &revert), t);
    CHECK(!revert);
    CHECK_EQ(cpu_clock_choice(0, CLOCK_MARK(t, 0), RESET_SOFTWARE, &revert), 0);
    CHECK(!revert);
    CHECK_EQ(cpu_clock_choice(0, 0, RESET_POWER, &revert), 0);
    CHECK(!revert);
  }
}

// The display's SPI at each speed: the expected rates, and never over 20 MHz
static void test_lcd_spi(void) {
  static const struct {
    int mhz;
    uint32_t init, reg, pixel;  // kHz
  } WANT[] = {
      {72, 4500, 9000, 18000},  {80, 5000, 10000, 20000},  {88, 5500, 11000, 11000},
      {96, 6000, 12000, 12000}, {128, 8000, 16000, 16000},
  };
  CHECK_EQ(LEN(WANT), N_CPU_SPEEDS);
  for (int s = 0; s < N_CPU_SPEEDS; s++) {
    uint32_t hz = CPU_MHZ[s] * 1000000u;
    CHECK_EQ(CPU_MHZ[s], WANT[s].mhz);
    CHECK_EQ(hz / lcd_spi_div(hz, LCD_INIT_HZ) / 1000, WANT[s].init);
    CHECK_EQ(hz / lcd_spi_div(hz, LCD_REG_HZ) / 1000, WANT[s].reg);
    CHECK_EQ(hz / lcd_spi_div(hz, LCD_PIXEL_HZ) / 1000, WANT[s].pixel);
  }
  CHECK_EQ(lcd_spi_div(80000001, LCD_PIXEL_HZ), 8);
  CHECK_EQ(lcd_spi_div(8000000, LCD_REG_HZ), 2);  // on HSI without the crystal
}

// ---------------------------------------------------------------- the week program
static period_t period(int start, int end, int idx) {
  return (period_t) {(uint16_t) start, (uint16_t) end, MODE_NORMAL, (uint8_t) idx, 200};
}

// A day's periods as the CX60 has them in its registers, and how many the panel then sees
static void week_set(int day, const period_t *p, int n) {
  for (int s = 0; s < PERIODS_PER_DAY; s++)
    mb_load((uint16_t) (0x118 + day * PERIODS_PER_DAY + s), 0);
  for (int i = 0; i < n; i++) {
    uint16_t r = (uint16_t) (0x01C + (day * PERIODS_PER_DAY + p[i].idx) * 4);
    mb_load(r, (int16_t) p[i].start);
    mb_load(r + 1, (int16_t) p[i].end);
    mb_load(r + 2, p[i].level);
    mb_load(r + 3, p[i].setpoint);
    mb_load((uint16_t) (0x118 + day * PERIODS_PER_DAY + p[i].idx), 1);
  }
  unit_poll();
}
static int periods(int day) {
  period_t w[PERIODS_PER_DAY];
  return sched_day(day, w);
}

static void test_schedule(void) {
  model_defaults();
  mb_init(3, FW_VERSION, FW_PATCH);
  // a new period is 08:00-16:00 if that's free, else the first free time
  CHECK(sched_edit(0, -1));
  CHECK_EQ(periods(0), 1);
  CHECK_EQ(M.period.p.start, 8 * 60);
  CHECK_EQ(M.period.p.end, 16 * 60);
  const period_t day[3] = {period(390, 480, 0), period(480, 930, 1), period(930, 1350, 2)};
  week_set(1, day, 3);
  CHECK(sched_edit(1, -1));
  CHECK_EQ(M.period.p.idx, 3);
  CHECK_EQ(M.period.p.start, 0);
  CHECK_EQ(M.period.p.end, 390);
  CHECK(!sched_can_add(1));
  const period_t full[3] = {period(0, 480, 0), period(480, 960, 1), period(960, 1440, 2)};
  week_set(2, full, 3);
  CHECK(!sched_can_add(2));
  CHECK(!sched_edit(2, -1));
  CHECK_EQ(periods(2), 3);
  // a gap no longer than the shortest period takes no new period, its start and end couldn't be edited
  const period_t gap[2] = {period(0, 480, 0), period(485, 1440, 1)};
  week_set(3, gap, 2);
  CHECK(!sched_can_add(3));
  const period_t gap6[2] = {period(0, 480, 0), period(486, 1440, 1)};
  week_set(3, gap6, 2);
  int32_t lo, hi;
  CHECK(sched_edit(3, -1));
  CHECK_EQ(M.period.p.start, 480);
  CHECK_EQ(M.period.p.end, 486);
  sched_range(V_PERIOD_START, &lo, &hi);
  CHECK_EQ(lo, 480);
  CHECK_EQ(hi, 481);
  // an edit stays between its neighbours, 08:00-15:30 between 06:30-08:00 and 15:30-22:30
  week_set(1, day, 3);
  CHECK(sched_edit(1, 1));
  val_set(V_PERIOD_START, 7 * 60);
  val_set(V_PERIOD_END, 1440);
  CHECK_EQ(M.period.p.start, 480);
  CHECK_EQ(M.period.p.end, 930);
  val_set(V_PERIOD_START, 1000);  // pushes the end along, as far as the next one
  CHECK_EQ(M.period.p.start, 925);
  CHECK_EQ(M.period.p.end, 930);
  val_set(V_PERIOD_END, 0);
  CHECK_EQ(M.period.p.start, 480);
  CHECK_EQ(M.period.p.end, 485);
  period_t w[PERIODS_PER_DAY];
  CHECK_EQ(sched_day(1, w), 3);
  CHECK_EQ(w[1].end, 485);
  // the screens are rebuilt when a day changes, also from the bus, and not when the bus says what it had
  layout_changes = 0;
  mb_load(0x01C + (1 * PERIODS_PER_DAY + 1) * 4 + 1, 485);
  unit_poll();
  CHECK_EQ(layout_changes, 0);
  for (int i = 0; i < 2; i++) {
    mb_load((uint16_t) (0x01C + i * 4), (int16_t) (360 + i * 120));  // Monday, start and end
    mb_load((uint16_t) (0x01D + i * 4), (int16_t) (420 + i * 120));
    mb_load((uint16_t) (0x118 + i), 1);
  }
  unit_poll();
  CHECK_EQ(periods(0), 2);
  CHECK(layout_changes > 0);
  layout_changes = 0;
  mb_load(0x119, 0);  // another panel removed the second
  unit_poll();
  CHECK_EQ(periods(0), 1);
  CHECK_EQ(layout_changes, 1);
}

const check_test_t CLOCK_TESTS[] = {
    {"clock", test_clock},     {"cpu_clock", test_cpu_clock}, {"cpu_trial", test_cpu_trial},
    {"lcd_spi", test_lcd_spi}, {"schedule", test_schedule},   {0},
};
