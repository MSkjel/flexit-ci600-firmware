// Home screen, its dashboard view and the fan speed overlay.
#include "ui_int.h"
#include <string.h>

// ---------------------------------------------------------------- dashboard
#define DASH_TILE_X 8  // the four tiles along the bottom: first x, x step, y, size
#define DASH_TILE_STEP 77
#define DASH_TILE_Y 124
#define DASH_TILE_W 73
#define DASH_TILE_H 64
#define RING_SIZE 112  // fan speed ring

typedef struct {
  lv_obj_t *big, *target, *state, *ring, *ring_icon, *ring_mode, *ring_sub, *ring_next;
  lv_obj_t *tile_cap[4], *tile_val[4], *tile_bar[4], *tile_stripe[4];
} dash_t;
SCREEN_STATE(dash_t, dash);

static const str_t MODE_CAPS[MODE_COUNT] = {
    [MODE_STOP] = S_STOP_CAPS, [MODE_MIN] = S_MIN_CAPS, [MODE_NORMAL] = S_NORMAL_CAPS, [MODE_MAX] = S_MAX_CAPS};
static const str_t BIG_CAPS[DT_COUNT] = {[DT_INDOOR] = S_INDOOR_CAPS,
                                         [DT_OUTDOOR] = S_OUTDOOR_CAPS,
                                         [DT_SUPPLY] = S_SUPPLY_AIR_CAPS,
                                         [DT_EXTRACT] = S_EXTRACT_AIR_CAPS};
static const str_t TILE_NAMES[TILE_UNIT] = {
    [TILE_OUTDOOR] = S_OUTDOOR, [TILE_SUPPLY] = S_SUPPLY, [TILE_EXTRACT] = S_EXTRACT, [TILE_ROOM] = S_ROOM,
    [TILE_ROTOR] = S_ROTOR,     [TILE_FILTER] = S_FILTER, [TILE_HEATER] = S_HEATER,   [TILE_RECOVERY] = S_RECOVERY};

// Next week program change today: "until 22:30" inside a period, "Min at 15:30" before one
static bool next_change(char *b, size_t n) {
  if (!M.week_program)
    return false;
  int d = M.wday % 7, now = M.hh * 60 + M.mi, best = 24 * 60;
  char t[12];
  for (int i = 0; i < M.n_periods[d]; i++) {
    const period_t *s = &M.week[d][i];
    int a = s->start, e = s->end;
    if (now >= a && now < e) {
      sprint(b, n, _(S_UNTIL), fmt_hm(t, sizeof t, e));
      return true;
    }
    if (a > now && a < best) {
      best = a;
      sprint(b, n, _(S_MODE_AT), tr(MODE_NAMES[s->level % MODE_COUNT]), fmt_hm(t, sizeof t, a));
    }
  }
  return best < 24 * 60;
}

static lv_color_t mode_color(void) {
  if (M.boost_left || M.mode_now == MODE_MAX)
    return T.warn;
  if (M.mode_now == MODE_STOP)
    return T.faint;
  return T.accent;
}

// Without an ESP, a tile set to an ESP item shows its factory default instead
int tile_used(val_t tile, int src) {
  return src >= TILE_ITEM && !HAS(FEAT_ESP) ? TILE_DEFAULTS[(tile - V_TILE1) & 3] : src;
}

static const item_t *tile_item(int src) { return src >= TILE_ITEM ? item_by_id(src - TILE_ITEM) : NULL; }

const char *tile_name(int src) {
  if (src >= TILE_ITEM)
    return tile_item(src) ? tile_item(src)->label : NULL;
  return src >= 0 && src < TILE_UNIT ? tr(TILE_NAMES[src]) : NULL;
}

static bool tile_has_bar(int src) {
  if (src >= TILE_ITEM)
    return tile_item(src) && (tile_item(src)->flags & ITEMF_LEVELS);
  return src == TILE_ROTOR || src == TILE_FILTER || src == TILE_HEATER || src == TILE_RECOVERY;
}

