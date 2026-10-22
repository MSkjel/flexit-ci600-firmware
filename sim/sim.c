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
  ui_push(&SCR_ALARM_POPUP, 0);
  M.alarms[0] = (alarm_t) {10, 18, 10, 8, 14, true};
  ui_alarms_changed();
  run(30);
  check(shows(lv_layer_top(), alarm_text(10)), "the alarm pop-up kept an alarm that changed");
  M.n_alarms = 0;
  ui_alarms_changed();
  ui_second();
  check(ui_top() != &SCR_ALARM_POPUP, "the alarm pop-up stayed after the alarms went");

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
  ui_push(&SCR_ALARM_POPUP, 0);
  ui_idle();
  check(ui_top() == &SCR_ALARM_POPUP, "idle closed an unread alarm pop-up");
  M.n_alarms = 0;
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
  go(S_KEY_HINTS);
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
  ui_pop();
  sim_esp_set(K_ANIM_CLEAR, 0);
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
  ui_pop();
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
  if (failed)
    printf("%d failed: screens that left LVGL's heap too little, steps that went somewhere else, or checks\n", failed);
  return failed != 0;
}
