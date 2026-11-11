// app/overlay.c: the overlays' pool, its slots and places, and their frames
#include "unit_test.h"

// ---------------------------------------------------------------- the overlays
uint8_t an[4][AH][AW / 2];

// A dot that moves, then stays put for a frame. Or moves on each one.
void dot_frames(bool moves) {
  memset(an, 0xFF, sizeof an);
  an[0][1][1] = 0x12;
  an[1][2][2] = 0x34;
  an[2][2][moves ? 1 : 2] = 0x34;
  an[3][0][0] = 0x56;
}

// rgb()'s colours as an overlay has them, 15 clear
static const uint32_t *clear15(void) {
  static uint32_t pal[16];
  for (int i = 0; i < 15; i++)
    pal[i] = 0xFF000000 | rgb(i);
  return pal;
}

// OVERLAY_BEGIN of kind, w x h at scale, then the data in pieces of piece bytes
static int32_t send_kind(int kind, int slot, int w, int h, int scale, const uint32_t *pal, uint32_t len,
                         const uint8_t *data, unsigned piece) {
  uint8_t b[OVERLAY_BEGIN_LEN];
  int n = sim_overlay_begin(b, ++id, slot, kind, w, h, scale, len, pal, 0, 0);
  return rest(pic_msg(K_OVERLAY_BEGIN, b, n), data, len, piece, plink_crc32(0, data, len));
}

// An animated OVERLAY_BEGIN at scale 2, colour 15 clear, then the data
int32_t overlay_send(int slot, int w, uint32_t len, const uint8_t *data, uint32_t crc) {
  uint8_t h[OVERLAY_BEGIN_LEN];
  int n = sim_overlay_begin(h, ++id, slot, OVERLAY_ANIMATED, w, AH, 2, len, clear15(), 0, 0);
  return rest(pic_msg(K_OVERLAY_BEGIN, h, n), data, len, 5, crc);
}

void script_to(int p, const uint8_t *s, int n) {
  int at = 0;
  do {
    uint8_t m[PBULK_MSG_MAX] = {(uint8_t) p, AT(at), AT(n), SCRIPT_REGS};
    int k = LV_MIN(n - at, (int) sizeof m - OVERLAY_SCRIPT_BYTES);
    memcpy(m + OVERLAY_SCRIPT_BYTES, s + at, (size_t) k);
    overlay_msg(K_OVERLAY_SCRIPT, m, OVERLAY_SCRIPT_BYTES + k);
    at += k;
  } while (at < n);
}

// start frames in, as the ESP has a copy start
void place_in(int p, int slot, int x, int y, int own, int start, int fade) {
  uint8_t m[OVERLAY_PLACE_LEN];
  overlay_msg(K_OVERLAY_PLACE, m, sim_overlay_place(m, p, slot, x, y, own, fade));
  if (start)
    script_to(p, (uint8_t[]) {OVERLAY_DO_FRAME, V16(start)}, 4);
}

void place(int p, int slot, int x, int y, int own, int start) { place_in(p, slot, x, y, own, start, 0); }

void overlay_clear_msg(unsigned v) {
  uint8_t m[4];
  plink_put32(m, v);
  pic_msg(K_OVERLAY_CLEAR, m, 4);
}

lv_obj_t *overlays;

lv_obj_t *show_overlays(void) { return overlays = overlay_show(lv_scr_act()); }

void no_overlays(void) {
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

bool overlay_pos(int k, int16_t *x, int16_t *y) {
  overlay_look_t l;
  if (!overlay_look(k, &l))
    return false;
  *x = l.x;
  *y = l.y;
  return true;
}

int look_opa(int place) {
  overlay_look_t l;
  return overlay_look(place, &l) ? l.opa : -1;
}

bool placed(void) {
  overlay_look_t l;
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (overlay_look(k, &l))
      return true;
  return false;
}

#define BEHIND 0xFF00FF  // what the overlays are drawn over, in no test palette

// Place k's box as the layer draws it over BEHIND into buf, in strips of lines. False if it isn't drawn.
static bool draw_box(int k, overlay_look_t *l, lv_color_t *buf, int lines) {
  if (!overlays || !lv_obj_is_valid(overlays) || !overlay_look(k, l) || !(l->tiles & 1))
    return false;
  int w = lv_area_get_width(&l->box);
  for (int y = l->box.y1; y <= l->box.y2; y += lines) {
    lv_area_t a = {l->box.x1, y, l->box.x2, LV_MIN(y + lines - 1, l->box.y2)};
    sim_draw(overlays, &a, lv_color_hex(BEHIND), buf + (y - l->box.y1) * w);
  }
  return true;
}

// Place k's box as the layer draws it: each pixel's palette index, -1 where it's see-through, -2 for something
// between. False if it isn't drawn.
bool drawn(int k, overlay_look_t *l, int *px) {
  static lv_color_t buf[PIC_MAX_W * PIC_MAX_H];
  if (!draw_box(k, l, buf, PIC_MAX_H))
    return false;
  for (uint32_t i = 0; i < lv_area_get_size(&l->box); i++) {
    px[i] = buf[i].full == lv_color_hex(BEHIND).full ? -1 : -2;
    for (int c = 0; c < 16; c++)
      if (buf[i].full == lv_color_hex(rgb(c)).full)
        px[i] = c;
  }
  return true;
}

// Whether place k shows frame f of the AW x AH frames, drawn at 2x
bool overlay_shows(int k, int f) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  overlay_look_t l;
  if (!drawn(k, &l, px) || lv_area_get_width(&l.box) != AW * 2 || lv_area_get_height(&l.box) != AH * 2)
    return false;
  for (int y = 0; y < AH * 2; y++)
    for (int x = 0; x < AW * 2; x++) {
      uint8_t b = an[f][y / 2][x / 4], c = x / 2 & 1 ? b & 15 : b >> 4;
      if (px[y * AW * 2 + x] != (c == 15 ? -1 : c))
        return false;
    }
  return true;
}

// the records of the 4 test frames: frame 0 whole, the last record back to frame 0. Frame f shows for delay + f
uint32_t overlay_records(uint8_t *d, int delay) {
  uint32_t n = 0;
  for (int f = 0; f <= 4; f++)
    n += plink_overlay_record(f ? &an[f - 1][0][0] : NULL, &an[f % 4][0][0], AW, AH, (uint8_t) (delay + f), d + n);
  return n;
}

