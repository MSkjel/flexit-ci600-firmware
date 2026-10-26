// app/values.c, panel.c and trend.c, and how ui/ writes numbers: the model's values, the ESP's items and the lights
#include "unit_test.h"

// ---------------------------------------------------------------- number and time formatting
static void test_formats(void) {
  char b[24];
  CHECK_STR(fmt_dec(b, sizeof b, 12345, 3), "12.345");
  CHECK_STR(fmt_dec(b, sizeof b, -12345, 2), "-123.45");
  CHECK_STR(fmt_dec(b, sizeof b, -5, 1), "-0.5");
  CHECK_STR(fmt_dec(b, sizeof b, 7, 0), "7");
  CHECK_STR(fmt_dec(b, sizeof b, INT32_MIN, 1), "-214748364.8");
  M.clock_24h = false;
  CHECK_STR(fmt_hm(b, sizeof b, 0), "12:00 am");
  CHECK_STR(fmt_hm(b, sizeof b, 13 * 60 + 5), "1:05 pm");
  CHECK_STR(fmt_hm(b, sizeof b, 1440), "12:00 am");
  M.clock_24h = true;
  CHECK_STR(fmt_hm(b, sizeof b, 13 * 60 + 5), "13:05");
  CHECK_STR(fmt_hm(b, sizeof b, 1440), "24:00");
  // a period's start: 0..1435 in 15-minute steps
  CHECK_EQ(step_value(1425, 1, 15, 0, 1435), 1435);
  CHECK_EQ(step_value(1435, -1, 15, 0, 1435), 1425);
  CHECK_EQ(step_value(0, -1, 15, 0, 1435), 0);
  CHECK_EQ(step_value(960, 1, 15, 5, 1440), 975);
  CHECK_EQ(step_value(-10, 3, 5, -90, 90), 5);
  CHECK_EQ(step_value(-88, -1, 5, -90, 90), -90);
  // no space before % or °, but µg/m³ starts with the same byte as °
  CHECK_STR(add_unit(strcpy(b, "5"), sizeof b, "%"), "5%");
  CHECK_STR(add_unit(strcpy(b, "21"), sizeof b, "\xC2\xB0"), "21\xC2\xB0");
  CHECK_STR(add_unit(strcpy(b, "12"), sizeof b, "\xC2\xB5g/m\xC2\xB3"), "12 \xC2\xB5g/m\xC2\xB3");
  CHECK_STR(fmt_pct(b, sizeof b, 50), "50%");
  M.language = 1;  // Norwegian puts a space before %
  CHECK_STR(fmt_pct(b, sizeof b, 50), "50 %");
  CHECK_STR(add_unit(strcpy(b, "5"), sizeof b, "%"), "5 %");
  M.language = 0;
  // texts are cut to fit their buffer
  CHECK_STR(fmt_dec(b, 5, 123456, 0), "1234");
  CHECK_STR(add_text(strcpy(b, "ab"), 4, "cd"), "abc");
}

// ---------------------------------------------------------------- values
static void test_values(void) {
  model_defaults();
  for (int v = V_NONE + 1; v < V_COUNT; v++) {
    val_def_t d = val_def((val_t) v);
    CHECK(d.min <= d.max);
    if (d.key)
      CHECK_EQ((int) val_by_key(d.key), v);  // keys must be unique, they identify values on the link and in flash
    if ((d.flags & (VF_CX60 | VF_BY_HAND)) == VF_CX60)
      CHECK_EQ((int) val_by_cx60(val_cx60_reg((val_t) v)), v);
  }
  val_set(V_BRIGHTNESS, 150);
  CHECK_EQ(M.brightness, 100);  // clamped
  val_set(V_ROOM_TEMP, 5);
  CHECK_EQ(M.t_room, T_NONE);  // read-only
  val_set_remote(V_BUS_ADDR, 2);
  CHECK_EQ(M.bus_addr, 3);  // the ESP may only read it
  val_load(V_THEME, 9);
  CHECK_EQ(M.theme, N_THEMES - 1);  // loaded from flash, also clamped
}

// ---------------------------------------------------------------- the ESP's items
static void trend_hours(int h) {
  for (int i = 0; i < h * 3600; i++)
    trend_second();
}

