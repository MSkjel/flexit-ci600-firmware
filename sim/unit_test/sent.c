// What goes to the LCD: the tiles that changed, and what a strip has only a part of (app/sent.c). The simulator's
// display stops any test in which the LCD doesn't show what was drawn, see flush() in simlib.c.
#include "unit_test.h"

#define TILE (SENT_TILE_W * SENT_TILE_H)

static lv_obj_t *box(int x, int y, int w, int h, uint32_t rgb) {
  lv_obj_t *o = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, (lv_coord_t) x, (lv_coord_t) y);
  lv_obj_set_size(o, (lv_coord_t) w, (lv_coord_t) h);
  lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

// Draws area a again. Returns the pixels that went to the LCD for it.
static unsigned long redraw(int x1, int y1, int x2, int y2) {
  unsigned long px = sim_px, sent = sim_sent;
  lv_area_t a = {(lv_coord_t) x1, (lv_coord_t) y1, (lv_coord_t) x2, (lv_coord_t) y2};
  _lv_inv_area(NULL, &a);
  lv_refr_now(NULL);
  CHECK_EQ(sim_px - px, lv_area_get_size(&a));  // no more than was asked for
  return sim_sent - sent;
}

static void test_sent(void) {
  lv_obj_clean(lv_scr_act());
  lv_obj_t *o = box(40, 20, 3, 3, 0xFF0000);
  lv_refr_now(NULL);
  // the screen drawn again as it is: none of it is sent
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 0);
  // with another colour in one tile, that tile
  lv_obj_set_style_bg_color(o, lv_color_hex(0x00FF00), 0);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), TILE);
  CHECK_EQ(sim_pixel(41, 21), sim_rgb888(lv_color_hex(0x00FF00).full));
  // over the corner of four
  lv_obj_set_pos(o, 47, 23);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 4 * TILE);
  // two boxes a screen apart: their tiles, not what's drawn between them
  lv_obj_t *far = box(300, 24, 3, 3, 0xFF0000);
  lv_obj_set_pos(o, 40, 24);
  redraw(0, 0, SCR_W - 1, SCR_H - 1);
  lv_obj_set_style_bg_color(o, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(far, lv_color_hex(0xFFFFFF), 0);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 2 * TILE);
  // An area is drawn as it is, not out to whole tiles. The tiles a strip has only a part of always go, and what the
  // LCD has of them isn't known after that. The strips end on 8 lines, so of lines 3 to 20 those are 3 to 7 and 16
  // to 20.
  CHECK_EQ(redraw(0, 3, SCR_W - 1, 20), 2 * 5 * SCR_W);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 2 * SENT_STRIP);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 0);
  // with the tile that changed between them
  lv_obj_set_pos(far, 300, 9);
  redraw(0, 0, SCR_W - 1, SCR_H - 1);
  lv_obj_set_style_bg_color(far, lv_color_hex(0x0000FF), 0);
  CHECK_EQ(redraw(0, 3, SCR_W - 1, 20), 2 * 5 * SCR_W + TILE);
  redraw(0, 0, SCR_W - 1, SCR_H - 1);
  // and of columns 5 to 50 the 11 to the left and the 3 to the right, over two rows of tiles
  CHECK_EQ(redraw(5, 8, 50, 23), (11 + 3) * 16);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 4 * TILE);
  // a small area is one strip whatever lines it's on: of 12 to 27 the 4 above and the 4 below a row of tiles
  CHECK_EQ(redraw(16, 12, 47, 27), 2 * 4 * 32);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 4 * TILE);
  // a narrow one down the screen is on more tiles than a strip tells apart, and goes whole
  CHECK_EQ(redraw(15, 0, 16, SCR_H - 1), 2 * SCR_H);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 2 * SCR_H / SENT_TILE_H * TILE);
  // without comparing all of it goes, and is compared with nothing after
  sim_whole = true;
  CHECK_EQ(redraw(0, 8, SCR_W - 1, 23), 2 * SENT_STRIP);
  sim_whole = false;
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 2 * SENT_STRIP);
  CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 0);
  CHECK_EQ(sim_stale(), 0);
  lv_obj_clean(lv_scr_act());
}

