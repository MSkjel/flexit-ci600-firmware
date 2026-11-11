// app/overlay_script.c: places' scripts, the frames they play, what they work out, wait for and bind
#include "unit_test.h"

// SEND to place p of a GLIDE of channel ch to the value v at once
#define TO_PLACE(p, ch, ...) \
  OVERLAY_DO_SEND, p, K_OVERLAY_GLIDE - K_OVERLAY_FRAMES, OVERLAY_GLIDE_HEAD + OVERLAY_GLIDE_EACH, 0, 0, 0, 0, ch, 0, \
      0, 1, OVERLAY_GLIDE_HEAD + 1, __VA_ARGS__
#define TO(ch, ...) TO_PLACE(OVERLAY_SELF, ch, __VA_ARGS__)
#define GET(what, p) OVERLAY_OP_GET, what, p
#define SET(r, ...) OVERLAY_DO_SET, r, X(__VA_ARGS__)  // register r is what those OPs work out

// A script being written, its GOTOs, IFs and UNTILs going to labels filled in when it's sent
typedef struct {
  uint8_t b[OVERLAY_SCRIPT_MAX + 16];
  int n, label[8], fix[16], to[16], fixes;
} code_t;

static void put(code_t *c, const uint8_t *b, int n) {
  memcpy(c->b + c->n, b, (size_t) n);
  c->n += n;
}
#define PUT(c, ...) put(c, BYTES(__VA_ARGS__))

static void label(code_t *c, int k) { c->label[k] = c->n; }

static void go(code_t *c, int k) {  // the byte label k is at
  c->fix[c->fixes] = c->n;
  c->to[c->fixes++] = k;
  c->n += 2;
}

// Its bytes, the labels filled in
static const uint8_t *done(code_t *c) {
  for (int i = 0; i < c->fixes; i++)
    memcpy(c->b + c->fix[i], (uint8_t[]) {AT(c->label[c->to[i]])}, 2);
  return c->b;
}

static void send(code_t *c, int p) { script_to(p, done(c), c->n); }

// Slot 0: the dot's 4 frames, 50, 60, 70 and 80 ms, the first 90 after the first time
static uint32_t dots(void) {
  no_overlays();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  while (overlay_gone() >= 0 || overlay_mark() >= 0)
    ;
  return n;
}

static int pos(int p, bool y) {
  int16_t px, py;
  return overlay_pos(p, &px, &py) ? (y ? py : px) : INT16_MIN;
}

// Place 0's x once its script s has worked out register 0, where PLACE put it if it didn't
#define AWAY -500
static int worked_out(const uint8_t *s, int n) {
  code_t c = {0};
  put(&c, s, n);
  PUT(&c, TO(OVERLAY_X, REG(0)));
  place(0, 0, AWAY, 0, 0, 0);
  send(&c, 0);
  sim_run(500);  // for those that take more than OVERLAY_SCRIPT_STEPS
  return pos(0, false);
}
#define WORKED_OUT(...) worked_out(BYTES(__VA_ARGS__))

static void test_script_maths(void) {
  dots();
  show_overlays();
  CHECK_EQ(WORKED_OUT(SET(0, V16(7), V8(6), OVERLAY_OP_MUL)), 42);
  CHECK_EQ(WORKED_OUT(SET(0, V16(7), V16(10), OVERLAY_OP_SUB)), -3);
  CHECK_EQ(WORKED_OUT(SET(0, V16(100), V16(7), OVERLAY_OP_DIV)), 14);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-7), V8(3), OVERLAY_OP_MOD)), -1);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-7), V8(3), OVERLAY_OP_WRAP)), 2);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-7), V8(0), OVERLAY_OP_WRAP)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V16(5), V16(0), OVERLAY_OP_DIV)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V16(5), V16(0), OVERLAY_OP_MOD)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V32(INT32_MIN), V8(-1), OVERLAY_OP_DIV)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V32(INT32_MIN), V8(-1), OVERLAY_OP_MOD)), 0);
  // 32 bits, wrapping round, by 65536 to fit x
  CHECK_EQ(WORKED_OUT(SET(0, V32(INT32_MIN), OVERLAY_OP_ABS, V32(65536), OVERLAY_OP_DIV)), 32767);
  CHECK_EQ(WORKED_OUT(SET(0, V32(INT32_MAX), V8(1), OVERLAY_OP_ADD, V32(65536), OVERLAY_OP_DIV)), -32768);
  CHECK_EQ(WORKED_OUT(SET(0, V32(INT32_MIN), OVERLAY_OP_NEG, V32(65536), OVERLAY_OP_DIV)), -32768);
  CHECK_EQ(WORKED_OUT(SET(0, V32(-123456), V16(1000), OVERLAY_OP_DIV)), -123);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-9), OVERLAY_OP_ABS)), 9);
  CHECK_EQ(WORKED_OUT(SET(0, V8(9), OVERLAY_OP_NEG)), -9);
  CHECK_EQ(WORKED_OUT(SET(0, V8(5), V8(3), OVERLAY_OP_MIN)), 3);
  CHECK_EQ(WORKED_OUT(SET(0, V8(5), V8(9), OVERLAY_OP_MAX)), 9);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 3, V16(9), SET(0, REG(3), REG(3), OVERLAY_OP_ADD)), 18);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V8(-128)), -128);
  // what's over when a * b is too much for 32 bits
  CHECK_EQ(WORKED_OUT(SET(0, V32(100000), V32(100000), V32(100000000), OVERLAY_OP_SCALE)), 100);
  CHECK_EQ(WORKED_OUT(SET(0, V8(5), V8(5), V8(0), OVERLAY_OP_SCALE)), 0);
  // what holds is 1, and one of two by it
  static const uint8_t CMP[] = {OVERLAY_OP_LT, OVERLAY_OP_LE, OVERLAY_OP_EQ,
                                OVERLAY_OP_NE, OVERLAY_OP_GE, OVERLAY_OP_GT};
  static const uint8_t HOLDS[][6] = {{1, 1, 0, 1, 0, 0}, {0, 1, 1, 0, 1, 0}, {0, 0, 0, 1, 1, 1}};  // 3 to 4, 4 and 2
  bool ok = true;
  for (int k = 0; k < 3; k++)
    for (int i = 0; i < 6; i++)
      ok &= WORKED_OUT(SET(0, V8(3), V8(k == 0 ? 4 : k == 1 ? 3 : 2), CMP[i])) == HOLDS[k][i];
  CHECK(ok);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-2), V8(7), OVERLAY_OP_AND)), 1);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-2), V8(0), OVERLAY_OP_AND)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V8(0), V8(7), OVERLAY_OP_OR)), 1);
  CHECK_EQ(WORKED_OUT(SET(0, V8(0), V8(0), OVERLAY_OP_OR)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V8(0), OVERLAY_OP_NOT)), 1);
  CHECK_EQ(WORKED_OUT(SET(0, V8(5), OVERLAY_OP_NOT)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V8(5), V8(11), V8(22), OVERLAY_OP_IF)), 11);
  CHECK_EQ(WORKED_OUT(SET(0, V8(0), V8(11), V8(22), OVERLAY_OP_IF)), 22);
  // sin and cos of any angle, rounded, and the angle and length of a way
  CHECK_EQ(WORKED_OUT(SET(0, V16(30), V16(1000), OVERLAY_OP_SIN)), 500);
  CHECK_EQ(WORKED_OUT(SET(0, V16(60), V16(1000), OVERLAY_OP_COS)), 500);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-90), V8(100), OVERLAY_OP_SIN)), -100);
  CHECK_EQ(WORKED_OUT(SET(0, V16(540), V8(100), OVERLAY_OP_COS)), -100);
  CHECK_EQ(WORKED_OUT(SET(0, V16(390), REG(5), OVERLAY_OP_SIN)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V8(0), V8(10), OVERLAY_OP_ATAN)), 90);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-10), V8(0), OVERLAY_OP_ATAN)), 180);
  CHECK_EQ(WORKED_OUT(SET(0, V8(0), V8(0), OVERLAY_OP_ATAN)), 0);
  CHECK_EQ(WORKED_OUT(SET(0, V8(30), V8(-40), OVERLAY_OP_HYPOT)), 50);
  CHECK_EQ(WORKED_OUT(SET(0, V32(3000000), V32(4000000), OVERLAY_OP_HYPOT, V8(2), OVERLAY_OP_DIV)),
           46339 / 2);  // as far as it's measured, 32767 each way
  // eased: half the way in is a quarter there, out three quarters, both half
  CHECK_EQ(WORKED_OUT(SET(0, V8(50), V8(100), V8(OVERLAY_EASE_IN), OVERLAY_OP_EASE)), 25);
  CHECK_EQ(WORKED_OUT(SET(0, V8(50), V8(100), V8(OVERLAY_EASE_OUT), OVERLAY_OP_EASE)), 75);
  CHECK_EQ(WORKED_OUT(SET(0, V8(25), V8(100), V8(OVERLAY_EASE_IN | OVERLAY_EASE_OUT), OVERLAY_OP_EASE)), 15);
  CHECK_EQ(WORKED_OUT(SET(0, V16(250), V8(100), V8(0), OVERLAY_OP_EASE)), 100);  // no further than there
  CHECK_EQ(WORKED_OUT(SET(0, V8(50), V8(0), V8(0), OVERLAY_OP_EASE)), 0);
  // a number of a list, the first below it and the last past it
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 1, V16(2), SET(0, REG(1), OVERLAY_OP_PICK, 4, 10, 0, 20, 0, 0xD0, 0xFF, 40, 0)),
           -48);
  CHECK_EQ(WORKED_OUT(SET(0, V8(-3), OVERLAY_OP_PICK, 3, 10, 0, 20, 0, 30, 0)), 10);
  CHECK_EQ(WORKED_OUT(SET(0, V32(100000), OVERLAY_OP_PICK, 3, 10, 0, 20, 0, 30, 0)), 30);
  CHECK_EQ(WORKED_OUT(SET(1, V8(0), OVERLAY_OP_PICK, 1, 7, 0), OVERLAY_DO_SET, 0, REG(1)), 7);
  // along a list: 10, 30 and -10 over 0 to 200
