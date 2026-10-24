// Headless simulator. Runs the UI on fake data and saves screenshots.
//   ./sim OUTDIR            every scene as OUTDIR/<name>.ppm
//   ./sim --perf            how much of the screen each key repaints
#include "ui.h"
#include "plink.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *outdir;
static int failed;  // screens low on LVGL heap, steps that ended up on the wrong screen, and failed checks

static uint32_t now;  // ms, for input.c

static void run(int ms) {
  for (int t = 0; t < ms; t += 5) {
    now += 5;
    input_tick(now);
    sim_run(5);
  }
}

static void seconds(int n) {
  while (n--) {
    run(1000);
    ui_second();
  }
}

static void shot(const char *name) {
  if (!sim_shot(outdir, name))
    failed++;
}

// For the checks: in golden.txt, not in the gallery
static void check_shot(const char *name) {
  char dir[256];
  snprintf(dir, sizeof dir, "%s/checks", outdir);
  sim_mkdir(dir);
  if (!sim_shot(dir, name))
    failed++;
}

// Press and release a key through input.c. HOME is BACK held for 1 s, OK_LONG is OK held for 2.2 s.
static void tap(int k) {
  int in = k == KEY_HOME ? IN_BACK : k == KEY_OK_LONG ? IN_OK : k - KEY_UP;
  uint8_t bit = (uint8_t) (1u << in);
  int hold = k == KEY_HOME ? 1000 : k == KEY_OK_LONG ? 2200 : 0;
  input_keys(bit, 0, bit, now);
  for (int t = 0; t < hold; t += 5) {
    now += 5;
    input_keys(0, 0, bit, now);
  }
  input_keys(0, bit, 0, now);
}

static void key(int k) {
  tap(k);
  run(30);
}
// Press DOWN until the selected row reads text, lists wrap around. Going by name so the scenes don't break
// when a menu changes.
static void go_text(const char *text) {
  for (int i = 0; i < 40; i++) {
    const char *t = ui_selected();
    if (t && !strcmp(t, text))
      return;
    key(KEY_DOWN);
  }
  printf("no row \"%s\" on \"%s\"\n", text, ui_title());
  failed++;
}

static void go(str_t row) { go_text(tr(row)); }

// OK on a row and check the title of the screen it opens.
static void open_as(str_t row, str_t title) {
  go(row);
  key(KEY_OK);
  if (strcmp(ui_title(), tr(title))) {
    printf("\"%s\" opened \"%s\"\n", tr(row), ui_title());
    failed++;
  }
}

static void open(str_t row) { open_as(row, row); }

// OK on a row and check it opens the editor.
static void edit(str_t row) {
  go(row);
  key(KEY_OK);
  if (ui_top() != &SCR_EDIT) {
    printf("\"%s\" opened no editor\n", tr(row));
    failed++;
  }
}

// Fresh fake data on the home screen, so nothing carries over from the scenes before.
static void fresh(void) {
  sim_model_init();
  ui_show_view(VIEW_DASH);
}

static void settle(void) {
  if (sim_link_settle() < 0) {
    printf("the link didn't settle\n");
    failed++;
  }
}

static void set_theme(int t) {
  M.theme = t;
  ui_rebuild();
}

// Whether the bouncing ball of sim_esp_send_animation is in column x of the 48 rows from y0: its orange as RGB565
static bool ball_at(int x, int y0) {
  for (int y = y0; y < y0 + 48; y++)
    if (sim_pixel(x, y) == 0xF78221)
      return true;
  return false;
}

// The ball is somewhere in the sprite's box at x0, y0, whatever frame it's on
static bool ball_in(int x0, int y0) {
  for (int x = x0; x < x0 + 48; x++)
    if (ball_at(x, y0))
      return true;
  return false;
}

static void check(bool ok, const char *what) {
  if (!ok) {
    printf("%s\n", what);
    failed++;
  }
}

// True if a label in o or below it reads text
static bool shows(lv_obj_t *o, const char *text) {
  if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), text))
    return true;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
    if (shows(lv_obj_get_child(o, i), text))
      return true;
  return false;
}

// True if a label in o or below it reads text and is at least partly on the screen
static bool on_screen(lv_obj_t *o, const char *text) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), text) && a.y2 >= 0 && a.y1 < SCR_H)
    return true;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
    if (on_screen(lv_obj_get_child(o, i), text))
      return true;
  return false;
}

static unsigned heap_used(void) {
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  return (unsigned) (m.total_size - m.free_size);
}

static void note(const char *text) {
  strcpy(M.note, text);
  M.note_new = true;
  ui_second();
}

