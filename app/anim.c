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
#define FULL 1000  // full size, sizes are in 1/1000

// Every slot's current frame (w * h / 2 bytes) and then its records, and the frames places of their own, packed
// from the start. Freeing one moves what's behind it down.
static uint8_t pool[ANIM_POOL];
static unsigned used;

typedef struct {
  bool ready, leaving;    // leaving: goes with its last place
  uint16_t w, h;          // size in animation pixels
  uint8_t scale, transp;  // scale 1 or 2. transp = transparent palette index, ANIM_OPAQUE for none
  uint16_t off;           // its frame in pool, the records right after. NONE = empty slot
  uint32_t len;           // record bytes
  uint32_t first, next;   // first = the second record, where we loop back to. next = record to show next
  uint32_t last;          // the record that takes the last frame back to the first
  uint32_t due;           // tick for the next record
  lv_color_t pal[16];
} slot_t;

// A value on its way from one to another
typedef struct {
  int32_t now, from, to;
  uint16_t ms, t;  // over ms, t of them gone. ms = 0 is still
} ramp_t;

enum { STAY, LEAVE, LEAVE_GONE };  // LEAVE_GONE answers ANIM_GONE once it's gone

typedef struct {
  uint8_t slot;    // ANIM_SLOTS = empty place
  uint16_t frame;  // a frame of its own in pool, NONE = the slot's
  uint32_t next, due;
  int16_t x, y;    // position on the screen, can be partly off it while moving
  int32_t fx, fy;  // the same in 1/1000 px
  int32_t vx, vy;  // drift in 1/1000 px/s, 0 0 = none
  bool torus;      // a drift put it there, on an ANIM_EDGE_SCREEN axis the part over an edge shows at the other one
  ramp_t gx, gy;   // a glide, in 1/1000 px
  struct {
    int16_t x0, y0, x1, y1;
    uint8_t edge[2];  // ANIM_EDGE_* for x and y
  } area;
  struct {
    bool on, resting;
    uint16_t speed[2], leg[2], rest[2];  // min, max
    int16_t heading, spread;
    uint32_t left;  // ms of this leg or rest
  } wander;
  ramp_t turn;   // angle in 1/1000 degree clockwise
  int16_t spin;  // degrees per second, 0 = none
  struct {
    bool on;
    uint8_t flags;  // ANIM_FACE_*
    int16_t facing, tilt, rate;
  } face;
  uint8_t flip;  // ANIM_FLIP_X, ANIM_FLIP_Y
  ramp_t size;   // 1/1000
  ramp_t opa;
  uint16_t rounds, rounds_fade;  // rounds to the end of, 0 = on and on
  uint8_t tag;                   // ANIM_ROUNDS's, for ANIM_GONE
  uint32_t ends;                 // a one frame animation's rounds end at this tick
  uint8_t leaving;               // STAY, or fading out to go
  bool held;                     // its own frame stays at the last one
  uint32_t rng;
  lv_img_dsc_t dsc;
  lv_obj_t *img;       // on the screensaver, while it's shown
  lv_obj_t *ghost[3];  // children of img a screen to the left, up, and both, for the part a drift wraps round
} place_t;

static slot_t S[ANIM_SLOTS];
static place_t P[ANIM_PLACES];
static uint8_t in = ANIM_SLOTS;  // the slot a transfer is coming in for
static lv_timer_t *timer, *mover;
static uint8_t gone_bits, gone_tag[ANIM_PLACES];  // places gone after their rounds, for ANIM_GONE

static const lv_point_t GHOST_AT[3] = {{-PIC_MAX_W, 0}, {0, -PIC_MAX_H}, {-PIC_MAX_W, -PIC_MAX_H}};

static uint32_t frame_bytes(const slot_t *s) { return (uint32_t) s->w / 2 * s->h; }
static uint8_t *records(const slot_t *s) { return pool + s->off + frame_bytes(s); }
static uint8_t *place_frame(const place_t *p) { return pool + (p->frame == NONE ? S[p->slot].off : p->frame); }
static uint16_t get16(const uint8_t *b) { return (uint16_t) (b[0] | b[1] << 8); }
static bool looping(const slot_t *s) { return s->first < s->len; }
static int32_t norm(int32_t a) { return (a % 360000 + 360000) % 360000; }  // 1/1000 degree to 0..360
static int32_t half(int32_t a) {                                           // to -180..180
  a = norm(a);
  return a > 180000 ? a - 360000 : a;
}
static int deg(const place_t *p) { return (int) (norm(p->turn.now) / 1000); }
static bool transformed(const place_t *p) { return deg(p) || p->flip || p->size.now != FULL; }
static bool moving(const place_t *p) {
  return p->vx || p->vy || p->gx.ms || p->wander.on || p->turn.ms || p->spin || p->face.on || p->size.ms || p->opa.ms ||
         p->leaving;
}

