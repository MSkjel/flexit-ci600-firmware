// Key test: UP and OK keep the fan in the air between the ducts.
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define STEP_MS 40
#define SUB 16  // positions and speeds are in 1/16 px
#define DUCTS 3
#define DUCT_W 36
#define DUCT_GAP 76       // between the halves
#define DUCT_SPACING 150  // from one duct to the next
#define DUCT_MOVE 50      // most the gap moves from one duct to the next
#define GAP_MIN (DUCT_GAP / 2 + 10)
#define GAP_MAX (BODY_H - DUCT_GAP / 2 - 10)
#define FAN_X 72    // centre
#define FAN_R 11    // half the box that hits things, a bit inside the icon
#define GRAVITY 14  // SUB per step, each step
#define LIFT (-96)  // SUB per step
#define FALL_MAX 112
#define SPEED_START 32  // ducts, SUB per step
#define SPEED_MAX 64

enum { KT_READY, KT_PAUSED, KT_RUNNING, KT_OVER };

typedef struct {
  lv_obj_t *fan, *score, *msg, *top[DUCTS], *bottom[DUCTS];
  int16_t x[DUCTS], gap[DUCTS];  // left edge in SUB, centre of the gap in px
  bool passed[DUCTS];
  int16_t y, vy;  // fan centre and speed, SUB
  uint16_t points, seed;
  uint8_t state, last;  // KT_*, the duct furthest right
  uint32_t over_at;
} keytest_t;
SCREEN_STATE(keytest_t, kt);

static uint16_t best;  // until the panel restarts

// xorshift, the same gaps for the same start time
static uint16_t kt_rand(void) {
  uint16_t x = kt->seed;
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  return kt->seed = x;
}

static int16_t next_gap(int prev) {
  int lo = LV_MAX(GAP_MIN, prev - DUCT_MOVE), hi = LV_MIN(GAP_MAX, prev + DUCT_MOVE);
  return (int16_t) (lo + kt_rand() % (hi - lo + 1));
}

// Faster with every point
static int duct_speed(void) { return LV_MIN(SPEED_START + kt->points, SPEED_MAX); }

static void place_duct(int i) {
  int x = kt->x[i] / SUB, top = kt->gap[i] - DUCT_GAP / 2;
  lv_obj_set_pos(kt->top[i], x, -CARD_RADIUS);  // the round end above the screen
  lv_obj_set_height(kt->top[i], top + CARD_RADIUS);
  lv_obj_set_pos(kt->bottom[i], x, top + DUCT_GAP);
  lv_obj_set_height(kt->bottom[i], BODY_H - top - DUCT_GAP + CARD_RADIUS);
}

static void place_fan(void) { lv_obj_set_pos(kt->fan, FAN_X - 15, kt->y / SUB - 14); }  // a 30 x 28 label

static void show_msg(const char *txt) {
  lv_label_set_text(kt->msg, txt);
  lv_obj_clear_flag(kt->msg, LV_OBJ_FLAG_HIDDEN);
}

static void kt_reset(void) {
  kt->state = KT_READY;
  kt->points = 0;
  kt->y = BODY_H / 2 * SUB;
  kt->vy = 0;
  kt->seed = (uint16_t) (lv_tick_get() | 1);
  int16_t gap = BODY_H / 2;
  for (int i = 0; i < DUCTS; i++) {
    kt->x[i] = (int16_t) ((SCR_W - 40 + i * DUCT_SPACING) * SUB);  // the first one in sight
    kt->gap[i] = gap = next_gap(gap);
    kt->passed[i] = false;
    place_duct(i);
  }
  kt->last = DUCTS - 1;
  lv_obj_set_style_text_color(kt->fan, T.accent, 0);
  place_fan();
  lv_label_set_text(kt->score, "");
  show_msg(_(S_KEYTEST_START));
}

static void kt_over(void) {
  kt->state = KT_OVER;
  kt->over_at = lv_tick_get();
  if (kt->points > best)
    best = kt->points;
  lv_obj_set_style_text_color(kt->fan, T.err, 0);
  char points[24], record[24];
  snprintf(points, sizeof points, _(S_KEYTEST_SCORE), kt->points);
  snprintf(record, sizeof record, _(S_KEYTEST_BEST), best);
  lv_label_set_text(kt->score, "");
  set_text(kt->msg, "%s\n%s\n%s", points, record, _(S_KEYTEST_AGAIN));
  lv_obj_clear_flag(kt->msg, LV_OBJ_FLAG_HIDDEN);
}