// Checks with no screenshot, after the scenes
static void checks(void) {
  // an item's help, a message over it and a help over that. Once the top help closes, the first one still gets
  // the ESP's answer.
  M.features |= FBIT(FEAT_ESP);  // back from standalone, the link is still up
  ui_go_home();
  key(KEY_OK);
  open(S_COMFORT);
  key(KEY_HELP);
  note("Washing machine done");
  key(KEY_HELP);
  key(KEY_OK);
  key(KEY_OK);
  settle();  // the ESP's answer
  run(30);
  char help[96];
  sim_text(help, sizeof help, SIM_ITEMS[0].help);
  check(ui_top() == &SCR_HELP && shows(lv_layer_top(), help), "the item's help didn't arrive under another help");

  // a second message replaces the first
  fresh();
  note("Washing machine done");
  note("Front door left open");
  check(shows(lv_layer_top(), "Front door left open") && !shows(lv_layer_top(), "Washing machine done"),
        "the second message isn't shown");
  key(KEY_OK);
  check(ui_top() == &SCR_HOME, "one OK didn't close the message");
  note("Washing machine done");
  key(KEY_HELP);
  note("Front door left open");
  key(KEY_OK);
  check(ui_top() == &SCR_HOME, "the first message's help stayed behind");

  // keys pressed while locked show one toast at a time
  fresh();
  M.key_lock = true;
  ui_value_changed(V_NONE);
  run(2000);
  unsigned before = heap_used();
  for (int i = 0; i < 20; i++) {
    tap(KEY_DOWN);
    run(100);
  }
  check(heap_used() < before + 1000, "toasts pile up");
  run(2000);
  check(heap_used() <= before, "the toast stays");
  M.key_lock = false;

  // a rebuild keeps the option being picked
  fresh();
  key(KEY_OK);
  open(S_DISPLAY);
  edit(S_THEME);
  key(KEY_DOWN);
  ui_layout_changed();
  run(30);
  check(!strcmp(ui_selected(), tr(THEME_NAMES[1])), "a rebuild reset the editor");
  key(KEY_OK);
  check(M.theme == 1, "OK after a rebuild saved the old value");

  // the alarm list rebuilds under a help when alarms go, and keeps its selection in the list
  fresh();
  for (int i = 0; i < 6; i++)
    M.alarms[i] = (alarm_t) {(uint8_t) (i + 1), 18, 10, 12, 51, true};
  M.n_alarms = 6;
  ui_push(&SCR_ALARMS, ALARMS_NOW);
  for (int i = 0; i < 5; i++)
    key(KEY_DOWN);
  key(KEY_HELP);
  M.n_alarms = 1;
  ui_alarms_changed();
  run(30);
  check(!shows(lv_scr_act(), alarm_text(6)), "the alarm list under the help kept a cleared alarm");
  key(KEY_BACK);
  check(ui_frame()->sel == 0 && ui_frame()->top == 0, "the alarm list's selection is past its end");
  M.n_alarms = 0;

  // the alarm pop-up follows the alarms, and closes once they're gone
  fresh();
  M.alarms[0] = (alarm_t) {9, 18, 10, 12, 51, true};
  M.n_alarms = 1;
  ui_push(&SCR_ALARM_POPUP, 9);
  M.alarms[0] = (alarm_t) {10, 18, 10, 8, 14, true};
  ui_alarms_changed();
  run(30);
  check(shows(lv_layer_top(), alarm_text(10)), "the alarm pop-up kept an alarm that changed");
  M.n_alarms = 0;
  ui_alarms_changed();
  ui_second();
  check(ui_top() != &SCR_ALARM_POPUP, "the alarm pop-up stayed after the alarms went");

  // the pop-up shows the new alarm, also under the screensaver, and doesn't come back on wake
  fresh();
  ui_second();
  M.alarms[0] = (alarm_t) {4, 18, 10, 12, 51, true};
  M.n_alarms = 1;
  ui_alarms_changed();
  ui_second();
  key(KEY_BACK);
  M.alarms[1] = (alarm_t) {9, 18, 10, 13, 5, true};
  M.n_alarms = 2;
  ui_alarms_changed();
  ui_second();
  check(shows(lv_layer_top(), alarm_text(9)) && !shows(lv_layer_top(), alarm_text(4)),
        "the alarm pop-up didn't show the new alarm");
  M.idle_after = 0;  // 15 s
  seconds(16);
  check(ui_top() == &SCR_SAVER, "the screensaver didn't cover the alarm pop-up");
  M.alarms[2] = M.alarms[1];
  M.alarms[1] = (alarm_t) {7, 18, 10, 13, 20, true};
  M.n_alarms = 3;
  ui_alarms_changed();
  ui_second();
  run(30);
  check(shows(lv_layer_top(), alarm_text(7)) && !shows(lv_layer_top(), alarm_text(9)),
        "the alarm pop-up didn't show the newer alarm");
  key(KEY_BACK);
  check(ui_top() == &SCR_HOME, "a new alarm under the screensaver stacked a second pop-up");
  M.alarms[3] = (alarm_t) {8, 18, 10, 13, 30, true};
  M.n_alarms = 4;
  ui_alarms_changed();
  ui_second();
  seconds(16);
  M.n_alarms = 0;
  ui_alarms_changed();
  tap(KEY_OK);  // only wakes it
  run(30);
  check(ui_top() == &SCR_HOME && !shows(lv_layer_top(), alarm_text(8)), "the gone alarm's pop-up came back on wake");
  ui_second();

  // only a more serious alarm takes over the pop-up, or the next one once the shown alarm goes
  fresh();
  M.alarms[0] = (alarm_t) {2, 18, 10, 2, 5, true};
  M.n_alarms = 1;
  ui_alarms_changed();
  ui_second();
  M.alarms[1] = (alarm_t) {9, 18, 10, 2, 10, true};
  M.alarms[2] = (alarm_t) {ALARM_FILTER, 18, 10, 2, 15, true};
  M.n_alarms = 3;
  ui_alarms_changed();
  ui_second();
  run(30);
  check(shows(lv_layer_top(), alarm_text(2)), "a less serious alarm took over the pop-up");
  key(KEY_HELP);
  memmove(&M.alarms[1], &M.alarms[0], 3 * sizeof *M.alarms);
  M.alarms[0] = (alarm_t) {1, 18, 10, 2, 20, true};
  M.n_alarms = 4;
  ui_alarms_changed();
  ui_second();
  run(30);
  check(ui_top() == &SCR_ALARM_POPUP && shows(lv_layer_top(), alarm_text(1)),
        "a more serious alarm didn't take over the pop-up");
  M.alarms[0] = M.alarms[2];
  M.alarms[1] = M.alarms[3];
  M.n_alarms = 2;
  ui_alarms_changed();
  run(30);
  check(ui_top() == &SCR_ALARM_POPUP && shows(lv_layer_top(), alarm_text(9)),
        "the next alarm didn't take over the pop-up once its own went");
  M.alarm_popups = POPUPS_NOT_FILTER;
  M.alarms[0] = M.alarms[1];
  M.n_alarms = 1;
  ui_alarms_changed();
  run(30);
  check(ui_top() == &SCR_HOME, "the pop-up showed the filter reminder, or a help stayed behind");
  M.n_alarms = 0;
  ui_second();

  // read-only ESP numbers, buttons and toggles do nothing on OK, and a toggle shows On or Off, not a switch
  fresh();
  M.items[3].flags |= ITEMF_RO;  // fireplace time
  for (int i = 0; i < SIM_N_ITEMS; i++) {
    if (!strcmp(SIM_ITEMS[i].name, "vacuum"))
      sim_item(i, &M.items[SIM_N_COMFORT]);
    if (!strcmp(SIM_ITEMS[i].name, "hallway_light"))
      sim_item(i, &M.items[SIM_N_COMFORT + 1]);
  }
  M.items[SIM_N_COMFORT].flags |= ITEMF_RO;
  M.items[SIM_N_COMFORT + 1].flags |= ITEMF_RO;
  M.items[SIM_N_COMFORT + 1].value = 1;
  key(KEY_OK);
  open(S_COMFORT);
  go_text(sim_item_label("fireplace_minutes"));
  key(KEY_OK);
  check(ui_top() == &SCR_LIST, "a read-only number opened the editor");
  key(KEY_BACK);
  open(S_SMART_HOME);
  go_text(sim_item_label("vacuum"));
  key(KEY_OK);
  check(!shows(lv_layer_top(), tr(S_SENT)), "a read-only button was pressed");
  go_text(sim_item_label("hallway_light"));
  check(shows(lv_scr_act(), tr(S_ON)), "a read-only toggle isn't shown as On");
  key(KEY_OK);
  check(M.items[SIM_N_COMFORT + 1].value == 1, "a read-only toggle was switched");

  // an ESP menu with nothing in it yet
  fresh();
  memset(M.items, 0, sizeof M.items);
  key(KEY_OK);
  open(S_SMART_HOME);
  check(shows(lv_scr_act(), tr(S_NOTHING_HERE_YET)), "the empty smart home menu isn't shown");

  // going home when idle keeps a message or alarm pop-up nobody has seen yet
  fresh();
  M.go_home = true;
  M.screensaver = 0;
  note("Washing machine done");
  ui_idle();
  check(ui_top() == &SCR_NOTE, "idle closed an unread message");
  key(KEY_OK);
  M.alarms[0] = (alarm_t) {9, 18, 10, 12, 51, true};
  M.n_alarms = 1;
  ui_push(&SCR_ALARM_POPUP, 9);
  ui_idle();
  check(ui_top() == &SCR_ALARM_POPUP, "idle closed an unread alarm pop-up");
  M.n_alarms = 0;
  ui_alarms_changed();
  M.go_home = false;

  // an ESP number with min == max opens, and the editor closes when the ESP restarts and forgets its item
  fresh();
  M.items[3].min = M.items[3].max = M.items[3].value = 15;  // fireplace time
  key(KEY_OK);
  open(S_COMFORT);
  go_text(sim_item_label("fireplace_minutes"));
  key(KEY_OK);
  check(ui_top() == &SCR_EDIT, "a number with min == max didn't open the editor");
  key(KEY_UP);
  key(KEY_BACK);
  key(KEY_OK);
  val_forget_esp();  // as a HELLO does
  ui_layout_changed();
  run(150);
  check(ui_top() != &SCR_EDIT, "the editor stayed open on an item the ESP forgot");

  // with the ESP away, its menus and the smart home view say so and an open editor closes
  fresh();
  key(KEY_OK);
  open(S_COMFORT);
  go_text(sim_item_label("fireplace_minutes"));
  key(KEY_OK);
  M.esp_online = false;
  ui_layout_changed();
  run(150);
  check(ui_top() == &SCR_LIST && shows(lv_scr_act(), tr(S_ESP_NOT_CONNECTED)),
        "an ESP item's editor or values stay with the ESP away");
  fresh();
  M.esp_online = false;
  sim_item(SIM_N_COMFORT, &M.items[SIM_N_COMFORT]);  // living room
  ui_replace(&SCR_HOME, VIEW_SMART);
  check(shows(lv_scr_act(), tr(S_ESP_NOT_CONNECTED)), "the smart home view shows values with the ESP away");

  // an ESP number steps from its min: 1, 3, 5 for a step of 2
  fresh();
  M.items[3].step = 2;  // fireplace time, 1..240
  M.items[3].value = 1;
  key(KEY_OK);
  open(S_COMFORT);
  go_text(sim_item_label("fireplace_minutes"));
  key(KEY_OK);
  key(KEY_UP);
  key(KEY_OK);
  check(M.items[3].value == 3, "an ESP number didn't step from its min");

  // the main menu and a list that get shorter while scrolled to the end don't scroll past it
  fresh();
  key(KEY_OK);
  go(S_SYSTEM);
  open(S_SYSTEM);
  key(KEY_BACK);
  M.features &= ~FBIT(FEAT_ESP);  // two tiles fewer
  ui_layout_changed();
  run(30);
  check(ui_frame()->top == 1, "the main menu kept a scroll position past its end");
  M.features |= FBIT(FEAT_ESP);
  ui_layout_changed();
  run(30);
  open(S_COMFORT);
  key(KEY_UP);  // wraps to the last row
  for (int i = 4; i < SIM_N_COMFORT; i++)
    M.items[i].flags &= (uint8_t) ~ITEMF_MENU;
  ui_layout_changed();
  run(30);
  check(ui_frame()->top == 0, "a shorter list kept its scroll position");

  // the Comfort tile only counts what its menu lists
  fresh();
  for (int i = 0; i < SIM_N_COMFORT; i++)
    M.items[i].flags &= (uint8_t) ~ITEMF_MENU;
  sim_item(SIM_N_COMFORT, &M.items[SIM_N_COMFORT]);  // so Smart home isn't empty too
  key(KEY_OK);
  check(shows(lv_scr_act(), tr(S_NOTHING_YET)), "the Comfort tile counts items its menu doesn't list");

  // night mode's hours on a 12 hour clock, and idle brightness 0 is Off in the editor
  fresh();
  M.clock_24h = false;
  key(KEY_OK);
  open(S_DISPLAY);
  open(S_NIGHT_MODE);
  check(shows(lv_scr_act(), "10:00 pm") && shows(lv_scr_act(), "7:00 am"), "night mode's hours aren't 12 hour times");
  edit(S_IDLE_BRIGHTNESS);
  check(M.night_idle_brightness == 0 && shows(lv_layer_top(), tr(S_OFF)) && !shows(lv_layer_top(), "%"),
        "0 isn't Off in the editor");
  key(KEY_BACK);

  // the day's idle brightness too, and Off turns the screen dark once idle
  fresh();
  M.idle_brightness = 0;
  M.idle_after = 0;  // 15 s
  key(KEY_OK);
  open(S_SCREENSAVER);
  edit(S_IDLE_BRIGHTNESS);
  check(shows(lv_layer_top(), tr(S_OFF)) && !shows(lv_layer_top(), "%"), "the day's idle brightness 0 isn't Off");
  key(KEY_BACK);
  ui_go_home();
  run(16000);
  check(input_idle() && input_backlight() == -1, "idle at Off, but the backlight is on");
  M.idle_brightness = 5;
  check(input_backlight() == 5, "idle at 5 %, but not at 5 %");
  key(KEY_BACK);  // wakes it up
  check(!input_idle(), "still idle after a key");

  // the trend axis goes down to -5° for a coldest -5.0°
  fresh();
  for (int i = 0; i < TREND_LEN; i++)
    M.trend[TR_OUTDOOR][i] = trend_code(TR_OUTDOOR, -50);
  ui_replace(&SCR_HOME, VIEW_TREND);
  check(shows(lv_scr_act(), "-5\xC2\xB0") && !shows(lv_scr_act(), "-10\xC2\xB0"), "the trend axis is 5° too low");

  // a comfort gauge's long label is cut to one line
  fresh();
  strcpy(M.items[1].label, "Living room humidit");
  ui_replace(&SCR_HOME, VIEW_COMFORT);
  check(!shows(lv_scr_act(), "Living room humidit"), "a long gauge label isn't cut");
}

