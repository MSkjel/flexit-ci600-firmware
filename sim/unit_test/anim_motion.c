// app/anim_motion.c and anim_draw.c: how the places move, turn, grow and fade, and what they redraw
#include "unit_test.h"
#include "anim_int.h"

static bool anim_angle(int k, int *deg, int *flip) {
  anim_look_t l;
  if (!anim_look(k, &l))
    return false;
  *deg = l.deg;
  *flip = l.flip;
  return true;
}

// Whether place 0's box has its top left at x, y
static bool box_at(int x, int y) {
  anim_look_t l;
  return anim_look(0, &l) && l.box.x1 == x && l.box.y1 == y;
}

static bool anim_at(int x, int y) {
  int16_t ax, ay;
  return anim_pos(0, &ax, &ay) && ax == x && ay == y && box_at(x, y);
}

// whether place 0 is drawn again a screen to the left, up, and both, for the part over the right or bottom edge
static bool ghosts_shown(bool right, bool below) {
  anim_look_t l;
  return anim_look(0, &l) && l.tiles == (1 | right << 1 | below << 2 | (right && below) << 3);
}

// GLIDE of place p's channel c to v over ms, eased
static void glide_msg(int p, int c, int v, int ms, int ease) {
  uint8_t m[7];
  anim_msg(K_ANIM_GLIDE, m, sim_anim_glide(m, p, 1u << c, ms, ease, &v));
}

static void drift_ch(int p, int c, int v) {
  uint8_t m[4];
  anim_msg(K_ANIM_DRIFT, m, sim_anim_drift(m, p, 1u << c, &v));
}

// place 0 to x, y over ms, or drifting vx, vy px/s, each one message
static void move_msg(int x, int y, int ms) {
  uint8_t m[9];
  int v[2] = {x, y};
  anim_msg(K_ANIM_GLIDE, m, sim_anim_glide(m, 0, 1u << ANIM_X | 1u << ANIM_Y, ms, 0, v));
}

static void drift_msg(int vx, int vy) {
  uint8_t m[6];
  int v[2] = {vx, vy};
  anim_msg(K_ANIM_DRIFT, m, sim_anim_drift(m, 0, 1u << ANIM_X | 1u << ANIM_Y, v));
}

