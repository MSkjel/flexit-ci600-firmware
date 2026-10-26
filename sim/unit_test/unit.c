// hw/mbserver.c, bus.c and unit.c: the Modbus server, the bus and the CX60's registers in and out of the model
#include "unit_test.h"

// ---------------------------------------------------------------- the Modbus server

static void test_mbserver(void) {
  uint8_t q[300], r[MB_MAX_FRAME];
  mb_init(3, FW_VERSION, FW_PATCH);
  // a frame longer than any reply isn't answered, the reply would overflow
  memset(q, 0, sizeof q);
  q[0] = 3, q[1] = 0x64;
  CHECK_EQ(mb_frame(q, pbus_put_crc(q, 280), r), 0);
  // the bootloader request is only accepted exactly as the updaters send it
  uint8_t enter[8] = {3, 0x64, 2, 0};
  CHECK_EQ(mb_frame(enter, pbus_put_crc(enter, 4), r), 6);
  CHECK(MB.enter_bootloader);
  MB.enter_bootloader = false;
  uint8_t other[8] = {3, 0x64, 3, 0};
  CHECK_EQ(mb_frame(other, pbus_put_crc(other, 4), r), 0);
  CHECK(!MB.enter_bootloader);
  // write coils with a byte count that doesn't match the coil count
  uint8_t coils[16] = {3, 0x0F, 0, 0, 0, 20, 1, 0xFF};
  CHECK_EQ(mb_frame(coils, pbus_put_crc(coils, 8), r), 5);
  CHECK_EQ(r[1], 0x8F);
  // only an actual change is reported to the main loop
  uint8_t w[8] = {0, 0x06, 0, 0x0C, 0, 200};
  uint16_t reg;
  mb_frame(w, pbus_put_crc(w, 6), r);
  CHECK(mb_take_changed(&reg));
  CHECK_EQ(reg, 0x0C);
  CHECK(!mb_take_changed(&reg));
  mb_frame(w, pbus_put_crc(w, 6), r);
  CHECK(!mb_take_changed(&reg));  // same value again
  // discovery reads the input registers: our mark, the patch level and the version
  uint8_t disc[8] = {3, 0x04, 0, 0, 0, 5};
  CHECK_EQ(mb_frame(disc, pbus_put_crc(disc, 6), r), 15);
  CHECK_EQ(r[3] << 8 | r[4], FW_TYPE);
  CHECK_EQ(r[5] << 8 | r[6], 0);
  CHECK_EQ(r[7] << 8 | r[8], FW_PATCH);
  CHECK_EQ(r[9] << 8 | r[10], FW_VERSION);
  // Fan down twice, NORMAL -> MIN -> STOP. The CX60 has read MIN when STOP comes, and its relay of MIN mustn't
  // undo STOP.
  mb_write(0x000, 1);
  uint8_t rd[8] = {3, 0x03, 0, 0, 0, 1};
  CHECK_EQ(mb_frame(rd, pbus_put_crc(rd, 6), r), 7);
  CHECK_EQ(r[4], 1);
  mb_write(0x000, 0);
  uint8_t relay[8] = {0, 0x65, 0, 0, 0, 1};
  mb_frame(relay, pbus_put_crc(relay, 6), r);
  CHECK_EQ(MB.hold[0], 0);
  CHECK(MB.pending[0] & 1);  // the coil stays set for the CX60 to read STOP
  CHECK_EQ(mb_frame(rd, pbus_put_crc(rd, 6), r), 7);
  CHECK_EQ(r[4], 0);
  relay[5] = 0;
  mb_frame(relay, pbus_put_crc(relay, 6), r);
  CHECK_EQ(MB.hold[0], 0);
  CHECK(!(MB.pending[0] & 1));
}

