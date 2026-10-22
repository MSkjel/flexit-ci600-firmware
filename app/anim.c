// Screensaver animation, see anim.h
#include "anim.h"
#include "input.h"
#include "pdeflate.h"
#include "picture.h"
#include "plink.h"
#include <string.h>

#define MIN_FRAME_MS 40  // shortest frame time, whatever the record asks for
#define MOVE_MS 50       // a motion step, 20 a second

static uint8_t pool[ANIM_POOL];  // current frame unpacked, w * h / 2 bytes, then the records

static struct {
  bool ready;
  uint16_t w, h;   // size in animation pixels
  int16_t x, y;    // position on the screen, can be partly off it while moving
  int32_t fx, fy;  // the same in 1/1000 px, where a drift adds up
  int16_t vx, vy;  // drift in px/s, 0 0 = none
  bool torus;      // a drift put it where it is, so the part over an edge shows at the other one
  struct {
    int16_t x0, y0, x1, y1;  // from, to
    uint16_t ms, t;          // over ms, t of them gone. ms = 0 is no glide
  } glide;
  uint8_t scale, transp;  // scale 1 or 2. transp = transparent palette index, ANIM_OPAQUE for none
  uint32_t len;           // record bytes
  uint32_t first, next;   // first = the second record, where we loop back to. next = record to show next
  lv_color_t pal[16];
  lv_img_dsc_t dsc;
  lv_obj_t *img;       // on the screensaver, while it's shown
  lv_obj_t *ghost[3];  // children of img a screen to the left, up, and both, for the part a drift wraps round
  lv_timer_t *timer, *mover;
} A;

static const lv_point_t GHOST_AT[3] = {{-PIC_MAX_W, 0}, {0, -PIC_MAX_H}, {-PIC_MAX_W, -PIC_MAX_H}};

static uint32_t frame_bytes(void) { return (uint32_t) A.w / 2 * A.h; }
static uint8_t *records(void) { return pool + frame_bytes(); }
static uint16_t get16(const uint8_t *b) { return (uint16_t) (b[0] | b[1] << 8); }

static bool moving(void) { return A.vx || A.vy || A.glide.ms; }

static void stop(void) {
  A.vx = A.vy = 0;
  A.glide.ms = 0;
  if (A.mover)
    lv_timer_pause(A.mover);
}