// GLIDE and DRIFT of x and y: glides land on the target, drifts wrap round the screen, bad messages are ignored
static void test_anim_move(void) {
  no_anims();
  dot_frames(false);
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  move_msg(100, 50, 0);  // no animation yet
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 10, 10, 0, 0);
  show_anims();
  CHECK(anim_at(10, 10) && ghosts_shown(false, false));  // 16x8 where it was placed
  uint8_t glide[9] = {0, 1u << ANIM_X, 0, 0, 0, 100}, drift[4] = {0, 1u << ANIM_X, 0xD8, 0xFF};
  anim_msg(K_ANIM_GLIDE, glide, 6);  // the wrong length
  anim_msg(K_ANIM_GLIDE, glide, 9);  // one channel, two values
  anim_msg(K_ANIM_DRIFT, drift, 5);
  anim_msg(K_ANIM_GLIDE, NULL, 0);
  glide[1] = 0;  // no channel
  anim_msg(K_ANIM_GLIDE, glide, 5);
  glide[1] = 1u << ANIM_CHANNELS;  // no such channel
  anim_msg(K_ANIM_GLIDE, glide, 7);
  glide[1] = 1u << ANIM_X | 1u << ANIM_SIZE;
  plink_put16(glide + 7, 5);  // x is fine, size 5 isn't, so neither goes
  anim_msg(K_ANIM_GLIDE, glide, sizeof glide);
  glide[1] = 1u << ANIM_X;
  glide[4] = ANIM_EASE_IN << 2;  // no such ease
  anim_msg(K_ANIM_GLIDE, glide, 7);
  drift[1] = 1u << ANIM_SIZE;  // only x, y and turn drift
  anim_msg(K_ANIM_DRIFT, drift, sizeof drift);
  glide[0] = 1;  // no such place
  glide[4] = 0;
  anim_msg(K_ANIM_GLIDE, glide, 7);
  sim_run(200);
  CHECK(anim_at(10, 10));
  move_msg(100, 50, 0);  // ms = 0 is a jump
  CHECK(anim_at(100, 50));
  move_msg(310, 50, 0);  // partly off the right edge: that's where it is, no wrapping until it drifts
  CHECK(anim_at(310, 50) && ghosts_shown(false, false));
  drift_msg(-40, 0);  // 2 px a step
  sim_run(50);
  CHECK(anim_at(308, 50) && ghosts_shown(true, false));
  move_msg(-20, 60, 1000);  // a glide can end partly off the screen
  sim_run(500);
  int16_t x, y;
  anim_pos(0, &x, &y);
  CHECK_EQ(x, 144);
  CHECK_EQ(y, 55);  // halfway from 308, 50
  sim_run(500);
  CHECK(anim_at(-20, 60));  // landed exactly
  sim_run(500);
  CHECK(anim_at(-20, 60));  // and stays
  drift_msg(-40, 0);        // a drift puts it on the screen again, from the right
  sim_run(50);
  CHECK(anim_at(298, 60) && ghosts_shown(false, false));
  sim_run(50 * 149);
  CHECK(anim_at(0, 60));  // 2 px a step
  sim_run(50);
  CHECK(anim_at(318, 60) && ghosts_shown(true, false));  // over the right edge, the ghost draws the rest
  sim_run(50 * 159);
  CHECK(anim_at(0, 60) && ghosts_shown(false, false));
  drift_msg(0, 100);  // 5 px a step down
  sim_run(50 * 35);
  CHECK(anim_at(0, 235) && ghosts_shown(false, true));
  sim_run(50);
  CHECK(anim_at(0, 0) && ghosts_shown(false, false));
  sim_run(50);
  CHECK(anim_at(0, 5));
  drift_msg(-1, 0);  // the sub-pixel part adds up
  sim_run(50);
  CHECK(anim_at(319, 5) && ghosts_shown(true, false));
  sim_run(50 * 19);
  CHECK(anim_at(319, 5));
  sim_run(50);
  CHECK(anim_at(318, 5));
  drift_msg(0, 0);
  sim_run(1000);
  CHECK(anim_at(318, 5));
  // LVGL too busy for two steps: the next one makes up for them
  move_msg(100, 100, 0);
  drift_msg(40, 0);
  sim_run(50);
  lv_tick_inc(150);
  lv_timer_handler();
  CHECK(anim_at(108, 100));
  drift_msg(0, 0);
  // the axes go their own ways: x drifts while y glides
  move_msg(100, 100, 0);
  drift_ch(0, ANIM_X, 40);
  glide_msg(0, ANIM_Y, 120, 500, 0);
  sim_run(500);
  CHECK(anim_at(120, 120));
  sim_run(500);
  CHECK(anim_at(140, 120));
  drift_msg(0, 0);
  move_msg(318, 5, 0);
  drift_msg(-1, 0);
  drift_msg(0, 0);  // on the screen's torus again, still
  // the animation outlives its image, and the motion with it
  lv_obj_del(anims);
  drift_msg(-1, 0);
  sim_run(2000);  // nothing moves without an image
  show_anims();
  CHECK(anim_at(318, 5) && ghosts_shown(true, false));
  sim_run(1000);
  CHECK(anim_at(317, 5));
  lv_obj_del(anims);
  // placing it again stops it and puts it there
  place(0, 0, 20, 10, 0, 0);
  show_anims();
  sim_run(1000);
  CHECK(anim_at(20, 10));
  drift_msg(-1, 0);
  anim_clear_msg(0);  // clearing the slot takes the place with it
  CHECK(!anim_pos(0, &x, &y));
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 20, 10, 0, 0);
  show_anims();
  sim_run(2000);
  CHECK(anim_at(20, 10));
  glide_msg(0, ANIM_X, -32768, 0, 0);  // a glide across the whole int16 range
  glide_msg(0, ANIM_X, 32767, 65535, 0);
  sim_run(50000);
  int16_t gx, gy;
  anim_pos(0, &gx, &gy);
  CHECK_EQ(gx, 17232);
  CHECK_EQ(gy, 10);
  sim_run(15550);  // landed
  anim_pos(0, &gx, &gy);
  CHECK_EQ(gx, 32767);
  CHECK_EQ(gy, 10);
  // a message every 30 ms doesn't hold it up, it drifts on at 2 px a step
  move_msg(100, 100, 0);
  drift_msg(40, 0);
  for (int i = 0; i < 33; i++) {
    glide_msg(0, ANIM_OPACITY, 100, 0, 0);
    sim_run(30);
  }
  CHECK(anim_at(138, 100));
  lv_obj_del(anims);
  anim_clear_msg(ANIM_ALL);
}

