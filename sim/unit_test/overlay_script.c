// app/overlay_script.c: places' scripts, the frames they play, what they work out and wait for
#include "unit_test.h"

#define V32(v) \
  OVERLAY_V32, (uint8_t) (v), (uint8_t) ((uint32_t) (v) >> 8), (uint8_t) ((uint32_t) (v) >> 16), \
      (uint8_t) ((uint32_t) (v) >> 24)
// SEND of a GLIDE of channel ch to the value v at once
#define TO(ch, ...) \
  OVERLAY_DO_SEND, K_OVERLAY_GLIDE - K_OVERLAY_FRAMES, OVERLAY_GLIDE_HEAD + 2, 0, 1u << (ch), 0, 0, 0, 0, 0, 1, \
      OVERLAY_GLIDE_HEAD, __VA_ARGS__
#define BYTES(...) (const uint8_t[]){__VA_ARGS__}, (int) sizeof((const uint8_t[]) {__VA_ARGS__})

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
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(7), OVERLAY_DO_MUL, 0, V16(6)), 42);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(7), OVERLAY_DO_SUB, 0, V16(10)), -3);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(100), OVERLAY_DO_DIV, 0, V16(7)), 14);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(-7), OVERLAY_DO_MOD, 0, V16(3)), -1);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(5), OVERLAY_DO_DIV, 0, V16(0)), 0);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(5), OVERLAY_DO_MOD, 0, V16(0)), 0);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V32(INT32_MIN), OVERLAY_DO_DIV, 0, V16(-1)), 0);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V32(INT32_MIN), OVERLAY_DO_MOD, 0, V16(-1)), 0);
  // 32 bits, wrapping round, by 65536 to fit x
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_ABS, 0, V32(INT32_MIN), OVERLAY_DO_DIV, 0, V32(65536)), 32767);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V32(INT32_MAX), OVERLAY_DO_ADD, 0, V16(1), OVERLAY_DO_DIV, 0, V32(65536)),
           -32768);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V32(-123456), OVERLAY_DO_DIV, 0, V16(1000)), -123);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_ABS, 0, V16(-9)), 9);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(5), OVERLAY_DO_MIN, 0, V16(3)), 3);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 0, V16(5), OVERLAY_DO_MAX, 0, V16(9)), 9);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 3, V16(9), OVERLAY_DO_SET, 0, REG(3), OVERLAY_DO_ADD, 0, REG(0)), 18);
  // sin and cos of any angle, rounded
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SIN, 0, V16(30), V16(1000)), 500);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_COS, 0, V16(60), V16(1000)), 500);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SIN, 0, V16(-90), V16(100)), -100);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_COS, 0, V16(540), V16(100)), -100);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SIN, 0, V16(390), REG(5)), 0);
  // a number of a list, the first below it and the last past it
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_SET, 1, V16(2), OVERLAY_DO_PICK, 0, REG(1), 4, 10, 0, 20, 0, 0xD0, 0xFF, 40, 0), -48);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_PICK, 0, V16(-3), 3, 10, 0, 20, 0, 30, 0), 10);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_PICK, 0, V32(100000), 3, 10, 0, 20, 0, 30, 0), 30);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_PICK, 1, V16(0), 1, 7, 0, OVERLAY_DO_SET, 0, REG(1)), 7);
  // random, both ends in: the least and most of 50, as 10 * least + most
  code_t c = {0};
  PUT(&c, OVERLAY_DO_SET, 1, V16(100), OVERLAY_DO_SET, 2, V16(-100));
  label(&c, 0);
  PUT(&c, OVERLAY_DO_RAND, 4, V16(1), V16(3), OVERLAY_DO_MIN, 1, REG(4), OVERLAY_DO_MAX, 2, REG(4));
  PUT(&c, OVERLAY_DO_ADD, 3, V16(1), OVERLAY_DO_IF, REG(3), OVERLAY_IF_LT, V16(50));
  go(&c, 0);
  PUT(&c, OVERLAY_DO_SET, 0, REG(1), OVERLAY_DO_MUL, 0, V16(10), OVERLAY_DO_ADD, 0, REG(2));
  CHECK_EQ(worked_out(done(&c), c.n), 13);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_RAND, 0, V16(20), V16(10)), 20);  // the first if the second is less
  // a GOTO past the first 256 bytes
  c = (code_t) {0};
  PUT(&c, OVERLAY_DO_GOTO);
  go(&c, 0);
  for (int i = 0; i < 70; i++)
    PUT(&c, OVERLAY_DO_SET, 0, V16(1));
  label(&c, 0);
  PUT(&c, OVERLAY_DO_ADD, 0, V16(41));
  CHECK(c.label[0] > 255);
  CHECK_EQ(worked_out(done(&c), c.n), 41);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_RAND, 0, V32(INT32_MIN), V32(INT32_MAX), OVERLAY_DO_SET, 0, V16(1)), 1);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// GET, DIST and ANGLE: where the others are and how they look