// What the RX interrupt does with a burst. Here a link frame gets no answer, the link hasn't started.
static void test_bus_burst(void) {
  model_defaults();
  unit_init();
  uint8_t b[64], r[MB_MAX_FRAME];
  uint8_t link[PBULK_FRAME(0)] = {PBULK_ADDR, PBULK_FC_SEND, 0, 3};
  int nl = pbus_put_crc(link, PBULK_PAYLOAD);
  uint8_t rd[8] = {3, 0x03, 0, 0x0C, 0, 1}, wr[8] = {0, 0x06, 0, 0x0C, 0, 200};
  pbus_put_crc(rd, 6);
  pbus_put_crc(wr, 6);
  bool modbus = true;
  CHECK_EQ(bus_burst(link, nl, 0, 2000, 2, r, &modbus), 0);
  CHECK(!modbus);
  // the CX60 right behind a link frame: the link frame goes unanswered, the CX60's is handled
  memcpy(b, link, (size_t) nl);
  memcpy(b + nl, wr, 8);
  uint16_t reg;
  CHECK_EQ(bus_burst(b, nl + 8, 0, 2000, 2, r, &modbus), 0);
  CHECK(modbus);
  CHECK_EQ(MB.hold[0x0C], 200);
  CHECK(mb_take_changed(&reg));
  CHECK_EQ(reg, 0x0C);
  memcpy(b + nl, rd, 8);
  CHECK_EQ(bus_burst(b, nl + 8, 0, 2000, 2, r, &modbus), 7);
  CHECK(modbus);
  CHECK_EQ(r[0], 3);
  CHECK_EQ(r[4], 200);
  // a noise byte that looks like a link frame's start, in front of the CX60's
  b[0] = PBULK_ADDR;
  memcpy(b + 1, rd, 8);
  CHECK_EQ(bus_burst(b, 9, 0, 2000, 2, r, &modbus), 7);
  CHECK(modbus);
  CHECK_EQ(r[0], 3);
  CHECK_EQ(r[4], 200);
  // a link frame cut short is dropped
  modbus = true;
  CHECK_EQ(bus_burst(link, nl - 1, 0, 2000, 2, r, &modbus), 0);
  CHECK(!modbus);
}

// ---------------------------------------------------------------- the CX60's clock, which the panels keep
void relay(uint16_t reg, uint16_t v) {  // the CX60 passing a panel's change on to all panels
  uint8_t q[8] = {0, 0x65, (uint8_t) (reg >> 8), (uint8_t) reg, (uint8_t) (v >> 8), (uint8_t) v}, r[MB_MAX_FRAME];
  mb_frame(q, pbus_put_crc(q, 6), r);
}

void block(uint16_t reg, const uint16_t *v, int n) {  // an FC16 broadcast, the CX60's own blocks
  uint8_t q[MB_MAX_FRAME] = {0, 0x10, (uint8_t) (reg >> 8), (uint8_t) reg, 0, (uint8_t) n, (uint8_t) (2 * n)};
  uint8_t r[MB_MAX_FRAME];
  for (int i = 0; i < n; i++) {
    q[7 + 2 * i] = (uint8_t) (v[i] >> 8);
    q[8 + 2 * i] = (uint8_t) v[i];
  }
  mb_frame(q, pbus_put_crc(q, 7 + 2 * n), r);
}

void cx60(uint16_t reg, uint16_t v) { block(reg, &v, 1); }  // one register of a status block

static bool clock_coils(void) { return mb_pending(0x94) && mb_pending(0x95); }