#define CURVE(a) WORKED_OUT(SET(0, V16(a), V16(200), OVERLAY_OP_CURVE, 3, 10, 0, 30, 0, 0xF6, 0xFF))
  CHECK_EQ(CURVE(0), 10);
  CHECK_EQ(CURVE(50), 20);
  CHECK_EQ(CURVE(100), 30);
  CHECK_EQ(CURVE(150), 10);
  CHECK_EQ(CURVE(200), -10);
  CHECK_EQ(CURVE(-5), 10);
  CHECK_EQ(CURVE(900), -10);
  CHECK_EQ(WORKED_OUT(SET(0, V8(5), V8(0), OVERLAY_OP_CURVE, 2, 10, 0, 30, 0)), 10);  // no way to go along
  CHECK_EQ(WORKED_OUT(SET(0, V8(5), V8(9), OVERLAY_OP_CURVE, 1, 10, 0)), 10);
  // random, both ends in: the least and most of 50, as 10 * least + most
  code_t c = {0};
  PUT(&c, OVERLAY_DO_SET, 1, V16(100), OVERLAY_DO_SET, 2, V16(-100));
  label(&c, 0);
  PUT(&c, SET(4, V8(1), V8(3), OVERLAY_OP_RAND), SET(1, REG(1), REG(4), OVERLAY_OP_MIN));
  PUT(&c, SET(2, REG(2), REG(4), OVERLAY_OP_MAX), SET(3, REG(3), V8(1), OVERLAY_OP_ADD));
  PUT(&c, OVERLAY_DO_IF, X(REG(3), V8(50), OVERLAY_OP_LT));
  go(&c, 0);
  PUT(&c, SET(0, REG(1), V8(10), OVERLAY_OP_MUL, REG(2), OVERLAY_OP_ADD));
  CHECK_EQ(worked_out(done(&c), c.n), 13);
  CHECK_EQ(WORKED_OUT(SET(0, V8(20), V8(10), OVERLAY_OP_RAND)), 20);  // the first if the second is less
  CHECK_EQ(WORKED_OUT(SET(0, V32(INT32_MIN), V32(INT32_MAX), OVERLAY_OP_RAND), OVERLAY_DO_SET, 0, V8(1)), 1);
  // at random by a number: the same for the same one, another for most others, and nothing of none
  code_t h = {0};
  PUT(&h, SET(1, V8(7), V16(1000), OVERLAY_OP_HASH), SET(2, V8(7), V16(1000), OVERLAY_OP_HASH));
  PUT(&h, SET(3, V8(8), V16(1000), OVERLAY_OP_HASH), SET(4, V8(9), V16(1000), OVERLAY_OP_HASH));
  PUT(&h, SET(0, REG(1), V8(0), OVERLAY_OP_GE, REG(1), V16(1000), OVERLAY_OP_LT, OVERLAY_OP_AND));
  PUT(&h, SET(0, REG(0), REG(1), REG(2), OVERLAY_OP_NE, V8(2), OVERLAY_OP_MUL, OVERLAY_OP_ADD));  // + 2 if they differ
  PUT(&h, SET(0, REG(0), REG(1), REG(3), OVERLAY_OP_NE, REG(1), REG(4), OVERLAY_OP_NE, OVERLAY_OP_OR, V8(4),
              OVERLAY_OP_MUL, OVERLAY_OP_ADD));
  CHECK_EQ(worked_out(done(&h), h.n), 1 + 4);
  CHECK_EQ(WORKED_OUT(SET(0, V8(7), V8(0), OVERLAY_OP_HASH)), 0);
  // a GOTO past the first 256 bytes
  c = (code_t) {0};
  PUT(&c, OVERLAY_DO_GOTO);
  go(&c, 0);
  for (int i = 0; i < 70; i++)
    PUT(&c, OVERLAY_DO_SET, 0, V16(1));
  label(&c, 0);
  PUT(&c, SET(0, REG(0), V8(41), OVERLAY_OP_ADD));
  CHECK(c.label[0] > 255);
  CHECK_EQ(worked_out(done(&c), c.n), 41);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// Place 0 placed anew, with a script that puts value in its x each motion step
static void watch(const uint8_t *value, int n) {
  code_t c = {0};
  label(&c, 0);
  PUT(&c, OVERLAY_DO_SET, 0);
  put(&c, value, n);
  PUT(&c, TO(OVERLAY_X, REG(0)), OVERLAY_DO_WAIT, V8(1), OVERLAY_DO_GOTO);
  go(&c, 0);
  place(0, 0, AWAY, 0, 0, 0);
  send(&c, 0);
}
#define WATCH(...) watch(BYTES(X(__VA_ARGS__)))
// What it is once the place has been there for ms
static int after(int ms, const uint8_t *value, int n) {
  watch(value, n);
  sim_run((uint32_t) ms);
  return pos(0, false);
}
#define AFTER(ms, ...) after(ms, BYTES(X(__VA_ARGS__)))

