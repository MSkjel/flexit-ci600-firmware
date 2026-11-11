// app/overlay_motion.c and overlay_draw.c: how the places move, turn, grow and fade, and what they redraw
#include "unit_test.h"
#include "overlay_int.h"

static bool overlay_angle(int k, int *deg, int *flip) {
  overlay_look_t l;
  if (!overlay_look(k, &l))
    return false;
  *deg = l.deg;
  *flip = l.view & (OVERLAY_VIEW_FLIP_X | OVERLAY_VIEW_FLIP_Y);
  return true;
}

// Whether place 0's box has its top left at x, y
static bool box_at(int x, int y) {
  overlay_look_t l;
  return overlay_look(0, &l) && l.box.x1 == x && l.box.y1 == y;
}

static bool overlay_at(int x, int y) {
  int16_t ax, ay;
  return overlay_pos(0, &ax, &ay) && ax == x && ay == y && box_at(x, y);
}

// whether place 0 is drawn again a screen to the left, up, and both, for the part over the right or bottom edge
static bool ghosts_shown(bool right, bool below) {
  overlay_look_t l;
  return overlay_look(0, &l) && l.tiles == (1 | right << 1 | below << 2 | (right && below) << 3);
}

// GLIDE of place p's channel c to v over ms, eased
static void glide_msg(int p, int c, int v, int ms, int ease) {
  uint8_t m[OVERLAY_GLIDE_HEAD + OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, p, ms, ease, EACH(c, v)));
}

static void drift_ch(int p, int c, int v) {
  uint8_t m[OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_DRIFT, m, sim_overlay_drift(m, p, EACH(c, v)));
}

// place 0 to x, y over ms, or drifting vx, vy px/s, each one message
static void move_msg(int x, int y, int ms) {
  uint8_t m[OVERLAY_GLIDE_HEAD + 2 * OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, 0, ms, 0, EACH(OVERLAY_X, x, OVERLAY_Y, y)));
}

static void drift_msg(int vx, int vy) {
  uint8_t m[OVERLAY_DRIFT_HEAD + 2 * OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_DRIFT, m, sim_overlay_drift(m, 0, EACH(OVERLAY_X, vx, OVERLAY_Y, vy)));
}

// GLIDE and DRIFT of x and y: glides land on the target, drifts wrap round the screen, bad messages are ignored
static void test_anim_move(void) {
  no_overlays();
  dot_frames(false);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  move_msg(100, 50, 0);  // no animation yet
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 10, 10, 0, 0);
  show_overlays();
  CHECK(overlay_at(10, 10) && ghosts_shown(false, false));  // 16x8 where it was placed
  uint8_t glide[10] = {0, 0, 0, 0, OVERLAY_X, 100, 0, OVERLAY_SIZE, 5, 0}, drift[5] = {0, OVERLAY_X, 0xD8, 0xFF};
  overlay_msg(K_OVERLAY_GLIDE, glide, 6);  // a value cut short
  overlay_msg(K_OVERLAY_GLIDE, glide, 8);  // a channel without one
  overlay_msg(K_OVERLAY_DRIFT, drift, 5);
  overlay_msg(K_OVERLAY_GLIDE, NULL, 0);
  overlay_msg(K_OVERLAY_GLIDE, glide, OVERLAY_GLIDE_HEAD);  // no channel
  overlay_msg(K_OVERLAY_GLIDE, glide, sizeof glide);        // x is fine, size 5 isn't, so neither goes
  glide[4] = OVERLAY_CHANNELS;                              // no such channel
  overlay_msg(K_OVERLAY_GLIDE, glide, 7);
  glide[4] = OVERLAY_X;
  glide[OVERLAY_GLIDE_EASE] = OVERLAY_EASE_IN << 2;  // no such ease
  overlay_msg(K_OVERLAY_GLIDE, glide, 7);
  drift[1] = OVERLAY_SIZE;  // only those before size drift
  overlay_msg(K_OVERLAY_DRIFT, drift, 4);
  glide[0] = 1;  // no such place
  glide[OVERLAY_GLIDE_EASE] = 0;
  overlay_msg(K_OVERLAY_GLIDE, glide, 7);
  sim_run(200);
  CHECK(overlay_at(10, 10));
  move_msg(100, 50, 0);  // ms = 0 is a jump
  CHECK(overlay_at(100, 50));
  move_msg(310, 50, 0);  // partly off the right edge: that's where it is, no wrapping until it drifts
  CHECK(overlay_at(310, 50) && ghosts_shown(false, false));
  drift_msg(-40, 0);  // 2 px a step
  sim_run(50);
  CHECK(overlay_at(308, 50) && ghosts_shown(true, false));
  move_msg(-20, 60, 1000);  // a glide can end partly off the screen
  sim_run(500);
  int16_t x, y;
  overlay_pos(0, &x, &y);
  CHECK_EQ(x, 144);
  CHECK_EQ(y, 55);  // halfway from 308, 50
  sim_run(500);
  CHECK(overlay_at(-20, 60));  // landed exactly
  sim_run(500);
  CHECK(overlay_at(-20, 60));  // and stays
  drift_msg(-40, 0);           // a drift puts it on the screen again, from the right
  sim_run(50);
  CHECK(overlay_at(298, 60) && ghosts_shown(false, false));
  sim_run(50 * 149);
  CHECK(overlay_at(0, 60));  // 2 px a step
  sim_run(50);
  CHECK(overlay_at(318, 60) && ghosts_shown(true, false));  // over the right edge, the ghost draws the rest
  sim_run(50 * 159);
  CHECK(overlay_at(0, 60) && ghosts_shown(false, false));
  drift_msg(0, 100);  // 5 px a step down
  sim_run(50 * 35);
  CHECK(overlay_at(0, 235) && ghosts_shown(false, true));
  sim_run(50);
  CHECK(overlay_at(0, 0) && ghosts_shown(false, false));
  sim_run(50);
  CHECK(overlay_at(0, 5));
  drift_msg(-1, 0);  // the sub-pixel part adds up
  sim_run(50);
  CHECK(overlay_at(319, 5) && ghosts_shown(true, false));
  sim_run(50 * 19);
  CHECK(overlay_at(319, 5));
  sim_run(50);
  CHECK(overlay_at(318, 5));
  drift_msg(0, 0);
  sim_run(1000);
  CHECK(overlay_at(318, 5));
  // LVGL too busy for two steps: the next one makes up for them
  move_msg(100, 100, 0);
  drift_msg(40, 0);
  sim_run(50);
  lv_tick_inc(150);
  lv_timer_handler();
  CHECK(overlay_at(108, 100));
  drift_msg(0, 0);
  // the axes go their own ways: x drifts while y glides
  move_msg(100, 100, 0);
  drift_ch(0, OVERLAY_X, 40);
  glide_msg(0, OVERLAY_Y, 120, 500, 0);
  sim_run(500);
  CHECK(overlay_at(120, 120));
  sim_run(500);
  CHECK(overlay_at(140, 120));
  drift_msg(0, 0);
  move_msg(318, 5, 0);
  drift_msg(-1, 0);
  drift_msg(0, 0);  // on the screen's torus again, still
  // the animation outlives its image, and the motion with it
  lv_obj_del(overlays);
  drift_msg(-1, 0);
  sim_run(2000);  // nothing moves without an image
  show_overlays();
  CHECK(overlay_at(318, 5) && ghosts_shown(true, false));
  sim_run(1000);
  CHECK(overlay_at(317, 5));
  lv_obj_del(overlays);
  // placing it again stops it and puts it there
  place(0, 0, 20, 10, 0, 0);
  show_overlays();
  sim_run(1000);
  CHECK(overlay_at(20, 10));
  drift_msg(-1, 0);
  overlay_clear_msg(0);  // clearing the slot takes the place with it
  CHECK(!overlay_pos(0, &x, &y));
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 20, 10, 0, 0);
  show_overlays();
  sim_run(2000);
  CHECK(overlay_at(20, 10));
  glide_msg(0, OVERLAY_X, -32768, 0, 0);  // a glide across the whole int16 range
  glide_msg(0, OVERLAY_X, 32767, 65535, 0);
  sim_run(50000);
  int16_t gx, gy;
  overlay_pos(0, &gx, &gy);
  CHECK_EQ(gx, 17232);
  CHECK_EQ(gy, 10);
  sim_run(15550);  // landed
  overlay_pos(0, &gx, &gy);
  CHECK_EQ(gx, 32767);
  CHECK_EQ(gy, 10);
  // a message every 30 ms doesn't hold it up, it drifts on at 2 px a step
  move_msg(100, 100, 0);
  drift_msg(40, 0);
  for (int i = 0; i < 33; i++) {
    glide_msg(0, OVERLAY_OPACITY, 100, 0, 0);
    sim_run(30);
  }
  CHECK(overlay_at(138, 100));
  lv_obj_del(overlays);
  overlay_clear_msg(OVERLAY_ALL);
}