static void test_anim(void) {
  no_overlays();
  dot_frames(false);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK(!placed());  // not placed yet
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  CHECK(show_overlays());
  CHECK(!overlay_shows(0, 0));
  place(0, 0, 0, 10, 0, 0);
  CHECK(placed() && overlay_shows(0, 0));  // shown as it's placed
  sim_run(50);                             // the first frame for 50 ms, the next for 60, ...
  CHECK(overlay_shows(0, 1));
  sim_run(60);
  CHECK(overlay_shows(0, 2));  // same as frame 1, so a record with no rows
  sim_run(70);
  CHECK(overlay_shows(0, 3));
  sim_run(80);
  CHECK(overlay_shows(0, 0));  // and back to the start
  lv_obj_del(overlays);

  // a width that's odd, its rows as many bytes, then bad sizes and bad data
  CHECK_EQ(status(overlay_send(0, AW - 1, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  show_overlays();
  overlay_look_t l;
  CHECK(overlay_look(0, &l) && lv_area_get_width(&l.box) == 2 * (AW - 1));
  lv_obj_del(overlays);
  CHECK_EQ(status(overlay_send(0, 0, n, d, 0)), PIC_BAD_SIZE);
  CHECK(!placed());  // the slot's gone with it
  CHECK_EQ(status(overlay_send(0, PIC_MAX_W, n, d, 0)), PIC_BAD_SIZE);
  CHECK_EQ(status(overlay_send(0, AW, OVERLAY_POOL, d, 0)), PIC_TOO_BIG);
  CHECK_EQ(status(overlay_send(0, AW, UINT32_MAX, d, 0)), PIC_TOO_BIG);  // doesn't wrap round to fit
  uint8_t big[OVERLAY_BEGIN_LEN];                                        // a frame bigger than the pool, 15000 bytes
  int bn = sim_overlay_begin(big, ++id, 1, OVERLAY_ANIMATED, 300, 100, 1, 4, clear15(), 0, 0);
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn)), PIC_TOO_BIG);
  big[OVERLAY_BEGIN_KIND] = OVERLAY_FIELD + 1;  // no such kind
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn)), PIC_BAD);
  big[OVERLAY_BEGIN_KIND] = OVERLAY_ANIMATED;
  big[OVERLAY_BEGIN_SLOT] = OVERLAY_SLOTS;  // no such slot
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn)), PIC_BAD);
  CHECK_EQ(status(pic_msg(K_OVERLAY_BEGIN, big, bn - 1)), PIC_BAD);  // cut short
  uint8_t bad[sizeof d];
  memcpy(bad, d, n);
  bad[4] = AH + 1;
  CHECK_EQ(status(overlay_send(0, AW, n, bad, plink_crc32(0, bad, n))), PIC_BAD);
  CHECK_EQ(status(overlay_send(0, AW, n, d, 12345)), PIC_BAD_CRC);
  uint8_t five[5] = {0x11, 0x11, 0x11, 0x11, 0x11}, over[sizeof d + 5 + PDEFLATE_BOUND(5)];
  uint32_t on = plink_overlay_record(NULL, &an[0][0][0], AW, AH, 5, over);
  memcpy(over + on, (uint8_t[]) {5, 0, 0, 4, 1}, 5);
  on += 5 + pdeflate(five, 5, 5, 5, over + on + 5, PDEFLATE_BOUND(5));  // then 5 bytes for a rectangle of 4
  CHECK_EQ(status(overlay_send(0, AW, on, over, plink_crc32(0, over, on))), PIC_BAD);
  uint32_t part = plink_overlay_record(NULL, &an[0][0][0], AW - 2, AH, 5, over);  // the first not the whole frame
  CHECK_EQ(status(overlay_send(0, AW, part, over, plink_crc32(0, over, part))), PIC_BAD);
  CHECK_EQ(status(overlay_send(0, AW, n - 1, d, plink_crc32(0, d, n - 1))), PIC_BAD);  // the last record cut short
  CHECK_EQ(overlay_used(), 0);
  // A picture coming in cancels an animation coming in. A finished animation stays until the ESP clears it.
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  uint8_t h[OVERLAY_BEGIN_LEN];
  int hn = sim_overlay_begin(h, ++id, 1, OVERLAY_ANIMATED, AW, AH, 1, n, clear15(), 0, 0);
  CHECK_EQ(pic_msg(K_OVERLAY_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  fill(0, H, 5);
  CHECK_EQ(status(send_picture(16)), PIC_DONE);
  CHECK(shows());
  CHECK(placed());
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(end(plink_crc32(0, d, n)), -1);  // the rest is no longer expected
  CHECK_EQ(status(overlay_send(1, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  CHECK(placed());
  overlay_clear_msg(OVERLAY_ALL);
  CHECK(!placed());
  CHECK(shows());
  CHECK_EQ(overlay_used(), 0);
  // Clearing another slot or a place leaves it coming in, clearing its own slot drops the rest of it
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  h[0] = ++id;
  CHECK_EQ(pic_msg(K_OVERLAY_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  overlay_clear_msg(0 | 300 << 8);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(status(end(plink_crc32(0, d, n))), PIC_DONE);
  CHECK_EQ(overlay_used(), AW / 2 * AH + n);
  h[0] = ++id;
  CHECK_EQ(pic_msg(K_OVERLAY_BEGIN, h, hn), -1);
  CHECK_EQ(data(d, n / 2), -1);
  overlay_clear_msg(1 | 300 << 8);
  CHECK_EQ(data(d + n / 2, n - n / 2), -1);
  CHECK_EQ(end(plink_crc32(0, d, n)), -1);
  CHECK_EQ(overlay_used(), 0);
}

// Two slots and places in step and out of step, sharing the pool
static void test_anim_slots(void) {
  no_overlays();
  dot_frames(false);
  uint8_t d[512], d2[OVERLAY_POOL];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  uint32_t n2 = overlay_records(d2, 10);  // the same frames in slot 1, every one twice as long
  CHECK_EQ(status(overlay_send(1, AW, n2, d2, plink_crc32(0, d2, n2))), PIC_DONE);
  unsigned frame = AW / 2 * AH, both = 2 * frame + n + n2;
  CHECK_EQ(overlay_used(), both);
  place(0, 0, 0, 0, 0, 0);
  place(1, 0, 100, 0, 0, 0);    // in step with place 0
  place(2, 0, 200, 0, 1, 254);  // its own frame, 254 frames ahead: 2 of the 4, a script to get it there
  place(3, 1, 0, 100, 0, 0);    // slot 1
  place(4, 1, 100, 100, 1, 0);  // its own frame, in step as it happens
  CHECK_EQ(overlay_used(), both + 2 * frame + SCRIPT_POOL + 4);
  place(5, 2, 0, 0, 0, 0);                // no animation there
  place(6, 0, -10, PIC_MAX_H - 1, 0, 0);  // partly off the screen, where something can come in from
  place(OVERLAY_PLACES, 0, 0, 0, 0, 0);   // no such place
  int16_t x, y;
  CHECK(overlay_pos(2, &x, &y));
  CHECK_EQ(x, 200);
  CHECK_EQ(y, 0);
  CHECK(!overlay_pos(5, &x, &y));
  CHECK(overlay_pos(6, &x, &y));
  CHECK_EQ(x, -10);
  CHECK_EQ(y, PIC_MAX_H - 1);
  show_overlays();
  // slot 0 shows a frame for 50, 60, 70, 80 ms, slot 1 for 100, 110, ...; place 2 starts on frame 2
  CHECK(overlay_shows(0, 0) && overlay_shows(1, 0) && overlay_shows(2, 2) && overlay_shows(3, 0) &&
        overlay_shows(4, 0));
  sim_run(50);
  CHECK(overlay_shows(0, 1) && overlay_shows(1, 1) && overlay_shows(2, 2) && overlay_shows(3, 0) &&
        overlay_shows(4, 0));
  sim_run(50);  // 100: place 2 on to frame 3 at 70, slot 1 to frame 1
  CHECK(overlay_shows(0, 1) && overlay_shows(2, 3) && overlay_shows(3, 1) && overlay_shows(4, 1));
  sim_run(10);  // 110: slot 0 to frame 2
  CHECK(overlay_shows(0, 2) && overlay_shows(1, 2) && overlay_shows(2, 3));
  sim_run(40);  // 150: place 2 back to frame 0
  CHECK(overlay_shows(2, 0) && overlay_shows(0, 2));
  // a new place takes the old one's spot, a place with a frame of its own gives it back when cleared
  place(2, 1, 50, 50, 0, 0);
  CHECK_EQ(overlay_used(), both + frame);
  CHECK(overlay_pos(2, &x, &y));
  CHECK_EQ(x, 50);
  overlay_clear_msg(OVERLAY_PLACE_BIT | 4);
  CHECK_EQ(overlay_used(), both);
  CHECK(!overlay_pos(4, &x, &y));
  // the pool is packed: clearing slot 0 moves slot 1 down, and it plays on from where it was
  overlay_clear_msg(0);
  CHECK_EQ(overlay_used(), frame + n2);
  CHECK(!overlay_pos(0, &x, &y));
  CHECK(!overlay_pos(1, &x, &y));
  CHECK(overlay_pos(3, &x, &y));
  CHECK(overlay_shows(3, 1));
  sim_run(60);  // 210
  CHECK(overlay_shows(3, 2));
  // one byte too many for what's left
  CHECK_EQ(status(overlay_send(0, AW, OVERLAY_POOL - overlay_used() - frame + 1, d, 0)), PIC_TOO_BIG);
  CHECK_EQ(overlay_used(), frame + n2);
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(overlay_used(), 0);
  // slot 1 padded with empty records until only one frame of its own fits behind slot 0 and it
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  uint32_t m = (OVERLAY_POOL - overlay_used() - 2 * frame - n2) / 5;
  for (uint32_t i = 0; i < m; i++)
    memcpy(d2 + n2 + 5 * i, (uint8_t[]) {10, 0, 0, 0, 0}, 5);
  uint32_t n3 = n2 + 5 * m;
  CHECK_EQ(status(overlay_send(1, AW, n3, d2, plink_crc32(0, d2, n3))), PIC_DONE);
  CHECK(OVERLAY_POOL - overlay_used() >= frame && OVERLAY_POOL - overlay_used() < 2 * frame);
  place(0, 0, 0, 0, 1, 0);
  place(1, 0, 20, 0, 1, 0);  // no room for its frame
  place(2, 1, 40, 0, 0, 0);  // no frame needed
  CHECK(overlay_pos(0, &x, &y) && !overlay_pos(1, &x, &y) && overlay_pos(2, &x, &y));
  overlay_clear_msg(OVERLAY_PLACE_BIT | 0);
  place(1, 0, 20, 0, 1, 0);
  CHECK(!overlay_pos(0, &x, &y) && overlay_pos(1, &x, &y));
  overlay_clear_msg(OVERLAY_ALL);
  CHECK_EQ(overlay_used(), 0);
  CHECK(!placed());
  lv_obj_clean(lv_scr_act());
}

void frames_msg(int slot, int rate) {
  uint8_t m[3] = {(uint8_t) slot};
  plink_put16(m + 1, rate);
  overlay_msg(K_OVERLAY_FRAMES, m, sizeof m);
}

// A script that waits for rounds rounds, then fades out over fade ms and goes, answering GONE with tag
void rounds_end(int p, int rounds, int fade, int tag) {
  uint8_t m[] = {OVERLAY_DO_ROUNDS, V16(rounds), OVERLAY_DO_END, V16(fade), (uint8_t) tag};
  script_to(p, m, sizeof m);
}

// ROUNDS and END: it plays to the end of its rounds, a frame of its own holds the last one while it fades, then it
// goes and the panel says so
static void test_anim_rounds(void) {
  no_overlays();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);  // 50, 60, 70, 80 ms, and 90 for the first frame after that
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  while (overlay_gone() >= 0)
    ;
  place(2, 0, 0, 10, 1, 0);
  show_overlays();
  rounds_end(2, 1, 200, 7);
  sim_run(250);
  CHECK(overlay_shows(2, 3));  // the last frame of its round, to 260
  CHECK_EQ(look_opa(2), 1000);
  sim_run(110);
  CHECK(look_opa(2) > 0);
  CHECK(look_opa(2) < 1000);
  CHECK_EQ(overlay_gone(), -1);
  sim_run(200);
  int16_t x, y;
  CHECK(!overlay_pos(2, &x, &y));
  CHECK_EQ(overlay_gone(), 2 | 7 << 8);
  CHECK_EQ(overlay_gone(), -1);
  // in step with the slot, the round it's in counts
  lv_obj_clean(lv_scr_act());
  place(0, 0, 0, 40, 0, 0);
  show_overlays();
  rounds_end(0, 2, 0, 9);
  sim_run(250);
  CHECK(overlay_pos(0, &x, &y));
  CHECK_EQ(overlay_gone(), -1);
  sim_run(450);
  CHECK(!overlay_pos(0, &x, &y));
  CHECK_EQ(overlay_gone(), 0 | 9 << 8);
  // placed again before the ESP heard, there's nothing to tell
  place(3, 0, 0, 60, 1, 0);
  show_overlays();
  rounds_end(3, 1, 0, 1);
  sim_run(400);
  CHECK(!overlay_pos(3, &x, &y));
  place(3, 0, 0, 60, 1, 0);
  CHECK_EQ(overlay_gone(), -1);
  // another script ends it, one cut short or that's no script at all doesn't start
  lv_obj_clean(lv_scr_act());
  show_overlays();
  rounds_end(3, 1, 0, 2);
  script_to(3, (uint8_t[]) {OVERLAY_DO_ROUNDS, V16(0)}, 4);
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {3, AT(0), AT(7), OVERLAY_DO_ROUNDS, V16(1)}, 8);  // 3 bytes short
  overlay_msg(K_OVERLAY_SCRIPT, (uint8_t[]) {3, 0}, 2);
  sim_run(1000);
  CHECK(overlay_pos(3, &x, &y));
  CHECK_EQ(overlay_gone(), -1);
  CHECK_EQ(overlay_used(), AW / 2 * AH + n + AW / 2 * AH);
  // a single frame shows for its frame time each round
  uint8_t one[64];
  uint32_t n1 = plink_overlay_record(NULL, &an[0][0][0], AW, AH, 10, one);
  CHECK_EQ(status(overlay_send(1, AW, n1, one, plink_crc32(0, one, n1))), PIC_DONE);
  lv_obj_clean(lv_scr_act());
  place(4, 1, 100, 100, 0, 0);
  show_overlays();
  rounds_end(4, 3, 0, 2);
  sim_run(250);
  CHECK(overlay_pos(4, &x, &y));
  sim_run(150);
  CHECK(!overlay_pos(4, &x, &y));
  CHECK_EQ(overlay_gone(), 4 | 2 << 8);
  // and only while it's shown, at its frame rate
  lv_obj_clean(lv_scr_act());
  place(4, 1, 100, 100, 0, 0);
  rounds_end(4, 2, 0, 3);
  frames_msg(1, 50);  // 200 ms a round
  sim_run(3000);
  show_overlays();
  sim_run(350);
  CHECK(overlay_pos(4, &x, &y));
  sim_run(100);
  CHECK(!overlay_pos(4, &x, &y));
  CHECK_EQ(overlay_gone(), 4 | 3 << 8);
  // cleared while it fades out after its rounds, alone or with its slot: it fades on as it was and still says so
  for (int slot = 0; slot < 2; slot++) {
    place(2, 0, 0, 10, 1, 0);
    rounds_end(2, 1, 400, 5);
    sim_run(300);
    overlay_clear_msg(slot ? 0 | 100 << 8 : OVERLAY_PLACE_BIT | 2 | 100 << 8);
    sim_run(200);
    CHECK(overlay_pos(2, &x, &y));
    CHECK_EQ(overlay_gone(), -1);
    sim_run(200);
    CHECK(!overlay_pos(2, &x, &y));
    CHECK_EQ(overlay_gone(), 2 | 5 << 8);
  }
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// FRAMES: a slot's frames go at its rate, the one it's on too, 0 holds it
static void test_anim_frames(void) {
  no_overlays();
  dot_frames(true);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 10);  // 100, 110, 120, 130 ms
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  show_overlays();
  frames_msg(0, 200);  // twice as fast, 50 ms left of the first frame
  sim_run(80);
  CHECK(overlay_shows(0, 1));
  sim_run(50);  // 130, the next one 55 ms after 50
  CHECK(overlay_shows(0, 2));
  sim_run(60);
  CHECK(overlay_shows(0, 3));
  frames_msg(0, 0);
  sim_run(1000);
  CHECK(overlay_shows(0, 3));
  frames_msg(0, 100);
  frames_msg(OVERLAY_SLOTS, 0);  // no such slot, too fast, the wrong length
  frames_msg(0, 1001);
  overlay_msg(K_OVERLAY_FRAMES, (uint8_t[]) {0, 0}, 2);
  sim_run(100);
  CHECK(overlay_shows(0, 0));  // on at once, for 140 ms
  sim_run(100);
  CHECK(overlay_shows(0, 1));
  // to 1 % and back after a moment: what's left of the frame is back to its time at 100 %, not 11 s
  frames_msg(0, 1);
  sim_run(50);
  frames_msg(0, 100);
  sim_run(100);
  CHECK(overlay_shows(0, 2));
  lv_obj_clean(lv_scr_act());
  overlay_clear_msg(OVERLAY_ALL);
}

// ---------------------------------------------------------------- the picture's parts in front of places

static void part_msg(int k, int x0, int y0, int x1, int y1, unsigned colours, int depth) {
  uint8_t m[PIC_PART_LEN] = {(uint8_t) k};
  plink_put16(m + PIC_PART_X0, (uint16_t) x0);
  plink_put16(m + PIC_PART_Y0, (uint16_t) y0);
  plink_put16(m + PIC_PART_X1, (uint16_t) x1);
  plink_put16(m + PIC_PART_Y1, (uint16_t) y1);
  plink_put16(m + PIC_PART_COLOURS, (uint16_t) colours);
  m[PIC_PART_DEPTH] = (uint8_t) depth;
  overlay_part(m, PIC_PART_LEN);
}

static void depth_msg(int p, int depth) {
  uint8_t m[OVERLAY_GLIDE_HEAD + OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, p, 0, 0, EACH(OVERLAY_DEPTH, depth)));
}

static int look_depth(int p) {
  overlay_look_t l;
  return overlay_look(p, &l) ? l.depth : -1;
}

// The picture's palette index at x, y on the screen, -1 off it
static int pic_index(const lv_obj_t *pic, int x, int y) {
  lv_area_t a = {x - pic->coords.x1, y - pic->coords.y1, x - pic->coords.x1, y - pic->coords.y1};
  lv_color_t c;
  if (!pic_read(&a, &c, 1))
    return -1;
  for (int i = 0; i < 16; i++)
    if (c.full == lv_color_hex(rgb(i)).full)
      return i;
  return -2;
}

static int was[PIC_MAX_W * PIC_MAX_H];

// Place k is drawn as in was, but for what's in x0..x1 - 1, y0..y1 - 1 of colours, which shows what's behind it
static bool behind(int k, const lv_obj_t *pic, int x0, int y0, int x1, int y1, unsigned colours) {
  static int px[PIC_MAX_W * PIC_MAX_H];
  overlay_look_t l;
  if (!drawn(k, &l, px))
    return false;
  int w = lv_area_get_width(&l.box), left = 0;
  for (int i = 0; i < (int) lv_area_get_size(&l.box); i++) {
    int x = l.box.x1 + i % w, y = l.box.y1 + i / w, c = pic_index(pic, x, y);
    bool in = x >= x0 && x < x1 && y >= y0 && y < y1 && (colours == PIC_ALL_COLOURS || (c >= 0 && colours >> c & 1));
    if (px[i] != (in ? -1 : was[i]))
      return false;
    left += in && was[i] != -1;
  }
  return colours == 0 || left > 0;  // and something was left that would have been drawn
}

static bool draw_was(int k) {
  overlay_look_t l;
  return drawn(k, &l, was);
}

static void test_anim_parts(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  fill(0, H, 3);
  CHECK_EQ(status(send_picture(16)), PIC_DONE);
  lv_obj_t *pic = pic_show(lv_scr_act());
  lv_obj_update_layout(pic);
  int px0 = pic->coords.x1, py0 = pic->coords.y1;

  // pic_in() against the picture's own pixels, along every row and in every colour
  for (int y = py0 - 1; y <= py0 + H; y++)
    for (int x1 = px0 - 2; x1 < px0 + W + 2; x1 += 3)
      for (int x2 = x1; x2 < px0 + W + 2; x2 += 2)
        for (unsigned colours = 1; colours < 0x10000; colours = colours * 3 + 1) {
          uint32_t bits[2] = {0, 0}, want[2] = {0, 0};
          pic_in(y, x1, x2, (uint16_t) colours, bits, x1);
          for (int x = x1; x <= x2; x++) {
            int c = pic_index(pic, x, y);
            if (c >= 0 && colours >> c & 1)
              want[(x - x1) / 32] |= 1u << (x - x1) % 32;
          }
          CHECK(bits[0] == want[0] && bits[1] == want[1]);
        }

  // A place over the whole picture, every pixel drawn
  memset(an, 0x12, sizeof an);
  uint8_t d[512];
  uint32_t n = overlay_records(d, 5);
  CHECK_EQ(status(overlay_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  show_overlays();
  place(0, 0, px0 - 3, py0 - 1, 0, 0);
  CHECK_EQ(look_depth(0), OVERLAY_NEAREST);
  CHECK(draw_was(0));

  // Nearer parts cover it once it's farther, and only there
  unsigned colours = 1u << 3 | 1u << 7 | 1u << 9;
  part_msg(0, px0, py0, px0 + W, py0 + H, colours, 10);
  CHECK(behind(0, pic, 0, 0, 0, 0, 0));  // at OVERLAY_NEAREST nothing's in front
  depth_msg(0, 10);
  CHECK(behind(0, pic, 0, 0, 0, 0, 0));  // nor at the part's own depth
  depth_msg(0, 9);
  CHECK_EQ(look_depth(0), 9);
  CHECK(behind(0, pic, px0, py0, px0 + W, py0 + H, colours));
  part_msg(1, px0 + 2, py0 + 1, px0 + 5, py0 + 4, PIC_ALL_COLOURS, 200);
  CHECK(!behind(0, pic, px0, py0, px0 + W, py0 + H, colours));
  part_msg(0, 0, 0, 0, 0, 0, 0);  // empty, gone
  CHECK(behind(0, pic, px0 + 2, py0 + 1, px0 + 5, py0 + 4, PIC_ALL_COLOURS));
  part_msg(PIC_PARTS, 0, 0, PIC_MAX_W, PIC_MAX_H, PIC_ALL_COLOURS, 255);  // no such part
  depth_msg(0, OVERLAY_NEAREST + 1);                                      // no such depth
  CHECK_EQ(look_depth(0), 9);
  CHECK(behind(0, pic, px0 + 2, py0 + 1, px0 + 5, py0 + 4, PIC_ALL_COLOURS));

  // Turned, the same
  uint8_t m[16];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, 0, 0, 0, EACH(OVERLAY_TURN, 30)));
  part_msg(0, px0, py0, px0 + W, py0 + H, colours, 10);
  part_msg(1, 0, 0, 0, 0, 0, 0);
  depth_msg(0, OVERLAY_NEAREST);
  CHECK(draw_was(0));
  depth_msg(0, 0);
  CHECK(behind(0, pic, px0, py0, px0 + W, py0 + H, colours));
  overlay_parts_clear();
  CHECK(behind(0, pic, 0, 0, 0, 0, 0));

  // Depth before the size map and the number, and PLACE starts it nearest
  place(1, 0, 40, 40, 0, 0);
  depth_msg(0, 100);
  depth_msg(1, 50);
  overlay_look_t l[2];
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order > l[1].order);
  place(0, 0, 40, 40, 0, 0);
  CHECK_EQ(look_depth(0), OVERLAY_NEAREST);
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order > l[1].order);
  depth_msg(0, 10);
  CHECK(overlay_look(0, &l[0]) && overlay_look(1, &l[1]) && l[0].order < l[1].order);

  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
}

// ---------------------------------------------------------------- stills

#define SW 40  // a still's frame, in pieces of 8 rows with a clear band and clear edges
#define SH 30
static uint8_t sf[SH][SW / 2];

static void still_frame(void) {
  for (int y = 0; y < SH; y++)
    for (int x = 0; x < SW; x++) {
      int c = (x * 3 + y * 5) % 17;
      if (c > 15 || y < 3 || (y >= 16 && y < 24) || (y >= 8 && y < 16 && (x < 5 || x >= 36)))
        c = 15;
      uint8_t *b = &sf[y][x / 2];
      *b = x & 1 ? (uint8_t) ((*b & 0xF0) | c) : (uint8_t) ((*b & 0x0F) | c << 4);
    }
}

// GLIDE of place p's channels to their values, at once
static void glide_now(int p, const int *v, int n) {
  uint8_t m[32];
  overlay_msg(K_OVERLAY_GLIDE, m, sim_overlay_glide(m, p, 0, 0, v, n));
}
#define GLIDE_NOW(p, ...) glide_now(p, EACH(__VA_ARGS__))

static void flip_now(int p, int flip) {
  uint8_t m[OVERLAY_VIEW_LEN];
  overlay_msg(K_OVERLAY_VIEW, m, sim_overlay_view(m, p, OVERLAY_VIEW_FLIP_X | OVERLAY_VIEW_FLIP_Y, (unsigned) flip));
}

typedef struct {
  int x, y, flip;
  int to[3];  // turn, size and opacity
} still_look_t;

// What place 0 shows of slot as k has it, drawn whole into buf. False if it isn't drawn, or drawn in strips of 8 and 3
// lines it isn't the same.
static bool draw_as(int slot, const still_look_t *k, overlay_look_t *l, lv_color_t *buf) {
  static lv_color_t strips[PIC_MAX_W * PIC_MAX_H];
  place(0, slot, k->x, k->y, 0, 0);
  GLIDE_NOW(0, OVERLAY_TURN, k->to[0], OVERLAY_SIZE, k->to[1], OVERLAY_OPACITY, k->to[2]);
  flip_now(0, k->flip);
  if (!draw_box(0, l, buf, PIC_MAX_H))
    return false;
  size_t n = lv_area_get_size(&l->box) * sizeof *buf;
  return draw_box(0, l, strips, 8) && !memcmp(strips, buf, n) && draw_box(0, l, strips, 3) && !memcmp(strips, buf, n);
}

// A still draws what an animated overlay of the same frame draws, however it's placed and turned, in pieces
static void test_still(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  still_frame();
  static uint8_t a[OVERLAY_RECORD_HEAD + PDEFLATE_BOUND(SW / 2 * SH)], st[2048];
  uint32_t an_n = plink_overlay_record(NULL, &sf[0][0], SW, SH, 10, a);
  uint32_t n = plink_overlay_pieces(&sf[0][0], SW, SH, 1u << 15, 10, st, sizeof st);
  CHECK(n && n < sizeof st);
  CHECK(!plink_overlay_pieces(&sf[0][0], SW, SH, 1u << 15, 10, st, n - 1));  // doesn't fit
  static const still_look_t LOOKS[] = {
      {60, 50, 0, {0, 100, 100}},
      {60, 50, 0, {0, 50, 100}},
      {60, 50, 0, {0, 173, 100}},
      {60, 50, 0, {30, 100, 100}},
      {60, 50, 0, {90, 100, 100}},
      {60, 50, 0, {180, 100, 100}},
      {60, 50, 0, {211, 81, 100}},
      {60, 50, OVERLAY_VIEW_FLIP_X, {0, 100, 100}},
      {60, 50, OVERLAY_VIEW_FLIP_Y, {0, 100, 100}},
      {60, 50, OVERLAY_VIEW_FLIP_X | OVERLAY_VIEW_FLIP_Y, {45, 120, 100}},
      {60, 50, 0, {0, 100, 50}},
      {60, 50, 0, {30, 100, 50}},
      {-15, PIC_MAX_H - 12, 0, {0, 100, 100}},  // partly off the screen
      {PIC_MAX_W - 20, -7, 0, {60, 100, 100}},
  };
  show_overlays();
  static lv_color_t want[PIC_MAX_W * PIC_MAX_H], got[PIC_MAX_W * PIC_MAX_H];
  for (int scale = 1; scale <= 2; scale++) {
    CHECK_EQ(status(send_kind(OVERLAY_ANIMATED, 0, SW, SH, scale, clear15(), an_n, a, 200)), PIC_DONE);
    CHECK_EQ(status(send_kind(OVERLAY_STILL, 1, SW, SH, scale, clear15(), n, st, 200)), PIC_DONE);
    CHECK_EQ(overlay_used(), SW / 2 * SH + an_n + n);  // a still has no frame
    for (int i = 0; i < (int) LEN(LOOKS); i++) {
      overlay_look_t la, ls;
      bool ok = draw_as(0, &LOOKS[i], &la, want) && draw_as(1, &LOOKS[i], &ls, got) && ls.still && !la.still &&
                !memcmp(&la.box, &ls.box, sizeof la.box) && !memcmp(want, got, lv_area_get_size(&la.box) * sizeof *got);
      uint32_t ink = 0;
      for (uint32_t k = 0; k < lv_area_get_size(&la.box); k++)
        ink += want[k].full != lv_color_hex(BEHIND).full;
      if (!CHECK(ok && ink > 100))
        printf("    look %d at scale %d\n", i, scale);
    }
  }

  // behind a part of the picture, the same
  part_msg(0, 70, 55, 90, 62, PIC_ALL_COLOURS, 10);
  overlay_look_t la, ls;
  CHECK(draw_as(0, &LOOKS[3], &la, want));
  depth_msg(0, 9);
  CHECK(draw_box(0, &la, want, PIC_MAX_H));
  CHECK(draw_as(1, &LOOKS[3], &ls, got));
  depth_msg(0, 9);
  CHECK(draw_box(0, &ls, got, PIC_MAX_H) && !memcmp(want, got, lv_area_get_size(&la.box) * sizeof *got));
  CHECK(got[(58 - ls.box.y1) * lv_area_get_width(&ls.box) + 80 - ls.box.x1].full == lv_color_hex(BEHIND).full);
  overlay_parts_clear();

  // its pixels: what's in its pieces, 16 where there are none
  static uint8_t px[SW * SH];
  place(0, 1, 60, 50, 0, 0);
  CHECK(overlay_pixels(0, px));
  int none = 0, wrong = 0;
  for (int y = 0; y < SH; y++)
    for (int x = 0; x < SW; x++) {
      int c = x & 1 ? sf[y][x / 2] & 15 : sf[y][x / 2] >> 4, p = px[y * SW + x];
      none += p == 16;
      wrong += p != c && !(p == 16 && c == 15);
    }
  CHECK_EQ(wrong, 0);
  CHECK(none >= SW * 11);  // the clear rows at the top and in the middle at least
  place(0, 0, 60, 50, 0, 0);
  CHECK(overlay_pixels(0, px) && px[0] == 15);

  // no room on LVGL's heap to unpack a piece: nothing drawn, and drawn once there is
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  void *hog = lv_mem_alloc(mon.free_biggest_size - OVERLAY_PIECE_MAX / 2);
  CHECK(hog);
  static int drawn_px[PIC_MAX_W * PIC_MAX_H];
  place(0, 1, 60, 50, 0, 0);
  CHECK(drawn(0, &ls, drawn_px));
  int shown = 0;
  for (uint32_t i = 0; i < lv_area_get_size(&ls.box); i++)
    shown += drawn_px[i] != -1;
  CHECK_EQ(shown, 0);
  CHECK(!overlay_pixels(0, px));
  lv_mem_free(hog);
  CHECK(drawn(0, &ls, drawn_px));
  for (uint32_t i = 0; i < lv_area_get_size(&ls.box); i++)
    shown += drawn_px[i] != -1;
  CHECK(shown > 0);
  lv_obj_clean(lv_scr_act());

  // a frame of its own takes no room, its rounds go by at its delay
  overlay_clear_msg(0);
  CHECK_EQ(overlay_used(), n);
  place(2, 1, 0, 0, 1, 7);  // without a frame to start on, its script isn't taken
  CHECK_EQ(overlay_used(), n);
  show_overlays();
  rounds_end(2, 3, 0, 4);
  int16_t x, y;
  sim_run(250);
  CHECK(overlay_pos(2, &x, &y));
  sim_run(100);
  CHECK(!overlay_pos(2, &x, &y));
  CHECK_EQ(overlay_gone(), 2 | 4 << 8);
  lv_obj_clean(lv_scr_act());

  // a still bigger than the pool, its pieces small
  static uint8_t wide[PIC_MAX_H][PIC_MAX_W / 2];
  memset(wide, 0xFF, sizeof wide);
  wide[100][80] = 0x12;
  uint32_t wn = plink_overlay_pieces(&wide[0][0], PIC_MAX_W, PIC_MAX_H, 1u << 15, 10, st, sizeof st);
  CHECK(wn && wn < 64);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 2, PIC_MAX_W, PIC_MAX_H, 1, clear15(), wn, st, 200)), PIC_DONE);
  CHECK_EQ(overlay_used(), n + wn);
  show_overlays();
  place(3, 2, 0, 0, 0, 0);
  CHECK(drawn(3, &ls, drawn_px));
  CHECK(drawn_px[100 * PIC_MAX_W + 160] == 1 && drawn_px[100 * PIC_MAX_W + 161] == 2);
  CHECK(drawn_px[100 * PIC_MAX_W + 162] == -1 && drawn_px[99 * PIC_MAX_W + 160] == -1);
  // and all clear, one empty piece
  memset(wide, 0xFF, sizeof wide);
  wn = plink_overlay_pieces(&wide[0][0], PIC_MAX_W, PIC_MAX_H, 1u << 15, 10, st, sizeof st);
  CHECK_EQ(wn, OVERLAY_RECORD_HEAD);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 2, PIC_MAX_W, PIC_MAX_H, 1, clear15(), wn, st, 200)), PIC_DONE);
  place(3, 2, 0, 0, 0, 0);
  CHECK(!drawn(3, &ls, drawn_px) || drawn_px[100 * PIC_MAX_W + 160] == -1);
  no_overlays();
}