// Drawn the same over and over, a row of tiles still goes again each second, the whole screen in 30
static void test_sent_again(void) {
  lv_obj_clean(lv_scr_act());
  lv_refr_now(NULL);
  for (int row = 0; row < SCR_H / SENT_TILE_H; row++) {
    sim_run(1000);
    CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), SENT_STRIP);
    CHECK_EQ(redraw(0, 0, SCR_W - 1, SCR_H - 1), 0);
  }
}

static bool is(const lv_area_t *a, int x1, int y1, int x2, int y2) {
  return a->x1 == x1 && a->y1 == y1 && a->x2 == x2 && a->y2 == y2;
}

// The rectangles a strip's tiles go in
static void test_sent_runs(void) {
  static lv_color_t px[SENT_STRIP];
  // across the screen, every other pair of tiles
  lv_area_t a = {0, 16, SCR_W - 1, 23}, r;
  uint32_t changed = 0x33333;
  int n = 0;
  for (const lv_color_t *p; (p = sent_run(&a, px, &changed, &r)) != NULL; n++)
    CHECK(is(&r, n * 64, 16, n * 64 + 31, 23) && p == px + n * 64);
  CHECK_EQ(n, 5);
  // a strip of several rows: the rows under a run go with it while they changed just the same
  a = (lv_area_t) {32, 0, 79, 31};
  changed = 6 | 6 << 3 | 7 << 6 | 1 << 9;
  CHECK(sent_run(&a, px, &changed, &r) == px + 16 && is(&r, 48, 0, 79, 15));
  CHECK(sent_run(&a, px, &changed, &r) == px + 2 * 48 * SENT_TILE_H && is(&r, 32, 16, 79, 23));
  CHECK(sent_run(&a, px, &changed, &r) == px + 3 * 48 * SENT_TILE_H && is(&r, 32, 24, 47, 31));
  CHECK(!sent_run(&a, px, &changed, &r) && !changed);
  // Whatever the strip and its tiles, each pixel of it on one of them is in one rectangle, and no other is
  uint32_t seed = 1;
  for (int k = 0; k < 4000; k++) {
    seed = seed * 1664525 + 1013904223;
    int w = 1 + (int) (seed >> 8) % 120, h = 1 + (int) (seed >> 16) % LV_MIN(SENT_STRIP / w, 40);
    int x = (int) (seed >> 3) % (SCR_W - w + 1), y = (int) (seed >> 20) % (SCR_H - h + 1);
    a = (lv_area_t) {(lv_coord_t) x, (lv_coord_t) y, (lv_coord_t) (x + w - 1), (lv_coord_t) (y + h - 1)};
    int cols = a.x2 / SENT_TILE_W - x / SENT_TILE_W + 1, tiles = cols * (a.y2 / SENT_TILE_H - y / SENT_TILE_H + 1);
    seed = seed * 1664525 + 1013904223;
    uint32_t want = tiles > 32 ? ~0u : seed >> 5 & seed >> 13 & (tiles < 32 ? (1u << tiles) - 1 : ~0u);
    static uint8_t in[SENT_STRIP];
    memset(in, 0, sizeof in);
    changed = want;
    bool ok = true;
    for (const lv_color_t *p; (p = sent_run(&a, px, &changed, &r)) != NULL;) {
      ok &= r.x1 >= a.x1 && r.x2 <= a.x2 && r.y1 >= a.y1 && r.y2 <= a.y2 && p == px + (r.y1 - y) * w + r.x1 - x;
      for (int j = r.y1; ok && j <= r.y2; j++)
        for (int i = r.x1; i <= r.x2; i++)
          in[(j - y) * w + i - x]++;
    }
    for (int j = y; j <= a.y2; j++)
      for (int i = x; i <= a.x2; i++) {
        int tile = (j / SENT_TILE_H - y / SENT_TILE_H) * cols + i / SENT_TILE_W - x / SENT_TILE_W;
        ok &= in[(j - y) * w + i - x] == (tiles > 32 || (want >> tile & 1));
      }
    CHECK(ok);
  }
}

const check_test_t SENT_TESTS[] = {
    {"sent", test_sent}, {"sent_again", test_sent_again}, {"sent_runs", test_sent_runs}, {0}};