static void ramp_to(ramp_t *r, int32_t to, uint16_t ms) {
  r->from = r->now;
  r->to = to;
  r->ms = ms;
  r->t = 0;
  if (!ms)
    r->now = to;
}

// One step on, false if it's still
static bool ramp_step(ramp_t *r) {
  if (!r->ms)
    return false;
  r->t = r->ms - r->t > MOVE_MS ? (uint16_t) (r->t + MOVE_MS) : r->ms;
  r->now = r->from + (int32_t) ((int64_t) (r->to - r->from) * r->t / r->ms);
  if (r->t == r->ms)
    r->ms = 0;
  return true;
}

static int zoom(const place_t *p, int n) {
  return p->size.now == FULL ? n : LV_MAX(1, (n * p->size.now + FULL / 2) / FULL);
}

// The image's size: the frame's at its size, or the box round that turned
static void box(const place_t *p, int *w, int *h) {
  const slot_t *s = &S[p->slot];
  int fw = zoom(p, s->w * s->scale), fh = zoom(p, s->h * s->scale), d = deg(p);
  int32_t c = LV_ABS(lv_trigo_cos((int16_t) d)), sn = LV_ABS(lv_trigo_sin((int16_t) d));  // 1/32767
  *w = d ? (int) ((fw * c + fh * sn + LV_TRIGO_SIN_MAX - 1) / LV_TRIGO_SIN_MAX) : fw;
  *h = d ? (int) ((fw * sn + fh * c + LV_TRIGO_SIN_MAX - 1) / LV_TRIGO_SIN_MAX) : fh;
}

// The box's top left from x, y: it's centred on the frame
static void offset(const place_t *p, int *ox, int *oy) {
  const slot_t *s = &S[p->slot];
  int w, h;
  box(p, &w, &h);
  *ox = (s->w * s->scale - w) / 2;
  *oy = (s->h * s->scale - h) / 2;
}

static uint32_t rnd(place_t *p) {
  uint32_t x = p->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return p->rng = x;
}

static int32_t pick(place_t *p, int32_t lo, int32_t hi) {
  return hi > lo ? lo + (int32_t) (rnd(p) % (uint32_t) (hi - lo + 1)) : lo;
}

// ---------------------------------------------------------------- the pool

static void reclaim(void);

// Takes n bytes, NONE if they don't fit even without what's fading out
static uint16_t take(uint32_t n) {
  if (n > ANIM_POOL - used)
    reclaim();
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
  p->gx.ms = p->gy.ms = 0;
  p->wander.on = false;
}

// How a new place starts: still, upright, full size, opaque, on the whole screen
static void reset(place_t *p) {
  stop(p);
  p->torus = false;
  p->area.x0 = p->area.y0 = 0;
  p->area.x1 = PIC_MAX_W;
  p->area.y1 = PIC_MAX_H;
  p->area.edge[0] = p->area.edge[1] = ANIM_EDGE_SCREEN;
  p->turn = (ramp_t) {0};
  p->spin = 0;
  p->face.on = false;
  p->flip = 0;
  p->size = (ramp_t) {FULL, FULL, FULL, 0, 0};
  p->opa = (ramp_t) {LV_OPA_COVER, LV_OPA_COVER, LV_OPA_COVER, 0, 0};
  p->rounds = 0;
  p->leaving = STAY;
  p->held = false;
}

static void wake_mover(void) {
  lv_timer_reset(mover);  // or the first step comes at once
  lv_timer_resume(mover);
}

// The image's top left
static void corner(const place_t *p, int *x, int *y) {
  offset(p, x, y);
  *x += p->x;
  *y += p->y;
}