// ---------------------------------------------------------------- views

// A place's box, where its frame is in it, its OVERLAY_VIEW_* and its shape's runs, width, top and bottom each
typedef struct {
  int pan[2], w, h;
  unsigned view;
  int runs;
  int16_t shape[9];
} box_view_t;

// What pixel u, y of the box with view v shows of sf at scale k, worked out on its own: its colour, -1 for none
static int view_px(const box_view_t *v, int k, int u, int y) {
  if (v->view & OVERLAY_VIEW_FLIP_X)
    u = v->w - 1 - u;
  if (v->view & OVERLAY_VIEW_FLIP_Y)
    y = v->h - 1 - y;
  bool in = !v->runs;
  for (int i = 0, x = 0; i < v->runs; x += v->shape[3 * i++])
    in |= u >= x && u < x + v->shape[3 * i] && y >= v->shape[3 * i + 1] && y < v->shape[3 * i + 2];
  if (!in)
    return -1;
  int at[2] = {u - v->pan[0], y - v->pan[1]}, n[2] = {SW * k, SH * k};
  for (int a = 0; a < 2; a++) {
    int t = at[a] >= 0 ? at[a] / n[a] : -((n[a] - 1 - at[a]) / n[a]);
    if (!(v->view & (a ? OVERLAY_VIEW_REPEAT_Y : OVERLAY_VIEW_REPEAT_X)) && t)
      return -1;
    at[a] -= t * n[a];
    if (v->view & OVERLAY_VIEW_MIRROR && t & 1)
      at[a] = n[a] - 1 - at[a];
    at[a] /= k;
  }
  int c = at[0] & 1 ? sf[at[1]][at[0] / 2] & 15 : sf[at[1]][at[0] / 2] >> 4;
  return c == 15 ? -1 : c;
}

