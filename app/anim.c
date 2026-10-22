// Screensaver animation, see anim.h
#include "anim.h"
#include "input.h"
#include "pdeflate.h"
#include "picture.h"
#include "plink.h"
#include <string.h>

#define MIN_FRAME_MS 40  // shortest frame time, whatever the record asks for

static uint8_t pool[ANIM_POOL];  // current frame unpacked, w * h / 2 bytes, then the records

static struct {
  bool ready;
  uint16_t w, h, x, y;    // size in animation pixels, position on the screen
  uint8_t scale, transp;  // scale 1 or 2. transp = transparent palette index, ANIM_OPAQUE for none
  uint32_t len;           // record bytes
  uint32_t first, next;   // first = the second record, where we loop back to. next = record to show next
  lv_color_t pal[16];
  lv_img_dsc_t dsc;
  lv_obj_t *img;  // on the screensaver, while it's shown
  lv_timer_t *timer;
} A;

static uint32_t frame_bytes(void) { return (uint32_t) A.w / 2 * A.h; }
static uint8_t *records(void) { return pool + frame_bytes(); }
static uint16_t get16(const uint8_t *b) { return (uint16_t) (b[0] | b[1] << 8); }

void anim_clear(void) { A.ready = false; }
bool anim_ready(void) { return A.ready; }

int anim_open(const uint8_t *b, int n, uint32_t *len) {
  anim_clear();
  if (n != 63)
    return PIC_BAD;
  A.w = get16(b + 1);
  A.h = get16(b + 3);
  A.scale = b[5];
  A.x = get16(b + 6);
  A.y = get16(b + 8);
  A.transp = b[10];
  A.len = *len = plink_number(b + 11, 4);
  for (int i = 0; i < 16; i++)
    A.pal[i] = lv_color_make(b[15 + 3 * i], b[16 + 3 * i], b[17 + 3 * i]);
  if (!A.w || !A.h || A.w % 2 || A.h > 255 || (A.scale != 1 && A.scale != 2) || A.transp > ANIM_OPAQUE ||
      A.x + A.w * A.scale > PIC_MAX_W || A.y + A.h * A.scale > PIC_MAX_H)
    return PIC_BAD_SIZE;
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
  lv_area_t c;
  lv_obj_get_coords(A.img, &c);
  a = (lv_area_t) {c.x1 + a.x1 * A.scale, c.y1 + a.y1 * A.scale, c.x1 + (a.x2 + 1) * A.scale - 1,
                   c.y1 + (a.y2 + 1) * A.scale - 1};
  lv_obj_invalidate_area(A.img, &a);
}

static void gone(lv_event_t *e) {
  if (lv_event_get_target(e) == A.img)
    A.img = NULL;
}

lv_obj_t *anim_show(lv_obj_t *parent) {
  if (!A.ready)
    return NULL;
  A.img = lv_img_create(parent);
  lv_img_set_src(A.img, &A.dsc);
  lv_obj_set_pos(A.img, A.x, A.y);
  lv_obj_add_event_cb(A.img, gone, LV_EVENT_DELETE, NULL);
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
}