// The ghosts show where it hangs over the right or bottom edge, a drift keeps x, y on the screen for that
static void ghosts(void) {
  bool right = A.torus && A.x + A.w * A.scale > PIC_MAX_W, below = A.torus && A.y + A.h * A.scale > PIC_MAX_H;
  bool on[3] = {right, below, right && below};
  for (int i = 0; i < 3; i++)
    if (A.ghost[i]) {
      if (on[i])
        lv_obj_clear_flag(A.ghost[i], LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(A.ghost[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// LVGL redraws a moved image but not its children
static void ghosts_redraw(void) {
  for (int i = 0; i < 3; i++)
    if (A.ghost[i])
      lv_obj_invalidate(A.ghost[i]);
}

// Puts it at fx, fy (1/1000 px), and the image if it's shown. The old and new areas are redrawn.
static void place(int32_t fx, int32_t fy) {
  int16_t x = A.x, y = A.y;
  A.fx = fx;
  A.fy = fy;
  A.x = (int16_t) ((fx >= 0 ? fx : fx - 999) / 1000);
  A.y = (int16_t) ((fy >= 0 ? fy : fy - 999) / 1000);
  if (!A.img)
    return;
  bool moved = A.x != x || A.y != y;
  if (moved)
    ghosts_redraw();  // where they were
  lv_obj_set_pos(A.img, A.x, A.y);
  lv_obj_update_layout(A.img);
  ghosts();
  if (moved)
    ghosts_redraw();
}

void anim_clear(void) {
  A.ready = false;
  stop();
}
bool anim_ready(void) { return A.ready; }

void anim_pos(int16_t *x, int16_t *y) {
  *x = A.x;
  *y = A.y;
}

int anim_open(const uint8_t *b, int n, uint32_t *len) {
  anim_clear();
  if (n != 63)
    return PIC_BAD;
  A.w = get16(b + 1);
  A.h = get16(b + 3);
  A.scale = b[5];
  unsigned x = get16(b + 6), y = get16(b + 8);
  A.transp = b[10];
  A.len = *len = plink_number(b + 11, 4);
  for (int i = 0; i < 16; i++)
    A.pal[i] = lv_color_make(b[15 + 3 * i], b[16 + 3 * i], b[17 + 3 * i]);
  if (!A.w || !A.h || A.w % 2 || A.h > 255 || (A.scale != 1 && A.scale != 2) || A.transp > ANIM_OPAQUE ||
      x + A.w * A.scale > PIC_MAX_W || y + A.h * A.scale > PIC_MAX_H)
    return PIC_BAD_SIZE;
  A.torus = false;
  place((int32_t) x * 1000, (int32_t) y * 1000);
  return frame_bytes() > ANIM_POOL || A.len > ANIM_POOL - frame_bytes() ? PIC_TOO_BIG : -1;
}

void anim_bytes(uint32_t at, const uint8_t *b, unsigned n) { memcpy(records() + at, b, n); }

// A record is delay, x / 2, y, w / 2, h, then the rectangle deflated. Applies the one at offset at to the frame,
// or only checks it. Returns its length, 0 if bad. area gets the changed rectangle in animation pixels.
static uint32_t record(uint32_t at, bool apply, lv_area_t *area) {
  const uint8_t *r = records() + at, *end = records() + A.len;
  unsigned rb = A.w / 2u;
  if (end - r < 5 || r[1] + r[3] > rb || r[2] + r[4] > A.h || !r[3] != !r[4])
    return 0;
  uint32_t n = 0;
  if (r[3]) {
    n = pinflate(r + 5, (uint32_t) (end - r - 5), apply ? pool + r[2] * rb + r[1] : NULL, r[3], rb,
                 (uint32_t) r[3] * r[4]);
    if (!n)
      return 0;
  }
  if (area)
    *area = (lv_area_t) {r[1] * 2, r[2], (r[1] + r[3]) * 2 - 1, r[2] + r[4] - 1};
  return 5 + n;
}

// a record's delay is in 10 ms
static uint32_t period(uint8_t delay) { return delay * 10u < MIN_FRAME_MS ? MIN_FRAME_MS : delay * 10u; }

bool anim_close(void) {
  uint32_t at = 0;
  while (at < A.len) {
    uint32_t k = record(at, false, NULL);
    if (!k)
      return false;
    at += k;
  }
  if (!A.len)
    return false;
  memset(pool, A.transp < 16 ? A.transp * 0x11 : 0, frame_bytes());  // all transparent, or colour 0 if opaque
  A.first = A.next = record(0, true, NULL);
  memset(&A.dsc, 0, sizeof A.dsc);
  A.dsc.header.cf = LV_IMG_CF_USER_ENCODED_1;
  A.dsc.header.w = A.w * A.scale;
  A.dsc.header.h = A.h * A.scale;
  A.dsc.data = pool;
  A.dsc.data_size = frame_bytes();
  lv_timer_set_period(A.timer, period(records()[0]));
  A.ready = true;
  return true;
}

// apply the next record and redraw what it changed
static void tick(lv_timer_t *t) {
  if (!A.ready || !A.img || A.first >= A.len || input_backlight() < 0)
    return;
  uint8_t delay = records()[A.next];
  lv_area_t a;
  A.next += record(A.next, true, &a);
  if (A.next >= A.len)
    A.next = A.first;
  lv_timer_set_period(t, period(delay));
  if (a.x2 < a.x1)
    return;  // nothing changed
  for (int i = -1; i < 3; i++) {
    lv_obj_t *o = i < 0 ? A.img : A.ghost[i];
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
      continue;
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    lv_area_t d = {c.x1 + a.x1 * A.scale, c.y1 + a.y1 * A.scale, c.x1 + (a.x2 + 1) * A.scale - 1,
                   c.y1 + (a.y2 + 1) * A.scale - 1};
    lv_obj_invalidate_area(o, &d);
  }
}

// One step of a glide or drift. A drift wraps round like on a torus, the ghosts draw the part over the edge.
static void move_step(lv_timer_t *t) {
  if (!A.ready || !A.img || input_backlight() < 0)
    return;
  if (A.glide.ms) {
    A.glide.t = A.glide.ms - A.glide.t > MOVE_MS ? (uint16_t) (A.glide.t + MOVE_MS) : A.glide.ms;
    int32_t x = A.glide.x0 + (int32_t) ((int64_t) (A.glide.x1 - A.glide.x0) * A.glide.t / A.glide.ms),
            y = A.glide.y0 + (int32_t) ((int64_t) (A.glide.y1 - A.glide.y0) * A.glide.t / A.glide.ms);
    place(x * 1000, y * 1000);
    if (A.glide.t == A.glide.ms)
      stop();
    return;
  }
  if (!moving()) {
    lv_timer_pause(t);
    return;
  }
  int32_t fx = (A.fx + A.vx * MOVE_MS) % (PIC_MAX_W * 1000), fy = (A.fy + A.vy * MOVE_MS) % (PIC_MAX_H * 1000);
  A.torus = true;
  place(fx < 0 ? fx + PIC_MAX_W * 1000 : fx, fy < 0 ? fy + PIC_MAX_H * 1000 : fy);
}

void anim_move(const uint8_t *b, int n) {
  if (!A.ready || n < 1 || n != (b[0] == ANIM_GLIDE ? 7 : b[0] == ANIM_DRIFT ? 5 : 1) || b[0] > ANIM_DRIFT)
    return;
  stop();
  if (b[0] == ANIM_GLIDE) {
    int16_t x = (int16_t) get16(b + 1), y = (int16_t) get16(b + 3);
    uint16_t ms = get16(b + 5);
    A.torus = false;  // a glide can end partly off the screen, and that's all
    if (!ms) {
      place((int32_t) x * 1000, (int32_t) y * 1000);
      return;
    }
    A.glide.x0 = A.x;
    A.glide.y0 = A.y;
    A.glide.x1 = x;
    A.glide.y1 = y;
    A.glide.ms = ms;
    A.glide.t = 0;
  } else if (b[0] == ANIM_DRIFT) {
    A.vx = (int16_t) get16(b + 1);
    A.vy = (int16_t) get16(b + 3);
  }
  if (moving()) {
    lv_timer_reset(A.mover);  // or the first step comes at once
    lv_timer_resume(A.mover);
  }
}

static void gone(lv_event_t *e) {
  if (lv_event_get_target(e) == A.img) {
    A.img = NULL;
    memset(A.ghost, 0, sizeof A.ghost);  // its children go with it
  }
}

lv_obj_t *anim_show(lv_obj_t *parent) {
  if (!A.ready)
    return NULL;
  A.img = lv_img_create(parent);
  lv_img_set_src(A.img, &A.dsc);
  lv_obj_add_flag(A.img, LV_OBJ_FLAG_OVERFLOW_VISIBLE);  // the ghosts hang outside it
  lv_obj_clear_flag(A.img, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(A.img, gone, LV_EVENT_DELETE, NULL);
  for (int i = 0; i < 3; i++) {
    A.ghost[i] = lv_img_create(A.img);
    lv_img_set_src(A.ghost[i], &A.dsc);
    lv_obj_set_pos(A.ghost[i], GHOST_AT[i].x, GHOST_AT[i].y);
  }
  place(A.fx, A.fy);
  return A.img;
}

// ---------------------------------------------------------------- LVGL decoder: colour and alpha, a line at a time
static bool ours(const void *src) {
  return lv_img_src_get_type(src) == LV_IMG_SRC_VARIABLE &&
         ((const lv_img_dsc_t *) src)->header.cf == LV_IMG_CF_USER_ENCODED_1;
}

static lv_res_t dec_info(lv_img_decoder_t *d, const void *src, lv_img_header_t *h) {
  (void) d;
  if (!ours(src))
    return LV_RES_INV;
  *h = ((const lv_img_dsc_t *) src)->header;
  h->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;  // with alpha, so the transparent colour shows through
  return LV_RES_OK;
}

static lv_res_t dec_open(lv_img_decoder_t *d, lv_img_decoder_dsc_t *dsc) {
  (void) d;
  if (!ours(dsc->src) || !A.ready)
    return LV_RES_INV;
  dsc->img_data = NULL;  // makes LVGL ask for lines
  return LV_RES_OK;
}

static lv_res_t dec_line(lv_img_decoder_t *d, lv_img_decoder_dsc_t *dsc, lv_coord_t x, lv_coord_t y, lv_coord_t len,
                         uint8_t *buf) {
  (void) d;
  (void) dsc;
  const uint8_t *row = pool + (y / A.scale) * (A.w / 2);
  for (lv_coord_t i = 0; i < len; i++, buf += LV_IMG_PX_SIZE_ALPHA_BYTE) {
    int sx = (x + i) / A.scale;
    uint8_t c = sx & 1 ? row[sx >> 1] & 15 : row[sx >> 1] >> 4;
    memcpy(buf, &A.pal[c], sizeof(lv_color_t));
    buf[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = c == A.transp ? LV_OPA_TRANSP : LV_OPA_COVER;
  }
  return LV_RES_OK;
}

static void dec_close(lv_img_decoder_t *d, lv_img_decoder_dsc_t *dsc) {
  (void) d;
  (void) dsc;
}

void anim_init(void) {
  lv_img_decoder_t *d = lv_img_decoder_create();
  lv_img_decoder_set_info_cb(d, dec_info);
  lv_img_decoder_set_open_cb(d, dec_open);
  lv_img_decoder_set_read_line_cb(d, dec_line);
  lv_img_decoder_set_close_cb(d, dec_close);
  if (!A.timer)
    A.timer = lv_timer_create(tick, 100, NULL);
  if (!A.mover) {
    A.mover = lv_timer_create(move_step, MOVE_MS, NULL);
    lv_timer_pause(A.mover);
  }
}
