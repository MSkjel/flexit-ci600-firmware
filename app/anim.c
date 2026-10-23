// Screensaver animations, see anim.h
#include "anim.h"
#include "input.h"
#include "pdeflate.h"
#include "picture.h"
#include "plink.h"
#include <string.h>

#define MIN_FRAME_MS 40  // shortest frame time, whatever the record asks for
#define MOVE_MS 50       // a motion step, 20 a second
#define NONE 0xFFFF

// Every slot's current frame (w * h / 2 bytes) and then its records, and the frames places of their own, packed
// from the start. Freeing one moves what's behind it down.
static uint8_t pool[ANIM_POOL];
static unsigned used;

typedef struct {
  bool ready;
  uint16_t w, h;          // size in animation pixels
  uint8_t scale, transp;  // scale 1 or 2. transp = transparent palette index, ANIM_OPAQUE for none
  uint16_t off;           // its frame in pool, the records right after. NONE = empty slot
  uint32_t len;           // record bytes
  uint32_t first, next;   // first = the second record, where we loop back to. next = record to show next
  uint32_t due;           // tick for the next record
  lv_color_t pal[16];
} slot_t;

typedef struct {
  uint8_t slot;    // ANIM_SLOTS = empty place
  uint16_t frame;  // a frame of its own in pool, NONE = the slot's
  uint32_t next, due;
  int16_t x, y;    // position on the screen, can be partly off it while moving
  int32_t fx, fy;  // the same in 1/1000 px, where a drift adds up
  int16_t vx, vy;  // drift in px/s, 0 0 = none
  bool torus;      // a drift put it where it is, so the part over an edge shows at the other one
  struct {
    int16_t x0, y0, x1, y1;  // from, to
    uint16_t ms, t;          // over ms, t of them gone. ms = 0 is no glide
  } glide;
  lv_img_dsc_t dsc;
  lv_obj_t *img;       // on the screensaver, while it's shown
  lv_obj_t *ghost[3];  // children of img a screen to the left, up, and both, for the part a drift wraps round
} place_t;

static slot_t S[ANIM_SLOTS];
static place_t P[ANIM_PLACES];
static uint8_t in = ANIM_SLOTS;  // the slot a transfer is coming in for
static lv_timer_t *timer, *mover;

static const lv_point_t GHOST_AT[3] = {{-PIC_MAX_W, 0}, {0, -PIC_MAX_H}, {-PIC_MAX_W, -PIC_MAX_H}};

static uint32_t frame_bytes(const slot_t *s) { return (uint32_t) s->w / 2 * s->h; }
static uint8_t *records(const slot_t *s) { return pool + s->off + frame_bytes(s); }
static uint8_t *place_frame(const place_t *p) { return pool + (p->frame == NONE ? S[p->slot].off : p->frame); }
static uint16_t get16(const uint8_t *b) { return (uint16_t) (b[0] | b[1] << 8); }
static bool moving(const place_t *p) { return p->vx || p->vy || p->glide.ms; }

// ---------------------------------------------------------------- the pool

// Takes n bytes, NONE if they don't fit
static uint16_t take(uint32_t n) {
  if (n > ANIM_POOL - used)
    return NONE;
  used += n;
  return (uint16_t) (used - n);
}

// Frees the n bytes at off and moves everything behind down
static void give(uint16_t off, uint32_t n) {
  memmove(pool + off, pool + off + n, used - off - n);
  used -= n;
  for (int i = 0; i < ANIM_SLOTS; i++)
    if (S[i].off != NONE && S[i].off > off)
      S[i].off -= (uint16_t) n;
  for (int i = 0; i < ANIM_PLACES; i++)
    if (P[i].frame != NONE && P[i].frame > off)
      P[i].frame -= (uint16_t) n;
}

// ---------------------------------------------------------------- places

static void stop(place_t *p) {
  p->vx = p->vy = 0;
  p->glide.ms = 0;
}