// Glides ease in, out or both: from 0 to 200 over a second, where it is at 250 and 500 ms
static void test_anim_ease(void) {
  no_overlays();
  dot_frames(false);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  show_overlays();
  static const int16_t WANT[4][2] = {{50, 100}, {12, 50}, {87, 150}, {31, 100}};  // none, in, out, both
  for (int e = 0; e < 4; e++) {
    glide_msg(0, OVERLAY_X, 0, 0, 0);
    glide_msg(0, OVERLAY_X, 200, 1000, e);
    int16_t x[2], y;
    sim_run(250);
    overlay_pos(0, &x[0], &y);
    sim_run(250);
    overlay_pos(0, &x[1], &y);
    CHECK_EQ(x[0], WANT[e][0]);
    CHECK_EQ(x[1], WANT[e][1]);
    sim_run(500);
    CHECK(overlay_pos(0, &x[0], &y));
    CHECK_EQ(x[0], 200);
  }
  lv_obj_del(overlays);
  overlay_clear_msg(OVERLAY_ALL);
}

// The frame's pixel at ix, iy of the 16 x 8 it's drawn at, scale 2, as drawn() gives it
static int frame_px(int ix, int iy) {
  uint8_t b = an[0][iy / 2][ix / 4];
  int k = ix / 2 & 1 ? b & 15 : b >> 4;
  return k == 15 ? -1 : k;
}

// Place k's pixel at bx, by of its box as drawn() gives it, -3 if it isn't drawn
static int drawn_px(int k, int bx, int by) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  overlay_look_t l;
  return drawn(k, &l, px) ? px[by * lv_area_get_width(&l.box) + bx] : -3;
}

// Whether place 0 is drawn w x h, every pixel the frame's at where(bx, by)
static bool drawn_as(int w, int h, void (*where)(int bx, int by, int *ix, int *iy)) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  overlay_look_t l;
  if (!drawn(0, &l, px) || lv_area_get_width(&l.box) != w || lv_area_get_height(&l.box) != h)
    return false;
  for (int by = 0; by < h; by++)
    for (int bx = 0; bx < w; bx++) {
      int ix, iy;
      where(bx, by, &ix, &iy);
      if (px[by * w + bx] != frame_px(ix, iy))
        return false;
    }
  return true;
}

static void at_same(int bx, int by, int *ix, int *iy) {
  *ix = bx;
  *iy = by;
}
static void at_90(int bx, int by, int *ix, int *iy) {  // turned a quarter clockwise: the right side faces down
  *ix = by;
  *iy = 7 - bx;
}
static void at_180(int bx, int by, int *ix, int *iy) {
  *ix = 15 - bx;
  *iy = 7 - by;
}
static void at_flip_x(int bx, int by, int *ix, int *iy) {
  *ix = 15 - bx;
  *iy = by;
}
static void at_flip_x_90(int bx, int by, int *ix, int *iy) {  // mirrored, then turned
  *ix = 15 - by;
  *iy = 7 - bx;
}

// The same frame each time, nothing in it symmetric
static void odd_frames(void) {
  memset(an, 0xFF, sizeof an);
  for (int f = 0; f < 4; f++) {
    an[f][0][0] = 0x12;
    an[f][1][1] = 0x3F;
    an[f][3][3] = 0x45;
  }
}

static void flip_msg(int p, int flip) {
  uint8_t m[OVERLAY_VIEW_LEN];
  overlay_msg(K_OVERLAY_VIEW, m, sim_overlay_view(m, p, OVERLAY_VIEW_FLIP_X | OVERLAY_VIEW_FLIP_Y, (unsigned) flip));
}

static void pivot_msg(int p, int x, int y) {
  uint8_t m[OVERLAY_GLIDE_HEAD + 2 * OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, p, 0, 0, EACH(OVERLAY_PIVOT_X, x, OVERLAY_PIVOT_Y, y)));
}