static void test_bus_clock(void) {
  model_defaults();
  unit_init();
  uint32_t t = 1792328641u;  // 2026-10-18 13:04:01
  // written at :01 of each minute
  stub_rtc = t;
  clock_from_unix(t);
  unit_second();
  CHECK(clock_coils());
  CHECK_EQ((uint16_t) MB.hold[0x94], t >> 16);
  CHECK_EQ((uint16_t) MB.hold[0x95], t & 0xFFFF);
  relay(0x94, (uint16_t) (t >> 16));
  relay(0x95, (uint16_t) t);
  CHECK(!clock_coils());
  // our own write coming back leaves the clock alone
  stub_rtc = t + 1;
  clock_from_unix(stub_rtc);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK_EQ(stub_rtc, t + 1);
  clock_from_unix(t + 59);  // 13:05:00
  unit_second();
  CHECK(!clock_coils());
  clock_from_unix(t + 60);
  unit_second();
  CHECK(clock_coils());
  CHECK_EQ((uint16_t) MB.hold[0x95], (t + 60) & 0xFFFF);
  relay(0x94, (uint16_t) (t >> 16));
  relay(0x95, (uint16_t) (t + 60));
  // Another panel sets the clock 18 hours on, which changes both words. The main loop runs between the two
  // relays, and the clock is only read once both are in.
  stub_rtc = t + 60;
  uint32_t t2 = (t | 0xFFFF) + 1 + 100;
  relay(0x94, (uint16_t) (t2 >> 16));
  for (int i = 0; i < 2; i++, stub_ms += 5)
    unit_poll();
  relay(0x95, (uint16_t) t2);
  for (int i = 0; i < 2; i++, stub_ms += 5)
    unit_poll();
  CHECK_EQ(stub_rtc, t + 60);
  stub_ms += 600;
  unit_poll();
  CHECK_EQ(stub_rtc, t2);
  CHECK_EQ(clock_to_unix(), t2);
  // one only a second off isn't followed, the clocks would fall behind
  relay(0x95, (uint16_t) (t2 - 1));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK_EQ(stub_rtc, t2);
  // The settings block the CX60 sends when it starts has the clock it saved at its last power cut. Ours stays
  // and goes out right away, unless we have none.
  uint16_t old[2] = {(uint16_t) ((t2 - 7200) >> 16), (uint16_t) (t2 - 7200)};
  block(0x94, old, 2);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK_EQ(stub_rtc, t2);
  CHECK_EQ(clock_to_unix(), t2);
  unit_second();
  CHECK(clock_coils());
  CHECK_EQ((uint16_t) MB.hold[0x94], t2 >> 16);
  CHECK_EQ((uint16_t) MB.hold[0x95], t2 & 0xFFFF);
  stub_rtc = 0;
  clock_from_unix(0);
  old[1]--;
  block(0x94, old, 2);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK_EQ(stub_rtc, t2 - 7201);

  // another panel switches back first, at our 02:59:59, and ours isn't made again an hour later
  M.features = 0;
  M.auto_dst = true;
  M.dst_now = true;
  uint32_t autumn = 1792893600u;  // 2026-10-25 02:00 standard
  stub_rtc = autumn + 3599;
  clock_from_unix(stub_rtc);
  relay(0x94, (uint16_t) (autumn >> 16));
  relay(0x95, (uint16_t) autumn);
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK(!M.dst_now);
  CHECK_EQ(stub_rtc, autumn);
  clock_from_unix(autumn + 3600);
  clock_second();
  CHECK(!M.dst_now);
  CHECK_EQ(M.hh, 3);
  CHECK_EQ(M.mi, 0);
  // a stock panel's 02:00 after our spring switch is followed without switching again
  uint32_t spring = 1774749600u;  // 2026-03-29 02:00
  M.dst_now = false;
  stub_rtc = spring;
  clock_from_unix(spring);
  clock_second();
  CHECK(M.dst_now);
  CHECK_EQ(stub_rtc, spring + 3600);
  CHECK(clock_coils());
  relay(0x94, (uint16_t) ((spring + 1) >> 16));
  relay(0x95, (uint16_t) (spring + 1));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  clock_second();
  CHECK(M.dst_now);
  CHECK_EQ(stub_rtc, spring + 1);
  CHECK_EQ(M.hh, 2);
}

