// Screenshots for the ESP, see shot.h
#include "shot.h"
#include "bits.h"
#include "pbulk.h"
#include "picture.h"
#include "pjpeg.h"
#include "plink.h"
#include "sent.h"
#include "ui.h"
#include <string.h>

#define LOW PBULK_ANSWER_MAX  // the next tile is drawn once the stream has less than this waiting
#define BUF_SIZE (LOW + 720)  // the stream: what's waiting and the next tile. None in the sim is over 700 at q95
#define AGAIN_MS 200          // a tile we asked for and that wasn't drawn by then is asked for again

// pjpeg_tile()'s drop, more each time a tile doesn't fit. A block of only DC takes at most 24 bits, twice that with
// the zeros stuffed after FF bytes, so the last always fits behind what's waiting.
static const uint16_t DROP[] = {0, 1, 2, 3, 5, 8, 13, 21, 34, PJPEG_DC_ONLY};
_Static_assert(SHOT_TILE_W / 16 * 4 * 8 <= BUF_SIZE - LOW - SHOT_TILE_HEAD, "a tile of only DC fits");
_Static_assert(SHOT_TILE_H == SENT_TILE_H && SHOT_TILE_W % SENT_TILE_W == 0, "the LCD's tiles, a strip has all of one");

typedef uint32_t tiles_t[(SHOT_TILES + 31) / 32];
#define WORDS (sizeof(tiles_t) / sizeof(uint32_t))

static struct {
  tiles_t dirty;     // changed on the LCD since they were last sent
  tiles_t todo;      // still to send for the ESP's SHOT
  uint8_t quality;   // the SHOT's, 0 = none
  uint8_t want;      // the tile we wait to see drawn + 1, 0 = none
  uint32_t asked;    // lv_tick_get() when we asked for it to be drawn
  uint16_t sent;     // tiles since the SHOT
  bool done;         // its SHOT_DONE is still to go
  uint8_t last[2];   // the last two tiles into buf + 1, 0 = none, which may not have gone yet
  uint16_t at, len;  // buf has len bytes of the stream, sent up to at
  uint8_t buf[BUF_SIZE];
} S;

static bool running(void) { return S.quality && (S.want || bit_first(S.todo, WORDS) >= 0); }

static lv_area_t area_of(int t) {
  lv_coord_t x = (lv_coord_t) (t % SHOT_COLS * SHOT_TILE_W), y = (lv_coord_t) (t / SHOT_COLS * SHOT_TILE_H);
  return (lv_area_t) {x, y, (lv_coord_t) (x + SHOT_TILE_W - 1), (lv_coord_t) (y + SHOT_TILE_H - 1)};
}

// The tiles still in buf go back to dirty. Those the link took but the ESP hasn't acked don't: a HELLO makes it
// ask for them all and drop what came before.
void shot_reset(void) {
  if (S.at < S.len)
    for (int i = 0; i < 2; i++)
      if (S.last[i])
        bit_put(S.dirty, (unsigned) (S.last[i] - 1), true);
  memset(S.todo, 0, sizeof S.todo);
  memset(S.last, 0, sizeof S.last);
  S.quality = S.want = 0;
  S.at = S.len = 0;
  S.done = false;
}

void shot_ask(uint32_t v) {
  unsigned q = v & 0xFF;
  if (!q) {
    shot_reset();
    return;
  }
  S.quality = (uint8_t) LV_MIN(q, 100u);
  for (unsigned i = 0; i < WORDS; i++)
    S.todo[i] |= v & SHOT_ALL ? ~0u : S.dirty[i];
  if (SHOT_TILES % 32)
    S.todo[WORDS - 1] &= (1u << SHOT_TILES % 32) - 1;
  S.sent = 0;
  S.done = true;
}

void shot_changed(int x, int y) { bit_put(S.dirty, (unsigned) (y / SHOT_TILE_H * SHOT_COLS + x / SHOT_TILE_W), true); }