static void test_items(void) {
  model_defaults();
  val_forget_esp();
  memset(M.item_trends, 0, sizeof M.item_trends);
  M.esp_online = true;
  item_t *co2 = &M.items[2], *target = &M.items[3], *button = &M.items[4];
  *co2 = (item_t) {.type = ITEM_SENSOR,
                   .flags = ITEMF_VIEW | ITEMF_TREND | ITEMF_LEVELS,
                   .id = 77,
                   .max = 2000,
                   .level = {0, 0, 800, 1200}};
  *target = (item_t) {.type = ITEM_NUMBER, .flags = ITEMF_MENU, .id = 78, .step = 50, .min = 400, .max = 1500};
  *button = (item_t) {.type = ITEM_BUTTON, .flags = ITEMF_VIEW, .id = 79, .max = 1};
  val_t v = V_ITEM_FIRST + 2;
  CHECK(!val_known(V_ITEM_FIRST));
  CHECK(val_known(v));
  CHECK(item_of(v) == co2);
  CHECK(item_by_id(77) == co2);
  CHECK(!item_by_id(5));
  val_set_remote(v, 2500);  // from the ESP, taken as it is
  CHECK_EQ(co2->value, 2500);
  CHECK_EQ(item_pct(co2), 100);
  val_set(v, 400);  // a sensor can't be set on the panel
  CHECK_EQ(co2->value, 2500);
  val_set(v + 1, 2000);
  CHECK_EQ(target->value, 1500);  // a number the user sets is kept in its range
  char b[24];
  CHECK_STR(item_text(b, sizeof b, button, true), _(S_PRESS));
  item_t *washer = &M.items[6];
  *washer = (item_t) {.type = ITEM_TEXT, .flags = ITEMF_VIEW, .id = 80, .value = ITEM_UNKNOWN, .u.text = "Washing"};
  CHECK_STR(item_text(b, sizeof b, washer, true), "Washing");
  CHECK(val_def(v + 4).flags & VF_RO);
  washer->u.text[0] = 0;
  CHECK_STR(item_text(b, sizeof b, washer, true), "\xE2\x80\x93");

  trend_keep(77);
  for (int id = 100; id < 100 + N_TRENDS; id++)
    trend_keep(id);
  CHECK(trend_of(77) && trend_of(100) && !trend_of(100 + N_TRENDS - 1) && !trend_of(0));  // room for N_TRENDS
  M.items[5] = (item_t) {.type = ITEM_SENSOR, .flags = ITEMF_TREND, .id = 200, .value = 7};
  trend_hours(1);
  const item_trend_t *t = trend_of(77);
  CHECK(t);
  CHECK_EQ(t->pt[TREND_LEN - 1], 2500);
  CHECK_EQ(t->pt[TREND_LEN - 5], TREND_NONE);
  CHECK(!trend_of(100));  // no item has that id, so it's freed at its first point
  CHECK(trend_of(200));
  CHECK_EQ(trend_of(200)->pt[TREND_LEN - 1], 7);  // and an item that wants a trend gets the slot
  // The sensor becomes unavailable: a dash, not coloured by its levels, and no trend points
  T.accent = lv_color_hex(0x0000FF);
  T.err = lv_color_hex(0xFF0000);
  val_set_remote(v, ITEM_UNKNOWN);
  CHECK_STR(item_text(b, sizeof b, co2, true), "\xE2\x80\x93");
  CHECK(!item_pct(co2));
  CHECK_EQ(item_color(co2).full, T.accent.full);
  for (int i = 0; i < 900; i++)
    trend_second();
  CHECK_EQ(t->pt[TREND_LEN - 1], TREND_NONE);
  CHECK_EQ(t->pt[TREND_LEN - 2], 2500);
  val_forget_esp();
  trend_hours(23);
  CHECK(trend_of(77) && trend_of(200));  // ESP gone for less than a day, kept
  trend_hours(1);
  CHECK(!trend_of(77) && !trend_of(200));
}

// ---------------------------------------------------------------- the LEDs
static void test_leds(void) {
  model_defaults();
  bool l[3];
  M.led_ok = LED_OK_ALWAYS;
  panel_leds(l, 0);
  CHECK(!l[0] && !l[1] && l[2]);
  M.alarms[0] = (alarm_t) {.code = ALARM_FILTER, .active = true};
  M.n_alarms = 1;
  panel_leds(l, 0);
  CHECK(!l[0] && l[1] && !l[2]);
  M.alarms[0].code = ALARM_FILTER_GUARD;  // on the filter light too, as stock has it
  panel_leds(l, 0);
  CHECK(!l[0] && l[1] && !l[2]);
  M.alarms[1] = (alarm_t) {.code = 9, .active = true};
  M.n_alarms = 2;
  panel_leds(l, 0);
  CHECK(l[0] && l[1] && !l[2]);
  M.n_alarms = 0;
  M.led_ok = LED_OK_NOT_NIGHT;
  M.night = true;
  M.night_from = 22;
  M.night_to = 7;
  M.hh = 23;
  panel_leds(l, 0);
  CHECK(!l[2]);
  // a blink set by the ESP actually blinks
  M.leds = 3 << 4;  // the OK LED
  panel_leds(l, 100);
  bool a = l[2];
  panel_leds(l, 600);
  CHECK(a != l[2]);
  M.leds = 0;
  M.night = false;
}

const check_test_t VALUES_TESTS[] = {
    {"formats", test_formats}, {"values", test_values}, {"items", test_items}, {"leds", test_leds}, {0},
};