// Samples the lights for ms and writes the OK light's blinks to out as Morse: . and - for on, a space between
// characters and | for a longer pause. False if the alarm or filter light changed meanwhile.
static bool ok_light(char *out, size_t n, int ms) {
  bool l[3], first[3], same = true;
  panel_leds(first, now);
  bool was = first[2];
  int len = 0;  // ms the OK light has been as it is
  size_t k = 0;
  out[0] = 0;
  for (int t = 0; t < ms; t += 5) {
    panel_leds(l, now);
    same &= l[0] == first[0] && l[1] == first[1];
    if (l[2] != was) {
      int u = (len + 60) / 120;  // dots
      const char *m = was ? (u == 1 ? "." : u == 3 ? "-" : "?") : u == 1 ? "" : u == 3 ? " " : u >= 7 ? "|" : "?";
      if (k + strlen(m) < n) {
        strcpy(out + k, m);
        k += strlen(m);
      }
      was = l[2];
      len = 0;
    }
    len += 5;
    run(5);
  }
  return same;
}

static void press(const int *k, int n) {
  for (int i = 0; i < n; i++)
    key(k[i]);
}

static const int ABOUT_KEYS[] = {KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN, KEY_UP, KEY_DOWN, KEY_UP, KEY_DOWN, KEY_OK};

