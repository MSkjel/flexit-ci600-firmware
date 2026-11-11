// What the LCD was last sent, see sent.h
#include "sent.h"
#include "bits.h"
#include "shot.h"
#include <string.h>

#define COLS (SCR_W / SENT_TILE_W)
#define ROWS (SCR_H / SENT_TILE_H)
#define FORGET_MS 1000
_Static_assert(SCR_W % SENT_TILE_W == 0 && SCR_H % SENT_TILE_H == 0, "the screen is whole tiles");
_Static_assert(sizeof(lv_color_t) == 2 && SENT_TILE_W % 2 == 0, "sum() takes the pixels two at a time");

static uint32_t sums[ROWS * COLS];               // each tile's pixels as the LCD was sent them
static uint32_t known[(ROWS * COLS + 31) / 32];  // a bit for each tile the LCD is known to show so. None at the start

// The tiles a strip is on: the first one's column and row, and how many across and down
typedef struct {
  unsigned col, row, cols, rows;
} span_t;

static span_t span(const lv_area_t *a) {
  unsigned col = (unsigned) a->x1 / SENT_TILE_W, row = (unsigned) a->y1 / SENT_TILE_H;
  return (span_t) {col, row, (unsigned) a->x2 / SENT_TILE_W - col + 1, (unsigned) a->y2 / SENT_TILE_H - row + 1};
}

// A bit for each of n tiles. More than 32 are all of a narrow strip down the screen, which then goes whole.
static uint32_t bits(unsigned n) { return n < 32 ? (1u << n) - 1 : ~0u; }

// A tile's pixels, its rows pitch apart, as one number: MurmurHash3's mixing, two pixels at a time. A change to one
// pair always gives another number, a change to more all but once in 2^32.
static uint32_t sum(const lv_color_t *px, unsigned pitch) {
  uint32_t h = 0;
  for (int y = 0; y < SENT_TILE_H; y++, px += pitch)
    for (int x = 0; x < SENT_TILE_W; x += 2) {
      uint32_t k;
      memcpy(&k, px + x, sizeof k);
      k *= 0xCC9E2D51;
      h ^= (k << 15 | k >> 17) * 0x1B873593;
      h = (h << 13 | h >> 19) * 5 + 0xE6546B64;
    }
  return h;
}

// A tile garbled on its way to the LCD would stay until it's drawn differently. So a row of tiles in turn counts as
// unknown every FORGET_MS, and goes again when it's next drawn.
static void forget(void) {
  static uint32_t last;
  static uint8_t row;
  if (lv_tick_elaps(last) < FORGET_MS)
    return;
  last = lv_tick_get();
  for (unsigned c = 0; c < COLS; c++)
    bit_put(known, row * COLS + c, false);
  row = (uint8_t) ((row + 1) % ROWS);
}

// Tile t at x, y goes to the LCD with pixels we have no sum of
static void unknown(unsigned t, int x, int y) {
  bit_put(known, t, false);
  shot_changed(x, y);
}

uint32_t sent_all(const lv_area_t *a) {
  span_t s = span(a);
  for (unsigned r = s.row; r < s.row + s.rows; r++)
    for (unsigned c = s.col; c < s.col + s.cols; c++)
      unknown(r * COLS + c, (int) (c * SENT_TILE_W), (int) (r * SENT_TILE_H));
  return bits(s.cols * s.rows);
}

uint32_t sent_changed(const lv_area_t *a, const lv_color_t *px) {
  span_t s = span(a);
  if (s.cols * s.rows > 32)
    return sent_all(a);
  unsigned w = (unsigned) lv_area_get_width(a);
  uint32_t changed = 0;
  forget();
  for (unsigned r = 0, i = 0; r < s.rows; r++)
    for (unsigned c = 0; c < s.cols; c++, i++) {
      unsigned t = (s.row + r) * COLS + s.col + c;
      int x = (int) ((s.col + c) * SENT_TILE_W), y = (int) ((s.row + r) * SENT_TILE_H);
      if (x < a->x1 || x + SENT_TILE_W - 1 > a->x2 || y < a->y1 || y + SENT_TILE_H - 1 > a->y2) {
        unknown(t, x, y);  // only a part of it is here
      } else {
        uint32_t now = sum(px + (unsigned) (y - a->y1) * w + (unsigned) (x - a->x1), w);
        if (sums[t] != now)
          shot_changed(x, y);
        else if (bit(known, t))
          continue;
        sums[t] = now;
        bit_put(known, t, true);
      }
      changed |= 1u << i;
    }
  return changed;
}

const lv_color_t *sent_run(const lv_area_t *a, const lv_color_t *px, uint32_t *changed, lv_area_t *run) {
  if (!*changed)
    return NULL;
  span_t s = span(a);
  *run = *a;
  if (s.cols * s.rows > 32) {
    *changed = 0;
    return px;
  }
  unsigned r = (unsigned) __builtin_ctz(*changed) / s.cols, n = 0;
  uint32_t all = bits(s.cols), row = *changed >> r * s.cols & all;
  uint32_t tiles = row & ~(row + (row & -row)), beside = (tiles << 1 | tiles >> 1) & ~tiles & all;
  do
    *changed &= ~(tiles << (r + n++) * s.cols);
  while (r + n < s.rows && (*changed >> (r + n) * s.cols & (tiles | beside)) == tiles);
  int x1 = (int) ((s.col + (unsigned) __builtin_ctz(tiles)) * SENT_TILE_W), y1 = (int) ((s.row + r) * SENT_TILE_H);
  int x2 = (int) ((s.col + 32 - (unsigned) __builtin_clz(tiles)) * SENT_TILE_W) - 1,
      y2 = y1 + (int) n * SENT_TILE_H - 1;
  run->x1 = (lv_coord_t) LV_MAX(a->x1, x1);
  run->x2 = (lv_coord_t) LV_MIN(a->x2, x2);
  run->y1 = (lv_coord_t) LV_MAX(a->y1, y1);
  run->y2 = (lv_coord_t) LV_MIN(a->y2, y2);
  return px + (run->y1 - a->y1) * lv_area_get_width(a) + (run->x1 - a->x1);
}
