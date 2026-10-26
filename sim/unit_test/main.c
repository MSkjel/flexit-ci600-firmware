// Unit tests. The firmware's hw/ code runs on board_stub.c. Each test sets up what it needs, so any runs alone.
//   build/unit_test [-l | -v] [TEST...]
#include "unit_test.h"

model_t M;

// main.c's, which has the firmware's main()
void plat_value_changed(val_t v) { unit_value_changed(v); }
void plat_action(int a) {
  if (a == A_RESTART)
    board_reset();
  else
    unit_action(a);
}

// ---------------------------------------------------------------- the calls into the UI, see --wrap in the Makefile
// the week program, and the values the UI shows
int layout_changes, saver_builds, value_changes;
void __wrap_ui_layout_changed(void) { layout_changes++; }
void __real_ui_value_changed(val_t v);
void __wrap_ui_value_changed(val_t v) {
  saver_builds += v == V_SCREENSAVER;
  value_changes++;
  __real_ui_value_changed(v);
}

// keys and idle, from input.c
int keys[64], n_keys, idles, wakes, toasts, lock_notices[2];
void __wrap_ui_key(int k) {
  if (n_keys < (int) LEN(keys))
    keys[n_keys++] = k;
}
void __wrap_ui_idle(void) { idles++; }
void __wrap_ui_wake(void) { wakes++; }
void __wrap_ui_toast(const char *icon, str_t msg) {
  (void) icon;
  (void) msg;
  toasts++;
}
void __wrap_ui_key_lock_notice(bool toggled) { lock_notices[toggled]++; }

// link.c: what the ESP's messages do
int views, shown_view;
void __wrap_ui_show_view(int v) {
  views++;
  shown_view = v;
}

void forget_ui_calls(void) {
  layout_changes = saver_builds = value_changes = views = shown_view = 0;
  n_keys = idles = wakes = toasts = 0;
  memset(lock_notices, 0, sizeof lock_notices);
}

int main(int argc, char **argv) {
  sim_display_init();
  anim_init();
  static const check_test_t *const TESTS[] = {PICTURE_TESTS, ANIM_TESTS, ANIM_MOTION_TESTS, VALUES_TESTS,
                                              CLOCK_TESTS,   UNIT_TESTS, SETTINGS_TESTS,    UI_TESTS,
                                              LINK_TESTS,    NULL};
  return check_main("unit tests", TESTS, argc, argv);
}
