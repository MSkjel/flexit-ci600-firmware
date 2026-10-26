// Clock and summer time. Times are Unix-style seconds but local time, like the CX60 and the stock panel keep them.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void clock_from_unix(uint32_t t);  // sets M.yy .. M.ss and M.wday
uint32_t clock_to_unix(void);      // from M.yy .. M.ss
bool clock_summer(uint32_t t);     // t is local standard time. true in Central European summer time
void clock_second(void);           // 1 Hz. Switches summer time if auto_dst and there's no ESP to send the time
void clock_follow(uint32_t t, uint32_t from);  // clock was at from, now at t (from = t at boot). Sets dst_now
void unix_split(uint32_t t, uint16_t *yy, uint8_t *mo, uint8_t *dd, uint8_t *hh, uint8_t *mi);
int clock_month_days(unsigned yy, unsigned mo);  // days in month mo (1..12) of year yy