// NOISE goes smoothly between numbers at random, a place's own, and WAVE between low and high with its holds
static void test_script_waves(void) {
  dots();
  show_overlays();
  int lo = 9999, hi = -9999, step = 0;
  WATCH(OWN(OVERLAY_GET_MS), V16(400), V16(1000), OVERLAY_OP_NOISE);
  for (int i = 0, was = pos(0, false); i < 80; i++, was = pos(0, false)) {
    sim_run(50);
    lo = LV_MIN(lo, pos(0, false));
    hi = LV_MAX(hi, pos(0, false));
    step = LV_MAX(step, abs(pos(0, false) - was));
  }
  CHECK(lo >= 0 && hi <= 1000 && hi - lo > 300);  // ten of them
  CHECK(step > 0 && step < 200);                  // an eighth of the way each step, never a jump
  // up over 400 ms, there for 100, down over 200 and at low for 300
#define WAVE(flags) \
  OWN(OVERLAY_GET_MS), V8(-100), V16(300), V16(400), V16(200), V8(100), V16(300), V8(0), V8(flags), OVERLAY_OP_WAVE
  CHECK_EQ(AFTER(0, WAVE(0)), -100);
  CHECK_EQ(AFTER(200, WAVE(0)), 100);
  CHECK_EQ(AFTER(400, WAVE(0)), 300);
  CHECK_EQ(AFTER(450, WAVE(0)), 300);
  CHECK_EQ(AFTER(600, WAVE(0)), 100);
  CHECK_EQ(AFTER(700, WAVE(0)), -100);
  CHECK_EQ(AFTER(950, WAVE(0)), -100);
  CHECK_EQ(AFTER(1200, WAVE(0)), 100);                   // round again
  CHECK_EQ(AFTER(200, WAVE(OVERLAY_WAVE_SLOW_LOW)), 0);  // a quarter of the way up in half the time
  CHECK_EQ(AFTER(200, WAVE(OVERLAY_WAVE_SLOW_HIGH)), 200);
  CHECK_EQ(AFTER(600, WAVE(OVERLAY_WAVE_SLOW_LOW)), 0);  // and three quarters down, slow at low again
  CHECK_EQ(AFTER(600, WAVE(OVERLAY_WAVE_SLOW_HIGH)), 200);
  // with no times it stays low, with jitter it's still between the two and goes all the way
  CHECK_EQ(AFTER(500, OWN(OVERLAY_GET_MS), V8(7), V8(9), V8(0), V8(0), V8(0), V8(0), V8(0), V8(0), OVERLAY_OP_WAVE), 7);
  lo = 9999, hi = -9999;
  WATCH(OWN(OVERLAY_GET_MS), V8(-100), V16(300), V16(400), V16(200), V8(100), V16(300), V8(40), V8(0), OVERLAY_OP_WAVE);
  for (int i = 0; i < 60; i++) {
    sim_run(50);
    lo = LV_MIN(lo, pos(0, false));
    hi = LV_MAX(hi, pos(0, false));
  }
  CHECK(lo == -100 && hi == 300);
  // at full jitter it stops at the most and never turns back: up over a second, then down at once
  int back = 0;
  WATCH(OWN(OVERLAY_GET_MS), V8(0), V16(1000), V16(1000), V8(0), V8(0), V8(0), V8(100), V8(0), OVERLAY_OP_WAVE);
  for (int i = 0, was = pos(0, false); i < 2000; i++, was = pos(0, false)) {
    sim_run(50);
    back += pos(0, false) < was && pos(0, false) > was - 500;
  }
  CHECK_EQ(back, 0);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// GET, DIST and ANGLE: where the others are and how they look
static void test_script_looks(void) {
  dots();
  show_overlays();
  place(1, 0, 100, 50, 0, 0);
  place(2, 0, 130, 90, 0, 0);
#define READS(what, p) WORKED_OUT(OVERLAY_DO_SET, 0, GET(what, p))
  CHECK_EQ(READS(OVERLAY_X, 1), 100);
  CHECK_EQ(READS(OVERLAY_Y, 1), 50);
  CHECK_EQ(READS(OVERLAY_X, OVERLAY_SELF), AWAY);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OWN(OVERLAY_W)), AW * 2);
  CHECK_EQ(READS(OVERLAY_H, 1), AH * 2);
  CHECK_EQ(READS(OVERLAY_GET_HERE, 1), 1);
  CHECK_EQ(READS(OVERLAY_GET_HERE, 5), 0);
  CHECK_EQ(READS(OVERLAY_X, 5), 0);
  CHECK_EQ(READS(OVERLAY_SIZE, 1), 100);
  CHECK_EQ(READS(OVERLAY_OPACITY, 1), 100);
  CHECK_EQ(READS(OVERLAY_LIGHT, 1), 100);
  CHECK_EQ(READS(OVERLAY_LEVEL, 1), 100);
  CHECK_EQ(READS(OVERLAY_PIVOT_X, 1), OVERLAY_PIVOT_MAX / 2);
  CHECK_EQ(READS(OVERLAY_DEPTH, 1), OVERLAY_NEAREST);
  CHECK_EQ(READS(OVERLAY_GET_FRAME, 1), 0);  // no script
  uint8_t m[OVERLAY_GLIDE_HEAD + 8 * OVERLAY_GLIDE_EACH];
  overlay_msg(
      K_OVERLAY_GLIDE, m,
      sim_overlay_glide(m, 1, 0, 0, EACH(OVERLAY_TURN, 45, OVERLAY_SIZE, 200, OVERLAY_SIZE_Y, -40, OVERLAY_LIGHT, 30)));
  CHECK_EQ(READS(OVERLAY_TURN, 1), 45);
  CHECK_EQ(READS(OVERLAY_SIZE, 1), 200);
  CHECK_EQ(READS(OVERLAY_SIZE_X, 1), 100);
  CHECK_EQ(READS(OVERLAY_SIZE_Y, 1), -40);
  CHECK_EQ(READS(OVERLAY_LIGHT, 1), 30);
  // its view, its speed, depth and pivot, and its slot's
  overlay_msg(K_OVERLAY_GLIDE, m,
              sim_overlay_glide(m, 1, 0, 0,
                                EACH(OVERLAY_PAN_X, -7, OVERLAY_PAN_Y, 12, OVERLAY_SPEED, 250, OVERLAY_LEVEL, 150,
                                     OVERLAY_PIVOT_Y, 1000, OVERLAY_DEPTH, 40)));
  CHECK_EQ(READS(OVERLAY_PAN_X, 1), -7);
  CHECK_EQ(READS(OVERLAY_PAN_Y, 1), 12);
  CHECK_EQ(READS(OVERLAY_SPEED, 1), 250);
  CHECK_EQ(READS(OVERLAY_LEVEL, 1), 150);
  CHECK_EQ(READS(OVERLAY_PIVOT_Y, 1), 1000);
  CHECK_EQ(READS(OVERLAY_DEPTH, 1), 40);
  CHECK_EQ(READS(OVERLAY_GET_FRAMES, 1), 4);
  CHECK_EQ(READS(OVERLAY_GET_FRAME_W, 1), AW * 2);
  CHECK_EQ(READS(OVERLAY_GET_FRAME_H, 1), AH * 2);
  // where its pivot is across the screen, its area to start with: place 2's middle is 138, 94
  CHECK_EQ(READS(OVERLAY_GET_AT_X, 2), 138 * OVERLAY_AT_MAX / PIC_MAX_W);
  CHECK_EQ(READS(OVERLAY_GET_AT_Y, 2), 94 * OVERLAY_AT_MAX / PIC_MAX_H);
  // a number of its own, the same each time it's asked
  int seed = READS(OVERLAY_GET_SEED, 2);
  CHECK(seed >= 0 && seed <= OVERLAY_SEED_MAX && READS(OVERLAY_GET_SEED, 2) == seed);
  CHECK_EQ(READS(OVERLAY_GET_ROW, 2), 0);  // only a row's bind has one
  // from place 1's pivot, the middle of its bottom, to 2's middle is 30 right and 36 down
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_DIST, 1, 2), 46);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_DIST, 2, 1), 46);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_DIST, 1, 5), 0);
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, 1, 0, 0, EACH(OVERLAY_PIVOT_Y, 500)));
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_DIST, 1, 2), 50);  // middle to middle, 40 down
  int a = WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_ANGLE, 1, 2);
  CHECK(a >= 52 && a <= 54);
  place(2, 0, 100, 150, 0, 0);  // below
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_ANGLE, 1, 2), 90);
  place(2, 0, -2000, 50, 0, 0);  // far to the left
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_ANGLE, 1, 2), 180);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_DIST, 1, 2), 2100);
  place(2, 0, 100, 0, 0, 0);  // above
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_ANGLE, 1, 2), 270);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, OVERLAY_OP_ANGLE, 1, 1), 0);
  // what another's script is at, and how long each has been there
  place(3, 0, 0, 100, 1, 0);
  script_to(3, BYTES(OVERLAY_DO_FRAME, V16(2), OVERLAY_DO_RATE, V16(0)));
  CHECK_EQ(READS(OVERLAY_GET_FRAME, 3), 2);
  int ms = READS(OVERLAY_GET_MS, 3);  // the one before took 500
  CHECK(ms >= 495 && ms <= 505);
  CHECK_EQ(READS(OVERLAY_GET_ROUNDS, 3), 0);  // held
  int self = WORKED_OUT(OVERLAY_DO_WAIT, V16(200), OVERLAY_DO_SET, 0, OWN(OVERLAY_GET_MS));
  CHECK(self >= 200 && self <= 260);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// CLIP, FRAME and RATE on a frame of its own, ROUNDS of the clip and MARKs
