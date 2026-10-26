// What the UI gets: the fonts, the keys from app/input.c and the menus
#include "unit_test.h"

// ---------------------------------------------------------------- the fonts
// The letters' opacity table only has room for 4 bpp, see lv_draw_sw_letter.c in lib/lvgl-ci600.patch
static void test_fonts(void) {
  const lv_font_t *fonts[] = {&font_f12, &font_f14, &font_f16, &font_f20, &font_f28, &font_n40,
                              &font_n64, &icons_16, &icons_22, &icons_32, &icons_48};
  for (size_t i = 0; i < LEN(fonts); i++)
    CHECK(((const lv_font_fmt_txt_dsc_t *) fonts[i]->dsc)->bpp <= 4);
}

// ---------------------------------------------------------------- keys and idle (input.c): what reaches the UI
uint32_t now_ms = 1000;

// Hold key k for ms with the keys in also held the whole time. Calls input.c every 5 ms like the main loop.
static void hold(int k, uint32_t ms, uint8_t also) {
  uint8_t bit = (uint8_t) (1u << k);
  input_keys(bit, 0, bit | also, now_ms);
  for (uint32_t t = 0; t < ms; t += 5) {
    now_ms += 5;
    input_keys(0, 0, bit | also, now_ms);
    input_tick(now_ms);
  }
  input_keys(0, bit, also, now_ms);
}

static void test_input(void) {
  model_defaults();
  M.idle_after = 0;  // 15 s
  input_tick(now_ms);
  input_activity();  // awake, whatever the tests before left
  forget_ui_calls();
  hold(IN_OK, 50, 0);
  CHECK_EQ(n_keys, 1);
  CHECK_EQ(keys[0], KEY_OK);
  // idle at 15 s exactly
  now_ms += 14000;
  input_tick(now_ms);
  CHECK(!input_idle());
  now_ms += 1000 - 50;
  input_tick(now_ms);
  CHECK(input_idle());
  CHECK_EQ(idles, 1);
  // the first press only wakes it
  n_keys = 0;
  hold(IN_DOWN, 50, 0);
  CHECK(!input_idle());
  CHECK_EQ(wakes, 1);
  CHECK_EQ(n_keys, 0);
  // a long OK fires once, and tapping another key meanwhile doesn't fire it again
  hold(IN_OK, 2500, 0);
  CHECK_EQ(n_keys, 2);
  CHECK_EQ(keys[0], KEY_OK);
  CHECK_EQ(keys[1], KEY_OK_LONG);
  n_keys = 0;
  uint8_t ok = 1u << IN_OK;
  input_keys(ok, 0, ok, now_ms);
  for (int i = 0; i < 500; i++) {
    now_ms += 5;
    input_keys(0, 0, ok, now_ms);
  }
  hold(IN_DOWN, 20, ok);
  for (int i = 0; i < 100; i++) {
    now_ms += 5;
    input_keys(0, 0, ok, now_ms);
  }
  input_keys(0, ok, 0, now_ms);
  CHECK_EQ(n_keys, 3);
  CHECK_EQ(keys[1], KEY_OK_LONG);
  CHECK_EQ(keys[2], KEY_DOWN);
  // BACK tapped is BACK, held is HOME with no BACK after it
  n_keys = 0;
  hold(IN_BACK, 100, 0);
  hold(IN_BACK, 1000, 0);
  CHECK_EQ(n_keys, 2);
  CHECK_EQ(keys[0], KEY_BACK);
  CHECK_EQ(keys[1], KEY_HOME);
  // holding HELP for 3 s locks the keys, then a key press is refused with a toast
  n_keys = 0;
  hold(IN_HELP, 3100, 0);
  CHECK(M.key_lock);
  CHECK_EQ(n_keys, 0);
  CHECK_EQ(lock_notices[true], 1);
  hold(IN_OK, 50, 0);
  CHECK_EQ(n_keys, 0);
  CHECK_EQ(lock_notices[false], 1);
  hold(IN_HELP, 3100, 0);
  CHECK(!M.key_lock);
  CHECK_EQ(lock_notices[true], 2);
  CHECK_EQ(lock_notices[false], 1);
  // UP held repeats at 450 ms, then every 120 ms
  n_keys = 0;
  hold(IN_UP, 1000, 0);
  CHECK_EQ(n_keys, 6);
}

// ---------------------------------------------------------------- the menus
static void test_menus(void) {
  model_defaults();
  char why[96] = "";
  int bad = menu_check(why, sizeof why);
  CHECK_MSG(!bad, "%d problems, the first: %s", bad, why);
}

const check_test_t UI_TESTS[] = {
    {"fonts", test_fonts},
    {"input", test_input},
    {"menus", test_menus},
    {0},
};