// Place 0 showing slot with view v at 20, 30, turned turn, and its shape run by a script
static void view_place(int slot, const box_view_t *v, int turn) {
  place(0, slot, 20, 30, 0, 0);
  GLIDE_NOW(0, OVERLAY_PAN_X, v->pan[0], OVERLAY_PAN_Y, v->pan[1], OVERLAY_W, v->w, OVERLAY_H, v->h, OVERLAY_TURN,
            turn);
  uint8_t m[OVERLAY_VIEW_LEN];
  overlay_msg(K_OVERLAY_VIEW, m, sim_overlay_view(m, 0, 0xFF, v->view));
  if (!v->runs)
    return;
  uint8_t sc[2 + 2 * LEN(v->shape)] = {OVERLAY_DO_SHAPE, (uint8_t) (3 * v->runs)};
  for (int i = 0; i < 3 * v->runs; i++)
    plink_put16(sc + 2 + 2 * i, (uint16_t) v->shape[i]);
  script_to(0, sc, 2 + 6 * v->runs);
}

// Views: a box of any size showing its frame where pan puts it, once or round and round, every other one mirrored,
// only what a shape leaves, flipped and turned with it. Stills and animations alike, in strips too.
static void test_views(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  still_frame();
  static uint8_t a[OVERLAY_RECORD_HEAD + PDEFLATE_BOUND(SW / 2 * SH)], st[2048];
  uint32_t an_n = plink_overlay_record(NULL, &sf[0][0], SW, SH, 10, a);
  uint32_t n = plink_overlay_pieces(&sf[0][0], SW, SH, 1u << 15, 10, st, sizeof st);
  const unsigned F = OVERLAY_VIEW_FLIP_X | OVERLAY_VIEW_FLIP_Y;
  static const box_view_t VIEWS[] = {
      {{0, 0}, SW, SH, 0, 0, {0}},
      {{7, -5}, SW, SH, 0, 0, {0}},  // its frame partly out of its box
      {{-10, -4}, 25, 20, 0, 0, {0}},
      {{13, -9}, 100, 70, OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_REPEAT_Y, 0, {0}},
      {{-17, 0}, 130, SH, OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_MIRROR, 0, {0}},
      {{5, 11},
       90,
       75,
       OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_REPEAT_Y | OVERLAY_VIEW_MIRROR | OVERLAY_VIEW_FLIP_X,
       0,
       {0}},
      {{3, 4}, 60, 50, OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_REPEAT_Y, 3, {20, 5, 25, 0, 0, 0, 30, 0, 40}},
      {{-6, 0}, 70, 40, OVERLAY_VIEW_REPEAT_Y | OVERLAY_VIEW_FLIP_Y, 2, {10, 0, 40, 25, 12, 30}},
  };
  show_overlays();
  static lv_color_t whole[PIC_MAX_W * PIC_MAX_H], strips[PIC_MAX_W * PIC_MAX_H];
  static int px[PIC_MAX_W * PIC_MAX_H], turned[PIC_MAX_W * PIC_MAX_H];
  for (int scale = 1; scale <= 2; scale++) {
    CHECK_EQ(status(send_kind(OVERLAY_ANIMATED, 0, SW, SH, scale, clear15(), an_n, a, 200)), PIC_DONE);
    CHECK_EQ(status(send_kind(OVERLAY_STILL, 1, SW, SH, scale, clear15(), n, st, 200)), PIC_DONE);
    for (int i = 0; i < (int) LEN(VIEWS); i++)
      for (int slot = 0; slot < 2; slot++) {
        box_view_t v = VIEWS[i];
        v.w *= i ? 1 : scale;
        v.h *= i ? 1 : scale;
        view_place(slot, &v, 0);
        overlay_look_t l;
        int wrong = 0, ink = 0;
        bool ok = drawn(0, &l, px) && l.w == v.w && l.h == v.h && lv_area_get_width(&l.box) == v.w &&
                  lv_area_get_height(&l.box) == v.h;
        for (int y = 0; ok && y < v.h; y++)
          for (int x = 0; x < v.w; x++) {
            wrong += px[y * v.w + x] != view_px(&v, scale, x, y);
            ink += px[y * v.w + x] >= 0;
          }
        size_t bytes = (size_t) v.w * v.h * sizeof *whole;
        ok &= draw_box(0, &l, whole, PIC_MAX_H) && draw_box(0, &l, strips, 8) && !memcmp(whole, strips, bytes) &&
              draw_box(0, &l, strips, 3) && !memcmp(whole, strips, bytes);
        // turned half round it's flipped both ways
        box_view_t f = v;
        f.view ^= F;
        view_place(slot, &f, 180);
        ok &= drawn(0, &l, turned) && !memcmp(px, turned, (size_t) v.w * v.h * sizeof *px);
        if (!CHECK(ok && !wrong && ink > 100))
          printf("    view %d, slot %d at scale %d: %d wrong\n", i, slot, scale, wrong);
      }
  }
  // an empty box draws nothing, and a shape goes with its script
  overlay_look_t l;
  box_view_t v = {{0, 0}, 0, SH, 0, 0, {0}};
  view_place(1, &v, 0);
  CHECK(overlay_look(0, &l) && !l.tiles);
  v = (box_view_t) {{0, 0}, SW, SH, 0, 1, {5, 0, SH}};
  view_place(1, &v, 0);
  CHECK(drawn(0, &l, px) && px[1 * SW + 10] == -1);
  script_to(0, (uint8_t[]) {OVERLAY_DO_MARK, 1}, 2);
  CHECK(drawn(0, &l, px));
  int c = sf[1][10 / 2] >> 4;
  CHECK_EQ(px[1 * SW + 10], c == 15 ? -1 : c);
  no_overlays();
}