// Turns and FLIP: quarter turns and flips draw exactly, turns go the short way, spins add up, bad messages are
// ignored
static void test_anim_turn(void) {
  no_overlays();
  odd_frames();
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 10, 10, 0, 0);
  show_overlays();
  int a, flip;
  glide_msg(0, OVERLAY_TURN, 90, 0, 0);  // at once
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 90);
  CHECK(!flip);
  CHECK(drawn_as(8, 16, at_90));
  CHECK(box_at(14, 6));  // centred where the 16 x 8 frame was
  glide_msg(0, OVERLAY_TURN, 180, 0, 0);
  CHECK(drawn_as(16, 8, at_180) && box_at(10, 10));
  flip_msg(0, OVERLAY_VIEW_FLIP_X);
  glide_msg(0, OVERLAY_TURN, 0, 0, 0);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 0);
  CHECK_EQ(flip, OVERLAY_VIEW_FLIP_X);
  CHECK(drawn_as(16, 8, at_flip_x));
  glide_msg(0, OVERLAY_TURN, 90, 0, 0);
  CHECK(drawn_as(8, 16, at_flip_x_90));
  // a turn over time goes the short way round: 90 to 350 is 100 back, not 260 on
  flip_msg(0, 0);
  glide_msg(0, OVERLAY_TURN, 350, 1000, 0);
  sim_run(500);
  CHECK(overlay_angle(0, &a, &flip) && a >= 38 && a <= 42);
  sim_run(600);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 350);
  // a spin adds up past 360, and a stop keeps the angle
  drift_ch(0, OVERLAY_TURN, 90);
  sim_run(1000);
  CHECK(overlay_angle(0, &a, &flip) && a >= 75 && a <= 80);
  drift_ch(0, OVERLAY_TURN, 0);
  sim_run(1000);
  int b;
  CHECK(overlay_angle(0, &b, &flip));
  CHECK_EQ(b, a);
  // the wrong length and no such place change nothing
  flip_msg(5, OVERLAY_VIEW_FLIP_Y);
  overlay_msg(K_OVERLAY_VIEW, (uint8_t[]) {0, OVERLAY_VIEW_FLIP_Y, OVERLAY_VIEW_FLIP_Y, 0}, 4);
  overlay_msg(K_OVERLAY_VIEW, NULL, 0);
  CHECK(overlay_angle(0, &b, &flip));
  CHECK_EQ(b, a);
  CHECK(!flip);
  // placed again, it starts unturned
  lv_obj_del(overlays);
  place(0, 0, 10, 10, 0, 0);
  show_overlays();
  CHECK(overlay_angle(0, &a, &flip) && !a && !flip && drawn_as(16, 8, at_same));
  lv_obj_del(overlays);
  overlay_clear_msg(OVERLAY_ALL);
}

static void at_200(int bx, int by, int *ix, int *iy) {
  *ix = bx / 2;
  *iy = by / 2;
}
static void at_50(int bx, int by, int *ix, int *iy) {
  *ix = 2 * bx + 1;
  *iy = 2 * by + 1;
}
static void at_90_200(int bx, int by, int *ix, int *iy) {
  *ix = by / 2;
  *iy = 7 - bx / 2;
}

static int look_size(int place) {
  overlay_look_t l;
  return overlay_look(place, &l) ? l.size : -1;
}