// Glides ease in, out or both: from 0 to 200 over a second, where it is at 250 and 500 ms
static void test_anim_ease(void) {
  no_anims();
  dot_frames(false);
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  show_anims();
  static const int16_t WANT[4][2] = {{50, 100}, {12, 50}, {87, 150}, {31, 100}};  // none, in, out, both
  for (int e = 0; e < 4; e++) {
    glide_msg(0, ANIM_X, 0, 0, 0);
    glide_msg(0, ANIM_X, 200, 1000, e);
    int16_t x[2], y;
    sim_run(250);
    anim_pos(0, &x[0], &y);
    sim_run(250);
    anim_pos(0, &x[1], &y);
    CHECK_EQ(x[0], WANT[e][0]);
    CHECK_EQ(x[1], WANT[e][1]);
    sim_run(500);
    CHECK(anim_pos(0, &x[0], &y));
    CHECK_EQ(x[0], 200);
  }
  lv_obj_del(anims);
  anim_clear_msg(ANIM_ALL);
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
  anim_look_t l;
  return drawn(k, &l, px) ? px[by * lv_area_get_width(&l.box) + bx] : -3;
}

// Whether place 0 is drawn w x h, every pixel the frame's at where(bx, by)
static bool drawn_as(int w, int h, void (*where)(int bx, int by, int *ix, int *iy)) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  anim_look_t l;
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

static void flip_msg(int p, int flip) { anim_msg(K_ANIM_FLIP, (uint8_t[]) {(uint8_t) p, (uint8_t) flip}, 2); }

// Turns and FLIP: quarter turns and flips draw exactly, turns go the short way, spins add up, bad messages are
// ignored
static void test_anim_turn(void) {
  no_anims();
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 10, 10, 0, 0);
  show_anims();
  int a, flip;
  glide_msg(0, ANIM_TURN, 90, 0, 0);  // at once
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 90);
  CHECK(!flip);
  CHECK(drawn_as(8, 16, at_90));
  CHECK(box_at(14, 6));  // centred where the 16 x 8 frame was
  glide_msg(0, ANIM_TURN, 180, 0, 0);
  CHECK(drawn_as(16, 8, at_180) && box_at(10, 10));
  flip_msg(0, ANIM_FLIP_X);
  glide_msg(0, ANIM_TURN, 0, 0, 0);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 0);
  CHECK_EQ(flip, ANIM_FLIP_X);
  CHECK(drawn_as(16, 8, at_flip_x));
  glide_msg(0, ANIM_TURN, 90, 0, 0);
  CHECK(drawn_as(8, 16, at_flip_x_90));
  // a turn over time goes the short way round: 90 to 350 is 100 back, not 260 on
  flip_msg(0, 0);
  glide_msg(0, ANIM_TURN, 350, 1000, 0);
  sim_run(500);
  CHECK(anim_angle(0, &a, &flip) && a >= 38 && a <= 42);
  sim_run(600);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 350);
  // a spin adds up past 360, and a stop keeps the angle
  drift_ch(0, ANIM_TURN, 90);
  sim_run(1000);
  CHECK(anim_angle(0, &a, &flip) && a >= 75 && a <= 80);
  drift_ch(0, ANIM_TURN, 0);
  sim_run(1000);
  int b;
  CHECK(anim_angle(0, &b, &flip));
  CHECK_EQ(b, a);
  // a bad flip, the wrong length and no such place change nothing
  flip_msg(0, 4);
  flip_msg(5, ANIM_FLIP_Y);
  anim_msg(K_ANIM_FLIP, (uint8_t[]) {0, ANIM_FLIP_Y, 0}, 3);
  anim_msg(K_ANIM_FLIP, NULL, 0);
  CHECK(anim_angle(0, &b, &flip));
  CHECK_EQ(b, a);
  CHECK(!flip);
  // placed again, it starts unturned
  lv_obj_del(anims);
  place(0, 0, 10, 10, 0, 0);
  show_anims();
  CHECK(anim_angle(0, &a, &flip) && !a && !flip && drawn_as(16, 8, at_same));
  lv_obj_del(anims);
  anim_clear_msg(ANIM_ALL);
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
  anim_look_t l;
  return anim_look(place, &l) ? l.size : -1;
}

