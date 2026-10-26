// The home views besides the dashboard.
#include "ui.h"

static const str_t AXIS[5] = {S_AXIS_24H, S_AXIS_18H, S_AXIS_12H, S_AXIS_6H, S_AXIS_NOW};

// Time labels under a chart w wide, from -24 h on the left to now on the right
static void time_axis(lv_obj_t *b, int w) {
  for (int i = 0; i < 5; i++) {
    lv_obj_t *l = mk_label(b, &font_f12, T.faint, tr(AXIS[i]));
    lv_obj_set_pos(l, CHART_X + i * (w / 4) - (i ? (i == 4 ? 24 : 12) : 0), CHART_Y + CHART_H + 4);
  }
}

// Legend entry above the chart, a colour dot and a label
static lv_obj_t *legend(lv_obj_t *b, int x, lv_color_t c, const char *text) {
  mk_box(b, x, 9, 8, 8, c, 4);
  lv_obj_t *l = mk_label(b, &font_f12, T.dim, text);
  lv_obj_set_pos(l, x + 12, 4);
  return l;
}

// ---------------------------------------------------------------- airflow
typedef struct {
  lv_obj_t *t[4], *rotor, *rotor_lbl, *fan_s, *fan_e, *heat_lbl, *eff;
} flow_t;
SCREEN_STATE(flow_t, flow);

// Outdoor to supply along the top, extract to exhaust along the bottom, each with an arrowhead
static const lv_point_t flow_pts[2][2] = {{{4, 46}, {316, 46}}, {{316, 150}, {4, 150}}};
static const lv_point_t arrow_pts[2][3] = {{{306, 40}, {316, 46}, {306, 52}}, {{14, 144}, {4, 150}, {14, 156}}};