// Size and opacity: sizes draw exactly round the middle, sizes and fades get there over time, a place can fade in
// and a clear can fade it out first
static void test_anim_look(void) {
  no_overlays();
  odd_frames();
  uint8_t d[512], d2[OVERLAY_POOL];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 40, 40, 0, 0);
  show_overlays();
  glide_msg(0, OVERLAY_SIZE, 200, 0, 0);
  CHECK_EQ(look_size(0), 2000);
  CHECK(drawn_as(32, 16, at_200));
  CHECK(box_at(32, 36));
  glide_msg(0, OVERLAY_SIZE, 50, 0, 0);
  CHECK(drawn_as(8, 4, at_50) && box_at(44, 42));
  glide_msg(0, OVERLAY_TURN, 90, 0, 0);  // a quarter turn at twice the size
  glide_msg(0, OVERLAY_SIZE, 200, 0, 0);
  CHECK(drawn_as(16, 32, at_90_200) && box_at(40, 28));
  glide_msg(0, OVERLAY_TURN, 0, 0, 0);
  glide_msg(0, OVERLAY_SIZE, 100, 1000, 0);  // back over a second, then drawn straight again
  sim_run(500);
  CHECK_EQ(look_size(0), 1500);
  sim_run(600);
  CHECK_EQ(look_size(0), 1000);
  CHECK(drawn_as(16, 8, at_same));
  CHECK(box_at(40, 40));
  glide_msg(0, OVERLAY_SIZE, 9, 0, 0);  // out of range
  glide_msg(0, OVERLAY_SIZE, 401, 0, 0);
  glide_msg(0, OVERLAY_OPACITY, 101, 0, 0);
  glide_msg(0, OVERLAY_SPEED, 1001, 0, 0);
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(look_opa(0), 1000);
  // to see-through over a second, then opaque at once
  glide_msg(0, OVERLAY_OPACITY, 0, 1000, 0);
  sim_run(500);
  CHECK_EQ(look_opa(0), 500);
  CHECK_EQ(drawn_px(0, 0, 0), -2);  // half the frame's, half what's behind
  sim_run(600);
  int16_t x, y;
  CHECK_EQ(look_opa(0), 0);
  CHECK(overlay_pos(0, &x, &y));
  glide_msg(0, OVERLAY_OPACITY, 100, 0, 0);
  CHECK_EQ(look_opa(0), 1000);
  CHECK_EQ(drawn_px(0, 0, 0), 1);
  // placed fading in over 400 ms
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  place_in(1, 0, 100, 40, 0, 0, 400);
  show_overlays();
  CHECK_EQ(look_opa(1), 0);
  sim_run(200);
  CHECK_EQ(look_opa(1), 500);
  sim_run(250);
  CHECK_EQ(look_opa(1), 1000);
  // cleared over 500 ms: it stays while it fades, then goes, with nothing to tell the ESP
  overlay_clear_msg(OVERLAY_PLACE_BIT | 1 | 500 << 8);
  sim_run(250);
  CHECK(overlay_pos(1, &x, &y));
  CHECK_EQ(look_opa(1), 500);
  sim_run(300);
  CHECK(!overlay_pos(1, &x, &y));
  CHECK_EQ(overlay_gone(), -1);
  // a slot cleared over time goes with its last place, and gives back its room then
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 40, 0, 1, 0);
  show_overlays();
  unsigned used = overlay_used();
  overlay_clear_msg(0 | 300 << 8);
  CHECK_EQ(overlay_used(), used);
  CHECK(overlay_pos(0, &x, &y));
  CHECK(overlay_pos(1, &x, &y));
  place(2, 0, 80, 0, 0, 0);  // no new places for it
  CHECK(!overlay_pos(2, &x, &y));
  sim_run(400);
  CHECK_EQ(overlay_used(), 0);
  CHECK(!overlay_pos(0, &x, &y));
  CHECK(!overlay_pos(1, &x, &y));
  // also when its last places go at once or onto another slot
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK_EQ(status(overlay_send(1, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 40, 0, 0, 0);
  overlay_clear_msg(0 | 300 << 8);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  place(1, 1, 40, 0, 0, 0);
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  CHECK(overlay_pos(1, &x, &y));
  overlay_clear_msg(OVERLAY_ALL);
  // but a new animation that needs the room takes it at once
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  lv_obj_clean(lv_scr_act());
  place(0, 0, 0, 0, 0, 0);
  show_overlays();
  overlay_clear_msg(0 | 5000 << 8);
  unsigned frame = AW / 2 * AH;
  uint32_t n2 = overlay_records(d2, 5), m = (OVERLAY_POOL - frame - n2) / 5;
  for (uint32_t i = 0; i < m; i++)
    memcpy(d2 + n2 + 5 * i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  n2 += 5 * m;
  CHECK_EQ(status(overlay_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  CHECK(!overlay_pos(0, &x, &y));
  CHECK_EQ(overlay_used(), frame + n2);
  // not shown, it goes at once
  lv_obj_clean(lv_scr_act());
  place(2, 1, 0, 0, 0, 0);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 2 | 500 << 8);
  CHECK(!overlay_pos(2, &x, &y));
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(overlay_used(), 0);
}

static void area_msg(int x0, int y0, int x1, int y1, int ex, int ey) {
  uint8_t m[11] = {0};
  plink_put16(m + 1, x0);
  plink_put16(m + 3, y0);
  plink_put16(m + 5, x1);
  plink_put16(m + 7, y1);
  m[9] = (uint8_t) ex;
  m[10] = (uint8_t) ey;
  overlay_msg(K_OVERLAY_AREA, m, sizeof m);
}

static void wander_msg(int s0, int s1, int heading, int spread, int l0, int l1, int r0, int r1) {
  uint8_t m[17] = {0};
  int v[8] = {s0 * 10, s1 * 10, heading, spread, l0, l1, r0, r1};  // speeds in tenths
  for (int i = 0; i < 8; i++)
    plink_put16(m + 1 + 2 * i, v[i]);
  overlay_msg(K_OVERLAY_WANDER, m, sizeof m);
}

static void face_msg(int flags, int facing, int tilt, int rate, int len) {
  uint8_t m[8] = {0, (uint8_t) flags};
  plink_put16(m + 2, facing);
  plink_put16(m + 4, tilt);
  plink_put16(m + 6, rate);
  overlay_msg(K_OVERLAY_FACE, m, len);
}

// How far a run of steps went, whether it kept to x0..x1, y0..y1 with its 16 x 8, and how often it stood still
typedef struct {
  int lo_x, hi_x, lo_y, hi_y, rests;
  bool inside;
} trail_t;

static trail_t trail(int steps, int x0, int y0, int x1, int y1) {
  trail_t t = {999, -999, 999, -999, 0, true};
  int16_t x, y, px = -1, py = -1;
  int still = 0;
  for (int i = 0; i < steps; i++) {
    sim_run(50);
    overlay_pos(0, &x, &y);
    t.inside &= x >= x0 && x + 16 <= x1 && y >= y0 && y + 8 <= y1;
    t.lo_x = LV_MIN(t.lo_x, x);
    t.hi_x = LV_MAX(t.hi_x, x);
    t.lo_y = LV_MIN(t.lo_y, y);
    t.hi_y = LV_MAX(t.hi_y, y);
    still = x == px && y == py ? still + 1 : 0;
    t.rests += still == 3;
    px = x;
    py = y;
  }
  return t;
}

// K_OVERLAY_AREA, OVERLAY_WANDER and OVERLAY_FACE: a drift bounces inside its area or crosses out and back in, a wander
// keeps to its area, rests, and turns back off the edges, and a place can face where it goes
static void test_anim_paths(void) {
  no_overlays();
  odd_frames();
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_overlays();
  area_msg(50, 80, 200, 140, OVERLAY_EDGE_BOUNCE, OVERLAY_EDGE_BOUNCE);
  drift_msg(100, 37);
  trail_t t = trail(400, 50, 80, 200, 140);
  CHECK(t.inside);
  CHECK_EQ(t.lo_x, 50);
  CHECK_EQ(t.hi_x, 184);
  CHECK_EQ(t.lo_y, 80);
  CHECK_EQ(t.hi_y, 132);
  // out across the right edge and in at the left, and back
  area_msg(50, 80, 200, 140, OVERLAY_EDGE_CROSS, OVERLAY_EDGE_BOUNCE);
  move_msg(180, 100, 0);
  drift_msg(100, 0);
  int xs[5], want[5] = {185, 190, 195, 34, 39};
  int16_t x, y;
  for (int i = 0; i < 5; i++) {
    sim_run(50);
    overlay_pos(0, &x, &y);
    xs[i] = x;
  }
  CHECK(!memcmp(xs, want, sizeof xs));
  drift_msg(-100, 0);
  sim_run(50);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 200);
  sim_run(50);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 195);
  area_msg(50, 80, 70, 140, OVERLAY_EDGE_CROSS, OVERLAY_EDGE_BOUNCE);
  glide_msg(0, OVERLAY_SPEED, 1000, 0, 0);
  drift_msg(300, 0);  // ten times that is across the area many times a step, and still about it
  int lo = 9999, hi = -9999;
  for (int i = 0; i < 40; i++) {
    sim_run(50);
    overlay_pos(0, &x, &y);
    lo = LV_MIN(lo, x);
    hi = LV_MAX(hi, x);
  }
  CHECK(lo >= 50 - 16 && hi < 70 && hi > lo);
  glide_msg(0, OVERLAY_SPEED, 100, 0, 0);
  // a wander about its area, resting now and then. A seed of its own, the time it was placed depends on the tests
  // before.
  places[0].rng = 1;
  unsigned used = overlay_used();
  area_msg(60, 40, 260, 200, OVERLAY_EDGE_BOUNCE, OVERLAY_EDGE_BOUNCE);
  wander_msg(20, 60, 0, 180, 200, 800, 0, 300);
  CHECK_EQ(overlay_used(), used + OVERLAY_ROAM);  // what it takes while it does
  t = trail(1200, 60, 40, 260, 200);
  CHECK(t.inside && t.hi_x - t.lo_x > 100 && t.hi_y - t.lo_y > 60 && t.rests > 0);
  // heading right with little spread, it turns back off the right edge and goes all the way left
  wander_msg(100, 100, 0, 10, 300, 300, 0, 0);
  t = trail(200, 60, 40, 260, 200);
  CHECK(t.inside);
  CHECK_EQ(t.lo_x, 60);
  CHECK_EQ(t.hi_x, 244);
  CHECK(!t.rests);
  // a bad area or wander changes nothing
  area_msg(60, 40, 60, 200, OVERLAY_EDGE_BOUNCE, OVERLAY_EDGE_BOUNCE);
  area_msg(60, 40, 260, 200, OVERLAY_EDGE_BOUNCE + 1, OVERLAY_EDGE_BOUNCE);
  overlay_msg(K_OVERLAY_AREA, (uint8_t[]) {0, 0, 0, 0, 0, 1, 0, 1, 0, 0}, 10);
  t = trail(200, 60, 40, 260, 200);
  CHECK(t.inside);

  // frames that face right, mirrored to face left, leaning up to 30 degrees, upright when it stops
  area_msg(0, 0, PIC_MAX_W, PIC_MAX_H, OVERLAY_EDGE_BOUNCE, OVERLAY_EDGE_BOUNCE);
  face_msg(OVERLAY_FACE_MIRROR | OVERLAY_FACE_LEVEL, 0, 30, 0, 8);
  CHECK_EQ(overlay_used(), used + OVERLAY_ROAM);  // the two take it once
  move_msg(150, 100, 0);                          // the wander is over, and it still faces
  CHECK_EQ(overlay_used(), used + OVERLAY_ROAM);
  int a, flip;
  drift_msg(-50, 0);
  sim_run(50);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 0);
  CHECK_EQ(flip, OVERLAY_VIEW_FLIP_X);
  drift_msg(-50, 50);  // down to the left
  sim_run(50);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 330);
  CHECK_EQ(flip, OVERLAY_VIEW_FLIP_X);
  drift_msg(50, 50);  // down to the right: unmirrored, the same lean the other way
  sim_run(50);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 30);
  CHECK(!flip);
  drift_msg(0, 0);
  sim_run(50);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 0);
  CHECK(!flip);
  // 90 degrees a second
  face_msg(OVERLAY_FACE_MIRROR, 0, 30, 90, 8);
  drift_msg(50, -50);
  sim_run(100);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 351);
  sim_run(500);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 330);
  // frames that face up turn all the way round toward where it goes
  face_msg(0, 270, 180, 0, 8);
  drift_msg(50, 0);
  sim_run(50);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 90);
  CHECK(!flip);
  face_msg(0, 270, 181, 0, 8);
  face_msg(OVERLAY_FACE_LEVEL << 1, 270, 0, 0, 8);
  face_msg(0, 270, 0, -1, 8);
  face_msg(0, 270, 0, 0, 7);
  drift_msg(0, 50);
  sim_run(50);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 180);
  glide_msg(0, OVERLAY_TURN, 45, 0, 0);  // a turn takes over
  sim_run(200);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 45);
  CHECK_EQ(overlay_used(), used);  // and with neither, nothing
  // without the room neither starts, and it goes on as it did: slot 1 padded with empty records until the pool is
  // full
  static uint8_t d2[OVERLAY_POOL];
  uint32_t n2 = overlay_records(d2, 10);
  for (; used + AW / 2 * AH + n2 + 5 <= OVERLAY_POOL; n2 += 5)
    memcpy(d2 + n2, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  CHECK_EQ(status(overlay_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  unsigned full = overlay_used();
  CHECK(OVERLAY_POOL - full < OVERLAY_ROAM);
  move_msg(100, 100, 0);
  drift_msg(100, 0);
  wander_msg(0, 0, 0, 0, 0, 0, 0, 0);
  face_msg(0, 270, 180, 0, 8);
  sim_run(200);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 120);
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 45);
  CHECK_EQ(overlay_used(), full);
  // and a place that goes gives it back
  overlay_clear_msg(1);
  wander_msg(0, 0, 0, 0, 0, 0, 0, 0);
  CHECK_EQ(overlay_used(), used + OVERLAY_ROAM);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  CHECK_EQ(overlay_used(), used);
  lv_obj_del(overlays);
  overlay_clear_msg(OVERLAY_ALL);
}