// ---------------------------------------------------------------- blends and soft edges

// Screen pixel x, y of the overlays drawn over behind
static lv_color_t over(int x, int y, uint32_t behind) {
  lv_color_t c;
  lv_area_t a = {x, y, x, y};
  sim_draw(overlays, &a, lv_color_hex(behind), &c);
  return c;
}

// s over d at opacity a as an OVERLAY_BLEND_* says, worked out on its own a channel at a time
static lv_color_t blended(unsigned mode, lv_color_t s, lv_color_t d, int a) {
  lv_color_t w = lv_color_white();
  int c[3] = {LV_COLOR_GET_R(s), LV_COLOR_GET_G(s), LV_COLOR_GET_B(s)};
  int e[3] = {LV_COLOR_GET_R(d), LV_COLOR_GET_G(d), LV_COLOR_GET_B(d)};
  int m[3] = {LV_COLOR_GET_R(w), LV_COLOR_GET_G(w), LV_COLOR_GET_B(w)};
  for (int k = 0; k < 3; k++) {
    int t = c[k] * a / 255;
    e[k] = mode == OVERLAY_BLEND_ADD      ? LV_MIN(m[k], e[k] + t)
           : mode == OVERLAY_BLEND_SCREEN ? e[k] + t - e[k] * t / m[k]
                                          : e[k] * (m[k] - (m[k] - c[k]) * a / 255) / m[k];
  }
  LV_COLOR_SET_R(d, e[0]);
  LV_COLOR_SET_G(d, e[1]);
  LV_COLOR_SET_B(d, e[2]);
  return d;
}

// Blends: a colour added to, screened over or multiplied with what's behind it, at full and half opacity, and back to
// mixed. Soft edges: it fades in from the top of its box, and from a shape's run, but not past its other edges.
static void test_blends(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  static uint8_t solid[SH][SW / 2];
  static uint8_t st[2048];
  memset(solid, 0x55, sizeof solid);  // colour 5 all over
  uint32_t n = plink_overlay_pieces(&solid[0][0], SW, SH, 1u << 15, 10, st, sizeof st);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 1, SW, SH, 1, clear15(), n, st, 200)), PIC_DONE);
  show_overlays();
  place(0, 1, 20, 30, 0, 0);
  const uint32_t G = 0x8040C0;  // behind it
  lv_color_t c = lv_color_hex(rgb(5)), g = lv_color_hex(G);
  uint8_t m[OVERLAY_VIEW_LEN];
  for (unsigned mode = OVERLAY_BLEND_ADD; mode <= OVERLAY_BLEND_MULTIPLY; mode += OVERLAY_BLEND_ADD) {
    overlay_msg(K_OVERLAY_VIEW, m, sim_overlay_view(m, 0, OVERLAY_VIEW_BLEND, mode));
    GLIDE_NOW(0, OVERLAY_OPACITY, 100);
    bool full = over(30, 40, G).full == blended(mode, c, g, 255).full;
    GLIDE_NOW(0, OVERLAY_OPACITY, 50);
    lv_color_t half = over(30, 40, G);
    if (!CHECK(full && half.full == blended(mode, c, g, 128).full))
      printf("    blend %u\n", mode);
  }
  overlay_msg(K_OVERLAY_VIEW, m, sim_overlay_view(m, 0, OVERLAY_VIEW_BLEND, OVERLAY_BLEND_MIX));
  GLIDE_NOW(0, OVERLAY_OPACITY, 100);
  CHECK(over(30, 40, G).full == c.full);
  // fading in over the 8 rows from its top, more of it on each, all of it from the eighth on and at its sides
  script_to(0, (uint8_t[]) {OVERLAY_DO_SOFT, 0, 0, 0, 0, 8, 0, 0, 0}, 9);
  int was = -1;
  bool more = true;
  for (int y = 0; y < 8; y++) {  // further from what's behind on each row
    lv_color_t k = over(30, 30 + y, G);
    int away = abs(LV_COLOR_GET_R(k) - LV_COLOR_GET_R(g)) + abs(LV_COLOR_GET_G(k) - LV_COLOR_GET_G(g)) +
               abs(LV_COLOR_GET_B(k) - LV_COLOR_GET_B(g));
    more &= away > was && k.full != c.full;
    was = away;
  }
  CHECK(more && over(30, 38, G).full == c.full && over(20, 45, G).full == c.full && over(59, 45, G).full == c.full);
  // and from each run's top, the shape's set after it
  script_to(0, (uint8_t[]) {OVERLAY_DO_SOFT, 0, 0, 0, 0, 4, 0, 0, 0, OVERLAY_DO_SHAPE, 3, SW, 0, 10, 0, SH, 0}, 17);
  CHECK(over(30, 39, G).full != c.full && over(30, 40, G).full != c.full && over(30, 44, G).full == c.full);
  // none
  script_to(0, (uint8_t[]) {OVERLAY_DO_SOFT, 0, 0, 0, 0, 0, 0, 0, 0}, 9);
  CHECK(over(30, 30, G).full == c.full);
  no_overlays();
}

// ---------------------------------------------------------------- fields

// A field into slot: kind with a and b, w x h, its 16 colours pal
static int32_t send_field(int slot, int kind, int a, int b, int w, int h, const uint32_t *pal) {
  uint8_t d[OVERLAY_FIELD_LEN] = {(uint8_t) kind, (uint8_t) a, (uint8_t) b, (uint8_t) (b >> 8)};
  return send_kind(OVERLAY_FIELD, slot, w, h, 1, pal, sizeof d, d, 200);
}

// How far c is from d, each channel's
static int apart(lv_color_t c, lv_color_t d) {
  return abs(LV_COLOR_GET_R(c) - LV_COLOR_GET_R(d)) + abs(LV_COLOR_GET_G(c) - LV_COLOR_GET_G(d)) +
         abs(LV_COLOR_GET_B(c) - LV_COLOR_GET_B(d));
}

static void bind_now(int p, unsigned what, const uint8_t *v, int n) {
  uint8_t m[OVERLAY_BIND_VALUE + 64];
  overlay_msg(K_OVERLAY_BIND, m, sim_overlay_bind(m, p, what, v, n));
}
#define BIND(p, what, ...) bind_now(p, what, BYTES(__VA_ARGS__))