static void test_script_clips(void) {
  dots();
  place(0, 0, 0, 0, 1, 0);
  show_overlays();
  // 1 for 60 ms, 2 for 70 ms, round and round
  script_to(0, BYTES(OVERLAY_DO_CLIP, V16(1), V16(2), OVERLAY_CLIP_NOW, WHOLE));
  CHECK(overlay_shows(0, 1));
  sim_run(55);
  CHECK(overlay_shows(0, 1));
  sim_run(10);  // 65
  CHECK(overlay_shows(0, 2));
  sim_run(60);  // 125
  CHECK(overlay_shows(0, 2));
  sim_run(10);  // 135
  CHECK(overlay_shows(0, 1));
  // 1 and 2 to the end of their round, then 3 for a round of its own, then 0 and held there
  script_to(0, BYTES(OVERLAY_DO_CLIP, V16(1), V16(2), OVERLAY_CLIP_NOW, WHOLE, OVERLAY_DO_CLIP, V16(3), V16(3),
                     OVERLAY_CLIP_NEXT, WHOLE, OVERLAY_DO_MARK, 2, OVERLAY_DO_ROUNDS, V16(1), OVERLAY_DO_CLIP, V16(0),
                     V16(0), OVERLAY_CLIP_NOW, WHOLE, OVERLAY_DO_RATE, V16(0)));
  sim_run(125);
  CHECK(overlay_shows(0, 2));
  CHECK_EQ(overlay_mark(), -1);
  sim_run(10);  // 135, on to 3 at 130
  CHECK(overlay_shows(0, 3));
  CHECK_EQ(overlay_mark(), 0 | 2 << 8);
  CHECK_EQ(overlay_mark(), -1);
  sim_run(70);  // 205
  CHECK(overlay_shows(0, 3));
  sim_run(10);  // 215, its round of 80 ms over
  CHECK(overlay_shows(0, 0));
  sim_run(1000);
  CHECK(overlay_shows(0, 0));
  // two rounds, then at once on to 3, as a round just ended
  script_to(
      0, BYTES(OVERLAY_DO_CLIP, V16(1), V16(2), OVERLAY_CLIP_NOW, WHOLE, OVERLAY_DO_ROUNDS, V16(2), OVERLAY_DO_MARK, 1,
               OVERLAY_DO_CLIP, V16(3), V16(3), OVERLAY_CLIP_NEXT, WHOLE, OVERLAY_DO_RATE, V16(0)));
  sim_run(250);
  CHECK(overlay_shows(0, 2));
  CHECK_EQ(overlay_mark(), -1);
  sim_run(20);  // 270, round 2 ended at 260
  CHECK(overlay_shows(0, 3));
  CHECK_EQ(overlay_mark(), 0 | 1 << 8);
  sim_run(1000);
  CHECK(overlay_shows(0, 3));
  // FRAME goes there and on from it, a new script starts from the first frame
  script_to(0, BYTES(OVERLAY_DO_FRAME, V16(7)));  // 3 of 4
  CHECK(overlay_shows(0, 3));
  sim_run(85);
  CHECK(overlay_shows(0, 0));
  sim_run(80);  // 165, 0 is 90 ms after the first time
  CHECK(overlay_shows(0, 0));
  sim_run(10);
  CHECK(overlay_shows(0, 1));
  script_to(0, BYTES(OVERLAY_DO_SET, 0, V16(0)));
  CHECK(overlay_shows(0, 0));
  sim_run(55);
  CHECK(overlay_shows(0, 1));
  // at half speed, held a while, then on with the next frame at once
  script_to(0, BYTES(OVERLAY_DO_RATE, V16(50), OVERLAY_DO_WAIT, V16(200), OVERLAY_DO_RATE, V16(0), OVERLAY_DO_WAIT,
                     V16(500), OVERLAY_DO_RATE, V16(100)));
  sim_run(90);
  CHECK(overlay_shows(0, 0));
  sim_run(20);  // 110: 0 for 100 ms
  CHECK(overlay_shows(0, 1));
  sim_run(400);
  CHECK(overlay_shows(0, 1));
  sim_run(230);  // 740, on at 700
  CHECK(overlay_shows(0, 2));
  // in step with the slot, its rounds are the slot's
  place(1, 0, 40, 0, 0, 0);
  script_to(1, BYTES(OVERLAY_DO_ROUNDS, V16(1), OVERLAY_DO_MARK, 5));
  sim_run(400);
  CHECK_EQ(overlay_mark(), 1 | 5 << 8);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// What GET g of place 0 reads once it shows w x h of its frames
static int clip_reads(int w, int h, int g) {
  place(0, 0, AWAY, 0, 1, 0);
  script_to(0, BYTES(OVERLAY_DO_CLIP, V16(0), V16(3), OVERLAY_CLIP_NOW, AT(w), AT(h), OVERLAY_DO_SET, 0, OWN(g),
                     TO(OVERLAY_X, REG(0))));
  return pos(0, false);
}

// A CLIP's size: the box is what it shows of the frame, mirrored in place, and that's what GET reads
static void test_script_clip_size(void) {
  dots();
  show_overlays();
  CHECK_EQ(clip_reads(3, 2, OVERLAY_W), 6);
  CHECK_EQ(clip_reads(3, 2, OVERLAY_H), 4);
  CHECK_EQ(clip_reads(AW + 1, 0, OVERLAY_W), AW * 2 + 2);  // bigger than the frame, clear past it
  CHECK_EQ(clip_reads(0, 0, OVERLAY_H), AH * 2);
  // frame 0's dot is colours 1 and 2 at 2 and 3 across, 1 down
  place(0, 0, 10, 10, 1, 0);
  script_to(0, BYTES(OVERLAY_DO_CLIP, V16(0), V16(0), OVERLAY_CLIP_NOW, AT(AW / 2), AT(AH), OVERLAY_DO_RATE, V16(0)));
  sim_run(50);
  CHECK(!sim_stale());
  overlay_look_t l;
  int px[AW * AH * 4];
  CHECK(drawn(0, &l, px) && l.box.x1 == 10 && lv_area_get_width(&l.box) == AW && lv_area_get_height(&l.box) == AH * 2);
  CHECK(px[2 * AW + 4] == 1 && px[2 * AW + 6] == 2);
  uint8_t v[OVERLAY_VIEW_LEN];
  overlay_msg(K_OVERLAY_VIEW, v, sim_overlay_view(v, 0, OVERLAY_VIEW_FLIP_X, OVERLAY_VIEW_FLIP_X));
  sim_run(50);
  CHECK(!sim_stale());
  CHECK(drawn(0, &l, px) && l.box.x1 == 10 && px[2 * AW] == 2 && px[2 * AW + 2] == 1);
  script_to(0, BYTES(OVERLAY_DO_CLIP, V16(0), V16(0), OVERLAY_CLIP_NOW, WHOLE));
  sim_run(50);
  CHECK(!sim_stale());
  CHECK(overlay_look(0, &l) && l.box.x1 == 10 && lv_area_get_width(&l.box) == AW * 2);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// WAIT, UNTIL, GOTO and IF, and a script that never waits doesn't hold the rest up
static void test_script_waits(void) {
  dots();
  show_overlays();
  place(0, 0, 0, 0, 0, 0);
  script_to(0, BYTES(OVERLAY_DO_WAIT, V16(200), TO(OVERLAY_X, V16(5))));
  sim_run(150);
  CHECK_EQ(pos(0, false), 0);
  sim_run(110);
  CHECK_EQ(pos(0, false), 5);
  script_to(0, BYTES(OVERLAY_DO_WAIT, V16(-5), OVERLAY_DO_WAIT, V16(0), TO(OVERLAY_X, V16(6))));
  CHECK_EQ(pos(0, false), 6);
  // place 1 drifts right 100 px a second, place 0 waits till it's 50 px on
  place(1, 0, 0, 50, 0, 0);
  uint8_t m[OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_DRIFT, m, sim_overlay_drift(m, 1, EACH(OVERLAY_X, 100)));
  script_to(0, BYTES(OVERLAY_DO_UNTIL, X(GET(OVERLAY_X, 1), V8(50), OVERLAY_OP_GE), TO(OVERLAY_Y, V16(77))));
  sim_run(400);
  CHECK_EQ(pos(0, true), 0);
  sim_run(200);
  CHECK_EQ(pos(0, true), 77);
  script_to(0, BYTES(OVERLAY_DO_UNTIL, V8(1), TO(OVERLAY_Y, V16(78))));  // it holds already
  CHECK_EQ(pos(0, true), 78);
  // down from 3 to 0, and on past the end
  code_t c = {0};
  PUT(&c, OVERLAY_DO_SET, 0, V16(3));
  label(&c, 0);
  PUT(&c, SET(0, REG(0), V8(1), OVERLAY_OP_SUB), SET(1, REG(1), V8(10), OVERLAY_OP_ADD), OVERLAY_DO_IF, REG(0));
  go(&c, 0);
  PUT(&c, TO(OVERLAY_X, REG(1)), OVERLAY_DO_GOTO);
  go(&c, 1);
  PUT(&c, TO(OVERLAY_X, V16(99)));
  label(&c, 1);
  send(&c, 0);
  CHECK_EQ(pos(0, false), 30);
  // round and round: OVERLAY_SCRIPT_STEPS at a time, 3 a time round
  c = (code_t) {0};
  label(&c, 0);
  PUT(&c, SET(0, REG(0), V8(1), OVERLAY_OP_ADD), TO(OVERLAY_X, REG(0)), OVERLAY_DO_GOTO);
  go(&c, 0);
  send(&c, 0);
  CHECK_EQ(pos(0, false), (OVERLAY_SCRIPT_STEPS + 1) / 3);  // a move each third step, from the second
  sim_run(500);
  CHECK(pos(0, false) > 5 * OVERLAY_SCRIPT_STEPS / 3);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// SEND with values worked out, the shared registers, END and the GONE and MARK it answers
static void test_script_sends(void) {
  uint32_t n = dots();
  show_overlays();
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 0, 60, 0, 0);
  // a drift of a register's hundredths of a px a second, a flip to itself whatever place the message has
  script_to(0, BYTES(OVERLAY_DO_SET, 2, V16(2000), OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_DRIFT - K_OVERLAY_FRAMES,
                     OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH, 0, OVERLAY_X, 0, 0, 1, OVERLAY_DRIFT_HEAD + 1, REG(2),
                     OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_VIEW - K_OVERLAY_FRAMES, OVERLAY_VIEW_LEN, 5,
                     OVERLAY_VIEW_FLIP_X, OVERLAY_VIEW_FLIP_X, 0));
  sim_run(1000);
  int x = pos(0, false);
  CHECK(x >= 19 && x <= 21);
  overlay_look_t l;
  CHECK(overlay_look(0, &l) && l.view == OVERLAY_VIEW_FLIP_X);
  CHECK(!overlay_look(5, &l));
  // a TINT of red: its dark colours from black to red
  script_to(0, BYTES(OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_TINT - K_OVERLAY_FRAMES, OVERLAY_TINT_LEN, 0, 1, 0xFF, 0,
                     0, 0));
  CHECK(overlay_look(0, &l) && LV_COLOR_GET_R(l.pal[1]) && !LV_COLOR_GET_G(l.pal[1]) && !LV_COLOR_GET_B(l.pal[1]));
  // a glide of x and y together, over ms a register has and to what's worked out, and at once with none
#define MOVE(ease, ms, x, y) OVERLAY_DO_GLIDE, 2 | (ease) << 6, ms, OVERLAY_X, x, OVERLAY_Y, y
  script_to(1, BYTES(MOVE(0, V8(0), V8(0), V8(60))));
  CHECK_EQ(pos(1, false), 0);
  CHECK_EQ(pos(1, true), 60);
  // to another place, which goes on with what it does
  script_to(0, BYTES(OVERLAY_DO_SET, 0, V16(44), TO_PLACE(1, OVERLAY_Y, REG(0)), TO(OVERLAY_X, V16(3))));
  CHECK_EQ(pos(1, true), 44);
  CHECK_EQ(pos(1, false), 0);
  CHECK_EQ(pos(0, false), 3);
  script_to(0, BYTES(TO_PLACE(5, OVERLAY_Y, V16(1)), TO(OVERLAY_X, V16(4))));  // none there, it goes on
  CHECK_EQ(pos(0, false), 4);
  // what one sets in 8..11 the others see
  script_to(0, BYTES(OVERLAY_DO_SET, 8, V16(42), OVERLAY_DO_SET, 11, V16(-7)));
  script_to(1, BYTES(TO(OVERLAY_X, REG(8)), TO(OVERLAY_Y, REG(11))));
  CHECK_EQ(pos(1, false), 42);
  CHECK_EQ(pos(1, true), -7);
  script_to(1, BYTES(OVERLAY_DO_SET, 0, V16(1), TO(OVERLAY_X, REG(0))));  // its own 0..7 are its own
  script_to(0, BYTES(TO(OVERLAY_X, REG(0))));
  CHECK_EQ(pos(1, false), 1);
  CHECK_EQ(pos(0, false), 0);
  // a mark each, the last if there's more than one before the ESP hears
  script_to(0, BYTES(OVERLAY_DO_MARK, 3, OVERLAY_DO_MARK, 4));
  script_to(1, BYTES(OVERLAY_DO_MARK, 9));
  CHECK_EQ(overlay_mark(), 0 | 4 << 8);
  CHECK_EQ(overlay_mark(), 1 | 9 << 8);
  CHECK_EQ(overlay_mark(), -1);
  // END fades it out and it goes, in the middle of its frames and steps
  script_to(1, BYTES(OVERLAY_DO_END, V16(100), 6, TO(OVERLAY_X, V16(9))));
  CHECK_EQ(pos(1, false), 1);
  sim_run(60);
  CHECK(look_opa(1) > 0 && look_opa(1) < 1000);
  CHECK_EQ(overlay_gone(), -1);
  sim_run(100);
  CHECK_EQ(pos(1, false), INT16_MIN);
  CHECK_EQ(overlay_gone(), 1 | 6 << 8);
  // not shown, it goes at once
  lv_obj_clean(lv_scr_act());
  script_to(0, BYTES(OVERLAY_DO_MARK, 1, OVERLAY_DO_END, V16(1000), 8));
  CHECK_EQ(pos(0, false), INT16_MIN);
  CHECK_EQ(overlay_gone(), 0 | 8 << 8);
  CHECK_EQ(overlay_mark(), -1);  // nothing to tell of one that's gone
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  // all cleared, the shared registers are 0 again
  overlay_clear_msg(OVERLAY_ALL);
  dots();
  place(1, 0, 0, 60, 0, 0);
  script_to(1, BYTES(TO(OVERLAY_X, V16(5)), TO(OVERLAY_X, REG(8))));
  CHECK_EQ(pos(1, false), 0);
  overlay_clear_msg(OVERLAY_ALL);
}

// BIND in a script: what it binds is worked out each motion step with the script's registers, a register's before the
// channels', a frame's holds its frames. They go with FREE, with the script and with the place, and a SEND binds
// another place.
static void test_script_binds(void) {
  dots();
  show_overlays();
  place(0, 0, 100, 50, 0, 0);
  unsigned used = overlay_used();
  // 3 px on for each time round, from where it is
  code_t c = {0};
  PUT(&c, OVERLAY_DO_BIND, OVERLAY_BIND_ADD | OVERLAY_X, X(REG(0), V16(3000), OVERLAY_OP_MUL));
  label(&c, 0);
  PUT(&c, SET(0, REG(0), V8(1), OVERLAY_OP_ADD), OVERLAY_DO_WAIT, V8(1), OVERLAY_DO_GOTO);
  go(&c, 0);
  send(&c, 0);
  CHECK_EQ(overlay_used(), used + SCRIPT_POOL + (unsigned) c.n);  // its value is where the script has it
  CHECK_EQ(pos(0, false), 103);
  sim_run(100);
  CHECK_EQ(pos(0, false), 109);
  // a register bound to how long it's been there, and y to the register: a px down each 100 ms
  script_to(0, BYTES(OVERLAY_DO_BIND, OVERLAY_BIND_SET | (OVERLAY_BIND_REG + 1),
                     X(OWN(OVERLAY_GET_MS), V8(100), OVERLAY_OP_DIV), OVERLAY_DO_BIND, OVERLAY_BIND_ADD | OVERLAY_Y,
                     X(REG(1), V16(1000), OVERLAY_OP_MUL)));
  CHECK_EQ(pos(0, false), 100);  // what the script before bound went with it
  CHECK_EQ(pos(0, true), 51);
  sim_run(500);
  CHECK_EQ(pos(0, true), 56);
  // twice as far down and 7 px more, until it's freed
  script_to(
      0, BYTES(OVERLAY_DO_BIND, OVERLAY_BIND_ADD | OVERLAY_Y, V16(7000), OVERLAY_DO_BIND, OVERLAY_BIND_MUL | OVERLAY_Y,
               V16(2000), OVERLAY_DO_WAIT, V16(100), OVERLAY_DO_BIND, OVERLAY_BIND_FREE | OVERLAY_Y));
  CHECK_EQ(pos(0, true), 107);
  sim_run(150);
  CHECK_EQ(pos(0, true), 50);
  // a BIND from the ESP and a script's take each other's place, and the ESP's stays when the script goes
  uint8_t m[OVERLAY_BIND_VALUE + 3];
  overlay_msg(K_OVERLAY_BIND, m, sim_overlay_bind(m, 0, OVERLAY_BIND_ADD | OVERLAY_X, BYTES(V16(5000))));
  CHECK_EQ(overlay_used(), used + SCRIPT_POOL + 16 + 4);
  script_to(0, BYTES(OVERLAY_DO_BIND, OVERLAY_BIND_ADD | OVERLAY_X, V16(9000)));
  CHECK_EQ(pos(0, false), 109);
  CHECK_EQ(overlay_used(), used + SCRIPT_POOL + 5);
  overlay_msg(K_OVERLAY_BIND, m, sim_overlay_bind(m, 0, OVERLAY_BIND_ADD | OVERLAY_X, BYTES(V16(5000))));
  CHECK_EQ(pos(0, false), 105);
  script_to(0, BYTES(OVERLAY_DO_MARK, 1));
  CHECK_EQ(pos(0, false), 105);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {0, AT(0), AT(0), 0}, OVERLAY_SCRIPT_BYTES);
  CHECK_EQ(pos(0, false), 105);
  CHECK_EQ(overlay_used(), used + 4);
  // the ESP's can take the registers of the script the place has then, 0 without one
  overlay_msg(K_OVERLAY_BIND, m, sim_overlay_bind(m, 0, OVERLAY_BIND_SET | OVERLAY_Y, BYTES(REG(2))));
  CHECK_EQ(pos(0, true), 0);
  script_to(0, BYTES(OVERLAY_DO_SET, 2, V32(31000)));
  CHECK_EQ(pos(0, true), 31);
  // another place bound by a SEND, kept there on its own
  place(1, 0, 10, 10, 0, 0);
  script_to(0, BYTES(OVERLAY_DO_SEND, 1, K_OVERLAY_BIND - K_OVERLAY_FRAMES, OVERLAY_BIND_VALUE + 3, 0,
                     OVERLAY_BIND_ADD | OVERLAY_X, V16(4000), 0));
  CHECK_EQ(pos(1, false), 14);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  CHECK_EQ(pos(1, false), 14);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 1);
  CHECK_EQ(overlay_used(), used);
  // its frame bound to the time: a frame each 200 ms, round them, however long its frames are
  place(2, 0, 0, 100, 1, 0);
  script_to(2, BYTES(OVERLAY_DO_BIND, OVERLAY_BIND_SET | OVERLAY_BIND_FRAME,
                     X(OWN(OVERLAY_GET_MS), V16(200), OVERLAY_OP_DIV)));
  sim_run(100);
  CHECK(overlay_shows(2, 0));
  sim_run(150);
  CHECK(overlay_shows(2, 1));
  sim_run(400);
  CHECK(overlay_shows(2, 3));
  sim_run(200);
  CHECK(overlay_shows(2, 0));
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(overlay_used(), 0);
}