// BIND of place p: what, an op | what it binds, to the value in v
static void bind_to(int p, unsigned what, const uint8_t *v, int n) {
  uint8_t m[OVERLAY_BIND_VALUE + 64];
  overlay_msg(K_OVERLAY_BIND, m, sim_overlay_bind(m, p, what, v, n));
}
#define BIND(p, what, ...) bind_to(p, what, BYTES(__VA_ARGS__))
static void unbind(int p, int c) { bind_to(p, OVERLAY_BIND_FREE | (unsigned) c, NULL, 0); }

// A wave of place p's channel c from when it was placed, as the ESP binds one: added, or a share times it
static void wave_bind(int p, int c, int lo, int hi, int rise, int fall, int hold_hi, int hold_lo, int jitter,
                      int flags) {
  BIND(p, (scales(c) ? OVERLAY_BIND_MUL : OVERLAY_BIND_ADD) | (unsigned) c,
       X(OWN(OVERLAY_GET_MS), V32(lo * unit(c)), V32(hi * unit(c)), V16(rise), V16(fall), V16(hold_hi), V16(hold_lo),
         V8(jitter), V8(flags), OVERLAY_OP_WAVE));
}

// BIND and OVERLAY_OP_WAVE: a size breathes there and back, a blink holds at each end, a wave on x eases at its slow
// end, random legs stay between low and high, a channel takes one bind of each op and they go together, a bad one
// changes nothing, they go with their place, and there are OVERLAY_BINDS at most
static void test_anim_bind(void) {
  no_overlays();
  odd_frames();
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_overlays();
  unsigned used = overlay_used();
  wave_bind(0, OVERLAY_SIZE, 100, 120, 1000, 1000, 0, 0, 0, 0);
  CHECK_EQ(overlay_used(), used + 1 + 31);  // its value, and a byte for how long that is
  static const int BREATH[] = {1100, 1200, 1100, 1000, 1100};
  bool ok = true;
  for (int i = 0; i < 5; i++) {
    sim_run(500);
    ok &= look_size(0) == BREATH[i];
  }
  CHECK(ok);
  unbind(0, OVERLAY_SIZE);  // as it was at once
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(overlay_used(), used);
  // on for 500 ms and off for 300, at once
  place(0, 0, 100, 100, 0, 0);
  wave_bind(0, OVERLAY_OPACITY, 0, 100, 0, 0, 500, 300, 0, 0);
  sim_run(450);
  CHECK_EQ(look_opa(0), 1000);
  sim_run(100);
  CHECK_EQ(look_opa(0), 0);
  sim_run(200);
  CHECK_EQ(look_opa(0), 0);
  sim_run(100);
  CHECK_EQ(look_opa(0), 1000);
  // 100 px right and back, slow at the low end: it eases in going out and eases out coming back
  place(0, 0, 100, 100, 0, 0);
  wave_bind(0, OVERLAY_X, 0, 100, 1000, 1000, 0, 0, 0, OVERLAY_WAVE_SLOW_LOW);
  int16_t x, y;
  sim_run(500);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 125);
  CHECK_EQ(y, 100);
  sim_run(500);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 200);
  sim_run(500);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 125);
  sim_run(500);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 100);
  // random legs with times that vary
  place(0, 0, 100, 100, 0, 0);
  wave_bind(0, OVERLAY_SIZE, 50, 150, 200, 200, 0, 0, 50, OVERLAY_WAVE_RANDOM);
  int lo = 9999, hi = 0;
  for (int i = 0; i < 200; i++) {
    sim_run(50);
    lo = LV_MIN(lo, look_size(0));
    hi = LV_MAX(hi, look_size(0));
  }
  CHECK(lo >= 500 && hi <= 1500 && hi - lo > 500);
  // a share times what a glide sets, with something added, or set whatever that is
  BIND(0, OVERLAY_BIND_MUL | OVERLAY_SIZE, V16(1200));
  glide_msg(0, OVERLAY_SIZE, 200, 0, 0);
  CHECK_EQ(look_size(0), 2400);
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_SIZE, V16(-400));
  CHECK_EQ(look_size(0), 2000);
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, V16(500));
  CHECK_EQ(look_size(0), 200);
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_SIZE, V16(100));  // in place of the one that added
  CHECK_EQ(look_size(0), 700);
  unbind(0, OVERLAY_SIZE);  // all three
  CHECK_EQ(look_size(0), 2000);
  glide_msg(0, OVERLAY_SIZE, 100, 0, 0);
  BIND(0, OVERLAY_BIND_SET | OVERLAY_X, V32(50000));                                   // px in thousandths
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_Y, X(OWN(OVERLAY_X), V16(100), OVERLAY_OP_MUL));  // a tenth of where x has it
  CHECK(overlay_pos(0, &x, &y) && x == 50 && y == 105);
  unbind(0, OVERLAY_X);
  unbind(0, OVERLAY_Y);
  CHECK(overlay_pos(0, &x, &y) && x == 100 && y == 100);
  CHECK_EQ(overlay_used(), used);
  // bad ones change nothing
  BIND(0, OVERLAY_BIND_MUL | OVERLAY_BIND_FRAME, V8(1));    // a frame is only set
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_TARGETS, V8(1));  // nothing to bind
  BIND(0, OVERLAY_BIND_SET | (OVERLAY_BIND_REG + OVERLAY_REGS), V8(1));
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, OVERLAY_V16, 1);               // cut short
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, V16(500), 0);                  // something after it
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, X(V16(500), V8(1)));           // two numbers left
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, X(V16(500), OVERLAY_OP_ADD));  // nothing to add it to
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, X(V16(500), OVERLAY_OPS));
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, X(OWN(OVERLAY_GETS)));
  BIND(0, OVERLAY_BIND_SET | OVERLAY_SIZE, X(OVERLAY_OP_GET, OVERLAY_X, OVERLAY_PLACES));
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_SIZE, V8(1));
  BIND(5, OVERLAY_BIND_SET | OVERLAY_SIZE, V16(500));  // no such place
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(overlay_used(), used);
  // OVERLAY_BINDS at most, one more waits for one to go
  lv_obj_clean(lv_scr_act());
  place(1, 0, 10, 10, 0, 0);
  place(2, 0, 10, 50, 0, 0);
  show_overlays();
  for (int i = 0; i < OVERLAY_BINDS; i++) {
    bool mul = i % 2;
    BIND(i / (2 * OVERLAY_CHANNELS),
         (mul ? OVERLAY_BIND_MUL : OVERLAY_BIND_ADD) | (unsigned) (i / 2 % OVERLAY_CHANNELS), V16(mul ? 900 : 1));
  }
  BIND(2, OVERLAY_BIND_MUL | OVERLAY_SIZE, V16(1500));
  CHECK_EQ(look_size(0), 901);  // nine tenths, and one added
  CHECK_EQ(look_size(2), 1000);
  CHECK_EQ(overlay_used(), used + OVERLAY_BINDS * 4);
  unbind(0, OVERLAY_SIZE);
  BIND(2, OVERLAY_BIND_MUL | OVERLAY_SIZE, V16(1500));
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(look_size(2), 1500);
  // a place that goes takes its binds with it, and one leaving fades out flicker and all
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 1);
  CHECK_EQ(overlay_used(), used + 4);
  wave_bind(2, OVERLAY_OPACITY, 50, 100, 100, 100, 0, 0, 0, OVERLAY_WAVE_RANDOM);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 2 | 500 << 8);
  sim_run(300);
  CHECK(overlay_pos(2, &x, &y) && look_opa(2) >= 200 && look_opa(2) <= 400);
  sim_run(300);
  CHECK(!overlay_pos(2, &x, &y));
  CHECK_EQ(overlay_used(), used);
  // one on its way out with a frame of its own goes early for the room a bind needs, its own too, which it then
  // doesn't get: slot 1 padded with empty records until the pool is full
  static uint8_t d2[OVERLAY_POOL];
  uint32_t frame = AW / 2 * AH, n2 = overlay_records(d2, 10);
  place(0, 0, 100, 100, 1, 0);
  for (; used + 2 * frame + n2 + 5 <= OVERLAY_POOL; n2 += 5)
    memcpy(d2 + n2, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  CHECK_EQ(status(overlay_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0 | 500 << 8);
  CHECK(overlay_pos(0, &x, &y) && OVERLAY_POOL - overlay_used() < 5);
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_X, V32(5000));
  CHECK(!overlay_pos(0, &x, &y));
  CHECK_EQ(overlay_used(), used + frame + n2);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(overlay_used(), 0);
}