// Fields: the panel works out what a place shows, from left to right, round its middle, in a wedge or cloudy, and
// draws it by how far along the slot's colours that is, with the place's level and phase. Sized, turned and blended
// like a frame.
static void test_fields(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  const uint32_t G = 0x204060;  // behind it
  lv_color_t g = lv_color_hex(G), c = lv_color_hex(rgb(5));
  uint32_t fade[16], steps[16];  // one colour more and more opaque, and 16 of them
  for (int i = 0; i < 16; i++) {
    fade[i] = (uint32_t) (i * 17) << 24 | rgb(5);
    steps[i] = 0xFF000000 | rgb(i);
  }
  enum { X = 20, Y = 30, FW = 64, FH = 16 };
  show_overlays();
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_LINEAR, 0, 0, FW, FH, fade)), PIC_DONE);
  CHECK_EQ(overlay_used(), OVERLAY_FIELD_LEN);
  place(0, 0, X, Y, 1, 0);  // a frame of its own is nothing to it
  CHECK_EQ(overlay_used(), OVERLAY_FIELD_LEN);
  overlay_look_t l;
  CHECK(overlay_look(0, &l) && l.w == FW && l.h == FH && l.still);
  bool more = true, same = true;
  for (int x = 1; x < FW; x++) {  // more of it to the right, the same all the way down
    more &= apart(over(X + x, Y + 3, G), g) >= apart(over(X + x - 1, Y + 3, G), g);
    same &= over(X + x, Y, G).full == over(X + x, Y + FH - 1, G).full;
  }
  CHECK(more && same && over(X, Y, G).full == g.full && apart(over(X + FW - 1, Y, G), c) < 3);
  CHECK(over(X - 1, Y, G).full == g.full && over(X + FW, Y, G).full == g.full);
  GLIDE_NOW(0, OVERLAY_LEVEL, 200);  // twice as much: all of it from the middle on
  CHECK(over(X + FW / 2, Y, G).full == c.full && over(X + FW / 4, Y, G).full != c.full);
  GLIDE_NOW(0, OVERLAY_LEVEL, 0);
  CHECK(over(X + FW - 1, Y, G).full == g.full);
  GLIDE_NOW(0, OVERLAY_TURN, 180, OVERLAY_LEVEL, 100);  // turned round, from the right
  CHECK(over(X + FW - 1, Y, G).full == g.full && apart(over(X, Y, G), c) < 3);
  GLIDE_NOW(0, OVERLAY_TURN, 0, OVERLAY_SIZE_X, 50);  // half as wide, round its middle
  CHECK(over(X + FW / 4 - 1, Y, G).full == g.full && apart(over(X + 3 * FW / 4 - 1, Y, G), c) < 6);
  GLIDE_NOW(0, OVERLAY_SIZE_X, 100);
  uint8_t m[OVERLAY_VIEW_LEN];
  overlay_msg(K_OVERLAY_VIEW, m, sim_overlay_view(m, 0, OVERLAY_VIEW_BLEND, OVERLAY_BLEND_ADD));
  CHECK(over(X + FW - 1, Y, G).full == blended(OVERLAY_BLEND_ADD, c, g, 251).full);
  // up to the middle and back down, and from the top
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_LINEAR, OVERLAY_LINEAR_MIRROR, 0, FW, FH, fade)), PIC_DONE);
  place(0, 0, X, Y, 0, 0);
  same = true;
  for (int x = 0; x < FW / 2; x++)
    same &= over(X + x, Y, G).full == over(X + FW - 1 - x, Y, G).full;
  CHECK(same && over(X, Y, G).full == g.full && apart(over(X + FW / 2, Y, G), c) < 12);
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_LINEAR, OVERLAY_LINEAR_DOWN, 0, FW, FH, fade)), PIC_DONE);
  place(0, 0, X, Y, 0, 0);
  CHECK(apart(over(X + 9, Y, G), g) < 6 && over(X + 9, Y, G).full == over(X + 50, Y, G).full);
  CHECK(apart(over(X + 9, Y + FH - 1, G), c) < 12 && over(X + 9, Y + 8, G).full == over(X + 50, Y + 8, G).full);
  // round the middle: most there, the same either side of it, none in the corners
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_RADIAL, 0, 0, FW, FH, fade)), PIC_DONE);
  place(0, 0, X, Y, 0, 0);
  same = more = true;
  for (int x = 0; x < FW / 2; x++) {
    same &= over(X + x, Y + 5, G).full == over(X + FW - 1 - x, Y + 5, G).full &&
            over(X + x, Y + 5, G).full == over(X + x, Y + FH - 1 - 5, G).full;
    more &= !x || apart(over(X + x, Y + FH / 2, G), g) >= apart(over(X + x - 1, Y + FH / 2, G), g);
  }
  CHECK(same && more && over(X, Y, G).full == g.full && over(X + FW - 1, Y + FH - 1, G).full == g.full);
  CHECK(apart(over(X + FW / 2, Y + FH / 2, G), c) < 12);
  // a wedge from the middle of its left side: nothing above and below it there, all of its right side
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_CONE, 0, 0, FW, FH, fade)), PIC_DONE);
  place(0, 0, X, Y, 0, 0);
  CHECK(over(X + 4, Y + 1, G).full == g.full && over(X + 4, Y + FH - 2, G).full == g.full);
  CHECK(apart(over(X + 4, Y + FH / 2, G), c) < 80 && over(X + FW - 1, Y + 2, G).full != g.full);
  CHECK(apart(over(X + FW - 1, Y + FH / 2, G), g) > apart(over(X + FW - 1, Y + 2, G), g));
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_CONE, 255, 0, FW, FH, fade)), PIC_DONE);  // as wide at the left
  place(0, 0, X, Y, 0, 0);
  CHECK(over(X + 4, Y + 2, G).full == over(X + FW - 1, Y + 2, G).full && over(X + 4, Y + 2, G).full != g.full);
  // cloudy: here more and there less, the same wherever it's placed again, on by its pan, each place its own
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_NOISE, 2, 8, FW, FH, fade)), PIC_DONE);
  place(0, 0, X, Y, 0, 0);
  place(1, 0, X, Y + 40, 0, 0);
  int lo = 9999, hi = 0, other = 0;
  static lv_color_t was[FH][FW];
  for (int y = 0; y < FH; y++)
    for (int x = 0; x < FW; x++) {
      was[y][x] = over(X + x, Y + y, G);
      lo = LV_MIN(lo, apart(was[y][x], g));
      hi = LV_MAX(hi, apart(was[y][x], g));
      other += over(X + x, Y + 40 + y, G).full != was[y][x].full;
    }
  CHECK(hi - lo > 20 && other > FW * FH / 2);
  GLIDE_NOW(0, OVERLAY_PAN_X, 3, OVERLAY_PAN_Y, 2);
  same = true;
  for (int y = 2; y < FH; y++)
    for (int x = 3; x < FW; x++)
      same &= over(X + x, Y + y, G).full == was[y - 2][x - 3].full;
  CHECK(same);
  // and round: 64 of its 8 px on it's the same again, so a pan that drifts goes on for good
  bool round = true;
  GLIDE_NOW(0, OVERLAY_PAN_X, 64 * 8 - 5, OVERLAY_PAN_Y, 0);
  uint8_t dm[OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_DRIFT, dm, sim_overlay_drift(dm, 0, EACH(OVERLAY_PAN_X, 100)));
  sim_run(100);  // 10 px on, 5 past where it comes round
  overlay_msg(K_OVERLAY_DRIFT, dm, sim_overlay_drift(dm, 0, EACH(OVERLAY_PAN_X, 0)));
  for (int y = 0; y < FH; y++)
    for (int x = 5; x < FW; x++)
      round &= over(X + x, Y + y, G).full == was[y][x - 5].full;
  CHECK(overlay_look(0, &l) && l.pan[0] == 5 && round);
  // along 16 colours: each its own at its place, and phase on, round to the first again
  CHECK_EQ(status(send_field(0, OVERLAY_FIELD_LINEAR, 0, 0, 256, 8, steps)), PIC_DONE);
  place(0, 0, X, Y, 0, 0);
  bool own = true, on = true, half = true;
  for (int k = 0; k < 16; k++)
    own &= over(X + 17 * k, Y, G).full == lv_color_hex(rgb(k)).full;
  GLIDE_NOW(0, OVERLAY_PHASE, 3);
  for (int k = 0; k < 16; k++)
    on &= over(X + 17 * k, Y, G).full == lv_color_hex(rgb((k + 3) % 16)).full;
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_PHASE, V16(-2500));  // half a colour on
  for (int k = 0; k < 16; k++)
    half &=
        over(X + 17 * k, Y, G).full == lv_color_mix(lv_color_hex(rgb((k + 1) % 16)), lv_color_hex(rgb(k)), 128).full;
  CHECK(own && on && half);
  // what isn't one
  uint8_t bad[OVERLAY_FIELD_LEN + 1] = {OVERLAY_FIELD_NOISE, 2, 8, 0, 0};
  CHECK_EQ(status(send_kind(OVERLAY_FIELD, 1, FW, FH, 1, fade, OVERLAY_FIELD_LEN - 1, bad, 200)), PIC_BAD);
  CHECK_EQ(status(send_kind(OVERLAY_FIELD, 1, FW, FH, 1, fade, OVERLAY_FIELD_LEN + 1, bad, 200)), PIC_BAD);
  CHECK_EQ(status(send_field(1, OVERLAY_FIELDS, 0, 0, FW, FH, fade)), PIC_BAD);
  CHECK_EQ(status(send_field(1, OVERLAY_FIELD_NOISE, 0, 8, FW, FH, fade)), PIC_BAD);
  CHECK_EQ(status(send_field(1, OVERLAY_FIELD_NOISE, OVERLAY_NOISE_LAYERS + 1, 8, FW, FH, fade)), PIC_BAD);
  CHECK_EQ(status(send_field(1, OVERLAY_FIELD_NOISE, 1, OVERLAY_NOISE_MIN - 1, FW, FH, fade)), PIC_BAD);
  CHECK_EQ(status(send_field(1, OVERLAY_FIELD_NOISE, 1, OVERLAY_NOISE_MAX + 1, FW, FH, fade)), PIC_BAD);
  CHECK_EQ(status(send_field(1, OVERLAY_FIELD_LINEAR, 4, 0, FW, FH, fade)), PIC_BAD);
  CHECK_EQ(overlay_used(), OVERLAY_FIELD_LEN + 4);  // the first, and what its phase is bound to
  no_overlays();
}

// ---------------------------------------------------------------- rows, smooth and cycling colours

// Slot 1: a still w x h of colour 5 where there(x, y), clear elsewhere, and place 0 showing it at X, Y
enum { X = 100, Y = 60, RW = 40, RH = 16 };
static void shape_still(bool (*there)(int x, int y)) {
  static uint8_t f[RH][RW / 2], st[1024];
  for (int y = 0; y < RH; y++)
    for (int x = 0; x < RW; x++) {
      uint8_t *b = &f[y][x / 2], k = there(x, y) ? 5 : 15;
      *b = x & 1 ? (uint8_t) ((*b & 0xF0) | k) : (uint8_t) ((*b & 0x0F) | k << 4);
    }
  uint32_t n = plink_overlay_pieces(&f[0][0], RW, RH, 1u << 15, 10, st, sizeof st);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 1, RW, RH, 1, clear15(), n, st, 200)), PIC_DONE);
  place(0, 1, X, Y, 0, 0);
}
static bool a_bar(int x, int y) { return x >= 20 && x < 22 && y >= 0; }
static bool a_block(int x, int y) { return x >= 10 && x < 30 && y >= 4 && y < 12; }
static bool all_of_it(int x, int y) { return x >= 0 && y >= 0; }

static void view_now(int p, unsigned mask, unsigned flags) {
  uint8_t m[OVERLAY_VIEW_LEN];
  overlay_msg(K_OVERLAY_VIEW, m, sim_overlay_view(m, p, mask, flags));
}

// What's bound to a place's rows: each row of its frame shifted, sized across round its pivot, fainter, in another
// light, by where the row is. Redrawn only when that's another, and not when it's turned.
static void test_rows(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  const uint32_t G = 0x204060;
  lv_color_t g = lv_color_hex(G), c = lv_color_hex(rgb(5));
  show_overlays();
  shape_still(a_bar);
  CHECK(over(X + 20, Y + 9, G).full == c.full && over(X + 29, Y + 9, G).full == g.full);
  // a px to the right for each row down
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_SHIFT, X(OWN(OVERLAY_GET_ROW), V16(1000), OVERLAY_OP_MUL));
  bool leans = true;
  for (int y = 0; y < RH; y++)
    leans &= over(X + 19 + y, Y + y, G).full == g.full && over(X + 20 + y, Y + y, G).full == c.full &&
             over(X + 21 + y, Y + y, G).full == c.full && over(X + 22 + y, Y + y, G).full == g.full;
  CHECK(leans);
  sim_run(100);  // the same each step, so nothing's drawn again
  unsigned long px = sim_px;
  sim_run(300);
  CHECK_EQ(sim_px, px);
  // by how long it's been there, it's drawn again as it changes
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_SHIFT, X(OWN(OVERLAY_GET_MS), V8(10), OVERLAY_OP_MUL));
  sim_run(300);
  CHECK(sim_px > px && !sim_stale());
  // and by the step's time, not by when it's drawn: before the next step it's still as the step left it
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_SHIFT,
       X(OWN(OVERLAY_GET_MS), V16(150), OVERLAY_OP_MOD, V8(100), OVERLAY_OP_MUL));
  static int then[RW * RH], later[RW * RH];
  overlay_look_t at;
  do {
    px = sim_px;
    sim_run(5);
  } while (sim_px == px);
  px = sim_px;
  CHECK(drawn(0, &at, then));
  sim_run(20);
  CHECK(drawn(0, &at, later));
  CHECK(sim_px == px && !memcmp(then, later, sizeof then));
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_BIND_ROW_SHIFT);
  CHECK(over(X + 20, Y + 9, G).full == c.full && over(X + 29, Y + 9, G).full == g.full);
  // half as wide round its middle on its lower half: the bar a px wide there, what's 10 px from the middle 5
  shape_still(a_block);
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_SIZE,
       X(OWN(OVERLAY_GET_ROW), V8(8), OVERLAY_OP_LT, V16(1000), V16(500), OVERLAY_OP_IF));
  CHECK(over(X + 10, Y + 5, G).full == c.full && over(X + 9, Y + 5, G).full == g.full);
  CHECK(over(X + 15, Y + 9, G).full == c.full && over(X + 14, Y + 9, G).full == g.full);
  CHECK(over(X + 24, Y + 9, G).full == c.full && over(X + 25, Y + 9, G).full == g.full);
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_BIND_ROW_SIZE);
  // fainter from the top down, and dark below the middle
  shape_still(all_of_it);
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_OPACITY, X(OWN(OVERLAY_GET_ROW), V8(100), OVERLAY_OP_MUL));
  CHECK(over(X + 5, Y, G).full == g.full && over(X + 5, Y + 5, G).full == lv_color_mix(c, g, 128).full);
  CHECK(over(X + 5, Y + 10, G).full == c.full);
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_LIGHT,
       X(OWN(OVERLAY_GET_ROW), V8(12), OVERLAY_OP_LT, V16(1000), V8(0), OVERLAY_OP_IF));
  CHECK(over(X + 5, Y + 10, G).full == c.full && over(X + 5, Y + 12, G).full == lv_color_black().full);
  // a frame that repeats goes all the way across its rows at any size: a box three frames wide, half as wide and as
  // narrow as a row gets, and shifted as far as one can be
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_BIND_ROW_OPACITY);
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_BIND_ROW_LIGHT);
  GLIDE_NOW(0, OVERLAY_W, 3 * RW);
  view_now(0, OVERLAY_VIEW_REPEAT_X, OVERLAY_VIEW_REPEAT_X);
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_SIZE, V16(500));
  CHECK(over(X, Y + 5, G).full == c.full && over(X + 3 * RW - 1, Y + 5, G).full == c.full);
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_SIZE, V8(1));
  BIND(0, OVERLAY_BIND_SET | OVERLAY_BIND_ROW_SHIFT, V32(INT32_MAX));
  CHECK(over(X, Y + 5, G).full == c.full && over(X + 3 * RW - 1, Y + 5, G).full == c.full);
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_BIND_ROW_SIZE);
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_BIND_ROW_SHIFT);
  view_now(0, OVERLAY_VIEW_REPEAT_X, 0);
  GLIDE_NOW(0, OVERLAY_W, RW);
  // turned, its rows aren't the screen's, and it's drawn as it is
  GLIDE_NOW(0, OVERLAY_TURN, 90);
  overlay_look_t l;
  CHECK(overlay_look(0, &l));
  CHECK(over(l.box.x1 + 2, l.box.y1 + 2, G).full == c.full && over(l.box.x2 - 2, l.box.y2 - 2, G).full == c.full);
  no_overlays();
}