static lv_color_t tile_color(int src) {
  switch (src) {
    case TILE_OUTDOOR:
      return T.cold;
    case TILE_SUPPLY:
    case TILE_FILTER:
    case TILE_RECOVERY:
      return T.ok;
    case TILE_EXTRACT:
    case TILE_HEATER:
      return T.warm;
    case TILE_ROOM:
      return T.text;
    default:
      return T.accent;
  }
}

static int tile_src(int i) {
  int t = M.tiles[i] < TILE_UNIT || M.tiles[i] >= TILE_ITEM ? M.tiles[i] : TILE_OUTDOOR;
  return tile_used((val_t) (V_TILE1 + i), t);
}

static void tile_build(lv_obj_t *p, int i) {
  int src = tile_src(i);
  lv_obj_t *t = mk_box(p, DASH_TILE_X + i * DASH_TILE_STEP, DASH_TILE_Y, DASH_TILE_W, DASH_TILE_H, T.surface, 10);
  dash->tile_stripe[i] = mk_box(t, 0, 14, 3, 36, tile_color(src), 2);
  const char *name = tile_name(src);
  dash->tile_cap[i] = mk_label(t, &font_f12, T.dim, name ? name : NO_VALUE);
  lv_obj_set_pos(dash->tile_cap[i], 12, 7);
  lv_obj_set_width(dash->tile_cap[i], DASH_TILE_W - 16);
  lv_label_set_long_mode(dash->tile_cap[i], LV_LABEL_LONG_CLIP);
  dash->tile_val[i] = mk_label(t, &font_f20, T.text, "");
  lv_obj_set_pos(dash->tile_val[i], 12, 23);
  lv_obj_set_width(dash->tile_val[i], DASH_TILE_W - 16);  // clip long values to the tile
  lv_label_set_long_mode(dash->tile_val[i], LV_LABEL_LONG_CLIP);
  dash->tile_bar[i] = tile_has_bar(src) ? mk_bar(t, 12, 52, 50, 4, 0, 100, tile_color(src)) : NULL;
}

// Percentage, or a dash when pct < 0
static const char *pct_text(char *b, size_t n, int pct) { return pct < 0 ? NO_VALUE : fmt_pct(b, n, pct); }

// Text for tile source src. Sets *bar to the bar fill, -1 = empty, and *c if it needs another colour.
static const char *tile_value(int src, char *b, size_t n, int *bar, lv_color_t *c) {
  switch (src) {
    case TILE_OUTDOOR:
      return fmt_temp(b, n, M.t_outdoor, false);
    case TILE_SUPPLY:
      return fmt_temp(b, n, M.t_supply, false);
    case TILE_EXTRACT:
      return fmt_temp(b, n, M.t_extract, false);
    case TILE_ROOM:
      return fmt_temp(b, n, M.t_room, false);
    case TILE_ROTOR:
      *bar = M.rotor_pct;
      return pct_text(b, n, M.rotor_pct);
    case TILE_FILTER: {
      int days = filter_days_left();
      *bar = filter_life();
      *c = *bar < 10 ? T.err : *bar < 25 ? T.warn : T.ok;
      if (!days)
        return _(S_DUE);  // "Change" doesn't fit the tile
      sprint(b, n, _(S_DAYS_SHORT), days);
      return b;
    }
    case TILE_HEATER:
      *bar = M.heat_pct;
      return M.heat_pct ? pct_text(b, n, M.heat_pct) : M.heater_enabled ? _(S_OFF_LOWER) : NO_VALUE;
    case TILE_RECOVERY:
      *bar = heat_recovery();
      return pct_text(b, n, *bar);
    default: {  // an ESP item
      const item_t *it = tile_item(src);
      if (!it || !M.esp_online || it->type == ITEM_BUTTON)
        return NO_VALUE;
      if (it->type == ITEM_TOGGLE)
        *c = item_on(it) ? T.accent : T.faint;
      else if (it->flags & ITEMF_LEVELS) {
        *c = item_color(it);
        *bar = item_pct(it);
      }
      item_text(b, n, it, true);  // with its unit if that fits
      if (lv_txt_get_width(b, (uint32_t) strlen(b), &font_f20, 0, LV_TEXT_FLAG_NONE) > DASH_TILE_W - 16)
        item_text(b, n, it, false);
      return b;
    }
  }
}