// From lo to hi as an OVERLAY_GET_AT_* goes across the area
#define ACROSS(at, lo, hi) OWN(at), V16((hi) - (lo)), V16(OVERLAY_AT_MAX), OVERLAY_OP_SCALE, V16(lo), OVERLAY_OP_ADD

// OVERLAY_GET_AT_X and _Y: what's bound to them follows where its pivot is across its area, at once, and the bigger
// places are drawn in front
static void test_anim_where(void) {
  no_overlays();
  odd_frames();
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  show_overlays();
  area_msg(0, 0, 320, 200, OVERLAY_EDGE_BOUNCE, OVERLAY_EDGE_BOUNCE);
  BIND(0, OVERLAY_BIND_MUL | OVERLAY_SIZE, X(ACROSS(OVERLAY_GET_AT_Y, 500, 1000)));  // smaller further up
  move_msg(152, 96, 0);                                                              // its middle halfway down
  CHECK_EQ(look_size(0), 750);
  pivot_msg(0, OVERLAY_PIVOT_MAX / 2, OVERLAY_PIVOT_MAX);  // where it is is its pivot, here its bottom
  CHECK_EQ(look_size(0), 760);
  pivot_msg(0, OVERLAY_PIVOT_MAX / 2, OVERLAY_PIVOT_MAX / 2);
  move_msg(152, -4, 0);  // at the top
  CHECK_EQ(look_size(0), 500);
  move_msg(152, 296, 0);  // below the area it's the bottom's
  CHECK_EQ(look_size(0), 1000);
  BIND(0, OVERLAY_BIND_MUL | OVERLAY_SIZE,  // left to right too, the two multiplied
       X(ACROSS(OVERLAY_GET_AT_X, 500, 1000), ACROSS(OVERLAY_GET_AT_Y, 500, 1000), V16(1000), OVERLAY_OP_SCALE));
  move_msg(-8, -4, 0);
  CHECK_EQ(look_size(0), 250);
  move_msg(152, 96, 0);
  CHECK_EQ(look_size(0), 562);
  glide_msg(0, OVERLAY_SIZE, 200, 0, 0);  // and what a glide sets
  CHECK_EQ(look_size(0), 1124);
  glide_msg(0, OVERLAY_SIZE, 100, 0, 0);
  unbind(0, OVERLAY_SIZE);
  BIND(0, OVERLAY_BIND_MUL | OVERLAY_OPACITY, X(ACROSS(OVERLAY_GET_AT_Y, 200, 1000)));  // fainter further up
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(look_opa(0), 600);
  unbind(0, OVERLAY_OPACITY);
  int a, flip;
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_TURN, X(OWN(OVERLAY_GET_AT_X), V16(360), OVERLAY_OP_MUL));  // rolls as it goes
  CHECK(overlay_angle(0, &a, &flip));
  CHECK_EQ(a, 180);
  unbind(0, OVERLAY_TURN);
  BIND(0, OVERLAY_BIND_MUL | OVERLAY_SPEED, V16(500));
  drift_msg(40, 0);
  sim_run(500);
  int16_t x, y;
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(x, 162);  // half of 2 px a step
  CHECK_EQ(y, 96);
  drift_msg(0, 0);
  // the nearer place is in front, and they swap as they go
  lv_obj_clean(lv_scr_act());
  place(0, 0, 100, 150, 0, 0);
  place(1, 0, 200, 20, 0, 0);
  for (int p = 0; p < 2; p++)
    BIND(p, OVERLAY_BIND_MUL | OVERLAY_SIZE, X(ACROSS(OVERLAY_GET_AT_Y, 500, 1000)));
  show_overlays();
  overlay_look_t l[2];
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order > l[1].order);
  move_msg(100, 0, 0);
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order < l[1].order);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// Pan and the box move like the other channels: drifts at its speed, a glide stops them, what's bound adds, a box is
// no less than nothing and draws nothing there, and what they move is drawn again
static void test_anim_pan(void) {
  no_overlays();
  odd_frames();
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 50, 50, 0, 0);
  show_overlays();
  frames_msg(0, 0);
  overlay_look_t l;
  drift_ch(0, OVERLAY_PAN_Y, 40);
  sim_run(500);
  CHECK(overlay_look(0, &l) && l.pan[1] == 20 && l.w == AW * 2 && l.h == AH * 2);
  CHECK(!sim_stale());
  glide_msg(0, OVERLAY_SPEED, 50, 0, 0);
  sim_run(500);
  CHECK(overlay_look(0, &l) && l.pan[1] == 30);
  glide_msg(0, OVERLAY_PAN_Y, -3, 0, 0);
  sim_run(500);
  CHECK(overlay_look(0, &l) && l.pan[1] == -3);
  drift_ch(0, OVERLAY_W, -100);  // to nothing, and it stays there
  sim_run(1000);
  CHECK(overlay_look(0, &l) && l.w == 0 && !l.tiles);
  CHECK(!sim_stale());
  glide_msg(0, OVERLAY_W, OVERLAY_BOX_MAX + 1, 0, 0);  // too big, so not at all
  glide_msg(0, OVERLAY_W, -1, 0, 0);
  CHECK(overlay_look(0, &l) && l.w == 0);
  glide_msg(0, OVERLAY_W, 40, 0, 0);
  CHECK(overlay_look(0, &l) && l.w == 40 && l.tiles && lv_area_get_width(&l.box) == 40);
  // where it is across its area adds to pan
  area_msg(0, 0, 320, 240, OVERLAY_EDGE_BOUNCE, OVERLAY_EDGE_BOUNCE);
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_PAN_X, X(OWN(OVERLAY_GET_AT_X), V8(32), OVERLAY_OP_MUL));
  move_msg(140, 50, 0);  // its middle halfway across
  CHECK(overlay_look(0, &l) && l.pan[0] == 16);
  glide_msg(0, OVERLAY_PAN_X, 5, 0, 0);
  sim_run(100);
  CHECK(overlay_look(0, &l) && l.pan[0] == 21);
  CHECK(!sim_stale());
  // a pan that drifts goes round with a frame that repeats, 16 px, also from as far as a glide can put it, and round
  // two of them with every other one mirrored. One that doesn't repeat stops as far as that.
  uint8_t vm[OVERLAY_VIEW_LEN];
  unbind(0, OVERLAY_PAN_X);
  glide_msg(0, OVERLAY_SPEED, 100, 0, 0);
  glide_msg(0, OVERLAY_PAN_X, 32760, 0, 0);
  overlay_msg(K_OVERLAY_VIEW, vm, sim_overlay_view(vm, 0, OVERLAY_VIEW_REPEAT_X, OVERLAY_VIEW_REPEAT_X));
  CHECK(overlay_look(0, &l) && l.pan[0] == 32760 % 16);
  drift_ch(0, OVERLAY_PAN_X, 100);
  sim_run(1000);
  CHECK(overlay_look(0, &l) && l.pan[0] == (32760 + 100) % 16);
  CHECK(!sim_stale());
  drift_ch(0, OVERLAY_PAN_X, 0);
  glide_msg(0, OVERLAY_PAN_X, 40, 0, 0);
  overlay_msg(K_OVERLAY_VIEW, vm, sim_overlay_view(vm, 0, OVERLAY_VIEW_MIRROR, OVERLAY_VIEW_MIRROR));
  CHECK(overlay_look(0, &l) && l.pan[0] == 40 % 32);
  overlay_msg(K_OVERLAY_VIEW, vm, sim_overlay_view(vm, 0, OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_MIRROR, 0));
  glide_msg(0, OVERLAY_PAN_X, INT16_MAX - 10, 0, 0);
  drift_ch(0, OVERLAY_PAN_X, 100);
  sim_run(1000);
  CHECK(overlay_look(0, &l) && l.pan[0] == INT16_MAX);
  // mirrored by a size below 0, what a frame changes is drawn again where that shows: the dot that moves
  no_overlays();
  dot_frames(true);
  n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 50, 50, 0, 0);
  show_overlays();
  glide_msg(0, OVERLAY_SIZE_X, -100, 0, 0);
  int stale = 0;
  for (int i = 0; i < 30; i++) {  // each of its frames, two rounds of them
    sim_run(20);
    stale += sim_stale();
  }
  CHECK_EQ(stale, 0);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// px a loop of the 4 test frames redraws, a frame at a time from paused