static void line(lv_obj_t *p, const lv_point_t *pts, int n, lv_color_t c) {
  lv_obj_t *l = lv_line_create(p);
  lv_line_set_points(l, pts, n);
  lv_obj_set_style_line_color(l, c, 0);
  lv_obj_set_style_line_width(l, 3, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
}

// A filter, fan or heater on the air path, with a label under it if lbl is set
static void node(lv_obj_t *p, int cx, int cy, const char *ic, lv_color_t c, lv_obj_t **lbl) {
  lv_obj_t *n = mk_box(p, cx - 14, cy - 14, 28, 28, T.surface2, 14);
  lv_obj_set_style_border_width(n, 2, 0);
  lv_obj_set_style_border_color(n, T.surface, 0);
  lv_obj_center(mk_icon(n, &icons_16, c, ic));
  if (lbl) {
    *lbl = mk_label(p, &font_f12, T.dim, "");
    lv_obj_set_width(*lbl, 44);
    lv_obj_set_style_text_align(*lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(*lbl, cx - 22, cy + 16);
  }
}

// Air inlet or outlet name, with the temperature under it
static void port(lv_obj_t *p, int i, int x, int y, str_t cap, lv_color_t c, bool right) {
  lv_obj_t *l = mk_label(p, &font_f12, T.dim, tr(cap));
  lv_obj_t *v = flow->t[i] = mk_label(p, &font_f20, c, "");
  if (right) {
    lv_obj_set_width(l, 64);
    lv_obj_set_width(v, 66);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  }
  lv_obj_set_pos(l, x, y);
  lv_obj_set_pos(v, x - (right ? 2 : 0), y + 13);
}

static void flow_build(lv_obj_t *b, int first) {
  (void) first;
  mk_box(b, 74, 10, 172, 176, T.surface, CARD_RADIUS);
  line(b, flow_pts[0], 2, T.cold);
  line(b, arrow_pts[0], 3, T.ok);
  line(b, flow_pts[1], 2, T.warm);
  line(b, arrow_pts[1], 3, T.faint);

  port(b, 0, 6, 56, S_OUTDOOR_CAPS, T.cold, false);
  port(b, 1, 250, 56, S_SUPPLY_CAPS, T.ok, true);
  port(b, 2, 250, 104, S_EXTRACT_CAPS, T.warm, true);
  port(b, 3, 6, 104, S_EXHAUST_CAPS, T.faint, false);

  node(b, 96, 46, IC_FILTER, T.dim, NULL);
  node(b, 196, 46, IC_FAN, T.accent, &flow->fan_s);
  node(b, 228, 46, IC_FIRE, T.warm, &flow->heat_lbl);
  node(b, 124, 150, IC_FAN, T.accent, &flow->fan_e);
  node(b, 224, 150, IC_FILTER, T.dim, NULL);

  lv_obj_t *r = flow->rotor = mk_arc(b, 70, 7, 0, 100, T.accent);  // a full circle, from the top
  lv_obj_set_pos(r, 125, 63);
  lv_arc_set_bg_angles(r, 0, 360);
  lv_arc_set_rotation(r, 270);
  lv_obj_align_to(mk_icon(b, &icons_22, T.accent, IC_ROTOR), r, LV_ALIGN_CENTER, 0, -8);
  flow->rotor_lbl = mk_label(b, &font_f12, T.text, "");
  lv_obj_set_width(flow->rotor_lbl, 50);
  lv_obj_set_style_text_align(flow->rotor_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(flow->rotor_lbl, r, LV_ALIGN_CENTER, 0, 13);
  flow->eff = mk_label(b, &font_f12, T.dim, "");
  lv_obj_set_width(flow->eff, 160);
  lv_obj_set_style_text_align(flow->eff, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(flow->eff, 80, 14);
}

static void flow_update(void) {
  char a[24];
  set_text(flow->t[0], "%s", fmt_temp(a, M.t_outdoor, false));
  set_text(flow->t[1], "%s", fmt_temp(a, M.t_supply, false));
  set_text(flow->t[2], "%s", fmt_temp(a, M.t_extract, false));
  // No exhaust sensor, so estimate it. With the heater off, what the supply air gained came out of the extract
  // air, scaled by the airflows. Fan % stands in for airflow.
  if (!M.heat_pct && M.fan_sup && M.fan_ext && M.t_supply != T_NONE && M.t_outdoor != T_NONE && M.t_extract != T_NONE)
    set_text(flow->t[3], "~ %s",
             fmt_temp(a, (int16_t) (M.t_extract - (M.t_supply - M.t_outdoor) * M.fan_sup / M.fan_ext), false));
  else
    lv_label_set_text(flow->t[3], "");
  lv_arc_set_value(flow->rotor, M.rotor_pct);
  set_text(flow->rotor_lbl, "%d%%", M.rotor_pct);
  set_text(flow->fan_s, "%d%%", M.fan_sup);
  set_text(flow->fan_e, "%d%%", M.fan_ext);
  if (M.heat_pct)
    set_text(flow->heat_lbl, "%d%%", M.heat_pct);
  else
    lv_label_set_text(flow->heat_lbl, _(S_OFF_LOWER));
  int eff = heat_recovery();
  if (eff >= 0)
    set_text(flow->eff, _(S_HEAT_RECOVERY_PCT), eff);
  else
    lv_label_set_text(flow->eff, _(S_HEAT_RECOVERY));
}

const view_t FLOW_VIEW = {.build = flow_build, .update = flow_update, .help = S_AIRFLOW_VIEW_HELP};

// ---------------------------------------------------------------- trends: temperatures over 24 hours
#define TREND_W 270

typedef struct {
  lv_obj_t *chart, *legend[3], *ylbl[4];
  lv_chart_series_t *ser[3];
} trend_view_t;
SCREEN_STATE(trend_view_t, trend);

// Outdoor, supply, then extract or the panel sensor as the Indoor temperature setting says
static uint8_t trend_series(int i) {
  static const uint8_t SERIES[3] = {TR_OUTDOOR, TR_SUPPLY, TR_EXTRACT};
  return i == 2 && M.indoor_source == INDOOR_PANEL ? TR_ROOM : SERIES[i];
}

static void trend_build(lv_obj_t *b, int first) {
  (void) first;
  const lv_color_t cols[3] = {T.cold, T.ok, T.warm};
  for (int i = 0; i < 3; i++)
    trend->legend[i] = legend(b, 40 + i * 92, cols[i], "");
  trend->chart = mk_chart(b, TREND_W, 4);
  for (int i = 0; i < 3; i++)
    trend->ser[i] = lv_chart_add_series(trend->chart, cols[i], LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < 4; i++) {
    trend->ylbl[i] = mk_label(b, &font_f12, T.faint, "");
    lv_obj_set_width(trend->ylbl[i], 30);
    lv_obj_set_style_text_align(trend->ylbl[i], LV_TEXT_ALIGN_RIGHT, 0);
  }
  time_axis(b, TREND_W);
}

static void trend_update(void) {
  int lo = 1000, hi = -1000;
  for (int i = 0; i < 3; i++)
    for (int k = 0; k < TREND_LEN; k++) {
      int s = trend_series(i);
      int v = trend->ser[i]->y_points[k] = (lv_coord_t) trend_value(s, M.trend[s][k]);
      if (v == TREND_NONE)
        continue;
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
    }
  if (lo > hi) {
    lo = 0;
    hi = 300;
  }
  lo = (lo < 0 ? lo - 49 : lo) / 50 * 50;  // round out to 5 degrees
  hi = (hi < 0 ? hi - 49 : hi) / 50 * 50 + 50;
  if ((hi - lo) % 150)
    hi = lo + ((hi - lo) / 150 + 1) * 150;  // 3 equal steps of whole degrees
  lv_chart_set_range(trend->chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
  for (int i = 0; i < 4; i++) {
    set_text(trend->ylbl[i], "%d\xC2\xB0", (hi - (hi - lo) * i / 3) / 10);
    lv_obj_set_pos(trend->ylbl[i], 2, CHART_Y + CHART_H * i / 3 - 8);
  }
  char a[16];
  bool panel = M.indoor_source == INDOOR_PANEL;
  const int16_t now[3] = {M.t_outdoor, M.t_supply, panel ? M.t_room : M.t_extract};
  const str_t names[3] = {S_OUTDOOR, S_SUPPLY, panel ? S_ROOM : S_EXTRACT};
  for (int i = 0; i < 3; i++)
    set_text(trend->legend[i], "%s %s", tr(names[i]), fmt_temp(a, now[i], false));
  lv_chart_refresh(trend->chart);
}

const view_t TREND_VIEW = {.build = trend_build, .update = trend_update, .help = S_TRENDS_VIEW_HELP};

// ---------------------------------------------------------------- sensors: 24 h of ESP items with ITEMF_TREND
// Two per page, the first on the left axis and the second on the right. BACK shows the next two before moving on.
#define SENS_W 240
#define SENS_STEP 1000  // chart units per grid line. Points are scaled to them, the chart's coordinates are 16 bit

typedef struct {
  lv_obj_t *chart, *legend[2], *ylbl[2][5];
  lv_chart_series_t *ser[2];
  uint16_t id[2];  // ids of the items shown
  int n;
} sensors_t;
SCREEN_STATE(sensors_t, sens);

// An item with a 24 h trend
static bool trended(const item_t *it) { return (it->flags & ITEMF_TREND) && trend_of(it->id); }

static int sens_count(void) {
  int n = 0;
  for (int i = 0; i < N_ITEMS; i++)
    n += trended(&M.items[i]);
  return n;
}

static void sens_build(lv_obj_t *b, int first) {
  for (int i = 0, seen = 0; i < N_ITEMS && sens->n < 2; i++)
    if (trended(&M.items[i]) && seen++ >= first)
      sens->id[sens->n++] = M.items[i].id;
  const lv_color_t cols[2] = {T.accent, T.cold};
  sens->chart = mk_chart(b, SENS_W, 5);  // quarters, so both axes' labels sit on the lines
  for (int i = 0; i < sens->n; i++) {
    sens->legend[i] = legend(b, 40 + 140 * i, cols[i], "");
    sens->ser[i] = lv_chart_add_series(sens->chart, cols[i], i ? LV_CHART_AXIS_SECONDARY_Y : LV_CHART_AXIS_PRIMARY_Y);
    for (int k = 0; k < 5; k++) {  // as wide as their text, out from the chart's sides
      lv_obj_t *l = sens->ylbl[i][k] = mk_label(b, &font_f12, T.faint, "");
      if (i)
        lv_obj_set_pos(l, CHART_X + SENS_W + 2, CHART_Y + CHART_H * k / 4 - 8);
      else
        lv_obj_align(l, LV_ALIGN_TOP_RIGHT, CHART_X - 2 - SCR_W, CHART_Y + CHART_H * k / 4 - 8);
    }
  }
  time_axis(b, SENS_W);
}

// Smallest 1, 2 or 5 times a power of ten that is >= n
static int nice_step(int n) {
  for (int p = 1;; p *= 10)
    for (int m = 1; m <= 5; m += m == 1 ? 1 : 3)
      if (m * p >= n)
        return m * p;
}

static void sens_update(void) {
  for (int i = 0; i < sens->n; i++) {
    const item_trend_t *t = trend_of(sens->id[i]);
    const item_t *it = item_by_id(sens->id[i]);
    int lo = INT16_MAX, hi = INT16_MIN;
    for (int k = 0; t && k < TREND_LEN; k++) {
      int v = t->pt[k];
      if (v == TREND_NONE)
        continue;
      lo = v < lo ? v : lo;
      hi = v > hi ? v : hi;
    }
    if (lo > hi) {  // no data yet
      lo = 0;
      hi = 100;
    }
    int step = nice_step((hi - lo + 3) / 4 > 0 ? (hi - lo + 3) / 4 : 1), base;
    while (1) {  // grow the step until 4 steps up from lo, rounded down to a step, reach hi
      base = (lo >= 0 ? lo / step : (lo - step + 1) / step) * step;
      if (base + 4 * step >= hi)
        break;
      step = nice_step(step + 1);
    }
    lv_chart_set_range(sens->chart, i ? LV_CHART_AXIS_SECONDARY_Y : LV_CHART_AXIS_PRIMARY_Y, 0, 4 * SENS_STEP);
    for (int k = 0; k < TREND_LEN; k++) {
      int v = t ? t->pt[k] : TREND_NONE;
      sens->ser[i]->y_points[k] = v == TREND_NONE ? TREND_NONE : (lv_coord_t) ((v - base) * SENS_STEP / step);
    }
    int dec = it ? it->dec : 0, div = 1;
    while (dec && step % (div * 10) == 0) {  // only the decimals the step needs
      dec--;
      div *= 10;
    }
    char a[24];
    for (int k = 0; k < 5; k++)
      lv_label_set_text(sens->ylbl[i][k], fmt_dec(a, (base + (4 - k) * step) / div, (uint8_t) dec));
    if (it && M.esp_online)
      set_text(sens->legend[i], "%s %s", it->label, item_text(a, it, true));
    else
      lv_label_set_text(sens->legend[i], it ? it->label : "");
  }
  lv_chart_refresh(sens->chart);
}

const view_t SENSORS_VIEW = {.build = sens_build,
                             .update = sens_update,
                             .help = S_SENSORS_VIEW_HELP,
                             .per_page = 2,
                             .need = FBIT(FEAT_ESP),
                             .count = sens_count};

// ---------------------------------------------------------------- comfort: ESP items marked for it
// The first two sensors as gauges, toggles as chips highlighted when on
#define COMFORT_CHIPS 8

typedef struct {
  uint8_t gauge[2], chip[COMFORT_CHIPS], n_gauges, n_chips;  // indexes into M.items
  lv_obj_t *arc[2], *val[2], *chips[COMFORT_CHIPS];
} comfort_t;
SCREEN_STATE(comfort_t, comfort);

static bool on_comfort_view(const item_t *it) {
  return (it->flags & (ITEMF_COMFORT | ITEMF_VIEW)) == (ITEMF_COMFORT | ITEMF_VIEW);
}

static int comfort_count(void) {
  int n = 0;
  for (int i = 0; i < N_ITEMS; i++)
    n += M.items[i].type && on_comfort_view(&M.items[i]);
  return n;
}

// Arc gauge with the value, unit and label
static void gauge(lv_obj_t *b, int x, const item_t *it, lv_obj_t **arc, lv_obj_t **val) {
  *arc = mk_arc(b, 104, 8, 0, 100, T.ok);
  lv_obj_set_pos(*arc, x, 4);
  *val = mk_label(b, &font_f28, T.text, "");
  lv_obj_set_width(*val, 100);
  lv_obj_set_style_text_align(*val, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(*val, *arc, LV_ALIGN_CENTER, 0, -6);
  lv_obj_align_to(mk_label(b, &font_f12, T.dim, it->unit), *arc, LV_ALIGN_CENTER, 0, 20);
  lv_obj_t *cap = mk_label(b, &font_f12, T.dim, it->label);
  lv_obj_set_size(cap, 100, lv_font_get_line_height(&font_f12));  // one line, so a long label gets dots
  lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
  label_dots(cap);
  lv_obj_align_to(cap, *arc, LV_ALIGN_BOTTOM_MID, 0, 4);
}

// Toggle chip, an icon and name on a pill, highlighted when on
static lv_obj_t *chip(lv_obj_t *row, const item_t *it) {
  lv_obj_t *c = mk_box(row, 0, 0, LV_SIZE_CONTENT, 28, T.surface, 14);  // comfort_update colours it
  lv_obj_set_style_pad_hor(c, 10, 0);
  lv_obj_set_style_pad_column(c, 5, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mk_icon(c, &icons_16, T.text, icon_glyph(it->icon));
  mk_label(c, &font_f12, T.text, it->label);
  return c;
}

static void comfort_build(lv_obj_t *b, int first) {
  (void) first;
  if (!M.esp_online) {
    mk_empty(b, T.faint, IC_WIFI_OFF, _(S_ESP_NOT_CONNECTED), _(S_COMFORT_NEEDS_ESP));
    return;
  }
  for (int i = 0; i < N_ITEMS; i++) {
    const item_t *it = &M.items[i];
    if (!it->type || !on_comfort_view(it))
      continue;
    if (it->type == ITEM_SENSOR && comfort->n_gauges < 2)
      comfort->gauge[comfort->n_gauges++] = (uint8_t) i;
    else if (it->type == ITEM_TOGGLE && comfort->n_chips < COMFORT_CHIPS)
      comfort->chip[comfort->n_chips++] = (uint8_t) i;
  }
  int x = comfort->n_gauges == 2 ? 36 : 108;
  for (int g = 0; g < comfort->n_gauges; g++, x = 180)
    gauge(b, x, &M.items[comfort->gauge[g]], &comfort->arc[g], &comfort->val[g]);
  lv_obj_t *row = mk_group(b, 304, 72);
  lv_obj_set_pos(row, 8, comfort->n_gauges ? 126 : 70);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(row, 6, 0);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (int c = 0; c < comfort->n_chips; c++)
    comfort->chips[c] = chip(row, &M.items[comfort->chip[c]]);
}

static void comfort_update(void) {
  char a[24];
  for (int g = 0; g < comfort->n_gauges; g++) {
    const item_t *it = &M.items[comfort->gauge[g]];
    lv_arc_set_value(comfort->arc[g], item_pct(it));
    lv_obj_set_style_arc_color(comfort->arc[g], item_color(it), LV_PART_INDICATOR);
    lv_label_set_text(comfort->val[g], item_text(a, it, false));
  }
  for (int c = 0; c < comfort->n_chips; c++) {
    bool on = item_on(&M.items[comfort->chip[c]]);
    lv_obj_set_style_bg_color(comfort->chips[c], on ? T.accent : T.surface, 0);
    for (uint32_t k = 0; k < lv_obj_get_child_cnt(comfort->chips[c]); k++)
      lv_obj_set_style_text_color(lv_obj_get_child(comfort->chips[c], k), on ? T.on_accent : T.dim, 0);
  }
}

const view_t COMFORT_VIEW = {.build = comfort_build,
                             .update = comfort_update,
                             .help = S_COMFORT_VIEW_HELP,
                             .need = FBIT(FEAT_ESP),
                             .count = comfort_count};

// ---------------------------------------------------------------- smart home: ESP items as tiles
#define SMART_MAX 6  // tiles per page, three per row. BACK shows the next page before moving on.
#define SMART_W 96
#define SMART_H 88

typedef struct {
  int n;
  uint8_t item[SMART_MAX];  // indexes into M.items
  lv_obj_t *icon[SMART_MAX], *val[SMART_MAX], *unit[SMART_MAX];
} smart_t;
SCREEN_STATE(smart_t, smart);

static bool on_smart_view(const item_t *it) {
  return it->type && (it->flags & (ITEMF_COMFORT | ITEMF_VIEW)) == ITEMF_VIEW;
}

static int smart_count(void) {
  int n = 0;
  for (int i = 0; i < N_ITEMS; i++)
    n += on_smart_view(&M.items[i]);
  return n;
}

static void smart_build(lv_obj_t *b, int first) {
  if (!M.esp_online) {  // its values would be stale
    mk_empty(b, T.faint, IC_WIFI_OFF, _(S_ESP_NOT_CONNECTED), _(S_SMART_NEEDS_ESP));
    return;
  }
  for (int i = 0, seen = 0; i < N_ITEMS && smart->n < SMART_MAX; i++)
    if (on_smart_view(&M.items[i]) && seen++ >= first)
      smart->item[smart->n++] = (uint8_t) i;
  for (int k = 0; k < smart->n; k++) {
    const item_t *it = &M.items[smart->item[k]];
    lv_obj_t *t = mk_box(b, 8 + (k % 3) * (SMART_W + 8), 4 + (k / 3) * (SMART_H + 6), SMART_W, SMART_H, T.surface, 12);
    smart->icon[k] = mk_icon(t, &icons_22, T.accent, icon_glyph(it->icon));
    lv_obj_set_pos(smart->icon[k], 10, 9);
    lv_obj_t *l = mk_label(t, &font_f12, T.dim, it->label);
    lv_obj_set_size(l, SMART_W - 18, 16);
    label_dots(l);
    lv_obj_set_pos(l, 10, 38);
    smart->val[k] = mk_label(t, &font_f20, T.text, "");
    lv_obj_set_pos(smart->val[k], 10, 54);
    if (it->type == ITEM_TEXT) {  // a text can be longer than the tile
      lv_obj_set_width(smart->val[k], SMART_W - 20);
      label_dots(smart->val[k]);
    }
    smart->unit[k] = mk_label(t, &font_f12, T.dim, it->type == ITEM_SENSOR || it->type == ITEM_NUMBER ? it->unit : "");
  }
}

static void smart_update(void) {
  char buf[24];
  for (int k = 0; k < smart->n; k++) {
    const item_t *it = &M.items[smart->item[k]];
    if (it->type == ITEM_TEXT)
      label_fit(smart->val[k], item_text(buf, it, false), SMART_W - 20);
    else
      lv_label_set_text(smart->val[k], item_text(buf, it, false));
    if (it->type == ITEM_TOGGLE) {
      lv_color_t c = item_on(it) ? T.accent : T.faint;
      lv_obj_set_style_text_color(smart->val[k], c, 0);
      lv_obj_set_style_text_color(smart->icon[k], c, 0);
    } else if (it->flags & ITEMF_LEVELS) {
      lv_obj_set_style_text_color(smart->icon[k], item_color(it), 0);
    }
    lv_obj_align_to(smart->unit[k], smart->val[k], LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -3);
  }
}

const view_t SMART_VIEW = {.build = smart_build,
                           .update = smart_update,
                           .help = S_SMART_VIEW_HELP,
                           .per_page = SMART_MAX,
                           .need = FBIT(FEAT_ESP),
                           .count = smart_count};