// Size and opacity: sizes draw exactly round the middle, sizes and fades get there over time, a place can fade in
// and a clear can fade it out first
static void test_anim_look(void) {
  no_anims();
  odd_frames();
  uint8_t d[512], d2[ANIM_POOL];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 40, 40, 0, 0);
  show_anims();
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  CHECK_EQ(look_size(0), 2000);
  CHECK(drawn_as(32, 16, at_200));
  CHECK(box_at(32, 36));
  glide_msg(0, ANIM_SIZE, 50, 0, 0);
  CHECK(drawn_as(8, 4, at_50) && box_at(44, 42));
  glide_msg(0, ANIM_TURN, 90, 0, 0);  // a quarter turn at twice the size
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  CHECK(drawn_as(16, 32, at_90_200) && box_at(40, 28));
  glide_msg(0, ANIM_TURN, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 100, 1000, 0);  // back over a second, then drawn straight again
  sim_run(500);
  CHECK_EQ(look_size(0), 1500);
  sim_run(600);
  CHECK_EQ(look_size(0), 1000);
  CHECK(drawn_as(16, 8, at_same));
  CHECK(box_at(40, 40));
  glide_msg(0, ANIM_SIZE, 9, 0, 0);  // out of range
  glide_msg(0, ANIM_SIZE, 401, 0, 0);
  glide_msg(0, ANIM_OPACITY, 101, 0, 0);
  glide_msg(0, ANIM_SPEED, 1001, 0, 0);
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(look_opa(0), 1000);
  // to see-through over a second, then opaque at once
  glide_msg(0, ANIM_OPACITY, 0, 1000, 0);
  sim_run(500);
  CHECK_EQ(look_opa(0), 500);
  CHECK_EQ(drawn_px(0, 0, 0), -2);  // half the frame's, half what's behind
  sim_run(600);
  int16_t x, y;
  CHECK_EQ(look_opa(0), 0);
  CHECK(anim_pos(0, &x, &y));
  glide_msg(0, ANIM_OPACITY, 100, 0, 0);
  CHECK_EQ(look_opa(0), 1000);
  CHECK_EQ(drawn_px(0, 0, 0), 1);
  // placed fading in over 400 ms
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  place_in(1, 0, 100, 40, 0, 0, 400);
  show_anims();
  CHECK_EQ(look_opa(1), 0);
  sim_run(200);
  CHECK_EQ(look_opa(1), 500);
  sim_run(250);
  CHECK_EQ(look_opa(1), 1000);
  // cleared over 500 ms: it stays while it fades, then goes, with nothing to tell the ESP
  anim_clear_msg(ANIM_PLACE_BIT | 1 | 500 << 8);
  sim_run(250);
  CHECK(anim_pos(1, &x, &y));
  CHECK_EQ(look_opa(1), 500);
  sim_run(300);
  CHECK(!anim_pos(1, &x, &y));
  CHECK_EQ(anim_gone(), -1);
  // a slot cleared over time goes with its last place, and gives back its room then
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 40, 0, 1, 0);
  show_anims();
  unsigned used = anim_used();
  anim_clear_msg(0 | 300 << 8);
  CHECK_EQ(anim_used(), used);
  CHECK(anim_pos(0, &x, &y));
  CHECK(anim_pos(1, &x, &y));
  place(2, 0, 80, 0, 0, 0);  // no new places for it
  CHECK(!anim_pos(2, &x, &y));
  sim_run(400);
  CHECK_EQ(anim_used(), 0);
  CHECK(!anim_pos(0, &x, &y));
  CHECK(!anim_pos(1, &x, &y));
  // also when its last places go at once or onto another slot
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK_EQ(status(anim_send(1, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 40, 0, 0, 0);
  anim_clear_msg(0 | 300 << 8);
  anim_clear_msg(ANIM_PLACE_BIT | 0);
  place(1, 1, 40, 0, 0, 0);
  CHECK_EQ(anim_used(), AW / 2 * AH + n);
  CHECK(anim_pos(1, &x, &y));
  anim_clear_msg(ANIM_ALL);
  // but a new animation that needs the room takes it at once
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  lv_obj_clean(lv_scr_act());
  place(0, 0, 0, 0, 0, 0);
  show_anims();
  anim_clear_msg(0 | 5000 << 8);
  unsigned frame = AW / 2 * AH;
  uint32_t n2 = anim_records(d2, 5), m = (ANIM_POOL - frame - n2) / 5;
  for (uint32_t i = 0; i < m; i++)
    memcpy(d2 + n2 + 5 * i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  n2 += 5 * m;
  CHECK_EQ(status(anim_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  CHECK(!anim_pos(0, &x, &y));
  CHECK_EQ(anim_used(), frame + n2);
  // not shown, it goes at once
  lv_obj_clean(lv_scr_act());
  place(2, 1, 0, 0, 0, 0);
  anim_clear_msg(ANIM_PLACE_BIT | 2 | 500 << 8);
  CHECK(!anim_pos(2, &x, &y));
  anim_clear_msg(ANIM_ALL);
  CHECK_EQ(anim_used(), 0);
}

static void area_msg(int x0, int y0, int x1, int y1, int ex, int ey) {
  uint8_t m[11] = {0};
  plink_put16(m + 1, x0);
  plink_put16(m + 3, y0);
  plink_put16(m + 5, x1);
  plink_put16(m + 7, y1);
  m[9] = (uint8_t) ex;
  m[10] = (uint8_t) ey;
  anim_msg(K_ANIM_AREA, m, sizeof m);
}

static void wander_msg(int s0, int s1, int heading, int spread, int l0, int l1, int r0, int r1) {
  uint8_t m[17] = {0};
  int v[8] = {s0 * 10, s1 * 10, heading, spread, l0, l1, r0, r1};  // speeds in tenths
  for (int i = 0; i < 8; i++)
    plink_put16(m + 1 + 2 * i, v[i]);
  anim_msg(K_ANIM_WANDER, m, sizeof m);
}

static void face_msg(int flags, int facing, int tilt, int rate, int len) {
  uint8_t m[8] = {0, (uint8_t) flags};
  plink_put16(m + 2, facing);
  plink_put16(m + 4, tilt);
  plink_put16(m + 6, rate);
  anim_msg(K_ANIM_FACE, m, len);
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
    anim_pos(0, &x, &y);
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

// K_ANIM_AREA, ANIM_WANDER and ANIM_FACE: a drift bounces inside its area or crosses out and back in, a wander keeps
// to its area, rests, and turns back off the edges, and a place can face where it goes
static void test_anim_paths(void) {
  no_anims();
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_anims();
  area_msg(50, 80, 200, 140, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  drift_msg(100, 37);
  trail_t t = trail(400, 50, 80, 200, 140);
  CHECK(t.inside);
  CHECK_EQ(t.lo_x, 50);
  CHECK_EQ(t.hi_x, 184);
  CHECK_EQ(t.lo_y, 80);
  CHECK_EQ(t.hi_y, 132);
  // out across the right edge and in at the left, and back
  area_msg(50, 80, 200, 140, ANIM_EDGE_CROSS, ANIM_EDGE_BOUNCE);
  move_msg(180, 100, 0);
  drift_msg(100, 0);
  int xs[5], want[5] = {185, 190, 195, 34, 39};
  int16_t x, y;
  for (int i = 0; i < 5; i++) {
    sim_run(50);
    anim_pos(0, &x, &y);
    xs[i] = x;
  }
  CHECK(!memcmp(xs, want, sizeof xs));
  drift_msg(-100, 0);
  sim_run(50);
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(x, 200);
  sim_run(50);
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(x, 195);
  area_msg(50, 80, 70, 140, ANIM_EDGE_CROSS, ANIM_EDGE_BOUNCE);
  drift_msg(3000, 0);  // across the area many times a step, and still about it
  int lo = 9999, hi = -9999;
  for (int i = 0; i < 40; i++) {
    sim_run(50);
    anim_pos(0, &x, &y);
    lo = LV_MIN(lo, x);
    hi = LV_MAX(hi, x);
  }
  CHECK(lo >= 50 - 16 && hi < 70 && hi > lo);
  // a wander about its area, resting now and then. A seed of its own, the time it was placed depends on the tests
  // before.
  places[0].rng = 1;
  area_msg(60, 40, 260, 200, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  wander_msg(20, 60, 0, 180, 200, 800, 0, 300);
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
  area_msg(60, 40, 60, 200, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  area_msg(60, 40, 260, 200, ANIM_EDGE_BOUNCE + 1, ANIM_EDGE_BOUNCE);
  anim_msg(K_ANIM_AREA, (uint8_t[]) {0, 0, 0, 0, 0, 1, 0, 1, 0, 0}, 10);
  t = trail(200, 60, 40, 260, 200);
  CHECK(t.inside);

  // frames that face right, mirrored to face left, leaning up to 30 degrees, upright when it stops
  area_msg(0, 0, PIC_MAX_W, PIC_MAX_H, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  move_msg(150, 100, 0);
  face_msg(ANIM_FACE_MIRROR | ANIM_FACE_LEVEL, 0, 30, 0, 8);
  int a, flip;
  drift_msg(-50, 0);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 0);
  CHECK_EQ(flip, ANIM_FLIP_X);
  drift_msg(-50, 50);  // down to the left
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 330);
  CHECK_EQ(flip, ANIM_FLIP_X);
  drift_msg(50, 50);  // down to the right: unmirrored, the same lean the other way
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 30);
  CHECK(!flip);
  drift_msg(0, 0);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 0);
  CHECK(!flip);
  // 90 degrees a second
  face_msg(ANIM_FACE_MIRROR, 0, 30, 90, 8);
  drift_msg(50, -50);
  sim_run(100);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 351);
  sim_run(500);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 330);
  // frames that face up turn all the way round toward where it goes
  face_msg(0, 270, 180, 0, 8);
  drift_msg(50, 0);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 90);
  CHECK(!flip);
  face_msg(0, 270, 181, 0, 8);
  face_msg(ANIM_FACE_LEVEL << 1, 270, 0, 0, 8);
  face_msg(0, 270, 0, -1, 8);
  face_msg(0, 270, 0, 0, 7);
  drift_msg(0, 50);
  sim_run(50);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 180);
  glide_msg(0, ANIM_TURN, 45, 0, 0);  // a turn takes over
  sim_run(200);
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 45);
  lv_obj_del(anims);
  anim_clear_msg(ANIM_ALL);
}

