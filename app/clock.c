// see clock.h
#include "clock.h"
#include "model.h"
#include "plat.h"

static const uint8_t DIM[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};  // days in each month

static int leap(unsigned y) { return y % 4 == 0 && (y % 100 || y % 400 == 0); }

int clock_month_days(unsigned yy, unsigned mo) {
  return mo >= 1 && mo <= 12 ? DIM[mo - 1] + (mo == 2 && leap(yy)) : 31;
}

void unix_split(uint32_t t, uint16_t *yy, uint8_t *mo, uint8_t *dd, uint8_t *hh, uint8_t *mi) {
  uint32_t days = t / 86400, s = t % 86400;
  unsigned y = 1970, m = 0;
  while (days >= (uint32_t) (365 + leap(y)))
    days -= 365 + leap(y++);
  while (days >= (uint32_t) (DIM[m] + (m == 1 && leap(y))))
    days -= DIM[m] + (m == 1 && leap(y)), m++;
  *yy = (uint16_t) y;
  *mo = (uint8_t) (m + 1);
  *dd = (uint8_t) (days + 1);
  *hh = (uint8_t) (s / 3600);
  *mi = (uint8_t) (s / 60 % 60);
}

void clock_from_unix(uint32_t t) {
  unix_split(t, &M.yy, &M.mo, &M.dd, &M.hh, &M.mi);
  M.ss = t % 60;
  M.wday = (uint8_t) ((t / 86400 + 3) % 7);  // 1970-01-01 was a Thursday, 0 = Monday
}

static uint32_t days_to(unsigned yy, unsigned mo, unsigned dd) {  // since 1970-01-01
  uint32_t days = 0;
  for (unsigned y = 1970; y < yy; y++)
    days += 365 + leap(y);
  for (unsigned m = 1; m < mo && m <= 12; m++)
    days += DIM[m - 1] + (m == 2 && leap(yy));
  return days + (dd ? dd - 1u : 0u);
}

uint32_t clock_to_unix(void) { return days_to(M.yy, M.mo, M.dd) * 86400 + M.hh * 3600u + M.mi * 60u + M.ss; }

// 02:00 standard time on the last Sunday of March or October. Both have 31 days
static uint32_t change(unsigned y, unsigned mo) {
  uint32_t d = days_to(y, mo, 31);
  return (d - (d + 4) % 7) * 86400 + 2 * 3600u;  // weekday of day d is (d + 3) % 7, 0 = Monday. Step back to Sunday
}

bool clock_summer(uint32_t t) {
  uint16_t y;
  uint8_t mo, dd, hh, mi;
  unix_split(t, &y, &mo, &dd, &hh, &mi);
  return t >= change(y, 3) && t < change(y, 10);
}

// Wall time w is on summer time: from 03:00 on the March Sunday to 03:00 on the October Sunday. 02:xx on the
// October Sunday comes twice, so it can't tell: *unsure says so
static bool wall_summer(uint32_t w, bool *unsure) {
  uint16_t y;
  uint8_t mo, dd, hh, mi;
  unix_split(w, &y, &mo, &dd, &hh, &mi);
  uint32_t spring = change(y, 3) + 3600, autumn = change(y, 10) + 3600;
  *unsure = w >= autumn - 3600 && w < autumn;
  return w >= spring && w < autumn;
}

// dst_now is saved so a restart knows which side of a switch the clock is on. Saved right away: after a restart
// with the old flag the switch would be made again
static void set_dst(bool summer) {
  if (summer == M.dst_now)
    return;
  val_set_bus(V_DST_NOW, summer);
  plat_save(true);
}

void clock_follow(uint32_t t) {
  bool unsure, summer = wall_summer(t, &unsure);
  if (!unsure)
    set_dst(summer);
}

// Summer time is 02:00 last Sunday of March to 03:00 last Sunday of October. Sets the CU60's clock too. Only
// moves the clock in the hour after a switch, so a stale dst_now can't move it at any other time.
void clock_second(void) {
  if (!M.auto_dst || HAS(FEAT_ESP))
    return;
  uint32_t w = clock_to_unix();
  uint16_t y;
  uint8_t mo, dd, hh, mi;
  unix_split(w, &y, &mo, &dd, &hh, &mi);
  uint32_t spring = change(y, 3), autumn = change(y, 10) + 3600;
  bool forward = !M.dst_now && w >= spring && w < spring + 3600;  // 02:xx on the March Sunday doesn't exist
  bool back = M.dst_now && w >= autumn && w < autumn + 3600;      // 03:xx summer time is 02:xx again
  if (!forward && !back)
    return;
  set_dst(forward);
  clock_from_unix(forward ? w + 3600 : w - 3600);
  plat_time_set();
}