// The notices: the keys on About open them, not on another page, after a wrong key or after a pause
static void notices(void) {
  fresh();
  key(KEY_OK);
  open(S_SYSTEM);
  open(S_BUS);
  press(ABOUT_KEYS, LEN(ABOUT_KEYS));
  check(ui_top() == &SCR_INFO, "the keys did something on the Bus page");
  key(KEY_BACK);
  open(S_ABOUT);
  press(ABOUT_KEYS, 4);
  key(KEY_DOWN);
  press(ABOUT_KEYS + 4, 5);
  check(ui_top() == &SCR_INFO, "the notices opened after a wrong key");
  press(ABOUT_KEYS, 4);
  run(3500);
  press(ABOUT_KEYS + 4, 5);
  check(ui_top() == &SCR_INFO, "the notices opened after a pause");
  press(ABOUT_KEYS, LEN(ABOUT_KEYS));
  check(ui_top() == &SCR_NOTICES, "the keys didn't open the notices");
  check_shot("91_notices");
  run(12000);
  check_shot("91b_notices_later");
  check(!on_screen(lv_scr_act(), "CI600"), "the notices didn't go up");
  run(20000);
  check(on_screen(lv_scr_act(), "CI600"), "the notices didn't come round again");

  // the OK light spells the version, the filter light goes on as before
  M.alarms[0] = (alarm_t) {.code = ALARM_FILTER, .active = true};
  M.n_alarms = 1;
  char seen[64];
  bool l[3];
  check(ok_light(seen, sizeof seen, 16000), "the notices changed the filter light");
  check(!strncmp(seen, "----- .-.-.- .---- .-.-.- -----|-", 33), "the OK light doesn't spell 0.1.0");
  // under an alarm pop-up and after leaving, the OK light is as usual
  M.alarms[0].code = 9;
  ui_push(&SCR_ALARM_POPUP, 9);
  panel_leds(l, now);
  check(ok_light(seen, sizeof seen, 1000) && !seen[0] && l[0] && !l[2], "the OK light blinks under an alarm pop-up");
  ui_pop();
  M.n_alarms = 0;
  key(KEY_BACK);
  check(ui_top() == &SCR_INFO && !strcmp(ui_title(), tr(S_ABOUT)), "BACK didn't leave the notices for About");
  panel_leds(l, now);
  check(ok_light(seen, sizeof seen, 2000) && !seen[0] && l[2], "the OK light isn't back on after the notices");
}

// The key test's labels, by their order on the screen: the fan, the score and the message
static lv_obj_t *kt_label(int i) {
  lv_obj_t *body = lv_obj_get_child(lv_scr_act(), 0);  // ui_init() makes it first
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(body); c++) {
    lv_obj_t *o = lv_obj_get_child(body, c);
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), IC_FAN))
      return lv_obj_get_child(body, (int32_t) c + i);
  }
  return NULL;
}

static bool kt_over(void) { return strstr(lv_label_get_text(kt_label(2)), tr(S_KEYTEST_AGAIN)) != NULL; }

static int kt_score(void) { return atoi(lv_label_get_text(kt_label(1))); }

// The gap of the first duct the fan hasn't passed yet, top and bottom on the screen. True if a duct is at the fan.
static bool kt_gap(int *top, int *bottom) {
  lv_obj_t *fan = kt_label(0), *body = lv_obj_get_parent(fan);
  lv_area_t f, a, b;
  lv_obj_get_coords(fan, &f);
  int best = SCR_W * 2;
  bool at = false;
  for (uint32_t c = 0; c + 1 < lv_obj_get_child_cnt(body); c++) {
    lv_obj_t *o = lv_obj_get_child(body, c);
    if (!lv_obj_check_type(o, &lv_obj_class))
      continue;
    lv_obj_get_coords(o, &a);  // the top half, the bottom half follows it
    lv_obj_get_coords(lv_obj_get_child(body, c + 1), &b);
    c++;
    at |= a.x1 <= f.x2 && a.x2 >= f.x1;
    if (a.x2 >= f.x1 && a.x1 < best) {
      best = a.x1;
      *top = a.y2 + 1;
      *bottom = b.y1;
    }
  }
  return at;
}

// Runs for steps or until the fan hits something, pressing UP with ui_key() so input.c doesn't see a key. Keeps
// the fan around aim px below the middle of the next gap. Returns the score, 0 once it hit.
static int kt_steer(int steps, int aim) {
  lv_area_t f;
  int before = 0;
  for (int i = 0; i < steps && !kt_over(); i++) {
    int top, bottom;
    kt_gap(&top, &bottom);
    lv_obj_get_coords(kt_label(0), &f);
    int y = (f.y1 + f.y2) / 2;
    if (y > (top + bottom) / 2 + aim + 4 && y >= before)
      ui_key(KEY_UP);
    before = y;
    run(40);
  }
  return kt_score();
}

// Steers like kt_steer until the score is points. The steps it took, -1 if it hit first.
static int kt_steer_to(int points, int max_steps) {
  lv_area_t f;
  int before = 0;
  for (int i = 0; i < max_steps && !kt_over(); i++) {
    if (kt_score() >= points)
      return i;
    int top, bottom;
    kt_gap(&top, &bottom);
    lv_obj_get_coords(kt_label(0), &f);
    int y = (f.y1 + f.y2) / 2;
    if (y > (top + bottom) / 2 + 4 && y >= before)
      ui_key(KEY_UP);
    before = y;
    run(40);
  }
  return -1;
}