// Whether place p takes script s: it has the pool for it once it's all in
static bool takes(int p, const uint8_t *s, int n) {
  place(p, 0, 0, 0, p == 0, 0);
  unsigned was = overlay_used();
  script_to(p, s, n);
  return overlay_used() == was + SCRIPT_POOL + (unsigned) n;
}
#define TAKES(p, ...) takes(p, BYTES(__VA_ARGS__))

// Scripts that are dropped whole, and those cut short or that don't fit
static void test_script_bad(void) {
  dots();
  show_overlays();
  CHECK(TAKES(0, OVERLAY_DO_RATE, V16(50)));
  CHECK(!TAKES(1, OVERLAY_DO_RATE, V16(50)));  // frames on a place without its own
  CHECK(!TAKES(1, OVERLAY_DO_FRAME, V16(1)));
  CHECK(!TAKES(1, OVERLAY_DO_CLIP, V16(0), V16(1), OVERLAY_CLIP_NOW, WHOLE));
  CHECK(!TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_SET | OVERLAY_BIND_FRAME, V8(1)));
  CHECK(TAKES(0, OVERLAY_DO_BIND, OVERLAY_BIND_SET | OVERLAY_BIND_FRAME, V8(1)));
  CHECK(TAKES(1, OVERLAY_DO_ROUNDS, V16(1)));
  // going on at a step, or the end, only
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(5)));
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(8)));
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(0)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(1)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(9)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(256 + 5)));  // both its bytes
  CHECK(!TAKES(1, OVERLAY_DO_IF, REG(0), AT(1)));
  CHECK(TAKES(1, OVERLAY_DO_IF, REG(0), AT(4)));
  CHECK(!TAKES(1, OVERLAY_DO_GOTO, 0));  // cut short
  // values that can't be read or worked out
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_V16, 1));  // cut short
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_V8));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_VEXPR, V8(1)));  // without its end
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, X(V8(1))));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_VEXPR, OVERLAY_OP_END));  // nothing left
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, X(V8(1), V8(2))));                // two left
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, X(V8(1), OVERLAY_OP_ADD)));       // one to add
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, X(X(V8(1)))));                    // one inside another
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_OP_END));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_OP_ADD));  // on its own it has nothing to take
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, X(V8(1), OVERLAY_OPS)));
  uint8_t deep[2 + 2 * (OVERLAY_EXPR_STACK + 1) + OVERLAY_EXPR_STACK + 2] = {OVERLAY_DO_SET, 0, OVERLAY_VEXPR};
  for (unsigned k = OVERLAY_EXPR_STACK; k <= OVERLAY_EXPR_STACK + 1; k++) {  // as many as fit, and one more
    unsigned at = 3;
    for (unsigned i = 0; i < k; i++, at += 2)
      memcpy(deep + at, (uint8_t[]) {V8(1)}, 2);
    for (unsigned i = 1; i < k; i++)
      deep[at++] = OVERLAY_OP_ADD;
    deep[at++] = OVERLAY_OP_END;
    CHECK_EQ(takes(1, deep, (int) at), k == OVERLAY_EXPR_STACK);
  }
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, X(V8(0), OVERLAY_OP_PICK, 2, 1, 0, 2, 0)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, X(V8(0), OVERLAY_OP_PICK, 2, 1, 0, 2)));  // a list cut short
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, X(V8(0), OVERLAY_OP_PICK, 0)));           // or without numbers
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, X(V8(0), V8(1), OVERLAY_OP_CURVE, 0)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OWN(OVERLAY_GETS)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, GET(OVERLAY_X, OVERLAY_PLACES)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_OP_DIST, 0, OVERLAY_PLACES));
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_OP_DIST, 0, OVERLAY_SELF));
  // registers it hasn't
  CHECK(!TAKES(1, OVERLAY_DOS));
  CHECK(!TAKES(1, OVERLAY_DO_SET, OVERLAY_REGS, V16(1)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, SCRIPT_REGS, V16(1)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, REG(SCRIPT_REGS)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, REG(OVERLAY_REGS)));
  CHECK(TAKES(1, OVERLAY_DO_SET, SCRIPT_REGS - 1, REG(SCRIPT_REGS - 1)));
  CHECK(TAKES(1, OVERLAY_DO_SET, OVERLAY_REGS - 1, REG(OVERLAY_REGS - 1)));  // the shared ones are always there
  CHECK(!TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_SET | (OVERLAY_BIND_REG + SCRIPT_REGS), V8(1)));
  CHECK(TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_SET | (OVERLAY_BIND_REG + OVERLAY_OWN_REGS), V8(1)));
  // what can't be bound
  CHECK(TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_MUL | OVERLAY_DEPTH, V8(1)));
  CHECK(!TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_MUL | (OVERLAY_BIND_REG + 1), V8(1)));  // a register is only set
  CHECK(!TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_ADD | OVERLAY_BIND_ROW_SHIFT, V8(1)));
  CHECK(TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_PHASE, OWN(OVERLAY_GET_ROW)));
  CHECK(!TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_SET | OVERLAY_BIND_TARGETS, V8(1)));
  CHECK(!TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_SET | OVERLAY_X));  // no value
  CHECK(TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_FREE | OVERLAY_X));
  CHECK(TAKES(1, OVERLAY_DO_BIND, OVERLAY_BIND_FREE | OVERLAY_X, OVERLAY_DO_MARK, 1));  // a FREE has none
  // a GLIDE of one channel at least, no more than it has room for, each with its value
  CHECK(TAKES(1, OVERLAY_DO_GLIDE, 1, V8(0), OVERLAY_X, V8(1)));
  CHECK(!TAKES(1, OVERLAY_DO_GLIDE, 0, V8(0)));
  CHECK(!TAKES(1, OVERLAY_DO_GLIDE, OVERLAY_GLIDE_STEP + 1, V8(0)));
  CHECK(!TAKES(1, OVERLAY_DO_GLIDE, 2, V8(0), OVERLAY_X, V8(1)));
  CHECK(!TAKES(1, OVERLAY_DO_GLIDE, 1, V8(0), OVERLAY_X));
  // shapes: runs of three, none wider than nothing, none bottom above top
  CHECK(TAKES(1, OVERLAY_DO_SHAPE, 0));
  CHECK(TAKES(1, OVERLAY_DO_SHAPE, 3, 4, 0, 0, 0, 5, 0));
  CHECK(TAKES(1, OVERLAY_DO_SHAPE, 6, 4, 0, 0, 0, 5, 0, 0, 0, 9, 0, 9, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SHAPE, 2, 4, 0, 0, 0));
  CHECK(TAKES(1, OVERLAY_DO_SOFT, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK(TAKES(1, OVERLAY_DO_SOFT, 3, 0, 0, 0, 12, 0, OVERLAY_BOX_MAX & 255, OVERLAY_BOX_MAX >> 8));
  CHECK(!TAKES(1, OVERLAY_DO_SOFT, 0, 0, 0, 0, 0, 0, (OVERLAY_BOX_MAX + 1) & 255, (OVERLAY_BOX_MAX + 1) >> 8));
  CHECK(!TAKES(1, OVERLAY_DO_SOFT, 0, 0, 0, 0, 12, 0));           // cut short
  CHECK(!TAKES(1, OVERLAY_DO_SHAPE, 3, 0xFF, 0xFF, 0, 0, 5, 0));  // -1 wide
  CHECK(!TAKES(1, OVERLAY_DO_SHAPE, 3, 4, 0, 6, 0, 5, 0));        // bottom above top
  CHECK(!TAKES(1, OVERLAY_DO_SHAPE, 3, 4, 0, 0, 0, 5));           // cut short
  CHECK(!TAKES(0, OVERLAY_DO_CLIP, V16(0), V16(1), OVERLAY_CLIP_NEXT + 1, WHOLE));
  // what a SEND can't send, or put a value at
  CHECK(TAKES(1, TO(OVERLAY_X, V16(1))));
#define VIEW_SEND(p) OVERLAY_DO_SEND, p, K_OVERLAY_VIEW - K_OVERLAY_FRAMES, OVERLAY_VIEW_LEN, 0, 1, 1
  CHECK(TAKES(1, VIEW_SEND(OVERLAY_SELF), 0));
  CHECK(TAKES(1, VIEW_SEND(3), 0));
  CHECK(!TAKES(1, VIEW_SEND(OVERLAY_PLACES), 0));
  CHECK(
      !TAKES(1, OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_VIEW - K_OVERLAY_FRAMES, OVERLAY_VIEW_LEN + 1, 0, 1, 1, 0, 0));
  CHECK(TAKES(1, VIEW_SEND(OVERLAY_SELF), 1, 1, V16(1)));
  CHECK(!TAKES(1, VIEW_SEND(OVERLAY_SELF), 1, 0, V16(1)));  // over its place
  CHECK(!TAKES(1, VIEW_SEND(OVERLAY_SELF), 1, 2, V16(1)));  // past its end
  CHECK(!TAKES(1, OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_PLACE - K_OVERLAY_FRAMES, OVERLAY_PLACE_LEN, 0, 0, 0, 0, 0,
               0, 0, 0, 0, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_SCRIPT - K_OVERLAY_FRAMES, 3, 0, 0, 0, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_GONE - K_OVERLAY_FRAMES, 2, 0, 0, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_MARK - K_OVERLAY_FRAMES, 2, 0, 0, 0));
  uint8_t s[] = {TO(OVERLAY_X, V16(1))};
  for (int n = OVERLAY_GLIDE_HEAD; n <= OVERLAY_GLIDE_HEAD + 2 * OVERLAY_GLIDE_EACH; n++) {  // a channel and its value
    uint8_t g[32] = {OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_GLIDE - K_OVERLAY_FRAMES, (uint8_t) n};
    CHECK_EQ(takes(1, g, 4 + n + 1), (n - OVERLAY_GLIDE_HEAD) % OVERLAY_GLIDE_EACH == 0 && n > OVERLAY_GLIDE_HEAD);
  }
  for (int at = 0; at < (int) sizeof s; at++) {  // the value over the place, in the message, past its end
    s[12] = (uint8_t) at;
    CHECK_EQ(takes(1, s, sizeof s), at >= 1 && at <= OVERLAY_GLIDE_HEAD + OVERLAY_GLIDE_EACH - 2);
  }
  // the bytes in order, and no more than OVERLAY_SCRIPT_MAX
  place(1, 0, 0, 0, 0, 0);
  unsigned was = overlay_used();
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(6), 2, OVERLAY_DO_SET, 0}, 8);
  CHECK_EQ(overlay_used(), was + OVERLAY_SCRIPT_HEAD + 2 * 4 + 6);             // with the registers it says
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(3), AT(6), 2, V16(1)}, 9);  // a byte on
  CHECK_EQ(overlay_used(), was);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(2), AT(6), 2, V16(1), OVERLAY_DO_MARK, 1}, 11);  // no first
  CHECK_EQ(overlay_used(), was);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(6), 2, OVERLAY_DO_SET, 0}, 8);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(2), AT(7), 2, V16(1), OVERLAY_DO_MARK, 1}, 11);  // another len
  CHECK_EQ(overlay_used(), was);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(6), 2, OVERLAY_DO_SET, 0}, 8);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(2), AT(6), 3, V16(1), OVERLAY_DO_MARK, 1}, 11);  // other registers
  CHECK_EQ(overlay_used(), was);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(7), 2, OVERLAY_DO_SET, 0}, 8);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(2), AT(7), 2, V16(1), OVERLAY_DO_MARK, 1}, 11);
  CHECK_EQ(overlay_used(), was + OVERLAY_SCRIPT_HEAD + 2 * 4 + 7);
  CHECK_EQ(overlay_mark(), 1 | 1 << 8);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(2), OVERLAY_OWN_REGS + 1, OVERLAY_DO_MARK, 1}, 8);
  CHECK_EQ(overlay_used(), was);  // more registers than there are
  CHECK(TAKES(1, OVERLAY_DO_WAIT, V16(1000), TO(OVERLAY_X, V16(5))));
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(0), 0}, OVERLAY_SCRIPT_BYTES);  // none, the one it runs ends
  sim_run(1100);
  CHECK(overlay_used() == was && pos(1, false) == 0);
  uint8_t big[OVERLAY_SCRIPT_MAX + 1];
  int sets = (OVERLAY_SCRIPT_MAX - 4) / 4;
  for (int i = 0; i < sets; i++)
    memcpy(big + 4 * i, (uint8_t[]) {OVERLAY_DO_SET, 0, V16(1)}, 4);
  memcpy(big + 4 * sets, (uint8_t[]) {OVERLAY_DO_MARK, 1, OVERLAY_DO_MARK, 2}, 4);
  CHECK(takes(1, big, OVERLAY_SCRIPT_MAX));
  memcpy(big + 4 * sets, (uint8_t[]) {OVERLAY_DO_MARK, 1, OVERLAY_DO_SET, 0, REG(0)}, 5);
  CHECK(!takes(1, big, OVERLAY_SCRIPT_MAX + 1));
  // no room in the pool: the place stays without it
  overlay_clear_msg(OVERLAY_ALL);
  dot_frames(true);
  uint8_t d[OVERLAY_POOL];
  uint32_t n = overlay_records(d, 5);
  n = pad_records(d, n, OVERLAY_POOL - AW / 2 * AH - SCRIPT_POOL - 2);  // room for 2 bytes to 8
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK(!TAKES(1, OVERLAY_DO_MARK, 1, OVERLAY_DO_MARK, 2, OVERLAY_DO_MARK, 3, OVERLAY_DO_MARK, 4, OVERLAY_DO_MARK, 5));
  CHECK_EQ(pos(1, false), 0);
  CHECK(TAKES(1, OVERLAY_DO_MARK, 1));
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// OVERLAY_RECORD_MORE: a frame longer than OVERLAY_DELAY_MAX, not a frame of its own
static uint32_t more_records(uint8_t *d, bool first, bool last) {
  uint32_t n = 0;
  for (int f = 0; f <= 4; f++) {
    n += plink_overlay_record(f ? &an[f - 1][0][0] : NULL, &an[f % 4][0][0], AW, AH, (uint8_t) (5 + f), d + n);
    if ((f == 0 && first) || (f == 1 && !first) || (f == 4 && last)) {
      n += plink_overlay_head(d + n, 10, OVERLAY_RECORD_MORE, 0, 0, 0, 0);
    }
  }
  return n;
}