static void test_script_looks(void) {
  dots();
  show_overlays();
  place(1, 0, 100, 50, 0, 0);
  place(2, 0, 130, 90, 0, 0);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_X, 1), 100);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_Y, 1), 50);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_X, OVERLAY_SELF), AWAY);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_W, OVERLAY_SELF), AW * 2);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_H, 1), AH * 2);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_HERE, 1), 1);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_HERE, 5), 0);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_X, 5), 0);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_SIZE, 1), 100);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_OPACITY, 1), 100);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_LIGHT, 1), 100);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_FRAME, 1), 0);  // no script
  uint8_t m[OVERLAY_GLIDE_HEAD + 6];
  overlay_msg(K_OVERLAY_GLIDE, m,
              sim_overlay_glide(m, 1, 1u << OVERLAY_TURN | 1u << OVERLAY_SIZE | 1u << OVERLAY_LIGHT, 0, 0,
                                (int[]) {45, 200, 30}));
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_TURN, 1), 45);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_SIZE, 1), 200);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_LIGHT, 1), 30);
  // from place 1's middle to 2's is 30 right and 40 down
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_DIST, 0, 1, 2), 50);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_DIST, 0, 2, 1), 50);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_DIST, 0, 1, 5), 0);
  int a = WORKED_OUT(OVERLAY_DO_ANGLE, 0, 1, 2);
  CHECK(a >= 52 && a <= 54);
  place(2, 0, 100, 150, 0, 0);  // below
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_ANGLE, 0, 1, 2), 90);
  place(2, 0, -2000, 50, 0, 0);  // far to the left
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_ANGLE, 0, 1, 2), 180);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_DIST, 0, 1, 2), 2100);
  place(2, 0, 100, 0, 0, 0);  // above
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_ANGLE, 0, 1, 2), 270);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_ANGLE, 0, 1, 1), 0);
  // what another's script is at
  place(3, 0, 0, 100, 1, 0);
  script_to(3, BYTES(OVERLAY_DO_FRAME, V16(2), OVERLAY_DO_RATE, V16(0)));
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_FRAME, 3), 2);
  int ms = WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_MS, 3);  // the one before took 500
  CHECK(ms >= 495 && ms <= 505);
  CHECK_EQ(WORKED_OUT(OVERLAY_DO_GET, 0, OVERLAY_GET_ROUNDS, 3), 0);  // held
  int self = WORKED_OUT(OVERLAY_DO_WAIT, V16(200), OVERLAY_DO_GET, 0, OVERLAY_GET_MS, OVERLAY_SELF);
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
  script_to(0, BYTES(OVERLAY_DO_CLIP, V16(0), V16(3), OVERLAY_CLIP_NOW, AT(w), AT(h), OVERLAY_DO_GET, 0, (uint8_t) g,
                     OVERLAY_SELF, TO(OVERLAY_X, REG(0))));
  return pos(0, false);
}