static void tile_update(int i) {
  int src = tile_src(i), bar = -1;
  char b[32];
  lv_color_t c = tile_color(src);
  const char *v = tile_value(src, b, sizeof b, &bar, &c);
  if (tile_item(src) && tile_item(src)->type == ITEM_TEXT)
    label_fit(dash->tile_val[i], v, DASH_TILE_W - 16);
  else
    set_text(dash->tile_val[i], "%s", v);
  if (src >= TILE_ITEM)  // the ESP can rename items, a weather item's label is the current condition
    set_text(dash->tile_cap[i], "%s", tile_name(src) ? tile_name(src) : NO_VALUE);
  st_bg(dash->tile_stripe[i], c, 0);
  if (dash->tile_bar[i]) {
    st_bg(dash->tile_bar[i], c, LV_PART_INDICATOR);
    lv_bar_set_value(dash->tile_bar[i], bar < 0 ? 0 : bar, LV_ANIM_OFF);
  }
}

// Display > Home screen > Big temperature
static int16_t big_temp(void) {
  switch (M.dash_temp) {
    case DT_OUTDOOR:
      return M.t_outdoor;
    case DT_SUPPLY:
      return M.t_supply;
    case DT_EXTRACT:
      return M.t_extract;
    default:
      return indoor_temp();
  }
}

static void dash_build(lv_obj_t *b, int first) {
  (void) first;
  lv_obj_set_pos(mk_label(b, &font_f12, T.dim, tr(BIG_CAPS[M.dash_temp % DT_COUNT])), 14, 2);
  dash->big = mk_label(b, &font_n64, T.text, "");
  lv_obj_set_pos(dash->big, 8, 17);
  lv_obj_set_pos(mk_icon(b, &icons_16, T.accent, IC_THERMOSTAT), 14, 84);
  dash->target = mk_label(b, &font_f14, T.dim, "");
  lv_obj_set_pos(dash->target, 34, 83);
  dash->state = mk_label(b, &font_f14, T.warm, "");
  lv_obj_set_pos(dash->state, 14, 101);
  lv_label_set_recolor(dash->state, true);

  dash->ring = mk_arc(b, RING_SIZE, 9, 0, MODE_MAX, T.accent);  // a boost fills it
  lv_obj_set_pos(dash->ring, 196, 4);
  dash->ring_icon = mk_icon(b, &icons_32, T.accent, IC_FAN);
  lv_obj_align_to(dash->ring_icon, dash->ring, LV_ALIGN_CENTER, 0, -20);
  dash->ring_mode = mk_label(b, &font_f16, T.text, "");
  dash->ring_sub = mk_label(b, &font_f12, T.dim, "");
  dash->ring_next = mk_label(b, &font_f12, T.faint, "");

  for (int i = 0; i < 4; i++)
    tile_build(b, i);
}

// Status line under the setpoint saying what the unit is doing, in colour c
static void show_state(lv_color_t c, const char *text) {
  set_text(dash->state, "#%06x %s#", (unsigned) ui_hex(c), text);
}