static void test_script_more(void) {
  no_overlays();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = more_records(d, false, false);  // frame 1 160 ms
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 40, 0, 1, 0);
  show_overlays();
  script_to(1, BYTES(OVERLAY_DO_FRAME, V16(6)));  // 2 of 4, not 1 of 5
  CHECK(overlay_shows(1, 2));
  sim_run(55);
  CHECK(overlay_shows(0, 1));
  sim_run(150);  // 205
  CHECK(overlay_shows(0, 1));
  sim_run(10);  // 215
  CHECK(overlay_shows(0, 2));
  // a clip of frame 1 alone, 160 ms a round
  script_to(1, BYTES(OVERLAY_DO_CLIP, V16(1), V16(1), OVERLAY_CLIP_NOW, WHOLE, OVERLAY_DO_ROUNDS, V16(3),
                     OVERLAY_DO_MARK, 7));
  CHECK(overlay_shows(1, 1));
  sim_run(470);
  CHECK(overlay_shows(1, 1));
  CHECK_EQ(overlay_mark(), -1);
  sim_run(20);
  CHECK_EQ(overlay_mark(), 1 | 7 << 8);
  CHECK(overlay_shows(1, 1));
  // the first frame longer each time round, from the second record
  overlay_clear_msg(OVERLAY_ALL);
  n = more_records(d, true, false);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 0, 0, 0);
  sim_run(140);
  CHECK(overlay_shows(0, 0));
  sim_run(20);  // 160, 1 at 150
  CHECK(overlay_shows(0, 1));
  sim_run(380);  // 540, 0 at 360 for 90 + 100
  CHECK(overlay_shows(0, 0));
  sim_run(20);
  CHECK(overlay_shows(0, 1));
  // not the last
  overlay_clear_msg(OVERLAY_ALL);
  n = more_records(d, false, true);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_BAD);
  CHECK_EQ(overlay_used(), 0);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// What the checks above don't go through: a script that ends the last of the places, a place that goes for the