// The key test from the notices: ready, running, over, faster with every point, BACK to About, and the
// screensaver only when nobody presses UP
static void keytest(void) {
  fresh();
  key(KEY_OK);
  open(S_SYSTEM);
  open(S_ABOUT);
  press(ABOUT_KEYS, LEN(ABOUT_KEYS));
  run(500);
  key(KEY_OK);
  check(ui_top() == &SCR_KEYTEST, "OK on the notices didn't open the key test");
  bool l[3];
  panel_leds(l, now);
  check(l[2], "the OK light didn't go back on with the key test");
  check_shot("92_keytest_ready");
  key(KEY_UP);
  check(kt_steer(150, 0) >= 1, "the score didn't go up");
  check_shot("92b_keytest_running");
  // no real key, but a running key test keeps the screensaver away
  M.idle_after = 0;  // 15 s
  kt_steer(400, 0);
  check(ui_top() == &SCR_KEYTEST && !kt_over(), "the run stopped");
  // a pop-up pauses it, UP after it carries on
  M.alarms[0] = (alarm_t) {9, 18, 10, 12, 51, true};
  M.n_alarms = 1;
  ui_push(&SCR_ALARM_POPUP, 9);
  run(80);
  lv_area_t a, b;
  lv_obj_get_coords(kt_label(0), &a);
  run(500);
  lv_obj_get_coords(kt_label(0), &b);
  check(a.y1 == b.y1, "the fan moved under a pop-up");
  ui_pop();
  M.n_alarms = 0;
  ui_refresh();
  key(KEY_UP);
  kt_steer(400, 50);  // into the bottom half of a duct
  int top, bottom;
  check(kt_over() && kt_gap(&top, &bottom), "the fan went through a duct");
  check_shot("92c_keytest_over");

  // faster with every point, and still playable at full speed
  ui_replace(&SCR_KEYTEST, 0);
  key(KEY_UP);
  int early = kt_steer_to(5, 2000) >= 0 ? kt_steer_to(10, 2000) : -1;  // steps for points 5 to 10
  int late = kt_steer_to(35, 6000) >= 0 ? kt_steer_to(40, 2000) : -1;
  check(early > 0 && late > 0 && late * 5 < early * 4, "the ducts didn't speed up, or the fan hit one");
  kt_steer(400, 50);
  run(16000);
  check(ui_top() == &SCR_SAVER, "no screensaver once the fan hit");
  key(KEY_OK);
  key(KEY_OK);
  open(S_SYSTEM);
  open(S_ABOUT);
  press(ABOUT_KEYS, LEN(ABOUT_KEYS));
  key(KEY_OK);
  key(KEY_BACK);
  check(ui_top() == &SCR_INFO && !strcmp(ui_title(), tr(S_ABOUT)), "BACK didn't leave the key test for About");
}

// For --perf. Repainted pixels are what cost time on the panel.
static void measure(const char *what, int k) {
  run(200);
  sim_px = 0;
  if (k)
    tap(k);
  else
    ui_second();
  run(200);
  printf("  %-34s %6lu px  %3lu%%\n", what, sim_px, sim_px * 100 / (SCR_W * SCR_H));
}