static void dash_update(void) {
  char a[24], s[48];
  set_text(dash->big, "%s", fmt_temp(a, sizeof a, big_temp(), false));
  set_text(dash->target, _(S_TARGET), fmt_target(a, sizeof a, M.setpoint_now ? M.setpoint_now : M.setpoint));
  if (M.bus_offline)
    show_state(T.err, _(S_NO_CONTACT_WITH_UNIT));
  else if (M.heat_pct)
    show_state(T.warm, (lv_snprintf(s, sizeof s, _(S_HEATING_PCT), M.heat_pct), s));
  else if (M.cool_pct)
    show_state(T.cold, (lv_snprintf(s, sizeof s, _(S_COOLING_PCT), M.cool_pct), s));
  else if (!M.heater_enabled)
    show_state(T.faint, _(S_HEATER_OFF));
  else
    set_text(dash->state, "%s", "");

  lv_color_t mc = mode_color();
  lv_arc_set_value(dash->ring, M.boost_left ? MODE_MAX : M.mode_now);
  lv_obj_set_style_arc_color(dash->ring, mc, LV_PART_INDICATOR);
  st_text(dash->ring_icon, mc);
  set_text(dash->ring_icon, "%s", M.mode_now == MODE_STOP && !M.boost_left ? IC_FAN_OFF : IC_FAN);
  if (M.boost_left) {
    set_text(dash->ring_mode, "%s", _(S_BOOST_CAPS));
    set_text(dash->ring_sub, _(S_MIN_LEFT), M.boost_left);
    set_text(dash->ring_next, "%s", "");
  } else {
    set_text(dash->ring_mode, "%s", tr(MODE_CAPS[M.mode_now % MODE_COUNT]));
    if (M.fan_ext && M.fan_ext != M.fan_sup)
      set_text(dash->ring_sub, _(S_FANS_IN_OUT), M.fan_sup, M.fan_ext);
    else
      set_text(dash->ring_sub, "%s", fmt_pct(a, sizeof a, M.fan_sup));
    set_text(dash->ring_next, "%s", next_change(s, sizeof s) ? s : "");
  }
  lv_obj_align_to(dash->ring_mode, dash->ring, LV_ALIGN_CENTER, 0, 7);  // less room inside the ring further down
  lv_obj_align_to(dash->ring_sub, dash->ring, LV_ALIGN_CENTER, 0, 25);
  lv_obj_align_to(dash->ring_next, dash->ring, LV_ALIGN_CENTER, 0, 43);
  for (int i = 0; i < 4; i++)
    tile_update(i);
}

const view_t DASH_VIEW = {.build = dash_build, .update = dash_update, .help = S_DASHBOARD_VIEW_HELP};

// ---------------------------------------------------------------- home screen
static const view_t *const VIEWS[VIEW_COUNT] = {[VIEW_DASH] = &DASH_VIEW,       [VIEW_FLOW] = &FLOW_VIEW,
                                                [VIEW_TREND] = &TREND_VIEW,     [VIEW_SENSORS] = &SENSORS_VIEW,
                                                [VIEW_COMFORT] = &COMFORT_VIEW, [VIEW_SMART] = &SMART_VIEW};
static int view, page;  // current view, VIEW_*, and its page

// Installed and switched on in Display > Home screen > Views. The dashboard is always on.
static bool view_on(int v) { return HAS_ALL(VIEWS[v]->need) && (v == VIEW_DASH || M.view_on[v]); }

// The start view, or the dashboard if that view is off or needs missing equipment
int view_used(val_t v, int view) {
  (void) v;
  return view < VIEW_COUNT && view_on(view) ? view : VIEW_DASH;
}

// View is on and has something to show
static bool view_ok(int v) { return view_on(v) && (!VIEWS[v]->count || VIEWS[v]->count() > 0); }

// BACK shows a view's pages in turn
static int pages(const view_t *v) { return v->per_page ? (v->count() + v->per_page - 1) / v->per_page : 1; }

// First view from v onwards that is on and has something to show, v included
static int next_view(int v) {
  for (int i = 0; i < VIEW_COUNT; i++, v = (v + 1) % VIEW_COUNT)
    if (view_ok(v))
      return v;
  return VIEW_DASH;
}

static void home_build(lv_obj_t *b, intptr_t arg) {
  int v = next_view((int) arg % VIEW_COUNT);
  if (v != view || page >= pages(VIEWS[v]))
    page = 0;  // another view, or some items have gone
  view = v;
  int n = 0, pos = 0;
  for (int i = 0; i < VIEW_COUNT; i++)
    if (view_ok(i)) {
      if (i == view)
        pos = n;
      n++;
    }
  ui_view_dots(n, pos);
  ui_hints(S_FAN, S_MENU, S_VIEW, S_HELP);
  ui_help(VIEWS[view]->help);
  VIEWS[view]->build(b, page * VIEWS[view]->per_page);
}

static void home_update(void) { VIEWS[view]->update(); }