// room its own script's step takes when its round is over, shared registers a place on its way out no longer sets,
// a rate on a place whose slot has no frames to play, and one more number than a value's operations can hold
static void test_script_edges(void) {
  uint32_t n = dots();
  show_overlays();
  place(OVERLAY_PLACES - 1, 0, 10, 10, 0, 0);
  script_to(OVERLAY_PLACES - 1, BYTES(OVERLAY_DO_END, V8(0), 7));
  sim_run(100);
  CHECK_EQ(overlay_gone(), (OVERLAY_PLACES - 1) | 7 << 8);
  // its own going is the only room there is for what its script binds: slot 1 padded with empty records
  static uint8_t d2[OVERLAY_POOL];
  uint32_t frame = AW / 2 * AH, n2 = overlay_records(d2, 10), slot0 = overlay_used();
  place(0, 0, 0, 0, 1, 0);
  script_to(0, BYTES(OVERLAY_DO_ROUNDS, V16(1), OVERLAY_DO_SEND, OVERLAY_SELF, K_OVERLAY_BIND - K_OVERLAY_FRAMES,
                     OVERLAY_BIND_VALUE + 5, 0, OVERLAY_BIND_ADD | OVERLAY_X, V32(5000), 0));
  n2 = pad_records(d2, n2, OVERLAY_POOL - overlay_used() - frame);
  CHECK_EQ(status(overlay_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  CHECK(OVERLAY_POOL - overlay_used() < OVERLAY_RECORD_HEAD);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0 | 5000 << 8);
  sim_run(600);  // a round of its frames is 260 ms
  CHECK(pos(0, false) == INT16_MIN);
  CHECK_EQ(overlay_used(), slot0 + frame + n2);
  overlay_clear_msg(1);
  // a place on its way out sets none of the shared registers, the ones that stay see what they had
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 0, 60, 0, 0);
  script_to(0, BYTES(OVERLAY_DO_SET, 8, V16(42)));
  uint8_t m[OVERLAY_BIND_VALUE + 8];
  overlay_msg(K_OVERLAY_BIND, m, sim_overlay_bind(m, 0, OVERLAY_BIND_SET | (OVERLAY_BIND_REG + 9), BYTES(V16(7))));
  sim_run(100);
  script_to(1, BYTES(TO(OVERLAY_X, REG(8)), TO(OVERLAY_Y, REG(9))));
  CHECK(pos(1, false) == 42 && pos(1, true) == 7);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0 | 2000 << 8);  // what it bound is still worked out each step
  script_to(1, BYTES(OVERLAY_DO_SET, 8, V16(1), OVERLAY_DO_SET, 9, V16(2)));
  script_to(0, BYTES(OVERLAY_DO_SET, 8, V16(42)));
  sim_run(100);
  script_to(1, BYTES(TO(OVERLAY_X, REG(8)), TO(OVERLAY_Y, REG(9))));
  CHECK(pos(0, false) != INT16_MIN && pos(1, false) == 1 && pos(1, true) == 2);
  // nobody's place: nothing to read there, and what's sent there goes nowhere
  place(0, 0, 30, 0, 0, 0);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0,
                      X(GET(OVERLAY_GET_HERE, OVERLAY_NOBODY), V8(100), OVERLAY_OP_MUL, GET(OVERLAY_X, OVERLAY_NOBODY),
                        OVERLAY_OP_ADD, V8(9), OVERLAY_OP_ADD)),
           9);
  script_to(0, BYTES(TO_PLACE(OVERLAY_NOBODY, OVERLAY_X, V16(77)), TO(OVERLAY_Y, V16(5))));
  CHECK(pos(0, false) == 9 && pos(0, true) == 5);
  // a value with as many numbers waiting as it can hold, and an operation on the last of them alone