// ---------------------------------------------------------------- the installer's settings and the manual tests
static void test_advanced(void) {
  model_defaults();
  unit_init();
  // They come and go by register like the other settings
  cx60(0x159, (uint16_t) -150);
  unit_poll();
  CHECK_EQ(M.adv.deice_temp, -150);
  val_set(V_DEICE_TEMP, -200);
  CHECK_EQ(MB.hold[0x159], -200);
  CHECK(mb_pending(0x159));
  // DIP switches, the panels the unit found and the filter changes
  cx60(0x10F, 1);
  cx60(0x112, 1);
  cx60(0x09B, 0x0703);
  cx60(0x0CC, 4);
  unit_poll();
  CHECK_EQ(M.dip, 0x9);
  CHECK_EQ(M.panel_version[1], 0x0703);
  CHECK(!M.panel_version[0]);
  CHECK_EQ(M.filter_resets, 4);
  cx60(0x10F, 0);
  unit_poll();
  CHECK_EQ(M.dip, 0x8);
  // From the ESP after we started, like the CX60 sent it. A change of ours the CX60 hasn't read yet stays.
  plat_cx60_reg(0x0B5, 75);
  unit_poll();
  CHECK_EQ(M.adv.pi[0], 75);
  val_set(V_PI_HEAT_SUP_I, 500);
  plat_cx60_reg(0x0B6, 900);
  unit_poll();
  CHECK_EQ(M.adv.pi[1], 500);
  CHECK_EQ(MB.hold[0x0B6], 500);
  // A test writes its value, then its enable, and is over after a minute like in the CX60
  val_set(V_TEST_SPEED, MODE_MAX + 1);
  CHECK_EQ(MB.hold[0x0A8], MODE_MAX);
  CHECK_EQ(MB.hold[0x143], 1);
  CHECK(mb_pending(0x0A8));
  CHECK(mb_pending(0x143));
  val_set(V_TEST_HEATER, 1);
  CHECK_EQ(MB.hold[0x145], 1);
  CHECK_EQ(MB.hold[0x146], 1);
  for (int s = 0; s < 59; s++)
    unit_second();
  CHECK_EQ(M.test_speed, MODE_MAX + 1);
  CHECK(M.test_heater);
  unit_second();
  CHECK(!M.test_speed && !M.test_heater);
  // Off ends it right away
  val_set(V_TEST_ROTOR, 1);
  CHECK_EQ(MB.hold[0x148], 1);
  val_set(V_TEST_ROTOR, 0);
  CHECK(!MB.hold[0x148]);
}

// The CX60's limits are wider than the editors', so what it sends is taken if it fits the field. A list is kept to
// its options.
static void test_bus_values(void) {
  model_defaults();
  unit_init();
  cx60(R_SUPPLY_MAX, 500);
  cx60(R_FAN_MIN_SUP, 300);
  cx60(R_DEICE_TEMP, (uint16_t) -500);
  unit_poll();
  CHECK_EQ(M.supply_max, 500);
  CHECK_EQ(M.adv.deice_temp, -500);  // past the editors' 450 and -450
  CHECK_EQ(M.fan_pct[0][0], 255);    // not 300 cut to 8 bits, 44
  cx60(R_BOOST_SPEED, 9);
  cx60(R_FIRE_MODE, 0);
  cx60(R_COOLING, 2);
  unit_poll();
  CHECK_EQ(M.adv.boost_speed, MODE_MAX);
  CHECK_EQ(M.adv.fire_mode, 1);
  CHECK(M.cooling_enabled);
  // From the ESP's copy of the registers too
  plat_cx60_reg(R_AWAY_SPEED, -3);
  unit_poll();
  CHECK_EQ(M.adv.away_speed, MODE_MIN);
  // The same value again is no change, even one that didn't fit
  int was = value_changes;
  val_update(V_FAN_MIN_SUP, 300);
  val_update(V_BOOST_SPEED, 9);
  val_update(V_SUPPLY_MAX, 500);
  CHECK_EQ(value_changes, was);
  val_update(V_FAN_MIN_SUP, 60);
  CHECK_EQ(value_changes, was + 1);
  CHECK_EQ(M.fan_pct[0][0], 60);
}