typedef struct {  // a flushed area's pixels, and where they are
  const lv_color_t *px;
  int x, y, w;
} strip_t;

static uint32_t pixel(void *ctx, int x, int y) {
  const strip_t *s = ctx;
  return lv_color_to32(s->px[(y - s->y) * s->w + (x - s->x)]) & 0xFFFFFF;
}

void shot_flushed(const lv_area_t *a, const lv_color_t *px) {
  if (!S.want)
    return;
  int t = S.want - 1;
  lv_area_t r = area_of(t);
  if (!_lv_area_is_in(&r, a, 0))
    return;
  // behind what's waiting, less than LOW, see shot_poll()
  unsigned left = (unsigned) (S.len - S.at), room = BUF_SIZE - left - SHOT_TILE_HEAD, n = room + 1;
  memmove(S.buf, S.buf + S.at, left);
  uint8_t *h = S.buf + left;
  strip_t s = {px, a->x1, a->y1, lv_area_get_width(a)};
  for (unsigned k = 0; k < sizeof DROP / sizeof *DROP && n > room; k++)
    n = pjpeg_tile(h + SHOT_TILE_HEAD, room, S.quality, DROP[k], pixel, &s, r.x1, r.y1, SHOT_TILE_W / 16);
  h[SHOT_TILE_NUM] = (uint8_t) t;
  plink_put16(h + SHOT_TILE_LEN, (uint16_t) n);
  S.at = 0;
  S.len = (uint16_t) (left + SHOT_TILE_HEAD + n);
  S.last[1] = S.last[0];
  S.last[0] = (uint8_t) (t + 1);
  bit_put(S.dirty, (unsigned) t, false);
  bit_put(S.todo, (unsigned) t, false);
  S.want = 0;
  S.sent++;
}

void shot_poll(void) {
  if (!running() || S.len - S.at >= LOW || ui_held())
    return;  // none to send, or the next goes behind what's waiting
  if (S.want && lv_tick_elaps(S.asked) < AGAIN_MS)
    return;  // not drawn yet
  int t = S.want ? S.want - 1 : bit_first(S.todo, WORDS);
  if (t < 0)
    return;
  S.want = (uint8_t) (t + 1);
  S.asked = lv_tick_get();
  lv_area_t r = area_of(t);
  _lv_inv_area(lv_disp_get_default(), &r);
}

int shot_take(uint8_t *out, int room) {
  // DATA goes in 4s, the end of the last tile with zeros after it
  int left = S.len - S.at, end = running() ? left & ~3 : (left + 3) & ~3;
  int k = LV_MIN(end, (room - PBULK_MSG_HEAD) & ~3), n = 0;
  if (k > 0) {
    int b = LV_MIN(k, left);
    plink_put16(out, K_PIC_DATA);
    out[2] = (uint8_t) k;
    memcpy(out + PBULK_MSG_HEAD, S.buf + S.at, (size_t) b);
    memset(out + PBULK_MSG_HEAD + b, 0, (size_t) (k - b));
    S.at = (uint16_t) (S.at + b);
    n = PBULK_MSG_HEAD + k;
  }
  if (S.at == S.len)
    S.at = S.len = 0;
  if (S.done && !S.len && !running() && room - n >= PBULK_MSG_HEAD + 4) {
    plink_put16(out + n, K_SHOT_DONE);
    out[n + 2] = (uint8_t) plink_put_number(out + n + PBULK_MSG_HEAD, S.sent);
    n += PBULK_MSG_HEAD + out[n + 2];
    S.done = false;
  }
  return n;
}

unsigned shot_waiting(void) {
  unsigned left = (unsigned) (S.len - S.at), n = left ? left + 3 + PBULK_MSG_HEAD : 0;
  if (running() && n < PBULK_ANSWER_MAX)
    n = PBULK_ANSWER_MAX;  // the next tile is on its way
  return n + (S.done ? PBULK_MSG_HEAD + 1 : 0);
}