// A CLIP's size: the box is what it shows of the frame, mirrored in place, and that's what GET reads
static void test_script_clip_size(void) {
  dots();
  show_overlays();
  CHECK_EQ(clip_reads(3, 2, OVERLAY_GET_W), 6);
  CHECK_EQ(clip_reads(3, 2, OVERLAY_GET_H), 4);
  CHECK_EQ(clip_reads(AW + 1, 0, OVERLAY_GET_W), AW * 2);  // no more than the frame
  CHECK_EQ(clip_reads(0, 0, OVERLAY_GET_H), AH * 2);
  // frame 0's dot is colours 1 and 2 at 2 and 3 across, 1 down
  place(0, 0, 10, 10, 1, 0);
  script_to(0, BYTES(OVERLAY_DO_CLIP, V16(0), V16(0), OVERLAY_CLIP_NOW, AT(AW / 2), AT(AH), OVERLAY_DO_RATE, V16(0)));
  sim_run(50);
  CHECK(!sim_stale());
  overlay_look_t l;
  int px[AW * AH * 4];
  CHECK(drawn(0, &l, px) && l.box.x1 == 10 && lv_area_get_width(&l.box) == AW && lv_area_get_height(&l.box) == AH * 2);
  CHECK(px[2 * AW + 4] == 1 && px[2 * AW + 6] == 2);
  overlay_msg(K_OVERLAY_FLIP, (uint8_t[]) {0, OVERLAY_FLIP_X}, 2);
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
  uint8_t m[OVERLAY_DRIFT_HEAD + 2];
  overlay_msg(K_OVERLAY_DRIFT, m, sim_overlay_drift(m, 1, 1u << OVERLAY_X, (int[]) {100}));
  code_t c = {0};
  label(&c, 0);
  PUT(&c, OVERLAY_DO_GET, 0, OVERLAY_GET_X, 1, OVERLAY_DO_UNTIL, REG(0), OVERLAY_IF_GE, V16(50));
  go(&c, 0);
  PUT(&c, TO(OVERLAY_Y, V16(77)));
  send(&c, 0);
  sim_run(400);
  CHECK_EQ(pos(0, true), 0);
  sim_run(200);
  CHECK_EQ(pos(0, true), 77);
  // down from 3 to 0, and on past the end
  c = (code_t) {0};
  PUT(&c, OVERLAY_DO_SET, 0, V16(3));
  label(&c, 0);
  PUT(&c, OVERLAY_DO_SUB, 0, V16(1), OVERLAY_DO_ADD, 1, V16(10), OVERLAY_DO_IF, REG(0), OVERLAY_IF_GT, V16(0));
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
  PUT(&c, OVERLAY_DO_ADD, 0, V16(1), TO(OVERLAY_X, REG(0)), OVERLAY_DO_GOTO);
  go(&c, 0);
  send(&c, 0);
  CHECK_EQ(pos(0, false), OVERLAY_SCRIPT_STEPS / 3 + 1);
  sim_run(500);
  CHECK(pos(0, false) > 5 * OVERLAY_SCRIPT_STEPS / 3);
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// SEND with values from registers, the shared registers, END and the GONE and MARK it answers
static void test_script_sends(void) {
  uint32_t n = dots();
  show_overlays();
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 0, 60, 0, 0);
  // a drift of a register's hundredths of a px a second, a FLIP to itself whatever place the message has
  script_to(0, BYTES(OVERLAY_DO_SET, 2, V16(2000), OVERLAY_DO_SEND, K_OVERLAY_DRIFT - K_OVERLAY_FRAMES, 4, 0,
                     1u << OVERLAY_X, 0, 0, 1, OVERLAY_DRIFT_HEAD, REG(2), OVERLAY_DO_SEND,
                     K_OVERLAY_FLIP - K_OVERLAY_FRAMES, OVERLAY_FLIP_LEN, 5, OVERLAY_FLIP_X, 0));
  sim_run(1000);
  int x = pos(0, false);
  CHECK(x >= 19 && x <= 21);
  overlay_look_t l;
  CHECK(overlay_look(0, &l) && l.flip == OVERLAY_FLIP_X);
  CHECK(!overlay_look(5, &l));
  // a TINT of red: its dark colours from black to red
  script_to(0, BYTES(OVERLAY_DO_SEND, K_OVERLAY_TINT - K_OVERLAY_FRAMES, OVERLAY_TINT_LEN, 0, 1, 0xFF, 0, 0, 0));
  CHECK(overlay_look(0, &l) && LV_COLOR_GET_R(l.pal[1]) && !LV_COLOR_GET_G(l.pal[1]) && !LV_COLOR_GET_B(l.pal[1]));
  // a MOVE glides x and y together, over a register's ms, and at once with none
  script_to(1, BYTES(OVERLAY_DO_SET, 3, V16(400), OVERLAY_DO_MOVE, V16(80), V16(-40), REG(3), OVERLAY_EASE_IN));
  sim_run(200);
  CHECK(pos(1, false) > 0 && pos(1, false) < 40 && pos(1, true) > 20 && pos(1, true) < 60);  // slow at first
  sim_run(250);
  CHECK_EQ(pos(1, false), 80);
  CHECK_EQ(pos(1, true), -40);
  script_to(1, BYTES(OVERLAY_DO_MOVE, V16(0), V16(60), V16(0), 0));
  CHECK_EQ(pos(1, false), 0);
  CHECK_EQ(pos(1, true), 60);
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

// Whether place p takes script s: it has the pool for it once it's all in
static bool takes(int p, const uint8_t *s, int n) {
  place(p, 0, 0, 0, p == 0, 0);
  unsigned was = overlay_used();
  script_to(p, s, n);
  return overlay_used() == was + OVERLAY_SCRIPT_POOL + (unsigned) n;
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
  CHECK(TAKES(1, OVERLAY_DO_ROUNDS, V16(1)));
  // going on at a step, or the end, only
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(5)));
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(8)));
  CHECK(TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(0)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(1)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(9)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, V16(1), OVERLAY_DO_GOTO, AT(256 + 5)));  // both its bytes
  CHECK(!TAKES(1, OVERLAY_DO_IF, REG(0), OVERLAY_IF_EQ, V16(0), AT(2)));
  CHECK(!TAKES(1, OVERLAY_DO_UNTIL, REG(0), OVERLAY_IF_EQ, V16(0), AT(10)));
  CHECK(TAKES(1, OVERLAY_DO_UNTIL, REG(0), OVERLAY_IF_EQ, V16(0), AT(0)));
  CHECK(!TAKES(1, OVERLAY_DO_GOTO, 0));  // cut short
  // steps that can't be read
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, OVERLAY_V16, 1));  // cut short
  CHECK(TAKES(1, OVERLAY_DO_PICK, 0, V16(0), 2, 1, 0, 2, 0));
  CHECK(!TAKES(1, OVERLAY_DO_PICK, 0, V16(0), 2, 1, 0, 2));  // a list cut short
  CHECK(!TAKES(1, OVERLAY_DO_PICK, 0, V16(0), 0));           // or without numbers
  CHECK(!TAKES(1, OVERLAY_DOS));
  CHECK(!TAKES(1, OVERLAY_DO_SET, OVERLAY_REGS, V16(1)));
  CHECK(!TAKES(1, OVERLAY_DO_SET, 0, REG(OVERLAY_REGS)));
  CHECK(TAKES(1, OVERLAY_DO_SET, OVERLAY_REGS - 1, REG(OVERLAY_REGS - 1)));
  CHECK(!TAKES(1, OVERLAY_DO_GET, 0, OVERLAY_GETS, 1));
  CHECK(!TAKES(1, OVERLAY_DO_GET, 0, OVERLAY_GET_X, OVERLAY_PLACES));
  CHECK(!TAKES(1, OVERLAY_DO_IF, REG(0), OVERLAY_IF_GT + 1, V16(0), AT(0)));
  CHECK(!TAKES(0, OVERLAY_DO_CLIP, V16(0), V16(1), OVERLAY_CLIP_NEXT + 1, WHOLE));
  // what a SEND can't send, or put a value at
  CHECK(TAKES(1, TO(OVERLAY_X, V16(1))));
  CHECK(TAKES(1, OVERLAY_DO_SEND, K_OVERLAY_FLIP - K_OVERLAY_FRAMES, OVERLAY_FLIP_LEN, 0, 1, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SEND, K_OVERLAY_FLIP - K_OVERLAY_FRAMES, OVERLAY_FLIP_LEN + 1, 0, 1, 0, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SEND, K_OVERLAY_FLIP - K_OVERLAY_FRAMES, OVERLAY_FLIP_LEN, 0, 1, 1, 0, V16(1)));
  CHECK(
      !TAKES(1, OVERLAY_DO_SEND, K_OVERLAY_PLACE - K_OVERLAY_FRAMES, OVERLAY_PLACE_LEN, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SEND, K_OVERLAY_SCRIPT - K_OVERLAY_FRAMES, 3, 0, 0, 0, 0));
  CHECK(!TAKES(1, OVERLAY_DO_SEND, K_OVERLAY_GONE - K_OVERLAY_FRAMES, 2, 0, 0, 0));
  CHECK(TAKES(1, OVERLAY_DO_MOVE, V16(1), V16(2), V16(3), OVERLAY_EASE_IN | OVERLAY_EASE_OUT));
  CHECK(!TAKES(1, OVERLAY_DO_MOVE, V16(1), V16(2), V16(3), 4));  // no such ease
  CHECK(!TAKES(1, OVERLAY_DO_SEND, K_OVERLAY_MARK - K_OVERLAY_FRAMES, 2, 0, 0, 0));
  uint8_t s[] = {TO(OVERLAY_X, V16(1))};
  for (int at = 0; at < OVERLAY_GLIDE_HEAD + 3; at++) {  // the value at the place, the channels, past the end
    s[11] = (uint8_t) at;
    CHECK_EQ(takes(1, s, sizeof s), at >= 2 && at <= OVERLAY_GLIDE_HEAD);
  }
  s[1] = K_OVERLAY_DRIFT - K_OVERLAY_FRAMES;  // its channels no more than DRIFT takes
  s[11] = OVERLAY_GLIDE_HEAD;
  CHECK(!takes(1, s, sizeof s));
  // the bytes in order, and no more than OVERLAY_SCRIPT_MAX
  place(1, 0, 0, 0, 0, 0);
  unsigned was = overlay_used();
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(6), OVERLAY_DO_SET, 0}, 7);
  CHECK_EQ(overlay_used(), was + OVERLAY_SCRIPT_POOL + 6);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(3), AT(6), V16(1)}, 8);  // a byte on
  CHECK_EQ(overlay_used(), was);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(2), AT(6), V16(1), OVERLAY_DO_MARK, 1}, 10);  // no first
  CHECK_EQ(overlay_used(), was);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(6), OVERLAY_DO_SET, 0}, 7);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(2), AT(7), V16(1), OVERLAY_DO_MARK, 1}, 10);  // another len
  CHECK_EQ(overlay_used(), was);
  CHECK(TAKES(1, OVERLAY_DO_WAIT, V16(1000), TO(OVERLAY_X, V16(5))));
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {1, AT(0), AT(0)}, 5);  // none, the one it runs ends
  sim_run(1100);
  CHECK(overlay_used() == was && pos(1, false) == 0);
  uint8_t big[OVERLAY_SCRIPT_MAX + 1];
  int sets = (OVERLAY_SCRIPT_MAX - 4) / 4;
  for (int i = 0; i < sets; i++)
    memcpy(big + 4 * i, (uint8_t[]) {OVERLAY_DO_SET, 0, V16(1)}, 4);
  memcpy(big + 4 * sets, (uint8_t[]) {OVERLAY_DO_MARK, 1, OVERLAY_DO_MARK, 2}, 4);
  CHECK(takes(1, big, OVERLAY_SCRIPT_MAX));
  memcpy(big + 4 * sets, (uint8_t[]) {OVERLAY_DO_MARK, 1, OVERLAY_DO_ABS, 0, REG(0)}, 5);
  CHECK(!takes(1, big, OVERLAY_SCRIPT_MAX + 1));
  // no room in the pool: the place stays without it
  overlay_clear_msg(OVERLAY_ALL);
  dot_frames(true);
  uint8_t d[OVERLAY_POOL];
  uint32_t n = overlay_records(d, 5);
  uint32_t pad = (OVERLAY_POOL - AW / 2 * AH - n - OVERLAY_SCRIPT_POOL - 2) / 5 * 5;  // room for 2 bytes to 6
  for (uint32_t i = 0; i < pad; i += 5)
    memcpy(d + n + i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  CHECK_EQ(status(overlay_send(0, AW, n + pad, d, plink_crc32(0, d, n + pad))), PIC_DONE);
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
      memcpy(d + n, (uint8_t[]) {10, OVERLAY_RECORD_MORE, 0, 0, 0}, 5);
      n += 5;
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

const check_test_t OVERLAY_SCRIPT_TESTS[] = {
    {"overlay_script_maths", test_script_maths},
    {"overlay_script_looks", test_script_looks},
    {"overlay_script_clips", test_script_clips},
    {"overlay_script_clip_size", test_script_clip_size},
    {"overlay_script_waits", test_script_waits},
    {"overlay_script_sends", test_script_sends},
    {"overlay_script_bad", test_script_bad},
    {"overlay_script_more", test_script_more},
    {0},
};