// The ghosts show where it hangs over the right or bottom edge, a drift keeps x, y on the screen for that
static void ghosts(place_t *p) {
  const slot_t *s = &S[p->slot];
  bool right = p->torus && p->x + s->w * s->scale > PIC_MAX_W, below = p->torus && p->y + s->h * s->scale > PIC_MAX_H;
  bool on[3] = {right, below, right && below};
  for (int i = 0; i < 3; i++)
    if (p->ghost[i]) {
      if (on[i])
        lv_obj_clear_flag(p->ghost[i], LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(p->ghost[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// LVGL redraws a moved image but not its children
static void ghosts_redraw(place_t *p) {
  for (int i = 0; i < 3; i++)
    if (p->ghost[i])
      lv_obj_invalidate(p->ghost[i]);
}

// Puts it at fx, fy (1/1000 px), and the image if it's shown. The old and new areas are redrawn.
static void put(place_t *p, int32_t fx, int32_t fy) {
  int16_t x = p->x, y = p->y;
  p->fx = fx;
  p->fy = fy;
  p->x = (int16_t) ((fx >= 0 ? fx : fx - 999) / 1000);
  p->y = (int16_t) ((fy >= 0 ? fy : fy - 999) / 1000);
  if (!p->img)
    return;
  bool moved = p->x != x || p->y != y;
  if (moved)
    ghosts_redraw(p);  // where they were
  lv_obj_set_pos(p->img, p->x, p->y);
  lv_obj_update_layout(p->img);
  ghosts(p);
  if (moved)
    ghosts_redraw(p);
}

static void drop_place(place_t *p) {
  if (p->slot == ANIM_SLOTS)
    return;
  if (p->img)
    lv_obj_del(p->img);  // gone() clears img and the ghosts
  if (p->frame != NONE)
    give(p->frame, frame_bytes(&S[p->slot]));
  p->slot = ANIM_SLOTS;
  p->frame = NONE;
  stop(p);
}

static void drop_slot(int i) {
  slot_t *s = &S[i];
  if (s->off == NONE)
    return;
  for (int k = 0; k < ANIM_PLACES; k++)
    if (P[k].slot == i)
      drop_place(&P[k]);
  give(s->off, frame_bytes(s) + s->len);
  s->off = NONE;
  s->ready = false;
}

void anim_clear_all(void) {
  anim_abort();
  for (int i = 0; i < ANIM_SLOTS; i++)
    drop_slot(i);
}

void anim_clear(const uint8_t *b, int n) {
  uint32_t v = plink_number(b, n);
  if (v == ANIM_ALL)
    anim_clear_all();
  else if (v & ANIM_PLACE_BIT) {
    if ((v & ~ANIM_PLACE_BIT) < ANIM_PLACES)
      drop_place(&P[v & ~ANIM_PLACE_BIT]);
  } else if (v < ANIM_SLOTS)
    drop_slot((int) v);
}

bool anim_ready(void) {
  for (int i = 0; i < ANIM_PLACES; i++)
    if (P[i].slot != ANIM_SLOTS)
      return true;
  return false;
}

bool anim_pos(int place, int16_t *x, int16_t *y) {
  if (place < 0 || place >= ANIM_PLACES || P[place].slot == ANIM_SLOTS)
    return false;
  *x = P[place].x;
  *y = P[place].y;
  return true;
}

unsigned anim_used(void) { return used; }

// ---------------------------------------------------------------- a transfer

void anim_abort(void) {
  if (in < ANIM_SLOTS)
    drop_slot(in);
  in = ANIM_SLOTS;
}

int anim_open(const uint8_t *b, int n, uint32_t *len) {
  anim_abort();
  if (n != 60)
    return PIC_BAD;
  uint8_t i = b[1];
  if (i >= ANIM_SLOTS)
    return PIC_BAD;
  slot_t *s = &S[i];
  drop_slot(i);
  s->w = get16(b + 2);
  s->h = get16(b + 4);
  s->scale = b[6];
  s->transp = b[7];
  s->len = *len = plink_number(b + 8, 4);
  for (int k = 0; k < 16; k++)
    s->pal[k] = lv_color_make(b[12 + 3 * k], b[13 + 3 * k], b[14 + 3 * k]);
  if (!s->w || !s->h || s->w % 2 || s->h > 255 || (s->scale != 1 && s->scale != 2) || s->transp > ANIM_OPAQUE ||
      s->w * s->scale > PIC_MAX_W || s->h * s->scale > PIC_MAX_H)
    return PIC_BAD_SIZE;
  if (s->len > ANIM_POOL || (s->off = take(frame_bytes(s) + s->len)) == NONE)
    return PIC_TOO_BIG;
  in = i;
  return -1;
}

void anim_bytes(uint32_t at, const uint8_t *b, unsigned n) {
  if (in < ANIM_SLOTS)
    memcpy(records(&S[in]) + at, b, n);
}

// A record is delay, x / 2, y, w / 2, h, then the rectangle deflated. Applies the one at offset at to frame, or
// only checks it when frame is NULL. Returns its length, 0 if bad. area gets the changed rectangle in animation
// pixels.
static uint32_t record(const slot_t *s, uint32_t at, uint8_t *frame, lv_area_t *area) {
  const uint8_t *r = records(s) + at, *end = records(s) + s->len;
  unsigned rb = s->w / 2u;
  if (end - r < 5 || r[1] + r[3] > rb || r[2] + r[4] > s->h || !r[3] != !r[4])
    return 0;
  uint32_t n = 0;
  if (r[3]) {
    n = pinflate(r + 5, (uint32_t) (end - r - 5), frame ? frame + r[2] * rb + r[1] : NULL, r[3], rb,
                 (uint32_t) r[3] * r[4], 0);
    if (!n)
      return 0;
  }
  if (area)
    *area = (lv_area_t) {r[1] * 2, r[2], (r[1] + r[3]) * 2 - 1, r[2] + r[4] - 1};
  return 5 + n;
}

// a record's delay is in 10 ms
static uint32_t period(uint8_t delay) { return delay * 10u < MIN_FRAME_MS ? MIN_FRAME_MS : delay * 10u; }

// Something new to play: the timer looks soon and then waits for the next frame
static void wake(void) {
  lv_timer_set_period(timer, 10);
  lv_timer_reset(timer);
}

// The first frame into frame: all transparent, or colour 0 if opaque, then the first record
static void first_frame(const slot_t *s, uint8_t *frame) {
  memset(frame, s->transp < 16 ? s->transp * 0x11 : 0, frame_bytes(s));
  record(s, 0, frame, NULL);
}

bool anim_close(void) {
  if (in >= ANIM_SLOTS)
    return false;
  slot_t *s = &S[in];
  in = ANIM_SLOTS;
  uint32_t at = 0;
  while (at < s->len) {
    uint32_t k = record(s, at, NULL, NULL);
    if (!k) {
      drop_slot((int) (s - S));
      return false;
    }
    at += k;
  }
  if (!s->len) {
    drop_slot((int) (s - S));
    return false;
  }
  first_frame(s, pool + s->off);
  s->first = s->next = record(s, 0, NULL, NULL);
  s->due = lv_tick_get() + period(records(s)[0]);
  s->ready = true;
  wake();
  return true;
}

// ---------------------------------------------------------------- placing and moving

static void gone(lv_event_t *e) {
  for (int i = 0; i < ANIM_PLACES; i++)
    if (lv_event_get_target(e) == P[i].img) {
      P[i].img = NULL;
      memset(P[i].ghost, 0, sizeof P[i].ghost);  // its children go with it
    }
}

static void show_place(place_t *p, lv_obj_t *parent) {
  const slot_t *s = &S[p->slot];
  memset(&p->dsc, 0, sizeof p->dsc);
  p->dsc.header.cf = LV_IMG_CF_USER_ENCODED_1;
  p->dsc.header.w = s->w * s->scale;
  p->dsc.header.h = s->h * s->scale;
  p->dsc.data = pool;
  p->dsc.data_size = frame_bytes(s);
  p->img = lv_img_create(parent);
  lv_img_set_src(p->img, &p->dsc);
  lv_obj_add_flag(p->img, LV_OBJ_FLAG_OVERFLOW_VISIBLE);  // the ghosts hang outside it
  lv_obj_clear_flag(p->img, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(p->img, gone, LV_EVENT_DELETE, NULL);
  for (int i = 0; i < 3; i++) {
    p->ghost[i] = lv_img_create(p->img);
    lv_img_set_src(p->ghost[i], &p->dsc);
    lv_obj_set_pos(p->ghost[i], GHOST_AT[i].x, GHOST_AT[i].y);
  }
  put(p, p->fx, p->fy);
}

void anim_place(const uint8_t *b, int n) {
  if (n != 8 || b[0] >= ANIM_PLACES || b[1] >= ANIM_SLOTS || !S[b[1]].ready)
    return;
  place_t *p = &P[b[0]];
  const slot_t *s = &S[b[1]];
  unsigned x = get16(b + 2), y = get16(b + 4);
  if (x + s->w * s->scale > PIC_MAX_W || y + s->h * s->scale > PIC_MAX_H)
    return;
  lv_obj_t *parent = p->img ? lv_obj_get_parent(p->img) : NULL;
  drop_place(p);
  if (b[6]) {  // a frame of its own, started b[7] frames in
    if ((p->frame = take(frame_bytes(s))) == NONE)
      return;
    first_frame(s, pool + p->frame);
    p->next = s->first;
    uint8_t delay = records(s)[0];  // of the frame it shows
    for (int i = 0; i < b[7]; i++) {
      delay = records(s)[p->next];
      p->next += record(s, p->next, pool + p->frame, NULL);
      if (p->next >= s->len)
        p->next = s->first;
    }
    p->due = lv_tick_get() + period(delay);
    wake();
  }
  p->slot = b[1];
  p->torus = false;
  put(p, (int32_t) x * 1000, (int32_t) y * 1000);
  if (parent)
    show_place(p, parent);
}

// Applies the next record to frame and redraws what it changed on every place that shows that frame
static void step(const slot_t *s, uint8_t *frame, uint32_t *next, uint32_t *due, bool (*shows)(const place_t *, int),
                 int who) {
  uint8_t delay = records(s)[*next];
  lv_area_t a;
  *next += record(s, *next, frame, &a);
  if (*next >= s->len)
    *next = s->first;
  *due += period(delay);
  if (a.x2 < a.x1)
    return;  // nothing changed
  for (int k = 0; k < ANIM_PLACES; k++) {
    const place_t *p = &P[k];
    if (!shows(p, who))
      continue;
    for (int i = -1; i < 3; i++) {
      lv_obj_t *o = i < 0 ? p->img : p->ghost[i];
      if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
        continue;
      lv_area_t c;
      lv_obj_get_coords(o, &c);
      lv_area_t d = {c.x1 + a.x1 * s->scale, c.y1 + a.y1 * s->scale, c.x1 + (a.x2 + 1) * s->scale - 1,
                     c.y1 + (a.y2 + 1) * s->scale - 1};
      lv_obj_invalidate_area(o, &d);
    }
  }
}

static bool shows_slot(const place_t *p, int slot) { return p->slot == slot && p->frame == NONE && p->img; }
static bool is_place(const place_t *p, int k) { return p == &P[k] && p->img; }

static bool slot_shown(int i) {
  for (int k = 0; k < ANIM_PLACES; k++)
    if (shows_slot(&P[k], i))
      return true;
  return false;
}

// Every frame that's due goes on, then the timer waits for the next one. Only what's on the screen plays.
static void tick(lv_timer_t *t) {
  if (input_backlight() < 0)
    return;
  uint32_t now = lv_tick_get(), wait = 1000;
  for (int i = 0; i < ANIM_SLOTS; i++) {
    slot_t *s = &S[i];
    if (!s->ready || s->first >= s->len || !slot_shown(i))
      continue;
    if ((int32_t) (s->due - now) <= 0)
      step(s, pool + s->off, &s->next, &s->due, shows_slot, i);
    if ((int32_t) (s->due - now) <= 0)
      s->due = now + MIN_FRAME_MS;  // fell behind, like after the backlight was off
    if (s->due - now < wait)
      wait = s->due - now;
  }
  for (int k = 0; k < ANIM_PLACES; k++) {
    place_t *p = &P[k];
    if (p->slot == ANIM_SLOTS || p->frame == NONE || !p->img)
      continue;
    const slot_t *s = &S[p->slot];
    if ((int32_t) (p->due - now) <= 0)
      step(s, pool + p->frame, &p->next, &p->due, is_place, k);
    if ((int32_t) (p->due - now) <= 0)
      p->due = now + MIN_FRAME_MS;
    if (p->due - now < wait)
      wait = p->due - now;
  }
  lv_timer_set_period(t, wait < 10 ? 10 : wait);
}

// One step of every glide or drift. A drift wraps round like on a torus, the ghosts draw the part over the edge.
static void move_step(lv_timer_t *t) {
  if (input_backlight() < 0)
    return;
  bool any = false;
  for (int k = 0; k < ANIM_PLACES; k++) {
    place_t *p = &P[k];
    if (p->slot == ANIM_SLOTS || !p->img || !moving(p))
      continue;
    any = true;
    if (p->glide.ms) {
      p->glide.t = p->glide.ms - p->glide.t > MOVE_MS ? (uint16_t) (p->glide.t + MOVE_MS) : p->glide.ms;
      int32_t x = p->glide.x0 + (int32_t) ((int64_t) (p->glide.x1 - p->glide.x0) * p->glide.t / p->glide.ms),
              y = p->glide.y0 + (int32_t) ((int64_t) (p->glide.y1 - p->glide.y0) * p->glide.t / p->glide.ms);
      put(p, x * 1000, y * 1000);
      if (p->glide.t == p->glide.ms)
        stop(p);
      continue;
    }
    int32_t fx = (p->fx + p->vx * MOVE_MS) % (PIC_MAX_W * 1000), fy = (p->fy + p->vy * MOVE_MS) % (PIC_MAX_H * 1000);
    p->torus = true;
    put(p, fx < 0 ? fx + PIC_MAX_W * 1000 : fx, fy < 0 ? fy + PIC_MAX_H * 1000 : fy);
  }
  if (!any)
    lv_timer_pause(t);
}

void anim_move(const uint8_t *b, int n) {
  if (n < 2 || b[0] >= ANIM_PLACES || P[b[0]].slot == ANIM_SLOTS || b[1] > ANIM_DRIFT ||
      n != (b[1] == ANIM_GLIDE   ? 8
            : b[1] == ANIM_DRIFT ? 6
                                 : 2))
    return;
  place_t *p = &P[b[0]];
  stop(p);
  if (b[1] == ANIM_GLIDE) {
    int16_t x = (int16_t) get16(b + 2), y = (int16_t) get16(b + 4);
    uint16_t ms = get16(b + 6);
    p->torus = false;  // a glide can end partly off the screen, and that's all
    if (!ms) {
      put(p, (int32_t) x * 1000, (int32_t) y * 1000);
      return;
    }
    p->glide.x0 = p->x;
    p->glide.y0 = p->y;
    p->glide.x1 = x;
    p->glide.y1 = y;
    p->glide.ms = ms;
    p->glide.t = 0;
  } else if (b[1] == ANIM_DRIFT) {
    p->vx = (int16_t) get16(b + 2);
    p->vy = (int16_t) get16(b + 4);
  }
  if (moving(p)) {
    lv_timer_reset(mover);  // or the first step comes at once
    lv_timer_resume(mover);
  }
}

lv_obj_t *anim_show(lv_obj_t *parent) {
  lv_obj_t *first = NULL;
  bool moves = false;
  for (int k = 0; k < ANIM_PLACES; k++) {
    place_t *p = &P[k];
    if (p->slot == ANIM_SLOTS)
      continue;
    show_place(p, parent);
    if (!first)
      first = p->img;
    moves |= moving(p);
  }
  if (moves)
    lv_timer_resume(mover);
  if (first)
    wake();
  return first;
}

// ---------------------------------------------------------------- LVGL decoder: colour and alpha, a line at a time
static const place_t *place_of(const void *src) {
  if (lv_img_src_get_type(src) != LV_IMG_SRC_VARIABLE ||
      ((const lv_img_dsc_t *) src)->header.cf != LV_IMG_CF_USER_ENCODED_1)
    return NULL;
  for (int k = 0; k < ANIM_PLACES; k++)
    if (src == &P[k].dsc && P[k].slot != ANIM_SLOTS)
      return &P[k];
  return NULL;
}

static lv_res_t dec_info(lv_img_decoder_t *d, const void *src, lv_img_header_t *h) {
  (void) d;
  if (!place_of(src))
    return LV_RES_INV;
  *h = ((const lv_img_dsc_t *) src)->header;
  h->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;  // with alpha, so the transparent colour shows through
  return LV_RES_OK;
}

static lv_res_t dec_open(lv_img_decoder_t *d, lv_img_decoder_dsc_t *dsc) {
  (void) d;
  if (!place_of(dsc->src))
    return LV_RES_INV;
  dsc->img_data = NULL;  // makes LVGL ask for lines
  return LV_RES_OK;
}

static lv_res_t dec_line(lv_img_decoder_t *d, lv_img_decoder_dsc_t *dsc, lv_coord_t x, lv_coord_t y, lv_coord_t len,
                         uint8_t *buf) {
  (void) d;
  const place_t *p = place_of(dsc->src);
  if (!p)
    return LV_RES_INV;
  const slot_t *s = &S[p->slot];
  const uint8_t *row = place_frame(p) + (y / s->scale) * (s->w / 2);
  for (lv_coord_t i = 0; i < len; i++, buf += LV_IMG_PX_SIZE_ALPHA_BYTE) {
    int sx = (x + i) / s->scale;
    uint8_t c = sx & 1 ? row[sx >> 1] & 15 : row[sx >> 1] >> 4;
    memcpy(buf, &s->pal[c], sizeof(lv_color_t));
    buf[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = c == s->transp ? LV_OPA_TRANSP : LV_OPA_COVER;
  }
  return LV_RES_OK;
}

static void dec_close(lv_img_decoder_t *d, lv_img_decoder_dsc_t *dsc) {
  (void) d;
  (void) dsc;
}

void anim_init(void) {
  for (int i = 0; i < ANIM_SLOTS; i++)
    S[i].off = NONE;
  for (int k = 0; k < ANIM_PLACES; k++) {
    P[k].slot = ANIM_SLOTS;
    P[k].frame = NONE;
  }
  lv_img_decoder_t *d = lv_img_decoder_create();
  lv_img_decoder_set_info_cb(d, dec_info);
  lv_img_decoder_set_open_cb(d, dec_open);
  lv_img_decoder_set_read_line_cb(d, dec_line);
  lv_img_decoder_set_close_cb(d, dec_close);
  if (!timer)
    timer = lv_timer_create(tick, 100, NULL);
  if (!mover) {
    mover = lv_timer_create(move_step, MOVE_MS, NULL);
    lv_timer_pause(mover);
  }
}