static void kt_step(lv_timer_t *t) {
  (void) t;
  if (kt->state != KT_RUNNING)
    return;
  if (ui_top() != &SCR_KEYTEST) {  // a pop-up, UP carries on after it
    kt->state = KT_PAUSED;
    show_msg(_(S_KEYTEST_START));
    return;
  }
  input_activity();  // no screensaver mid-run
  kt->vy = (int16_t) LV_MIN(kt->vy + GRAVITY, FALL_MAX);
  kt->y = (int16_t) (kt->y + kt->vy);
  int v = duct_speed(), fy = kt->y / SUB;
  for (int i = 0; i < DUCTS; i++)
    kt->x[i] = (int16_t) (kt->x[i] - v);
  bool hit = fy - FAN_R < 0 || fy + FAN_R > BODY_H;
  for (int i = 0; i < DUCTS; i++) {
    if (kt->x[i] < -DUCT_W * SUB) {  // gone on the left, comes back on the right
      kt->x[i] = (int16_t) (kt->x[kt->last] + DUCT_SPACING * SUB);
      kt->gap[i] = next_gap(kt->gap[kt->last]);
      kt->passed[i] = false;
      kt->last = (uint8_t) i;
    }
    int x = kt->x[i] / SUB;
    if (x < FAN_X + FAN_R && x + DUCT_W > FAN_X - FAN_R &&
        (fy - FAN_R < kt->gap[i] - DUCT_GAP / 2 || fy + FAN_R > kt->gap[i] + DUCT_GAP / 2))
      hit = true;
    if (!kt->passed[i] && x + DUCT_W < FAN_X - FAN_R) {
      kt->passed[i] = true;
      set_text(kt->score, "%u", ++kt->points);
    }
    place_duct(i);
  }
  place_fan();
  if (hit)
    kt_over();
}

static lv_obj_t *mk_duct(lv_obj_t *b) {
  lv_obj_t *d = mk_card(b, 0, 0, DUCT_W, 0, T.accent, 2);
  lv_obj_set_style_bg_color(d, T.accent_soft, 0);
  return d;
}

static void kt_build(lv_obj_t *b, intptr_t arg) {
  (void) arg;
  memset(kt, 0, sizeof *kt);
  for (int i = 0; i < DUCTS; i++) {
    kt->top[i] = mk_duct(b);
    kt->bottom[i] = mk_duct(b);
  }
  kt->fan = mk_icon(b, &icons_32, T.accent, IC_FAN);
  kt->score = mk_label(b, &font_f28, T.text, "");
  lv_obj_align(kt->score, LV_ALIGN_TOP_MID, 0, 4);
  kt->msg = mk_label(b, &font_f16, T.text, "");
  lv_obj_set_style_text_align(kt->msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(kt->msg, T.surface, 0);
  lv_obj_set_style_bg_opa(kt->msg, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(kt->msg, CARD_RADIUS, 0);
  lv_obj_set_style_pad_hor(kt->msg, 18, 0);
  lv_obj_set_style_pad_ver(kt->msg, 10, 0);
  lv_obj_align(kt->msg, LV_ALIGN_CENTER, 24, 0);
  mk_timer(kt->fan, kt_step, STEP_MS);
  kt_reset();
  ui_hints(S_NONE, S_NONE, S_BACK, S_HELP);
  ui_help(S_KEYTEST_HELP);
}

static bool kt_key(int k) {
  if (k != KEY_UP && k != KEY_OK)
    return k == KEY_DOWN || k == KEY_OK_LONG;  // BACK and ? as usual
  if (kt->state == KT_OVER) {
    if (k == KEY_OK && lv_tick_get() - kt->over_at > 500)  // not straight on from the UP that hit
      kt_reset();
    return true;
  }
  if (kt->state != KT_RUNNING) {
    kt->state = KT_RUNNING;
    lv_obj_add_flag(kt->msg, LV_OBJ_FLAG_HIDDEN);
    set_text(kt->score, "%u", kt->points);
  }
  kt->vy = LIFT;
  return true;
}

const screen_t SCR_KEYTEST = {.title = S_KEYTEST, .build = kt_build, .key = kt_key};