// The ghosts show where it hangs over the right or bottom edge, a drift keeps the box's corner on the screen for that
static void ghosts(place_t *p) {
  int x, y, w, h;
  corner(p, &x, &y);
  box(p, &w, &h);
  bool right = p->torus && p->area.edge[0] == ANIM_EDGE_SCREEN && x + w > PIC_MAX_W,
       below = p->torus && p->area.edge[1] == ANIM_EDGE_SCREEN && y + h > PIC_MAX_H;
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

static int32_t wrap(int32_t v, int32_t m) { return (v % m + m) % m; }

// Puts it at fx, fy (1/1000 px), and the image if it's shown. The old and new areas are redrawn.
static void put(place_t *p, int32_t fx, int32_t fy) {
  int16_t x = p->x, y = p->y;
  int ox, oy;
  offset(p, &ox, &oy);
  if (p->torus && p->area.edge[0] == ANIM_EDGE_SCREEN)
    fx = wrap(fx + ox * 1000, PIC_MAX_W * 1000) - ox * 1000;
  if (p->torus && p->area.edge[1] == ANIM_EDGE_SCREEN)
    fy = wrap(fy + oy * 1000, PIC_MAX_H * 1000) - oy * 1000;
  p->fx = fx;
  p->fy = fy;
  p->x = (int16_t) ((fx >= 0 ? fx : fx - 999) / 1000);
  p->y = (int16_t) ((fy >= 0 ? fy : fy - 999) / 1000);
  if (!p->img)
    return;
  bool moved = p->x != x || p->y != y;
  if (moved)
    ghosts_redraw(p);  // where they were
  lv_obj_set_pos(p->img, (lv_coord_t) (p->x + ox), (lv_coord_t) (p->y + oy));
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
  reset(p);
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
  s->ready = s->leaving = false;
}

static bool has_places(int i) {
  for (int k = 0; k < ANIM_PLACES; k++)
    if (P[k].slot == i)
      return true;
  return false;
}

// It has faded out: it goes, and its slot if that was leaving and this was its last place
static void finish(place_t *p) {
  int k = (int) (p - P), i = p->slot;
  if (p->leaving == LEAVE_GONE) {
    gone_bits |= (uint8_t) (1u << k);
    gone_tag[k] = p->tag;
  }
  drop_place(p);
  if (S[i].leaving && !has_places(i))
    drop_slot(i);
}

// Fades out over ms and goes. One that isn't shown goes at once.
static void leave(place_t *p, uint16_t ms, uint8_t how) {
  p->leaving = how;
  p->rounds = 0;
  if (!p->img) {
    finish(p);
    return;
  }
  ramp_to(&p->opa, 0, ms ? ms : 1);  // goes at the next step, not while frames step
  wake_mover();
}

static void reclaim(void) {
  for (int k = 0; k < ANIM_PLACES; k++)
    if (P[k].slot != ANIM_SLOTS && P[k].leaving)
      finish(&P[k]);
  for (int i = 0; i < ANIM_SLOTS; i++)
    if (S[i].leaving)
      drop_slot(i);
}

void anim_clear_all(void) {
  anim_abort();
  for (int i = 0; i < ANIM_SLOTS; i++)
    drop_slot(i);
}

static void clear_slot(int i, uint16_t ms) {
  if (!ms || i == in || S[i].off == NONE) {
    drop_slot(i);
    return;
  }
  S[i].leaving = true;
  for (int k = 0; k < ANIM_PLACES; k++)
    if (P[k].slot == i && !P[k].leaving)
      leave(&P[k], ms, LEAVE);
  if (!has_places(i))
    drop_slot(i);
}

void anim_clear(const uint8_t *b, int n) {
  uint32_t v = plink_number(b, n), what = v & 0xFF;
  uint16_t ms = (uint16_t) (v >> 8);
  if (what == ANIM_ALL) {
    anim_abort();
    for (int i = 0; i < ANIM_SLOTS; i++)
      clear_slot(i, ms);
  } else if (what & ANIM_PLACE_BIT) {
    if ((what & ~ANIM_PLACE_BIT) >= ANIM_PLACES || P[what & ~ANIM_PLACE_BIT].slot == ANIM_SLOTS)
      return;
    place_t *p = &P[what & ~ANIM_PLACE_BIT];
    if (ms)
      leave(p, ms, LEAVE);
    else
      drop_place(p);
  } else if (what < ANIM_SLOTS) {
    clear_slot((int) what, ms);
  }
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

int32_t anim_gone(void) {
  for (int k = 0; k < ANIM_PLACES; k++)
    if (gone_bits >> k & 1) {
      gone_bits &= (uint8_t) ~(1u << k);
      return k | gone_tag[k] << 8;
    }
  return -1;
}

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
  s->last = UINT32_MAX;
  while (at < s->len) {
    uint32_t k = record(s, at, NULL, NULL);
    if (!k) {
      drop_slot((int) (s - S));
      return false;
    }
    if (at)
      s->last = at;
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

static void show_opa(place_t *p) {
  if (!p->img)
    return;
  lv_obj_set_style_opa(p->img, (lv_opa_t) p->opa.now, 0);  // the ghosts too
  ghosts_redraw(p);
}

static void show_place(place_t *p, lv_obj_t *parent) {
  const slot_t *s = &S[p->slot];
  int w, h;
  box(p, &w, &h);
  memset(&p->dsc, 0, sizeof p->dsc);
  p->dsc.header.cf = LV_IMG_CF_USER_ENCODED_1;
  p->dsc.header.w = (uint32_t) w;
  p->dsc.header.h = (uint32_t) h;
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
  if (p->opa.now != LV_OPA_COVER)
    lv_obj_set_style_opa(p->img, (lv_opa_t) p->opa.now, 0);
  put(p, p->fx, p->fy);
}

// After a turn, flip or size: the image takes the new box, the old and new areas are redrawn
static void reshape(place_t *p) {
  if (!p->img)
    return;
  int w, h;
  box(p, &w, &h);
  lv_obj_invalidate(p->img);
  ghosts_redraw(p);
  p->dsc.header.w = (uint32_t) w;
  p->dsc.header.h = (uint32_t) h;
  lv_img_set_src(p->img, &p->dsc);
  for (int i = 0; i < 3; i++)
    if (p->ghost[i])
      lv_img_set_src(p->ghost[i], &p->dsc);
  put(p, p->fx, p->fy);
  lv_obj_invalidate(p->img);
  ghosts_redraw(p);
}

static place_t *placed(const uint8_t *b) { return b[0] < ANIM_PLACES && P[b[0]].slot != ANIM_SLOTS ? &P[b[0]] : NULL; }

void anim_place(const uint8_t *b, int n) {
  if (n != 10 || b[0] >= ANIM_PLACES || b[1] >= ANIM_SLOTS || !S[b[1]].ready || S[b[1]].leaving)
    return;
  place_t *p = &P[b[0]];
  const slot_t *s = &S[b[1]];
  unsigned x = get16(b + 2), y = get16(b + 4);
  uint16_t fade = get16(b + 8);
  if (x + s->w * s->scale > PIC_MAX_W || y + s->h * s->scale > PIC_MAX_H)
    return;
  lv_obj_t *parent = p->img ? lv_obj_get_parent(p->img) : NULL;
  drop_place(p);
  gone_bits &= (uint8_t) ~(1u << b[0]);
  if (b[6]) {  // a frame of its own, started b[7] frames in
    if ((p->frame = take(frame_bytes(s))) == NONE)
      return;
    first_frame(s, pool + p->frame);
    p->next = s->first;
    uint8_t delay = records(s)[0];  // of the frame it shows
    for (int i = 0; i < b[7] && looping(s); i++) {
      delay = records(s)[p->next];
      p->next += record(s, p->next, pool + p->frame, NULL);
      if (p->next >= s->len)
        p->next = s->first;
    }
    p->due = lv_tick_get() + period(delay);
    wake();
  }
  p->slot = b[1];
  p->rng = (lv_tick_get() + 1) * 2654435761u ^ (uint32_t) (b[0] + 1) * 0x9E3779B9u;
  if (!p->rng)
    p->rng = 1;
  if (fade) {
    p->opa.now = 0;
    ramp_to(&p->opa, LV_OPA_COVER, fade);
    wake_mover();
  }
  put(p, (int32_t) x * 1000, (int32_t) y * 1000);
  if (parent)
    show_place(p, parent);
}

// It played to the end of its rounds: fades out, holding the last frame if it's its own
static void rounds_done(place_t *p) {
  p->held = p->frame != NONE;
  leave(p, p->rounds_fade, LEAVE_GONE);
}

// The record's rectangle on each of the place's images
static void redraw(const place_t *p, const slot_t *s, const lv_area_t *a) {
  for (int i = -1; i < 3; i++) {
    lv_obj_t *o = i < 0 ? p->img : p->ghost[i];
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
      continue;
    if (transformed(p)) {  // the changed rectangle doesn't map straight
      lv_obj_invalidate(o);
      continue;
    }
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    lv_area_t d = {c.x1 + a->x1 * s->scale, c.y1 + a->y1 * s->scale, c.x1 + (a->x2 + 1) * s->scale - 1,
                   c.y1 + (a->y2 + 1) * s->scale - 1};
    lv_obj_invalidate_area(o, &d);
  }
}

// Applies the next record to frame and redraws what it changed on every place that shows that frame. One that
// shows the last frame now has played another round.
static void step(const slot_t *s, uint8_t *frame, uint32_t *next, uint32_t *due, bool (*shows)(const place_t *, int),
                 int who) {
  uint8_t delay = records(s)[*next];
  lv_area_t a = {0, 0, -1, -1};
  *next += record(s, *next, frame, &a);
  if (*next >= s->len)
    *next = s->first;
  *due += period(delay);
  for (int k = 0; k < ANIM_PLACES; k++) {
    place_t *p = &P[k];
    if (!shows(p, who))
      continue;
    if (a.x2 >= a.x1)
      redraw(p, s, &a);
    if (*next == s->last && p->rounds && !--p->rounds)
      rounds_done(p);
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
    if (!s->ready || !looping(s) || !slot_shown(i))
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
    if (p->slot == ANIM_SLOTS || !p->img)
      continue;
    const slot_t *s = &S[p->slot];
    if (!looping(s)) {  // a still picture's rounds are its frame time
      if (p->rounds && (int32_t) (p->ends - now) <= 0)
        rounds_done(p);
      else if (p->rounds && p->ends - now < wait)
        wait = p->ends - now;
      continue;
    }
    if (p->frame == NONE || p->held)
      continue;
    if ((int32_t) (p->due - now) <= 0)
      step(s, pool + p->frame, &p->next, &p->due, is_place, k);
    if ((int32_t) (p->due - now) <= 0)
      p->due = now + MIN_FRAME_MS;
    if (p->due - now < wait)
      wait = p->due - now;
  }
  lv_timer_set_period(t, wait < 10 ? 10 : wait);
}

// Keeps one axis of a drift to the area: f the position in 1/1000 px, off the box's corner from it, n its size
static void keep(place_t *p, int axis, int32_t *f, int off, int n) {
  int32_t *v = axis ? &p->vy : &p->vx;
  int32_t lo = (axis ? p->area.y0 : p->area.x0) * 1000, hi = (axis ? p->area.y1 : p->area.x1) * 1000,
          c = *f + off * 1000, size = n * 1000;
  if (p->area.edge[axis] == ANIM_EDGE_CROSS) {
    if (*v > 0 && c >= hi)
      *f -= hi - lo + size;
    else if (*v < 0 && c + size <= lo)
      *f += hi - lo + size;
  } else if (p->area.edge[axis] == ANIM_EDGE_BOUNCE) {
    if (size >= hi - lo) {  // bigger than the area, it stays in the middle
      *f = lo + (hi - lo - size) / 2 - off * 1000;
      return;
    }
    bool back = (c < lo && *v < 0) || (c + size > hi && *v > 0);
    if (c < lo)
      *f += lo - c;
    else if (c + size > hi)
      *f -= c + size - hi;
    if (back) {
      *v = -*v;
      int h = axis ? -p->wander.heading : 180 - p->wander.heading;
      p->wander.heading = (int16_t) ((h % 360 + 360) % 360);
    }
  }
}

static void drift(place_t *p) {
  int ox, oy, w, h;
  offset(p, &ox, &oy);
  box(p, &w, &h);
  int32_t fx = p->fx + (int32_t) ((int64_t) p->vx * MOVE_MS / 1000),
          fy = p->fy + (int32_t) ((int64_t) p->vy * MOVE_MS / 1000);
  keep(p, 0, &fx, ox, w);
  keep(p, 1, &fy, oy, h);
  p->torus = true;
  put(p, fx, fy);
}

static void new_leg(place_t *p) {
  int32_t v = pick(p, p->wander.speed[0], p->wander.speed[1]) * 1000;
  int d = p->wander.heading + pick(p, -p->wander.spread, p->wander.spread);
  d = (d % 360 + 360) % 360;
  p->vx = (int32_t) ((int64_t) v * lv_trigo_cos((int16_t) d) / LV_TRIGO_SIN_MAX);
  p->vy = (int32_t) ((int64_t) v * lv_trigo_sin((int16_t) d) / LV_TRIGO_SIN_MAX);
  p->wander.resting = false;
  p->wander.left =
      p->wander.leg[0] || p->wander.leg[1] ? (uint32_t) pick(p, p->wander.leg[0], p->wander.leg[1]) : UINT32_MAX;
}

// The next leg when this one or its rest is over
static void wander(place_t *p) {
  if (p->wander.left > MOVE_MS) {
    p->wander.left -= MOVE_MS;
    return;
  }
  if (!p->wander.resting && (p->wander.rest[0] || p->wander.rest[1])) {
    p->vx = p->vy = 0;
    p->wander.resting = true;
    p->wander.left = (uint32_t) pick(p, p->wander.rest[0], p->wander.rest[1]);
    return;
  }
  new_leg(p);
}

// Which way the frames face with flips f, in 1/1000 degree
static int32_t faces(const place_t *p, uint8_t f) {
  int32_t a = p->face.facing * 1000;
  if (f & ANIM_FLIP_X)
    a = 180000 - a;
  return f & ANIM_FLIP_Y ? -a : a;
}

// Where it's going, false when it stands still
static bool heading(const place_t *p, int32_t *h) {
  int32_t dx = p->gx.ms ? p->gx.to - p->gx.from : p->vx, dy = p->gx.ms ? p->gy.to - p->gy.from : p->vy;
  while (LV_ABS(dx) > 1456 || LV_ABS(dy) > 1456) {  // what lv_atan2 takes
    dx /= 2;
    dy /= 2;
  }
  if (!dx && !dy)
    return false;
  *h = lv_atan2(dy, dx) * 1000;
  return true;
}

// Turns toward where it goes, mirrored first if that's nearer
static void face(place_t *p) {
  int32_t h, to;
  if (heading(p, &h)) {
    uint8_t other = p->flip ^ ANIM_FLIP_X;
    int32_t alt = half(h - faces(p, other));
    to = half(h - faces(p, p->flip));
    if ((p->face.flags & ANIM_FACE_MIRROR) && LV_ABS(alt) + 10000 < LV_ABS(to)) {
      p->flip = other;
      p->turn.now = -p->turn.now;  // the mirror image of what it showed
      to = alt;
    }
    to = LV_CLAMP(-p->face.tilt * 1000, to, p->face.tilt * 1000);
  } else if (p->face.flags & ANIM_FACE_LEVEL) {
    to = 0;
  } else {
    return;
  }
  int32_t a = half(p->turn.now), d = half(to - a), most = p->face.rate * MOVE_MS;
  if (p->face.rate && LV_ABS(d) > most)
    d = d < 0 ? -most : most;
  p->turn.now = a + d;
}

static void move_place(place_t *p) {
  int was = deg(p);
  uint8_t flip = p->flip;
  int32_t size = p->size.now;
  if (p->wander.on)
    wander(p);
  if (p->gx.ms) {
    ramp_step(&p->gx);
    ramp_step(&p->gy);
    put(p, p->gx.now, p->gy.now);
  } else if (p->vx || p->vy) {
    drift(p);
  }
  if (p->face.on)
    face(p);
  else if (!ramp_step(&p->turn) && p->spin)
    p->turn.now = norm(p->turn.now + p->spin * MOVE_MS);
  ramp_step(&p->size);
  if (deg(p) != was || p->flip != flip || p->size.now != size)
    reshape(p);
  if (ramp_step(&p->opa))
    show_opa(p);
  if (p->leaving && !p->opa.ms)
    finish(p);
}

// One step of everything that moves, turns, sizes or fades
static void move_step(lv_timer_t *t) {
  if (input_backlight() < 0)
    return;
  bool any = false;
  for (int k = 0; k < ANIM_PLACES; k++) {
    place_t *p = &P[k];
    if (p->slot == ANIM_SLOTS || !p->img || !moving(p))
      continue;
    any = true;
    move_place(p);
  }
  if (!any)
    lv_timer_pause(t);
}

void anim_move(const uint8_t *b, int n) {
  place_t *p = n >= 2 ? placed(b) : NULL;
  if (!p || b[1] > ANIM_WANDER || n != (b[1] == ANIM_GLIDE ? 8 : b[1] == ANIM_DRIFT ? 6 : b[1] == ANIM_WANDER ? 18 : 2))
    return;
  stop(p);
  if (b[1] == ANIM_GLIDE) {
    int32_t x = (int16_t) get16(b + 2) * 1000, y = (int16_t) get16(b + 4) * 1000;
    uint16_t ms = get16(b + 6);
    p->torus = false;  // a glide can end partly off the screen, and that's all
    if (!ms) {
      put(p, x, y);
      return;
    }
    p->gx.now = p->fx;
    p->gy.now = p->fy;
    ramp_to(&p->gx, x, ms);
    ramp_to(&p->gy, y, ms);
  } else if (b[1] == ANIM_DRIFT) {
    p->vx = (int16_t) get16(b + 2) * 1000;
    p->vy = (int16_t) get16(b + 4) * 1000;
  } else if (b[1] == ANIM_WANDER) {
    for (int i = 0; i < 2; i++) {
      p->wander.speed[i] = LV_MIN(get16(b + 2 + 2 * i), 10000);
      p->wander.leg[i] = get16(b + 10 + 2 * i);
      p->wander.rest[i] = get16(b + 14 + 2 * i);
    }
    p->wander.heading = (int16_t) (((int16_t) get16(b + 6) % 360 + 360) % 360);
    p->wander.spread = (int16_t) LV_CLAMP(0, (int16_t) get16(b + 8), 180);
    p->wander.on = true;
    new_leg(p);
  }
  if (moving(p))
    wake_mover();
}

void anim_turn(const uint8_t *b, int n) {
  place_t *p = n >= 3 ? placed(b) : NULL;
  if (!p || b[1] > ANIM_FACE || b[2] > (ANIM_FLIP_X | ANIM_FLIP_Y) ||
      n != (b[1] == ANIM_GLIDE   ? 7
            : b[1] == ANIM_DRIFT ? 5
            : b[1] == ANIM_FACE  ? 10
                                 : 3))
    return;
  if (b[1] == ANIM_FACE && (b[3] > (ANIM_FACE_MIRROR | ANIM_FACE_LEVEL) || (int16_t) get16(b + 6) < 0 ||
                            (int16_t) get16(b + 6) > 180 || (int16_t) get16(b + 8) < 0))
    return;
  p->flip = b[2];
  p->turn.ms = 0;
  p->turn.now = norm(p->turn.now);
  p->spin = 0;
  p->face.on = false;
  if (b[1] == ANIM_GLIDE) {
    int32_t to = ((int16_t) get16(b + 3) % 360 + 360) % 360 * 1000;
    ramp_to(&p->turn, p->turn.now + half(to - p->turn.now), get16(b + 5));  // the short way round
  } else if (b[1] == ANIM_DRIFT) {
    p->spin = (int16_t) get16(b + 3);
  } else if (b[1] == ANIM_FACE) {
    p->face.on = true;
    p->face.flags = b[3];
    p->face.facing = (int16_t) get16(b + 4);
    p->face.tilt = (int16_t) get16(b + 6);
    p->face.rate = (int16_t) get16(b + 8);
  }
  reshape(p);
  if (moving(p))
    wake_mover();
}

void anim_area(const uint8_t *b, int n) {
  place_t *p = n == 11 ? placed(b) : NULL;
  if (!p)
    return;
  int16_t x0 = (int16_t) get16(b + 1), y0 = (int16_t) get16(b + 3), x1 = (int16_t) get16(b + 5),
          y1 = (int16_t) get16(b + 7);
  if (x1 <= x0 || y1 <= y0 || b[9] > ANIM_EDGE_BOUNCE || b[10] > ANIM_EDGE_BOUNCE)
    return;
  p->area.x0 = x0;
  p->area.y0 = y0;
  p->area.x1 = x1;
  p->area.y1 = y1;
  p->area.edge[0] = b[9];
  p->area.edge[1] = b[10];
  put(p, p->fx, p->fy);
}

void anim_size(const uint8_t *b, int n) {
  place_t *p = n == 5 ? placed(b) : NULL;
  if (!p)
    return;
  uint16_t pct = get16(b + 1), ms = get16(b + 3);
  if (pct < 10 || pct > 400)
    return;
  ramp_to(&p->size, pct * (FULL / 100), ms);
  if (ms)
    wake_mover();
  else
    reshape(p);
}

void anim_fade(const uint8_t *b, int n) {
  place_t *p = n == 4 ? placed(b) : NULL;
  if (!p)
    return;
  uint16_t ms = get16(b + 2);
  ramp_to(&p->opa, b[1], ms);
  if (ms)
    wake_mover();
  else
    show_opa(p);
}

void anim_rounds(const uint8_t *b, int n) {
  place_t *p = n == 6 ? placed(b) : NULL;
  if (!p)
    return;
  const slot_t *s = &S[p->slot];
  p->rounds = get16(b + 1);
  p->rounds_fade = get16(b + 3);
  p->tag = b[5];
  p->held = false;
  p->ends = lv_tick_get() + p->rounds * period(records(s)[0]);
  if (p->rounds)
    wake();
}

bool anim_angle(int place, int *d, int *flip) {
  if (place < 0 || place >= ANIM_PLACES || P[place].slot == ANIM_SLOTS)
    return false;
  *d = deg(&P[place]);
  *flip = P[place].flip;
  return true;
}

bool anim_look(int place, int *size, int *opa) {
  if (place < 0 || place >= ANIM_PLACES || P[place].slot == ANIM_SLOTS)
    return false;
  *size = (int) P[place].size.now;
  *opa = (int) P[place].opa.now;
  return true;
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
  const uint8_t *frame = place_frame(p);
  if (!transformed(p)) {
    const uint8_t *row = frame + (y / s->scale) * (s->w / 2);
    for (lv_coord_t i = 0; i < len; i++, buf += LV_IMG_PX_SIZE_ALPHA_BYTE) {
      int sx = (x + i) / s->scale;
      uint8_t c = sx & 1 ? row[sx >> 1] & 15 : row[sx >> 1] >> 4;
      memcpy(buf, &s->pal[c], sizeof(lv_color_t));
      buf[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = c == s->transp ? LV_OPA_TRANSP : LV_OPA_COVER;
    }
    return LV_RES_OK;
  }
  // Each pixel's middle from the box's, back through the turn, the size and the flip into the frame, in 1/65536 px
  int fw = s->w * s->scale, fh = s->h * s->scale, angle = deg(p);
  int32_t c = lv_trigo_cos((int16_t) angle), sn = lv_trigo_sin((int16_t) angle);  // 1/32767
  int64_t k = (int64_t) LV_TRIGO_SIN_MAX * p->size.now, dx = (int64_t) (2 * x + 1 - (int) dsc->header.w) * 32768,
          dy = (int64_t) (2 * y + 1 - (int) dsc->header.h) * 32768;
  int32_t u = (int32_t) ((dx * c + dy * sn) * FULL / k), v = (int32_t) ((dy * c - dx * sn) * FULL / k);
  int32_t du = (int32_t) ((int64_t) c * 65536 * FULL / k), dv = (int32_t) (-(int64_t) sn * 65536 * FULL / k);
  for (lv_coord_t i = 0; i < len; i++, buf += LV_IMG_PX_SIZE_ALPHA_BYTE, u += du, v += dv) {
    int32_t fx = (p->flip & ANIM_FLIP_X ? -u : u) + (fw << 15), fy = (p->flip & ANIM_FLIP_Y ? -v : v) + (fh << 15);
    buf[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = LV_OPA_TRANSP;
    if (fx < 0 || fy < 0 || fx >> 16 >= fw || fy >> 16 >= fh)
      continue;  // outside the frame, in the box's corners
    int sx = (fx >> 16) / s->scale, sy = (fy >> 16) / s->scale;
    uint8_t q = frame[sy * (s->w / 2) + sx / 2];
    q = sx & 1 ? q & 15 : q >> 4;
    memcpy(buf, &s->pal[q], sizeof(lv_color_t));
    buf[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = q == s->transp ? LV_OPA_TRANSP : LV_OPA_COVER;
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
    reset(&P[k]);
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