// OVERLAY_VIEW_SMOOTH: on whole pixels it's drawn as without, part of a pixel on its edges are part of what's behind,
// and turned they're soft. Its box is a pixel more each way.
static void test_smooth(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  const uint32_t G = 0x204060;
  lv_color_t g = lv_color_hex(G), c = lv_color_hex(rgb(5));
  show_overlays();
  shape_still(a_block);
  static lv_color_t hard[RH][RW];
  for (int y = 0; y < RH; y++)
    for (int x = 0; x < RW; x++)
      hard[y][x] = over(X + x, Y + y, G);
  overlay_look_t l;
  view_now(0, OVERLAY_VIEW_SMOOTH, OVERLAY_VIEW_SMOOTH);
  CHECK(overlay_look(0, &l) && l.box.x1 == X - 1 && l.box.x2 == X + RW && l.box.y1 == Y - 1 && l.box.y2 == Y + RH);
  bool same = true;
  for (int y = 0; y < RH; y++)
    for (int x = 0; x < RW; x++)
      same &= over(X + x, Y + y, G).full == hard[y][x].full;
  CHECK(same);
  // half a px to the right: half of it on the pixel at each end
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_X, V16(500));
  lv_color_t half = lv_color_mix(c, g, 128);
  CHECK(apart(over(X + 10, Y + 8, G), half) < 3 && apart(over(X + 30, Y + 8, G), half) < 3);
  CHECK(over(X + 11, Y + 8, G).full == c.full && over(X + 29, Y + 8, G).full == c.full);
  CHECK(over(X + 9, Y + 8, G).full == g.full && over(X + 31, Y + 8, G).full == g.full);
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_Y, V16(250));  // and a quarter down: three quarters of the top row
  CHECK(apart(over(X + 15, Y + 4, G), lv_color_mix(c, g, 192)) < 3);
  CHECK(apart(over(X + 15, Y + 12, G), lv_color_mix(c, g, 64)) < 3);
  // without it that's where it was, and nothing's drawn anew
  view_now(0, OVERLAY_VIEW_SMOOTH, 0);
  same = true;
  for (int y = 0; y < RH; y++)
    for (int x = 0; x < RW; x++)
      same &= over(X + x, Y + y, G).full == hard[y][x].full;
  CHECK(same);
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_X);
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_Y);
  // a tenth of a px a step: drawn again each px, and smooth each quarter of one, not each step
  uint8_t m[OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_DRIFT, m, sim_overlay_drift(m, 0, EACH(OVERLAY_X, 2)));
  sim_run(50);
  unsigned long px = sim_px, plain;
  sim_run(2000);
  plain = sim_px - px;
  view_now(0, OVERLAY_VIEW_SMOOTH, OVERLAY_VIEW_SMOOTH);
  sim_run(50);
  px = sim_px;
  sim_run(2000);
  CHECK(sim_px - px >= 3 * plain && sim_px - px <= 5 * plain && !sim_stale());
  overlay_msg(K_OVERLAY_DRIFT, m, sim_overlay_drift(m, 0, EACH(OVERLAY_X, 0)));
  // turned an eighth: some of its pixels between its colour and what's behind, none without
  place(0, 1, X, Y, 0, 0);
  GLIDE_NOW(0, OVERLAY_TURN, 45);
  int soft[2] = {0, 0};
  for (int on = 0; on < 2; on++) {
    view_now(0, OVERLAY_VIEW_SMOOTH, on ? OVERLAY_VIEW_SMOOTH : 0);
    CHECK(overlay_look(0, &l));
    for (int y = l.box.y1; y <= l.box.y2; y++)
      for (int x = l.box.x1; x <= l.box.x2; x++) {
        lv_color_t k = over(x, y, G);
        soft[on] += k.full != c.full && k.full != g.full;
      }
  }
  CHECK(!soft[0] && soft[1] > 20);
  no_overlays();
}

// The cycling colours of a slot: a place's phase takes each on to the one after it, round them, part of the way
// between two, and drifts
static void test_cycle(void) {
  no_overlays();
  pic_msg(K_PIC_CLEAR, NULL, 0);
  const uint32_t G = 0x204060;
  static uint8_t f[4][4] = {{0x12, 0x34, 0x56, 0xFF}}, st[64];  // colours 1 to 6 across, then clear
  for (int y = 1; y < 4; y++)
    memcpy(f[y], f[0], sizeof f[0]);
  uint32_t n = plink_overlay_pieces(&f[0][0], 8, 4, 1u << 15, 10, st, sizeof st);
  uint8_t b[OVERLAY_BEGIN_LEN];
  int bn = sim_overlay_begin(b, ++id, 0, OVERLAY_STILL, 8, 4, 1, n, clear15(), 0, 0);
  plink_put16(b + OVERLAY_BEGIN_CYCLES, 1u << 2 | 1u << 3 | 1u << 5);  // 2, 3 and 5 go round
  CHECK_EQ(status(rest(pic_msg(K_OVERLAY_BEGIN, b, bn), st, n, 200, plink_crc32(0, st, n))), PIC_DONE);
  show_overlays();
  place(0, 0, X, Y, 0, 0);
#define SHOWS(x, k) (over(X + (x), Y, G).full == lv_color_hex(rgb(k)).full)
  CHECK(SHOWS(0, 1) && SHOWS(1, 2) && SHOWS(2, 3) && SHOWS(3, 4) && SHOWS(4, 5) && SHOWS(5, 6));
  GLIDE_NOW(0, OVERLAY_PHASE, 1);
  CHECK(SHOWS(0, 1) && SHOWS(1, 3) && SHOWS(2, 5) && SHOWS(3, 4) && SHOWS(4, 2) && SHOWS(5, 6));
  GLIDE_NOW(0, OVERLAY_PHASE, 2);
  CHECK(SHOWS(1, 5) && SHOWS(2, 2) && SHOWS(4, 3));
  GLIDE_NOW(0, OVERLAY_PHASE, -3);  // all the way round
  CHECK(SHOWS(1, 2) && SHOWS(2, 3) && SHOWS(4, 5));
  BIND(0, OVERLAY_BIND_ADD | OVERLAY_PHASE, V16(500));  // half way on to the next
  CHECK(over(X + 1, Y, G).full == lv_color_mix(lv_color_hex(rgb(3)), lv_color_hex(rgb(2)), 128).full && SHOWS(0, 1));
  BIND(0, OVERLAY_BIND_FREE | OVERLAY_PHASE);
  uint8_t m[OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH];
  overlay_msg(K_OVERLAY_DRIFT, m, sim_overlay_drift(m, 0, EACH(OVERLAY_PHASE, 2)));  // 2 colours a second
  sim_run(1000);
  CHECK(SHOWS(1, 5) && SHOWS(2, 2) && SHOWS(4, 3) && !sim_stale());
  sim_run(500);
  CHECK(SHOWS(1, 2) && SHOWS(2, 3) && SHOWS(4, 5) && !sim_stale());
  no_overlays();
}

// ---------------------------------------------------------------- sheets

// The dot's frames as a sheet, frame f delay + f ticks: the record that starts it, then its pieces
static uint32_t sheet_records(uint8_t *d, int delay) {
  uint32_t n = 0;
  for (int f = 0; f < 4; f++) {
    memcpy(d + n, (uint8_t[]) {(uint8_t) (delay + f), OVERLAY_RECORD_FRAME, 0, 0, 0}, OVERLAY_RECORD_HEAD);
    n += OVERLAY_RECORD_HEAD;
    n += plink_overlay_pieces(&an[f][0][0], AW, AH, 1u << 15, 0, d + n, 256);
  }
  return n;
}

// Which of the 4 frames place k shows, -1 for none
static int frame_shown(int k) {
  for (int f = 0; f < 4; f++)
    if (overlay_shows(k, f))
      return f;
  return -1;
}