static void perf(void) {
  measure("idle second, home", 0);
  measure("home -> menu (OK)", KEY_OK);
  measure("idle second, menu", 0);
  measure("menu: next tile (DOWN)", KEY_DOWN);
  measure("menu: next tile (DOWN)", KEY_DOWN);
  measure("menu: previous tile (UP)", KEY_UP);
  measure("menu -> list (OK)", KEY_OK);
  measure("idle second, list", 0);
  measure("list: next row (DOWN)", KEY_DOWN);
  measure("list: next row (DOWN)", KEY_DOWN);
  measure("list: scrolls (DOWN x4, last)", KEY_DOWN);
  measure("", KEY_DOWN);
  measure("", KEY_DOWN);
  measure("", KEY_DOWN);
  measure("list -> menu (BACK)", KEY_BACK);
  measure("menu -> home (BACK)", KEY_BACK);
  measure("home: next view (BACK? no: DOWN)", KEY_DOWN);
  ui_go_home();
  ui_push(&SCR_DAY, 1);
  measure("day: next period (DOWN)", KEY_DOWN);
  measure("idle second, day", 0);
  ui_push(&SCR_NOTICES, 0);
  measure("notices going up, 200 ms", 0);
  ui_replace(&SCR_KEYTEST, 0);
  tap(KEY_UP);
  measure("key test running, 200 ms", KEY_UP);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: sim OUTDIR (every screen as OUTDIR/<name>.ppm) | sim --perf\n");
    return 2;
  }
  outdir = argv[1];
  sim_model_init();
  sim_display_init();
  ui_init();
  sim_esp_init();
  if (!strcmp(argv[1], "--perf")) {
    perf();
    return 0;
  }
  sim_mkdir(outdir);

  for (int t = 0; t < N_THEMES; t++) {
    set_theme(t);
    char n[32];
    snprintf(n, sizeof n, "theme%d_dashboard", t);
    shot(n);
  }
  set_theme(0);
  shot("01_dashboard");
  key(KEY_BACK);
  shot("02_airflow");
  key(KEY_BACK);
  shot("03_trends");
  key(KEY_BACK);
  shot("03b_sensors");
  key(KEY_BACK);
  shot("04_comfort");
  key(KEY_BACK);
  key(KEY_UP);
  shot("04c_fan_hud_opened");  // the first press only opens it
  key(KEY_UP);
  shot("05_fan_hud_max");
  key(KEY_UP);
  shot("06_fan_hud_boost");
  run(3000);
  shot("07_dashboard_boost");
  key(KEY_DOWN);
  key(KEY_DOWN);
  key(KEY_DOWN);
  run(3000);

  key(KEY_OK);
  shot("08_menu");
  key(KEY_HELP);
  shot("08b_menu_help");
  key(KEY_BACK);
  go(S_SYSTEM);
  shot("08c_menu_end");  // scrolled down to the last tiles
  open(S_TEMPERATURE);
  shot("09_temperature");
  edit(S_SETPOINT);
  key(KEY_UP);
  key(KEY_UP);
  shot("10_edit_setpoint");
  key(KEY_OK);
  shot("11_saved");
  run(2000);
  go(S_HEATER);
  shot("12_temperature_scrolled");
  key(KEY_BACK);
  open(S_VENTILATION);
  go(S_BOOST_NOW);
  shot("13_ventilation");
  key(KEY_BACK);
  open(S_COMFORT);
  shot("14_comfort_menu");
  key(KEY_BACK);
  open(S_SCHEDULE);
  shot("15_schedule_menu");
  open_as(S_WEEK_OVERVIEW, S_WEEK_PROGRAM);
  shot("16_week_program");
  key(KEY_BACK);
  open(S_TUESDAY);
  key(KEY_DOWN);
  shot("17_day");
  key(KEY_OK);
  shot("17b_period");
  edit(S_START);
  key(KEY_UP);
  key(KEY_UP);
  shot("17c_period_start");
  key(KEY_OK);
  run(2000);
  key(KEY_BACK);
  shot("17d_day_changed");
  go(S_ADD_PERIOD);
  key(KEY_OK);
  run(2000);
  shot("17e_period_added");
  edit(S_REMOVE_PERIOD);
  key(KEY_OK);
  run(2000);
  shot("17f_period_removed");
  key(KEY_BACK);
  key(KEY_BACK);
  open(S_DISPLAY);
  shot("18_display_menu");
  edit(S_THEME);
  shot("19_theme_picker");
  key(KEY_BACK);
  open(S_HOME_SCREEN);
  shot("19a_home_screen");
  open(S_DASHBOARD_TILES);
  shot("19b_tiles");
  edit(S_TILE_3);
  go_text(sim_item_label("co2"));
  shot("19c_tile_picker");
  key(KEY_OK);
  edit(S_TILE_4);
  go_text(sim_item_label("weather"));
  key(KEY_OK);
  run(2000);
  ui_go_home();
  shot("19d_dashboard_tiles");
  key(KEY_OK);
  open(S_SYSTEM);
  shot("20_system_menu");
  open(S_ABOUT);
  shot("21_about");
  key(KEY_BACK);
  open(S_BUS);
  shot("22_bus");
  key(KEY_BACK);
  open(S_DIAGNOSTICS);
  shot("22b_diagnostics");
  key(KEY_BACK);
  edit(S_FACTORY_RESET_UNIT);
  shot("23_confirm");
  run(2000);
  key(KEY_BACK);
  key(KEY_HELP);
  shot("24_help");
  key(KEY_BACK);
  fresh();
  M.alarms[0] = (alarm_t) {10, 18, 10, 8, 14, true};
  M.n_alarms = 1;
  M.filter_hours = 4400;
  run(2000);
  ui_second();
  shot("25_alarm_filter");
  key(KEY_BACK);
  M.alarms[0] = (alarm_t) {9, 18, 10, 12, 51, true};
  M.alarms[1] = (alarm_t) {10, 18, 10, 8, 14, true};
  M.n_alarms = 2;
  ui_second();
  shot("26_alarm_rotor");
  key(KEY_OK);
  shot("27_alarm_list");
  ui_go_home();
  shot("28_dashboard_alarms");
  fresh();
  M.idle_after = 0;  // 15 s
  seconds(16);
  shot("29_screensaver");
  key(KEY_OK);

  fresh();
  M.esp_online = false;
  ui_replace(&SCR_HOME, VIEW_COMFORT);
  shot("30_comfort_offline");
  fresh();
  key(KEY_OK);
  open(S_SYSTEM);
  open(S_EQUIPMENT);
  shot("35_equipment");
  go(S_EQUIP_ESP);
  key(KEY_OK);
  run(2000);
  shot("36_equipment_esp_off");
  ui_go_home();
  shot("37_no_esp_dashboard");
  key(KEY_BACK);
  key(KEY_BACK);
  key(KEY_BACK);
  shot("38_no_esp_back_to_dashboard");
  key(KEY_OK);
  shot("39_no_esp_menu");
  open(S_DATE_AND_TIME);
  shot("40_no_esp_time");
  ui_go_home();
  key(KEY_OK);
  open(S_VENTILATION);
  go(S_MIN_SUPPLY);
  shot("41_no_esp_ventilation_ec");
  M.features &= ~FBIT(FEAT_EC_FANS);
  ui_rebuild();
  shot("42_no_esp_ventilation_ac");
  fresh();
  set_theme(1);
  ui_replace(&SCR_HOME, VIEW_FLOW);
  shot("31_daylight_airflow");
  ui_push(&SCR_MENU, 0);
  shot("32_daylight_menu");
  set_theme(2);
  shot("33_graphite_menu");
  ui_show_view(VIEW_TREND);
  shot("34_graphite_trends");

  // ---- usability pass
  fresh();
  set_theme(0);
  M.bus_offline = true;
  ui_replace(&SCR_HOME, VIEW_DASH);
  shot("62_no_contact");
  key(KEY_OK);
  shot("63_no_contact_menu");
  ui_go_home();
  M.bus_offline = false;
  ui_value_changed(V_NONE);
  M.key_lock = true;
  ui_value_changed(V_NONE);
  key(KEY_OK);  // refused, with a toast saying why
  shot("64_key_lock");
  M.key_lock = false;
  ui_value_changed(V_NONE);
  run(2000);
  key(KEY_OK);
  open(S_DISPLAY);
  open(S_NIGHT_MODE);
  shot("65_night_menu");
  key(KEY_BACK);
  go(S_FAST_DISPLAY);
  shot("66_display_menu_end");
  ui_go_home();
  M.heat_pct = 0;
  M.fan_sup = M.fan_ext = 75;
  ui_replace(&SCR_HOME, VIEW_FLOW);
  shot("67_airflow_exhaust");
  M.heat_pct = 12;
  ui_go_home();
  M.fan_ext = 60;
  ui_replace(&SCR_HOME, VIEW_DASH);
  shot("68_dashboard_fans_differ");
  M.fan_ext = 75;
  ui_value_changed(V_NONE);

  // ---- the ESP link: both ends run the real protocol code over a fake CU60. The scenes after this keep what
  // it sent, the items and the picture.
  fresh();
  printf("\nESP link, one transaction after each CU60 status broadcast:\n");
  settle();
  sim_esp_report("hello and state sync");
  sim_esp_items();
  settle();
  sim_esp_report("the ESP's items");
  ui_show_view(VIEW_SMART);
  shot("50_smart_view");
  ui_go_home();
  key(KEY_OK);
  open(S_SMART_HOME);
  shot("51_smart_menu");
  go_text(sim_item_label("hallway_light"));
  key(KEY_OK);
  settle();
  sim_esp_report("light switched on the panel");
  run(2000);
  shot("52_smart_toggled");
  uint16_t tile = M.tiles[3];
  M.tiles[3] = (uint16_t) (TILE_ITEM + sim_item_id("washer"));
  ui_show_view(VIEW_DASH);
  run(100);
  shot("52d_dashboard_text");
  ui_show_view(VIEW_SMART);
  run(100);
  item_t *washer = (item_t *) item_by_id(sim_item_id("washer"));
  char text[ITEM_TEXT_LEN];
  strcpy(text, washer->text);
  check(shows(lv_scr_act(), text), "the text item isn't shown on the smart view");
  washer->text[0] = 0;
  ui_value_changed(V_NONE);
  run(100);
  check(!shows(lv_scr_act(), text) && shows(lv_scr_act(), "\xE2\x80\x93"), "an empty text isn't a dash");
  strcpy(washer->text, text);
  M.tiles[3] = tile;
  ui_go_home();
  key(KEY_OK);
  open(S_COMFORT);
  go_text(sim_item_label("fireplace"));
  key(KEY_HELP);
  settle();  // the ESP's answer
  run(2000);
  shot("52b_item_help");
  key(KEY_BACK);
  go_text(sim_item_label("fireplace_minutes"));
  key(KEY_OK);
  key(KEY_UP);
  shot("52c_item_edit");
  key(KEY_BACK);
  ui_go_home();
  sim_esp_notify("The washing machine is done.", ICN_WASHER, 0);
  settle();
  ui_second();
  shot("53_notification");
  key(KEY_OK);

  unsigned long c0 = sim_cycles;
  unsigned bytes = sim_esp_send_picture();
  settle();
  printf("  picture 320x240, 16 colours: %u bytes compressed (38400 raw), %lu cycles = %.1f min at 8 cycles/s\n", bytes,
         sim_cycles - c0, (sim_cycles - c0) / 8.0 / 60);
  sim_esp_report("picture");
  M.screensaver = 3;
  ui_push(&SCR_SAVER, 0);
  shot("54_saver_picture");
  ui_pop();
  M.screensaver = 4;
  ui_push(&SCR_SAVER, 0);
  shot("55_saver_picture_clock");
  ui_pop();
  M.screensaver = 5;
  ui_push(&SCR_SAVER, 0);
  shot("56_saver_picture_all");
  ui_pop();
  int y, cnt;
  c0 = sim_cycles;
  bytes = sim_esp_send_moon(&y, &cnt);
  settle();
  printf("  moon patch: rows %d-%d, %u bytes, %lu cycles = %.1f s at 8 cycles/s\n", y, y + cnt - 1, bytes,
         sim_cycles - c0, (sim_cycles - c0) / 8.0);
  M.screensaver = 4;
  ui_push(&SCR_SAVER, 0);
  shot("57_saver_moon_patch");
  ui_pop();
  c0 = sim_cycles;
  sim_esp_send_dawn();
  settle();
  printf("  palette only: %lu cycles = %.1f s\n", sim_cycles - c0, (sim_cycles - c0) / 8.0);
  ui_push(&SCR_SAVER, 0);
  shot("58_saver_dawn_palette");
  ui_pop();
  c0 = sim_cycles;
  bytes = sim_esp_send_animation(250, 20);
  settle();
  printf("  animation 48x48, 8 frames: %u bytes, %lu cycles = %.1f s\n", bytes, sim_cycles - c0,
         (sim_cycles - c0) / 8.0);
  ui_push(&SCR_SAVER, 0);
  shot("58b_saver_animation");
  sim_run(300);  // three frames later
  shot("58c_saver_animation_later");
  // drifting left at 60 px/s, it goes round the edge like on a torus: half of it on each side for a while
  sim_esp_move_animation(ANIM_DRIFT, -60, 0, 0);
  settle();
  int16_t ax, ay;
  int stale = 0;  // pixels a step missed, against a full redraw
  for (int i = 0; i < 200; i++) {
    run(50);
    stale += sim_stale();
    anim_pos(0, &ax, &ay);
    if (ax > 250 && ax <= 296)
      break;
  }
  check(ax > 250 && ax <= 296 && ay == 20, "the animation didn't drift round the left edge");
  check(ball_at(316, 20) && ball_at(4, 20) && !ball_at(160, 20), "the ball isn't drawn at both edges while wrapping");
  shot("58d_saver_animation_wrap");
  sim_esp_move_animation(ANIM_DRIFT, 0, -60, 0);  // round the top edge too, so it shows in all four corners
  settle();
  for (int i = 0; i < 80; i++) {
    run(50);
    stale += sim_stale();
  }
  check(!stale, "the part wrapped round an edge isn't redrawn as it moves");
  // a glide lands on the target, the frames keep playing
  sim_esp_move_animation(ANIM_GLIDE, 100, 100, 1000);  // above the clock band, which dims it
  settle();
  run(500);
  anim_pos(0, &ax, &ay);
  check(ax > 100 && ay < 100, "the animation isn't on its way");
  run(600);
  anim_pos(0, &ax, &ay);
  check(ax == 100 && ay == 100 && ball_in(100, 100), "the glide didn't land on 100,100");
  unsigned long px = sim_px;
  run(300);
  anim_pos(0, &ax, &ay);
  check(ax == 100 && sim_px > px, "the frames stopped after the glide");
  // the motion pauses with the backlight: idle at night with the night idle brightness 0
  sim_esp_move_animation(ANIM_DRIFT, 60, 0, 0);
  settle();
  ui_pop();
  int idle_after = M.idle_after;
  bool go_home = M.go_home;
  M.idle_after = 0;  // 15 s
  M.go_home = false;
  M.night = true;
  M.night_from = 0;
  M.night_to = 23;
  run(16000);  // idle, the screensaver comes up by itself, dark
  anim_pos(0, &ax, &ay);
  int16_t bx;
  run(1000);
  anim_pos(0, &bx, &ay);
  check(bx == ax, "the animation drifts while the backlight is off");
  M.night = false;
  run(1000);
  anim_pos(0, &bx, &ay);
  check(bx == ax + 60, "the animation didn't drift on when the backlight came back");
  key(KEY_OK);
  M.idle_after = (uint8_t) idle_after;
  M.go_home = go_home;
  sim_esp_set(K_ANIM_CLEAR, ANIM_ALL);
  settle();
  ui_go_home();
  key(KEY_OK);
  open(S_SCREENSAVER);
  go(S_PICTURE);
  shot("59_saver_menu_picture");
  c0 = sim_cycles;
  int quality;
  bytes = sim_esp_send_photo(&quality);
  settle();
  printf("  photo 320x240 as JPEG, quality %d: %u bytes, %lu cycles = %.1f s\n", quality, bytes, sim_cycles - c0,
         (sim_cycles - c0) / 8.0);
  shot("59b_saver_menu_photo");
  M.screensaver = 4;
  ui_push(&SCR_SAVER, 0);
  shot("59c_saver_photo");
  // The LCD keeps the old picture while a new one comes, until it's in or HOLD_MS has passed. A refused one leaves
  // the clock.
  uint32_t was = sim_pixel(4, 4);
  check(was, "the photo isn't shown");
  sim_esp_send_picture();
  sim_link_run(3);
  run(50);
  check(pic_state() == PIC_RECEIVING && ui_held() && sim_pixel(4, 4) == was,
        "the screen went black while a new picture came");
  settle();
  run(50);
  check(!ui_held() && sim_pixel(4, 4) && !sim_stale(), "the new picture isn't shown");
  was = sim_pixel(4, 4);
  sim_esp_send_picture();
  sim_link_run(3);  // its start, then nothing for a while
  run(14000);
  check(ui_held() && sim_pixel(4, 4) == was, "the screen gave up on the new picture early");
  run(1100);
  check(!ui_held() && !sim_pixel(4, 4) && !sim_stale(), "the screen held on past 15 s");
  settle();
  run(50);
  check(sim_pixel(4, 4) && !sim_stale(), "the late picture isn't shown");
  sim_esp_set(K_PIC_BEGIN, 0);  // one the panel refuses
  settle();
  run(50);
  check(pic_state() == PIC_NONE && !sim_pixel(4, 4), "the saver kept a picture the panel dropped");
  sim_esp_set(K_SCREENSAVER, SAVER_OFF);  // switched off from Home Assistant while it shows
  settle();
  run(200);
  check(ui_top() != &SCR_SAVER, "the screensaver stayed after it was switched off");
  M.screensaver = 4;
  sim_esp_send_picture();  // the drawing back for the scenes after
  settle();
  sim_esp_set(K_THEME, 1);
  settle();
  sim_esp_report("theme set by the ESP");
  ui_go_home();
  shot("60_theme_from_esp");
  sim_esp_set(K_SHOW_VIEW, VIEW_FLOW);
  settle();
  shot("61_view_from_esp");
  ui_go_home();
  key(KEY_OK);
  open(S_SYSTEM);
  open(S_BUS);
  shot("62_bus_link");  // the link's rows, after some traffic

  // ---- the panel full of items: the smart home view pages, and its menu only builds the visible rows
  sim_esp_fill();
  settle();
  sim_esp_report("the ESP fills the panel");
  ui_show_view(VIEW_SMART);
  key(KEY_BACK);
  shot("62b_smart_page_2");
  ui_go_home();
  key(KEY_OK);
  open(S_SMART_HOME);
  key(KEY_UP);  // wraps to the last row
  shot("62c_smart_menu_end");
  go_text("Basement sensor  3");
  key(KEY_OK);  // open the editor on a generated row
  shot("62d_smart_edit");
  key(KEY_BACK);

  // ---- settings: the Display submenus, screensaver text, dashboard, night theme
  ui_go_home();
  key(KEY_OK);
  open(S_DISPLAY);
  open(S_HOME_SCREEN);
  open(S_VIEWS);
  shot("80_views");
  key(KEY_BACK);
  key(KEY_BACK);
  open(S_LIGHTS);
  shot("82_lights");
  key(KEY_BACK);
  key(KEY_BACK);
  open(S_SCREENSAVER);
  shot("81_saver_menu");
  ui_go_home();
  key(KEY_OK);
  open(S_SYSTEM);
  go(S_RESET_PANEL_SETTINGS);
  shot("83_system_menu");
  ui_go_home();
  key(KEY_OK);
  open(S_ALARMS);
  shot("84_alarms_menu");
  ui_go_home();
  M.screensaver = 5;
  M.saver_text = 1;
  ui_push(&SCR_SAVER, 0);
  shot("85_saver_shadow");
  ui_pop();
  M.saver_top = true;
  ui_push(&SCR_SAVER, 0);
  shot("86_saver_shadow_top");
  ui_pop();
  M.saver_text = SAVER_TEXT_SMALL;
  ui_push(&SCR_SAVER, 0);
  shot("86b_saver_small_top");
  ui_pop();
  M.saver_top = false;
  ui_push(&SCR_SAVER, 0);
  shot("86c_saver_small");
  ui_pop();
  M.saver_text = SAVER_TEXT_PLAIN;
  ui_push(&SCR_SAVER, 0);
  shot("86d_saver_just_text");
  ui_pop();
  M.saver_top = true;
  M.saver_text = 0;
  ui_push(&SCR_SAVER, 0);
  shot("87_saver_band_top");
  ui_pop();
  M.saver_top = false;
  M.dash_temp = DT_OUTDOOR;
  M.temp_dec = false;
  ui_replace(&SCR_HOME, VIEW_DASH);
  shot("88_dashboard_outdoor_whole");
  M.dash_temp = DT_INDOOR;
  M.temp_dec = true;
  M.night = true;
  M.night_from = 12;
  M.night_to = 14;
  M.night_theme = 3;  // Graphite
  ui_second();
  shot("89_night_theme");
  M.night = false;
  M.night_theme = 0;
  ui_second();
  for (int v = VIEW_FLOW; v < VIEW_COUNT; v++)
    M.view_on[v] = false;
  ui_replace(&SCR_HOME, VIEW_DASH);
  key(KEY_BACK);
  shot("90_only_the_dashboard");  // BACK has nowhere to go
  for (int v = VIEW_FLOW; v < VIEW_COUNT; v++)
    M.view_on[v] = true;

  // Standalone, no ESP on the bus. Its views and menus are gone, and settings that pointed at them show their
  // fallbacks.
  M.home_view = VIEW_COMFORT;
  M.screensaver = 5;
  M.features &= ~FBIT(FEAT_ESP);
  ui_show_view(M.home_view);
  shot("70_standalone_dashboard");
  key(KEY_OK);
  shot("71_standalone_menu");
  open(S_DISPLAY);
  shot("72_standalone_display");
  open(S_HOME_SCREEN);
  open(S_DASHBOARD_TILES);
  edit(S_TILE_3);
  shot("73_standalone_tile_picker");  // set to CO2, shown as its fallback
  key(KEY_BACK);
  key(KEY_BACK);
  edit(S_START_VIEW);
  shot("74_standalone_view_picker");  // set to Comfort
  key(KEY_BACK);
  key(KEY_BACK);
  key(KEY_BACK);
  open(S_SCREENSAVER);
  shot("75_standalone_saver_menu");  // no text or picture rows
  edit(S_SHOW);
  shot("76_standalone_saver_picker");  // set to "Picture, all"
  key(KEY_BACK);
  key(KEY_BACK);
  open(S_DATE_AND_TIME);
  go(S_SUMMER_TIME);
  shot("77_standalone_time");
  key(KEY_BACK);
  open(S_SYSTEM);
  open(S_ABOUT);
  shot("78_standalone_about");
  key(KEY_BACK);
  open(S_BUS);
  shot("79_standalone_bus");
  ui_go_home();
  ui_push(&SCR_SAVER, 0);
  shot("79b_standalone_saver");
  ui_pop();

  checks();
  notices();
  keytest();
  if (failed)
    printf("%d failed: screens that left LVGL's heap too little, steps that went somewhere else, or checks\n", failed);
  return failed != 0;
}