static bool home_key(int k) {
  switch (k) {
    case KEY_BACK:  // the next page, else the next view
      ui_replace(&SCR_HOME, ++page < pages(VIEWS[view]) ? view : next_view((view + 1) % VIEW_COUNT));
      return true;
    case KEY_OK:
      ui_push(&SCR_MENU, 0);
      return true;
    case KEY_UP:
    case KEY_DOWN:
      ui_push(&SCR_FAN_HUD, M.mode);  // the first press only opens the overlay, later presses change the speed.
                                      // M.mode and not mode_now: a boost goes back to the speed set
      return true;
  }
  return false;
}

const screen_t SCR_HOME = {.build = home_build, .update = home_update, .key = home_key};

// ---------------------------------------------------------------- fan speed overlay
#define HUD_CLOSE_MS 2500
#define HUD_BOOST MODE_COUNT  // one level above MAX

static struct {
  lv_obj_t *bars[HUD_BOOST + 1], *names[HUD_BOOST + 1], *sub;
  uint8_t start;  // speed when the overlay opened
} hud;

static int hud_level(void) { return M.boost_on ? HUD_BOOST : M.mode_now; }

// UP past MAX starts a boost. The CX60 goes back to the old speed after a boost, so set the one the overlay
// opened on and not the MAX we stepped through.
static void hud_step(int k) {
  int lvl = hud_level() + (k == KEY_UP ? 1 : -1);
  if (lvl < 0 || lvl > HUD_BOOST)
    return;
  if (lvl == HUD_BOOST) {
    val_set(V_MODE, hud.start);
    val_set(V_BOOST, 1);
  } else {
    if (M.boost_on)
      val_set(V_BOOST, 0);
    val_set(V_MODE, lvl);
  }
}

static void hud_build(lv_obj_t *b, intptr_t arg) {
  hud.start = (uint8_t) arg;
  lv_obj_t *card = mk_card(b, 30, 28, 260, 140, T.line, 1);
  lv_obj_set_pos(mk_label(card, &font_f16, T.text, _(S_FAN_SPEED)), 16, 8);
  hud.sub = mk_label(card, &font_f12, T.dim, "");
  lv_obj_set_pos(hud.sub, 16, 28);
  for (int i = 0; i <= HUD_BOOST; i++) {
    int h = 10 + i * 11;
    hud.bars[i] = mk_box(card, 22 + i * 46, 104 - h, 32, h, T.surface2, 6);
    hud.names[i] =
        mk_text(card, &font_f12, T.dim, tr(i == HUD_BOOST ? S_BOOST : MODE_NAMES[i]), 46, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(hud.names[i], 15 + i * 46, 110);
  }
  ui_hints(S_CHANGE, S_DONE, S_DONE, S_NONE);
  ui_help(S_FAN_SPEED_HUD_HELP);
  ui_close_after(HUD_CLOSE_MS);
}

static void hud_update(void) {
  int lvl = hud_level();
  for (int i = 0; i <= HUD_BOOST; i++) {
    lv_color_t c = i > lvl                             ? T.surface2
                   : i == HUD_BOOST || lvl == MODE_MAX ? T.warn
                   : lvl == MODE_STOP                  ? T.faint
                                                       : T.accent;
    st_bg(hud.bars[i], c, 0);
    st_text(hud.names[i], i == lvl ? T.text : T.dim);
  }
  if (M.boost_left)
    set_text(hud.sub, _(S_BOOST_MIN_LEFT), M.boost_left);
  else if (M.fan_ext && M.fan_ext != M.fan_sup)
    set_text(hud.sub, _(S_FANS_SUPPLY_EXTRACT), M.fan_sup, M.fan_ext);
  else
    set_text(hud.sub, _(S_FANS_AT), M.fan_sup);
}

static bool hud_key(int k) {
  ui_close_after(HUD_CLOSE_MS);
  if (k == KEY_UP || k == KEY_DOWN) {
    hud_step(k);
    ui_refresh();
    return true;
  }
  if (k == KEY_OK) {
    ui_pop();
    return true;
  }
  return false;
}

const screen_t SCR_FAN_HUD = {.flags = SF_OVERLAY, .build = hud_build, .update = hud_update, .key = hud_key};