static void wave_msg(int p, int c, int lo, int hi, int rise, int fall, int hold_hi, int hold_lo, int jitter,
                     int flags) {
  uint8_t m[16] = {(uint8_t) p, (uint8_t) c};
  int v[6] = {lo, hi, rise, fall, hold_hi, hold_lo};
  for (int i = 0; i < 6; i++)
    plink_put16(m + 2 + 2 * i, v[i]);
  m[14] = (uint8_t) jitter;
  m[15] = (uint8_t) flags;
  anim_msg(K_ANIM_WAVE, m, sizeof m);
}

// WAVE: a size breathes there and back and settles, a blink holds at each end, a wave on x eases at its slow end,
// random legs stay between low and high, a wave scales a glide and a fade out, and there are ANIM_WAVES at most,
// counting the stopped ones only until they're needed
static void test_anim_wave(void) {
  no_anims();
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_anims();
  wave_msg(0, ANIM_SIZE, 100, 120, 1000, 1000, 0, 0, 0, 0);
  static const int BREATH[] = {1100, 1200, 1100, 1000, 1100};
  bool ok = true;
  for (int i = 0; i < 5; i++) {
    sim_run(500);
    ok &= look_size(0) == BREATH[i];
  }
  CHECK(ok);
  wave_msg(0, ANIM_SIZE, 100, 100, 500, 500, 0, 0, 0, 0);  // back down over the fall time
  sim_run(250);
  CHECK_EQ(look_size(0), 1050);
  sim_run(350);
  CHECK_EQ(look_size(0), 1000);
  // on for 500 ms and off for 300, at once
  wave_msg(0, ANIM_OPACITY, 0, 100, 0, 0, 500, 300, 0, 0);
  sim_run(450);
  CHECK_EQ(look_opa(0), 1000);
  sim_run(100);
  CHECK_EQ(look_opa(0), 0);
  sim_run(200);
  CHECK_EQ(look_opa(0), 0);
  sim_run(100);
  CHECK_EQ(look_opa(0), 1000);
  wave_msg(0, ANIM_OPACITY, 100, 100, 0, 0, 0, 0, 0, 0);
  // 100 px right and back, slow at the low end: it eases in going out and eases out coming back
  wave_msg(0, ANIM_X, 0, 100, 1000, 1000, 0, 0, 0, ANIM_WAVE_SLOW_LOW);
  int16_t x, y;
  sim_run(500);
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(x, 125);
  CHECK_EQ(y, 100);
  sim_run(500);
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(x, 200);
  sim_run(500);
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(x, 125);
  sim_run(500);
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(x, 100);
  wave_msg(0, ANIM_X, 0, 0, 0, 0, 0, 0, 0, 0);
  // random legs with times that vary
  wave_msg(0, ANIM_SIZE, 50, 150, 200, 200, 0, 0, 50, ANIM_WAVE_RANDOM);
  int lo = 9999, hi = 0;
  for (int i = 0; i < 200; i++) {
    sim_run(50);
    lo = LV_MIN(lo, look_size(0));
    hi = LV_MAX(hi, look_size(0));
  }
  CHECK(lo >= 500 && hi <= 1500 && hi - lo > 500);
  // it stays where low = high says, and scales what a glide sets
  wave_msg(0, ANIM_SIZE, 120, 120, 0, 0, 0, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  sim_run(1000);
  CHECK_EQ(look_size(0), 2400);
  wave_msg(0, ANIM_SIZE, 100, 100, 0, 0, 0, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 100, 0, 0);
  sim_run(100);
  CHECK_EQ(look_size(0), 1000);
  // bad ones change nothing
  wave_msg(0, ANIM_SIZE, 120, 110, 0, 0, 0, 0, 0, 0);
  wave_msg(0, ANIM_SIZE, 120, 120, 0, 0, 0, 0, 101, 0);
  wave_msg(0, ANIM_SIZE, 120, 120, 0, 0, 0, 0, 0, ANIM_WAVE_RANDOM << 1);
  wave_msg(0, ANIM_CHANNELS, 120, 120, 0, 0, 0, 0, 0, 0);
  anim_msg(K_ANIM_WAVE, (uint8_t[16]) {0, ANIM_SIZE, 120, 0, 120}, 15);
  sim_run(100);
  CHECK_EQ(look_size(0), 1000);
  // ANIM_WAVES at most, one more waits for one to go
  lv_obj_clean(lv_scr_act());
  place(1, 0, 10, 10, 0, 0);
  place(2, 0, 10, 50, 0, 0);
  show_anims();
  for (int i = 0; i < ANIM_WAVES; i++) {
    int c = i % ANIM_CHANNELS, v = c >= ANIM_SIZE ? 90 : 1;
    wave_msg(i / ANIM_CHANNELS, c, v, v, 0, 0, 0, 0, 0, 0);
  }
  wave_msg(2, ANIM_SIZE, 150, 150, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK_EQ(look_size(0), 900);
  CHECK_EQ(look_size(2), 1000);
  wave_msg(0, ANIM_SIZE, 100, 100, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  wave_msg(2, ANIM_SIZE, 150, 150, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(look_size(2), 1500);
  // one stopped but still on its way back makes room at once for a new one, not for a stop of none
  wave_msg(0, ANIM_OPACITY, 100, 100, 1000, 1000, 0, 0, 0, 0);
  wave_msg(2, ANIM_TURN, 0, 0, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK(look_opa(0) > 900 && look_opa(0) < 1000);
  wave_msg(2, ANIM_OPACITY, 50, 50, 0, 0, 0, 0, 0, 0);
  sim_run(100);
  CHECK_EQ(look_opa(0), 1000);
  CHECK_EQ(look_opa(2), 500);
  // a place that goes takes its waves with it, and one leaving fades out flicker and all
  anim_clear_msg(ANIM_PLACE_BIT | 1);
  wave_msg(2, ANIM_OPACITY, 50, 100, 100, 100, 0, 0, 0, ANIM_WAVE_RANDOM);
  anim_clear_msg(ANIM_PLACE_BIT | 2 | 500 << 8);
  sim_run(300);
  CHECK(anim_pos(2, &x, &y) && look_opa(2) >= 200 && look_opa(2) <= 400);
  sim_run(300);
  CHECK(!anim_pos(2, &x, &y));
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

static void map_msg(int p, int c, int left, int right, int top, int bottom) {
  uint8_t m[10] = {(uint8_t) p, (uint8_t) c};
  int v[4] = {left, right, top, bottom};
  for (int i = 0; i < 4; i++)
    plink_put16(m + 2 + 2 * i, v[i]);
  anim_msg(K_ANIM_MAP, m, sizeof m);
}

// MAP: size, opacity, turn and speed follow where its middle is across its area, and the places bigger from their
// map are drawn in front
static void test_anim_map(void) {
  no_anims();
  odd_frames();
  uint8_t d[512];
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  show_anims();
  area_msg(0, 0, 320, 200, ANIM_EDGE_BOUNCE, ANIM_EDGE_BOUNCE);
  map_msg(0, ANIM_SIZE, 100, 100, 50, 100);  // smaller further up
  move_msg(152, 96, 0);                      // the 16 x 8 frame's middle halfway down
  CHECK_EQ(look_size(0), 750);
  move_msg(152, -4, 0);  // at the top
  CHECK_EQ(look_size(0), 500);
  move_msg(152, 296, 0);  // below the area it's the bottom's
  CHECK_EQ(look_size(0), 1000);
  map_msg(0, ANIM_SIZE, 50, 100, 50, 100);  // left to right too, the two multiply
  move_msg(-8, -4, 0);
  CHECK_EQ(look_size(0), 250);
  move_msg(152, 96, 0);
  CHECK_EQ(look_size(0), 562);
  glide_msg(0, ANIM_SIZE, 200, 0, 0);  // and what a glide sets
  CHECK_EQ(look_size(0), 1124);
  glide_msg(0, ANIM_SIZE, 100, 0, 0);
  map_msg(0, ANIM_SIZE, 100, 100, 100, 100);
  map_msg(0, ANIM_OPACITY, 100, 100, 20, 100);  // fainter further up
  CHECK_EQ(look_size(0), 1000);
  CHECK_EQ(look_opa(0), 600);
  map_msg(0, ANIM_OPACITY, 100, 100, 100, 100);
  int a, flip;
  map_msg(0, ANIM_TURN, 0, 360, 0, 0);  // rolls as it goes, half way round in the middle
  CHECK(anim_angle(0, &a, &flip));
  CHECK_EQ(a, 180);
  map_msg(0, ANIM_TURN, 0, 0, 0, 0);
  map_msg(0, ANIM_X, 50, 50, 50, 50);  // not x or y
  map_msg(0, ANIM_SPEED, 50, 50, 100, 100);
  drift_msg(40, 0);
  sim_run(500);
  int16_t x, y;
  CHECK(anim_pos(0, &x, &y));
  CHECK_EQ(x, 162);  // half of 2 px a step
  CHECK_EQ(y, 96);
  drift_msg(0, 0);
  // the nearer place is in front, and they swap as they go
  lv_obj_clean(lv_scr_act());
  place(0, 0, 100, 150, 0, 0);
  place(1, 0, 200, 20, 0, 0);
  map_msg(0, ANIM_SIZE, 100, 100, 50, 100);
  map_msg(1, ANIM_SIZE, 100, 100, 50, 100);
  show_anims();
  anim_look_t l[2];
  CHECK(anim_look(0, &l[0]) && anim_look(1, &l[1]) && l[0].depth > l[1].depth);
  move_msg(100, 0, 0);
  CHECK(anim_look(0, &l[0]) && anim_look(1, &l[1]) && l[0].depth < l[1].depth);
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
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
  no_anims();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = anim_records(d, 10);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  for (int k = 0; k < ANIM_PLACES; k++)
    place(k, 0, 10 + 30 * k, 100, 0, 0);
  lv_mem_monitor_t m0, m1;
  lv_mem_monitor(&m0);
  show_anims();
  lv_mem_monitor(&m1);
  CHECK(m0.free_size - m1.free_size < 200);
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 100, 100, 0, 0);
  show_anims();
  frames_msg(0, 0);
  sim_run(100);
  unsigned long plain = loop_px();
  CHECK_EQ(plain, 8 * 4 + 8 * 2 + 8 * 6 + 8 * 4);  // the rectangles the records changed, at 2x
  flip_msg(0, ANIM_FLIP_X | ANIM_FLIP_Y);
  sim_run(100);
  CHECK_EQ(loop_px(), plain);
  CHECK(!sim_stale());
  glide_msg(0, ANIM_SIZE, 200, 0, 0);
  sim_run(100);
  CHECK_EQ(loop_px(), 18 * 10 + 18 * 6 + 17 * 13 + 17 * 9);  // twice that, and half a pixel round
  CHECK(!sim_stale());
  glide_msg(0, ANIM_TURN, 30, 0, 0);
  sim_run(100);
  loop_px();
  CHECK(!sim_stale());
  glide_msg(0, ANIM_TURN, 0, 0, 0);
  glide_msg(0, ANIM_SIZE, 100, 0, 0);
  sim_run(100);
  unsigned long px = sim_px;
  wave_msg(0, ANIM_SIZE, 97, 100, 1000, 1000, 0, 0, 0, 0);  // 16 x 8 all the way
  sim_run(3000);
  CHECK_EQ(sim_px, px);
  CHECK(look_size(0) < 1000);
  lv_obj_clean(lv_scr_act());
  anim_clear_msg(ANIM_ALL);
}

const check_test_t ANIM_MOTION_TESTS[] = {
    {"anim_move", test_anim_move}, {"anim_ease", test_anim_ease},     {"anim_turn", test_anim_turn},
    {"anim_look", test_anim_look}, {"anim_paths", test_anim_paths},   {"anim_wave", test_anim_wave},
    {"anim_map", test_anim_map},   {"anim_redraw", test_anim_redraw}, {0},
};