// A sheet plays its frames as an animated slot does, in step or on its own and in scripts, keeping no frame for
// either, and one that doesn't start with a frame is turned down
static void test_sheet(void) {
  no_overlays();
  dot_frames(true);
  uint8_t a[512], sh[512];
  uint32_t an_n = 0;
  for (int f = 0; f <= 4; f++)  // the first frame as long each round, as a sheet's is
    an_n +=
        plink_overlay_record(f ? &an[f - 1][0][0] : NULL, &an[f % 4][0][0], AW, AH, (uint8_t) (5 + f % 4), a + an_n);
  uint32_t sh_n = sheet_records(sh, 5);
  CHECK_EQ(status(send_kind(OVERLAY_ANIMATED, 0, AW, AH, 2, clear15(), an_n, a, 200)), PIC_DONE);
  CHECK_EQ(status(send_kind(OVERLAY_SHEET, 1, AW, AH, 2, clear15(), sh_n, sh, 200)), PIC_DONE);
  CHECK_EQ(overlay_used(), AW / 2 * AH + an_n + sh_n);
  place(0, 0, 0, 0, 0, 0);
  place(2, 1, 40, 0, 0, 0);
  place(1, 0, 80, 0, 1, 2);  // frames of its own, from the third
  place(3, 1, 120, 0, 1, 2);
  CHECK_EQ(overlay_used(), 2 * (AW / 2 * AH) + an_n + sh_n + 2 * (SCRIPT_POOL + 4));
  show_overlays();
  CHECK(frame_shown(1) == 2 && frame_shown(3) == 2);
  bool same = true;
  for (int t = 0; t < 2000; t += 50, sim_run(50))  // 0 and 2 in step with their slots, 1 and 3 on their own
    same &= frame_shown(0) >= 0 && frame_shown(0) == frame_shown(2) && frame_shown(1) == frame_shown(3);
  CHECK(same);
  // a clip, a frame, its rate and its rounds alike
  static const uint8_t S[] = {OVERLAY_DO_CLIP, V16(1),           V16(2), OVERLAY_CLIP_NOW, WHOLE,   OVERLAY_DO_ROUNDS,
                              V16(2),          OVERLAY_DO_FRAME, V16(3), OVERLAY_DO_RATE,  V16(50), OVERLAY_DO_ROUNDS,
                              V16(1),          OVERLAY_DO_MARK,  4};
  script_to(1, S, sizeof S);
  script_to(3, S, sizeof S);
  same = true;
  for (int t = 0; t < 2500; t += 50, sim_run(50))
    same &= frame_shown(1) >= 0 && frame_shown(1) == frame_shown(3);
  CHECK(same);
  CHECK_EQ(overlay_mark(), 1 | 4 << 8);
  CHECK_EQ(overlay_mark(), 3 | 4 << 8);
  CHECK(!sim_stale());
  lv_obj_clean(lv_scr_act());
  // its first record a piece, or one that makes nothing longer
  CHECK_EQ(status(send_kind(OVERLAY_SHEET, 1, AW, AH, 2, clear15(), sh_n - OVERLAY_RECORD_HEAD,
                            sh + OVERLAY_RECORD_HEAD, 200)),
           PIC_BAD);
  sh[OVERLAY_RECORD_X] = OVERLAY_RECORD_MORE;
  CHECK_EQ(status(send_kind(OVERLAY_SHEET, 1, AW, AH, 2, clear15(), sh_n, sh, 200)), PIC_BAD);
  no_overlays();
}

// Pieces that don't fit their still, or run past their rows or the end
static void test_still_bad(void) {
  no_overlays();
  uint8_t d[64];
  // 9 rows of 320 pixels, each in two runs, more than OVERLAY_PIECE_MAX. 8 fit.
  for (int rows = 9; rows >= 8; rows--) {
    memcpy(d, (uint8_t[]) {10, 0, 0, PIC_MAX_W / 2, (uint8_t) rows}, OVERLAY_RECORD_HEAD);
    for (int r = 0; r < rows; r++)
      memcpy(d + OVERLAY_RECORD_HEAD + 4 * r, (uint8_t[]) {255, 0x11, 157, 0x11}, 4);
    uint32_t n = OVERLAY_RECORD_HEAD + 4u * (unsigned) rows;
    CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, PIC_MAX_W, 16, 1, clear15(), n, d, 20)),
             rows == 8 ? PIC_DONE : PIC_BAD);
    if (rows == 8)  // and cut short
      CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, PIC_MAX_W, 16, 1, clear15(), n - 1, d, 20)), PIC_BAD);
  }
  CHECK_EQ(overlay_used(), 0);
  static const struct {
    uint8_t d[9];
    uint32_t n;
  } BAD[] = {
      {{10, 0, 0, 1, 1, 0x81, 0x11}, 7},   // a run of 3 in a row of 1
      {{10, 0, 0, 1, 1, 0x01, 0x11}, 7},   // 2 literal bytes in a row of 1
      {{10, 0, 0, 2, 1, 0x01, 0x11}, 7},   // and past the end
      {{10, 0, 3, 1, 2, 0, 1, 0, 2}, 9},   // rows past the frame's
      {{10, 4, 0, 1, 1, 0, 1}, 7},         // past its right edge
      {{10, 0, 0, 1, 0}, 5},               // no rows but a width
      {{10, 0, 0, 0, 0, 10, 0, 0, 0}, 9},  // and a piece cut short after an empty one
  };
  for (int i = 0; i < (int) LEN(BAD); i++)
    if (!CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, 8, 4, 1, clear15(), BAD[i].n, BAD[i].d, 3)), PIC_BAD))
      printf("    piece %d\n", i);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, 8, 4, 1, clear15(), 0, d, 3)), PIC_BAD);  // nothing at all
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 0, 8, 4, 1, clear15(), 7, (uint8_t[]) {10, 0, 0, 1, 1, 0, 0x12}, 3)),
           PIC_DONE);
  CHECK_EQ(overlay_used(), 7);
  no_overlays();
}

// Colours 0, 1, 2, 3 across, over and over
static void four_across(void) {
  for (int y = 0; y < AH; y++)
    for (int x = 0; x < AW / 2; x++)
      an[0][y][x] = (uint8_t) ((x * 2 % 4) << 4 | (x * 2 + 1) % 4);
}

// Each colour at its own opacity, and the place's on top
static void test_colour_opacity(void) {
  no_overlays();
  four_across();
  uint32_t pal[16] = {0x80000000 | rgb(0), 0xFF000000 | rgb(1), rgb(2), 0x11000000 | rgb(3)};
  uint8_t d[64];
  uint32_t n = plink_overlay_record(NULL, &an[0][0][0], AW, AH, 10, d), sn;
  static uint8_t st[64];
  sn = plink_overlay_pieces(&an[0][0][0], AW, AH, 1u << 2, 10, st, sizeof st);
  CHECK_EQ(status(send_kind(OVERLAY_ANIMATED, 0, AW, AH, 1, pal, n, d, 20)), PIC_DONE);
  CHECK_EQ(status(send_kind(OVERLAY_STILL, 1, AW, AH, 1, pal, sn, st, 20)), PIC_DONE);
  show_overlays();
  lv_color_t behind = lv_color_hex(BEHIND), buf[AW * AH];
  for (int slot = 0; slot < 2; slot++)
    for (int half = 0; half < 2; half++) {
      place(0, slot, 10, 10, 0, 0);
      if (half)
        GLIDE_NOW(0, OVERLAY_OPACITY, 50);
      overlay_look_t l;
      CHECK(draw_box(0, &l, buf, PIC_MAX_H));
      CHECK(l.colour_opa[0] == 136 && l.colour_opa[1] == 255 && l.colour_opa[2] == 0 && l.colour_opa[3] == 17 &&
            l.colour_opa[15] == 0);
      static const lv_opa_t OPA[2][4] = {{136, 255, 0, 17}, {68, 128, 0, 9}};
      int off = 0;
      for (int i = 0; i < AW * AH; i++) {
        int c = i % 4;
        lv_color_t want = lv_color_mix(lv_color_hex(rgb(c)), behind, OPA[half][c]);
        if (OPA[half][c] == 255)
          want = lv_color_hex(rgb(c));
        off += buf[i].full != (OPA[half][c] ? want : behind).full;
      }
      if (!CHECK_EQ(off, 0))
        printf("    slot %d at %s\n", slot, half ? "50 %" : "100 %");
    }
  no_overlays();
}

#define SHADE 0x204080  // what the light goes toward below 100
#define MID 0x788478    // half way from black to white, as the tint sees it

static void tint_msg(int p, int on, uint32_t c) {
  uint8_t m[OVERLAY_TINT_LEN] = {(uint8_t) p, (uint8_t) on, (uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c};
  overlay_msg(K_OVERLAY_TINT, m, sizeof m);
}

// A place's light toward the slot's shade or white, its tint over that, and the colour that glows left as it is
static void test_colour_light(void) {
  no_overlays();
  four_across();
  uint32_t pal[16] = {0xFF000000, 0xFFFFFFFF, 0xFF000000 | rgb(2), 0xFF000000 | MID};
  uint8_t d[64], b[OVERLAY_BEGIN_LEN];
  uint32_t n = plink_overlay_record(NULL, &an[0][0][0], AW, AH, 10, d);
  int k = sim_overlay_begin(b, ++id, 0, OVERLAY_ANIMATED, AW, AH, 1, n, pal, 1u << 2, SHADE);
  CHECK_EQ(status(rest(pic_msg(K_OVERLAY_BEGIN, b, k), d, n, 20, plink_crc32(0, d, n))), PIC_DONE);
  show_overlays();
  place(0, 0, 10, 10, 0, 0);
  CHECK_EQ(lv_color_brightness(lv_color_hex(MID)), 128);
  lv_color_t black = lv_color_black(), white = lv_color_white(), shade = lv_color_hex(SHADE), mid = lv_color_hex(MID),
             red = lv_color_hex(0xFF0000), buf[AW * AH];
  const struct {
    int light, tint;
    lv_color_t want[3];  // colours 0, 1 and 3
  } CASES[] = {
      {100, 0, {black, white, mid}},
      {0, 0, {shade, shade, shade}},
      {200, 0, {white, white, white}},
      {50, 0, {lv_color_mix(black, shade, 128), lv_color_mix(white, shade, 128), lv_color_mix(mid, shade, 128)}},
      {100, 1, {black, white, red}},
      {0, 1, {shade, shade, shade}},  // the light over the tint
  };
  for (unsigned i = 0; i < sizeof CASES / sizeof *CASES; i++) {
    GLIDE_NOW(0, OVERLAY_LIGHT, CASES[i].light);
    tint_msg(0, CASES[i].tint, 0xFF0000);
    sim_run(50);
    overlay_look_t l;
    CHECK(draw_box(0, &l, buf, PIC_MAX_H));
    CHECK_EQ(l.light, CASES[i].light * 10);
    int off = 0;
    for (int j = 0; j < AW * AH; j++) {
      int c = j % 4;
      lv_color_t want = c == 2 ? lv_color_hex(rgb(2)) : CASES[i].want[c / 2 + c % 2];
      off += buf[j].full != want.full || l.pal[c].full != want.full;
    }
    if (!CHECK_EQ(off, 0))
      printf("    light %d%s\n", CASES[i].light, CASES[i].tint ? ", tinted" : "");
    CHECK(!sim_stale());
  }
  overlay_look_t l;
  GLIDE_NOW(0, OVERLAY_LIGHT, OVERLAY_LIGHT_MAX + 1);  // too bright, so not at all
  CHECK(overlay_look(0, &l) && l.light == 0);
  GLIDE_NOW(0, OVERLAY_LIGHT, 100);
  tint_msg(0, 1, 0xFF0000);
  tint_msg(0, 2, 0x00FF00);  // neither on nor off
  CHECK(overlay_look(0, &l) && l.pal[3].full == red.full);
  tint_msg(0, 0, 0);
  CHECK(overlay_look(0, &l) && l.pal[3].full == mid.full);
  tint_msg(0, 1, 0xFF0000);
  GLIDE_NOW(0, OVERLAY_LIGHT, 50);
  place(0, 0, 10, 10, 0, 0);  // starts in its own colours
  CHECK(overlay_look(0, &l) && l.light == 1000 && l.pal[3].full == mid.full);
  no_overlays();
}

const check_test_t OVERLAY_TESTS[] = {
    {"overlay", test_anim},
    {"overlay_slots", test_anim_slots},
    {"overlay_rounds", test_anim_rounds},
    {"overlay_frames", test_anim_frames},
    {"overlay_parts", test_anim_parts},
    {"overlay_still", test_still},
    {"overlay_still_bad", test_still_bad},
    {"overlay_views", test_views},
    {"overlay_blends", test_blends},
    {"overlay_sheet", test_sheet},
    {"overlay_colour_opacity", test_colour_opacity},
    {"overlay_colour_light", test_colour_light},
    {"overlay_fields", test_fields},
    {"overlay_rows", test_rows},
    {"overlay_smooth", test_smooth},
    {"overlay_cycle", test_cycle},
    {0},
};