#define ONES V8(1), V8(1), V8(1), V8(1), V8(1), V8(1), V8(1), V8(1), V8(1), V8(1), V8(1)
#define ADDS \
  OVERLAY_OP_ADD, OVERLAY_OP_ADD, OVERLAY_OP_ADD, OVERLAY_OP_ADD, OVERLAY_OP_ADD, OVERLAY_OP_ADD, OVERLAY_OP_ADD, \
      OVERLAY_OP_ADD, OVERLAY_OP_ADD, OVERLAY_OP_ADD, OVERLAY_OP_ADD
  _Static_assert(OVERLAY_EXPR_STACK == 12, "eleven ones and a five");
  CHECK_EQ(WORKED_OUT(SET(0, ONES, V8(5), OVERLAY_OP_NEG, ADDS)), 6);
  // a rate on a place with a frame of its own that never changes leaves its rounds as long as they were: 200 ms
  no_overlays();
  uint8_t f[AH][AW / 2], one[64];
  memset(f, 0xFF, sizeof f);
  f[1][1] = 0x12;
  n = plink_overlay_record(NULL, &f[0][0], AW, AH, 20, one);
  CHECK_EQ(status(overlay_send(0, AW, n, one, plink_crc32(0, one, n))), PIC_DONE);
  place(0, 0, 0, 0, 1, 0);
  show_overlays();
  script_to(0,
            BYTES(OVERLAY_DO_RATE, V16(0), OVERLAY_DO_RATE, V16(100), OVERLAY_DO_ROUNDS, V16(1), OVERLAY_DO_MARK, 3));
  sim_run(300);
  CHECK_EQ(overlay_mark(), 0 | 3 << 8);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

const check_test_t OVERLAY_SCRIPT_TESTS[] = {
    {"overlay_script_maths", test_script_maths},
    {"overlay_script_waves", test_script_waves},
    {"overlay_script_looks", test_script_looks},
    {"overlay_script_clips", test_script_clips},
    {"overlay_script_clip_size", test_script_clip_size},
    {"overlay_script_waits", test_script_waits},
    {"overlay_script_sends", test_script_sends},
    {"overlay_script_binds", test_script_binds},
    {"overlay_script_bad", test_script_bad},
    {"overlay_script_more", test_script_more},
    {"overlay_script_edges", test_script_edges},
    {0},
};