static unsigned long loop_px(void) {
  unsigned long px = sim_px;
  for (int i = 0; i < 4; i++) {
    frames_msg(0, 100);  // on at once
    sim_run(20);
    frames_msg(0, 0);
    sim_run(30);
  }
  return sim_px - px;
}

// A frame redraws the rectangle it changed, flipped or sized too, and a size that leaves the box as it is redraws
// nothing. Every place is drawn by one object, without LVGL's heap.
static void test_anim_redraw(void) {
  no_overlays();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 10);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  for (int k = 0; k < OVERLAY_PLACES; k++)
    place(k, 0, 10 + 30 * k, 100, 0, 0);
  lv_mem_monitor_t m0, m1;
  lv_mem_monitor(&m0);
  show_overlays();
  lv_mem_monitor(&m1);
  CHECK(m0.free_size - m1.free_size < 200);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_overlays();
  frames_msg(0, 0);
  sim_run(100);
  unsigned long plain = loop_px();
  CHECK_EQ(plain, 8 * 4 + 8 * 2 + 8 * 6 + 8 * 4);  // the rectangles the records changed, at 2x
  flip_msg(0, OVERLAY_VIEW_FLIP_X | OVERLAY_VIEW_FLIP_Y);
  sim_run(100);
  CHECK_EQ(loop_px(), plain);
  CHECK(!sim_stale());
  glide_msg(0, OVERLAY_SIZE, 200, 0, 0);
  sim_run(100);
  CHECK_EQ(loop_px(), 18 * 10 + 18 * 6 + 17 * 13 + 17 * 9);  // twice that, and half a pixel round
  CHECK(!sim_stale());
  glide_msg(0, OVERLAY_TURN, 30, 0, 0);
  sim_run(100);
  loop_px();
  CHECK(!sim_stale());
  glide_msg(0, OVERLAY_TURN, 0, 0, 0);
  glide_msg(0, OVERLAY_SIZE, 100, 0, 0);
  sim_run(100);
  unsigned long px = sim_px;
  wave_bind(0, OVERLAY_SIZE, 97, 100, 1000, 1000, 0, 0, 0, 0);  // 16 x 8 all the way
  sim_run(2500);
  CHECK_EQ(sim_px, px);
  CHECK(look_size(0) < 1000);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