// ---------------------------------------------------------------- the CX60's registers, in and out of the model
static void test_unit(void) {
  model_defaults();
  unit_init();
  toasts = 0;
  // The CX60 ORs the heater flags of the panels present, so off has to clear all three and on only sets ours.
  // What it made of them, 0x10E, is what's shown.
  cx60(0x10E, 1);
  unit_poll();
  CHECK(M.heater_enabled);
  val_set(V_HEATER, 0);
  CHECK(!MB.hold[0x13F] && !MB.hold[0x140] && !MB.hold[0x141] && mb_pending(0x13F) && mb_pending(0x140) &&
        mb_pending(0x141));
  for (uint16_t r = 0x13F; r <= 0x141; r++)
    relay(r, 0);
  cx60(0x10E, 0);
  unit_poll();
  CHECK(!M.heater_enabled);
  val_set(V_HEATER, 1);
  CHECK(M.heater_enabled);
  CHECK_EQ(MB.hold[0x141], 1);
  CHECK(mb_pending(0x141));
  CHECK(!mb_pending(0x13F));
  CHECK(!mb_pending(0x140));
  relay(0x141, 1);
  for (int s = 0; s < 3; s++) {  // our flag is one vote, and the unit didn't take it: no heater, say
    cx60(0x0C3, (uint16_t) (200 + s));
    unit_poll();
    stub_ms += 1000;
    unit_second();
  }
  CHECK(!M.heater_enabled);
  CHECK_EQ(toasts, 1);
  toasts = 0;
  cx60(0x10E, 1);
  unit_poll();
  CHECK(M.heater_enabled);

  // The mode set and the one running. A boost runs MAX while NORMAL is set, and Boost -> Max on the overlay
  // then writes MAX all the same.
  cx60(0x000, MODE_NORMAL);
  cx60(0x0BF, MODE_MAX);
  cx60(0x0C1, 600);
  unit_poll();
  CHECK_EQ(M.mode, MODE_NORMAL);
  CHECK_EQ(M.mode_now, MODE_MAX);
  CHECK(M.boost_on);
  CHECK_EQ(M.boost_left, 10);
  val_set(V_BOOST, 0);
  val_set(V_MODE, MODE_MAX);
  CHECK(mb_pending(0x13B));
  CHECK_EQ(MB.hold[0x000], MODE_MAX);
  CHECK(mb_pending(0x000));
  relay(0x13B, 1);
  relay(0x000, MODE_MAX);
  cx60(0x0C1, 0);
  unit_poll();
  CHECK(!M.boost_on && !M.boost_left);

  // the setpoint in effect is 0xC2, 0xBE is the one set
  cx60(0x0BE, 210);
  cx60(0x0C2, 190);
  unit_poll();
  CHECK_EQ(M.setpoint_now, 190);
  CHECK_EQ(M.setpoint, 210);

  // extract regulation needs the extract sensor switched on, which the stock panel does along with it
  cx60(0x114, REG_SUPPLY);
  unit_poll();
  val_set(V_REGULATION, REG_EXTRACT);
  CHECK_EQ(MB.hold[0x114], REG_EXTRACT);
  CHECK(mb_pending(0x114));
  CHECK_EQ(MB.hold[0x136], 1);
  CHECK(mb_pending(0x136));
  // supply air too cold, 0x0C2 is min + 6
  cx60(0x00D, 160);
  cx60(0x0C2, 166);
  unit_poll();
  CHECK_EQ(M.supply_min, 160);
  CHECK_EQ(M.setpoint_now, 190);

  cx60(0x114, 7);  // anything but 0 is supply air to the CX60
  unit_poll();
  CHECK_EQ(M.regulation, REG_SUPPLY);

  // not the master, the ESP writes the setpoint, or we do while it's offline
  cx60(0x00F, 1);
  unit_poll();
  M.esp_online = true;
  val_set(V_SETPOINT, 215);
  CHECK(!mb_pending(0x00C));
  M.esp_online = false;
  val_set(V_SETPOINT, 220);
  CHECK(mb_pending(0x00C));
  CHECK_EQ(MB.hold[0x00C], 220);
  relay(0x00C, 210);
  unit_poll();
  CHECK_EQ(M.setpoint, 210);

  // EC fans come from the DIP switches, not the menu
  cx60(0x112, 0);
  unit_poll();
  val_set(V_FEAT_FIRST + FEAT_EC_FANS, 0);
  CHECK(HAS(FEAT_EC_FANS));

  // the CX60's block and another panel like us change the week program, a stock panel's relays don't
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  cx60(0x117, 0);
  unit_poll();
  CHECK(!M.week_program);
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  relay(0x117, 0);
  unit_poll();
  CHECK(!M.week_program);
  cx60(0x099, 0x0712);  // stock 7.18 at address 1
  val_set(V_WEEK_PROGRAM, 1);
  relay(0x117, 1);
  relay(0x117, 0);
  unit_poll();
  CHECK(M.week_program);

  // what the CX60 holds is shown as it is, its limits are wider than the editors'
  cx60(0x091, 300);
  cx60(0x016, 0);
  unit_poll();
  CHECK_EQ(M.boost_minutes, 300);
  CHECK_EQ(M.filter_days, 0);
  CHECK_EQ(filter_life(), 0);
  CHECK_EQ(filter_days_left(), 0);
  // a filter interval that isn't whole months is due when the CX60 says
  cx60(0x016, 100);
  cx60(0x0C0, 90 * 24);
  unit_poll();
  CHECK_EQ(M.filter_days, 100);
  CHECK_EQ(filter_days_left(), 10);

  // A boost the CX60 doesn't count down, nothing answers at address 1, is stopped after a while and the user told
  val_set(V_BOOST, 1);
  CHECK(mb_pending(0x13A));
  for (int s = 0; s < 12; s++) {  // counting down
    cx60(0x0C1, (uint16_t) (1800 - s));
    unit_poll();
    unit_second();
  }
  CHECK(M.boost_on && !mb_pending(0x13B) && !toasts);
  for (int s = 0; s < 10; s++) {  // stuck, 9 seconds without a change
    cx60(0x0C1, 1700);
    unit_poll();
    unit_second();
  }
  CHECK(!mb_pending(0x13B) && !toasts);
  cx60(0x0C1, 1700);
  unit_poll();
  unit_second();
  CHECK(mb_pending(0x13B));
  CHECK_EQ(toasts, 1);
  cx60(0x0C1, 0);
  unit_poll();

  // a stock panel sets back a time set on the keys: one toast, a later change is just followed
  toasts = 0;
  uint32_t t = 1792328641u;  // 2026-10-18 13:04:01
  stub_rtc = t;
  clock_from_unix(t);
  val_set(V_HOUR, 15);
  CHECK(clock_coils());
  CHECK_EQ(stub_rtc, t + 7200);
  relay(0x94, (uint16_t) ((t + 1) >> 16));
  relay(0x95, (uint16_t) (t + 1));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK_EQ(stub_rtc, t + 1);
  CHECK_EQ(toasts, 1);
  for (int s = 0; s < 5; s++)
    unit_second();
  relay(0x95, (uint16_t) (t + 600));
  unit_poll();
  stub_ms += 600;
  unit_poll();
  CHECK_EQ(stub_rtc, t + 600);
  CHECK_EQ(toasts, 1);
}

const check_test_t UNIT_TESTS[] = {
    {"mbserver", test_mbserver},
    {"bus_burst", test_bus_burst},
    {"bus_clock", test_bus_clock},
    {"unit", test_unit},
    {"advanced", test_advanced},
    {"bus_values", test_bus_values},
    {0},
};