static void at_narrow(int bx, int by, int *ix, int *iy) {  // half as wide
  *ix = 2 * bx + 1;
  *iy = by;
}
static void at_narrow_mirrored(int bx, int by, int *ix, int *iy) {
  *ix = 15 - 2 * bx;
  *iy = by;
}
static void at_low_upside_down(int bx, int by, int *ix, int *iy) {  // half as high, upside down
  *ix = bx;
  *iy = 7 - 2 * by;
}

// size_x, size_y and the pivot: a size across or down draws exactly, mirrored below 0. Flip, the sizes and turn go
// round the pivot, which stays where it is on the screen. A bad pivot changes nothing.
static void test_anim_pivot(void) {
  no_overlays();
  odd_frames();
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 40, 40, 0, 0);
  show_overlays();
  glide_msg(0, OVERLAY_SIZE_X, 50, 0, 0);  // round the middle
  CHECK(drawn_as(8, 8, at_narrow) && box_at(44, 40));
  glide_msg(0, OVERLAY_SIZE_X, -50, 0, 0);
  CHECK(drawn_as(8, 8, at_narrow_mirrored) && box_at(44, 40));
  glide_msg(0, OVERLAY_SIZE_X, 100, 0, 0);
  glide_msg(0, OVERLAY_SIZE_Y, -50, 0, 0);
  CHECK(drawn_as(16, 4, at_low_upside_down) && box_at(40, 42));
  glide_msg(0, OVERLAY_SIZE_Y, 100, 0, 0);
  glide_msg(0, OVERLAY_SIZE_X, -50, 0, 0);
  pivot_msg(0, OVERLAY_PIVOT_MAX, 0);  // round the top right corner, at 56, 40: mirrored it's right of it
  CHECK(drawn_as(8, 8, at_narrow_mirrored) && box_at(56, 40));
  glide_msg(0, OVERLAY_SIZE_X, 50, 0, 0);
  CHECK(drawn_as(8, 8, at_narrow) && box_at(48, 40));
  glide_msg(0, OVERLAY_SIZE_X, 100, 0, 0);
  flip_msg(0, OVERLAY_VIEW_FLIP_X);
  CHECK(drawn_as(16, 8, at_flip_x) && box_at(56, 40));
  flip_msg(0, 0);
  glide_msg(0, OVERLAY_SIZE, 200, 0, 0);
  CHECK(drawn_as(32, 16, at_200) && box_at(24, 40));
  glide_msg(0, OVERLAY_SIZE, 100, 0, 0);
  glide_msg(0, OVERLAY_TURN, 90, 0, 0);
  CHECK(drawn_as(8, 16, at_90) && box_at(48, 24));
  glide_msg(0, OVERLAY_TURN, 0, 0, 0);
  // over time it stays at the pivot, all the way through 0 to the other side
  overlay_look_t l;
  glide_msg(0, OVERLAY_SIZE_X, -100, 1000, 0);
  bool kept = true;
  for (int t = 0; t < 10; t++) {
    sim_run(100);
    kept &= overlay_look(0, &l) && (l.size_xy[0] < 0 ? l.box.x1 == 56 : l.box.x2 == 55);
  }
  CHECK(kept && l.size_xy[0] == -1000 && drawn_as(16, 8, at_flip_x));
  pivot_msg(0, OVERLAY_PIVOT_MAX + 1, 0);
  CHECK(overlay_look(0, &l) && l.pivot[0] == OVERLAY_PIVOT_MAX && box_at(56, 40));
  // placed again, round its middle
  lv_obj_del(overlays);
  place(0, 0, 40, 40, 0, 0);
  show_overlays();
  CHECK(overlay_look(0, &l) && l.pivot[0] == OVERLAY_PIVOT_MAX / 2 && l.size_xy[0] == 1000 && drawn_as(16, 8, at_same));
  lv_obj_del(overlays);
  overlay_clear_msg(OVERLAY_ALL);
}

const check_test_t OVERLAY_MOTION_TESTS[] = {
    {"overlay_move", test_anim_move},
    {"overlay_ease", test_anim_ease},
    {"overlay_turn", test_anim_turn},
    {"overlay_look", test_anim_look},
    {"overlay_paths", test_anim_paths},
    {"overlay_bind", test_anim_bind},
    {"overlay_where", test_anim_where},
    {"overlay_pan", test_anim_pan},
    {"overlay_redraw", test_anim_redraw},
    {"overlay_pivot", test_anim_pivot},
    {0},
};
