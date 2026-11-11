// Moves the places, see overlay_int.h: glides, drifts, the wander and the face, and with what's bound to its
// channels, what a place shows.
#include "overlay_int.h"
#include "input.h"
#include "plink.h"
#include <string.h>

move_t moves[MOVES];
static lv_timer_t *mover;
static uint32_t moved;  // the tick the motion has got to

#define AT(c) [(c) - OVERLAY_SIZE]
const int16_t CH_LO[] = {AT(OVERLAY_SIZE) = OVERLAY_SIZE_MIN * FULL / 100,
                         AT(OVERLAY_SIZE_X) = -OVERLAY_SIZE_MAX * FULL / 100,
                         AT(OVERLAY_SIZE_Y) = -OVERLAY_SIZE_MAX * FULL / 100, AT(FADE) = 0},
              CH_HI[] = {AT(OVERLAY_SIZE) = OVERLAY_SIZE_MAX * FULL / 100,
                         AT(OVERLAY_SIZE_X) = OVERLAY_SIZE_MAX * FULL / 100,
                         AT(OVERLAY_SIZE_Y) = OVERLAY_SIZE_MAX * FULL / 100,
                         AT(OVERLAY_OPACITY) = OVERLAY_OPACITY_MAX * FULL / 100,
                         AT(OVERLAY_SPEED) = OVERLAY_SPEED_MAX * FULL / 100,
                         AT(OVERLAY_LIGHT) = OVERLAY_LIGHT_MAX * FULL / 100,
                         AT(OVERLAY_LEVEL) = OVERLAY_LEVEL_MAX * FULL / 100,
                         AT(OVERLAY_PIVOT_X) = OVERLAY_PIVOT_MAX,
                         AT(OVERLAY_PIVOT_Y) = OVERLAY_PIVOT_MAX,
                         AT(OVERLAY_DEPTH) = OVERLAY_NEAREST,
                         AT(FADE) = FULL};
#undef AT
_Static_assert(OVERLAY_SPEED_MAX *FULL / 100 <= INT16_MAX && OVERLAY_LEVEL_MAX * FULL / 100 <= INT16_MAX,
               "the shares in 16 bits");

// Counts *left down by ms, true once it has run out
static bool run_out(uint16_t *left, uint32_t ms) {
  if (*left > ms) {
    *left = (uint16_t) (*left - ms);
    return false;
  }
  *left = 0;
  return true;
}

// ---------------------------------------------------------------- a channel's glide or drift

int32_t eased(unsigned e, int32_t f) {
  int64_t g = f;
  switch (e) {
    case OVERLAY_EASE_IN:
      return (int32_t) (g * g >> 16);
    case OVERLAY_EASE_OUT:
      return 65536 - (int32_t) ((65536 - g) * (65536 - g) >> 16);
    case OVERLAY_EASE_IN | OVERLAY_EASE_OUT:
      return (int32_t) (g * g * (3 * 65536 - 2 * g) >> 32);
  }
  return f;
}

static int32_t base(const place_t *p, int c) { return c < OVERLAY_SIZE ? p->big[c] : p->small[c - OVERLAY_SIZE]; }
static void rebase(place_t *p, int c, int32_t v) {
  if (c < OVERLAY_SIZE)
    p->big[c] = v;
  else
    p->small[c - OVERLAY_SIZE] = (int16_t) v;
}

static move_t *move_of(const place_t *p, int c) {
  for (int i = 0; i < MOVES; i++)
    if (moves[i].place == p - places && moves[i].ch == c)
      return &moves[i];
  return NULL;
}

// How far a glide still has to go
static int32_t to_go(const move_t *m) {
  uint32_t t = LV_MIN((uint32_t) m->steps * MOVE_MS, m->ms);
  int64_t d = m->d;
  return m->d - (int32_t) (m->ease ? d * eased(m->ease, (int32_t) (t * 65536u / m->ms)) >> 16 : d * t / m->ms);
}

int32_t ch_at(const place_t *p, int c) {
  const move_t *m = move_of(p, c);
  return base(p, c) - (m && m->ms ? to_go(m) : 0);
}

// ch_at() of all its channels into v, with one look through the moves. A channel has one at most.
static void chs_at(const place_t *p, int32_t *v) {
  for (int c = 0; c <= FADE; c++)
    v[c] = base(p, c);
  for (int i = 0; i < MOVES; i++)
    if (moves[i].place == p - places && moves[i].ms)
      v[moves[i].ch] -= to_go(&moves[i]);
}

void ch_set(place_t *p, int c, int32_t v) {
  move_t *m = move_of(p, c);
  if (m)
    m->place = OVERLAY_PLACES;
  rebase(p, c, v);
}

bool ch_moves(const place_t *p, int c) { return move_of(p, c); }

// A move for p to take. With all taken, the glide nearest its end is there at once and makes room. NULL if they all
// drift.
static move_t *move_for(const place_t *p) {
  move_t *soon = NULL;
  uint32_t least = UINT32_MAX;
  for (int i = 0; i < MOVES; i++) {
    move_t *m = &moves[i];
    uint32_t left = m->ms - LV_MIN((uint32_t) m->steps * MOVE_MS, m->ms);
    if (m->place == OVERLAY_PLACES)
      return m;
    if (m->ms && left < least) {
      least = left;
      soon = m;
    }
  }
  if (soon) {
    place_t *q = &places[soon->place];
    soon->place = OVERLAY_PLACES;
    if (q != p)
      update(q);
  }
  return soon;
}

static void start(move_t *m, const place_t *p, int c, int32_t d, uint16_t ms, unsigned ease) {
  if (m)
    *m = (move_t) {.d = d, .ms = ms, .ease = ease & 3, .place = (uint8_t) (p - places), .ch = (uint8_t) c};
}

void ch_glide(place_t *p, int c, int32_t to, uint16_t ms, unsigned ease) {
  int32_t from = ch_at(p, c);
  ch_set(p, c, to);
  if (ms && to != from)
    start(move_for(p), p, c, to - from, ms, ease);
}

void ch_drift(place_t *p, int c, int32_t rate) {
  ch_set(p, c, ch_at(p, c));
  if (rate)
    start(move_for(p), p, c, rate, 0, 0);
}

void moves_drop(const place_t *p) {
  for (int i = 0; i < MOVES; i++)
    if (moves[i].place == p - places)
      moves[i].place = OVERLAY_PLACES;
}

static uint32_t rnd(uint32_t *rng) {
  uint32_t x = *rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *rng = x;
}

int32_t pick(uint32_t *rng, int32_t lo, int32_t hi) {
  uint32_t span = (uint32_t) hi - (uint32_t) lo + 1u;  // 0 for all of them
  return hi > lo ? (int32_t) ((uint32_t) lo + (span ? rnd(rng) % span : rnd(rng))) : lo;
}

// ---------------------------------------------------------------- what a place shows

// Puts it where at has x and y, in 1/1000 px, round the screen on a drifting OVERLAY_EDGE_SCREEN axis. Returns its
// box.
static lv_area_t put(place_t *p, const int32_t *at) {
  lv_area_t b = box(p);
  for (int c = OVERLAY_X; c <= OVERLAY_Y; c++) {
    int32_t span = (c ? PIC_MAX_H : PIC_MAX_W) * MILLI, off = (c ? b.y1 : b.x1) * MILLI, v = at[c];
    if (wraps(p, c)) {  // a drifting axis doesn't glide
      int32_t was = p->big[c];
      p->big[c] = wrap(was + off, span) - off;
      p->jumped |= p->big[c] != was;
      v = wrap(v + off, span) - off;
    }
    int32_t px = floor_div(v, MILLI);
    *(c ? &p->y : &p->x) = (int16_t) px;
    p->now.sub[c] = (uint16_t) (v - px * MILLI);
  }
  return b;
}

// How far a pan goes before the box shows the same again, in px: its frame where that repeats, two of them with
// every other one mirrored, a NOISE's bumps. 0 if it never does.
static int32_t pan_round(const place_t *p, int axis) {
  const slot_t *s = &slots[p->slot];
  if (s->field) {
    const uint8_t *f = records(s);
    return f[OVERLAY_FIELD_KIND] == OVERLAY_FIELD_NOISE
               ? noise_cells(plink_get16(f + OVERLAY_FIELD_B)) * plink_get16(f + OVERLAY_FIELD_B)
               : 0;
  }
  if (!(p->view & (axis ? OVERLAY_VIEW_REPEAT_Y : OVERLAY_VIEW_REPEAT_X)))
    return 0;
  return (axis ? s->h : s->w) * s->scale * (p->view & OVERLAY_VIEW_MIRROR ? 2 : 1);
}

// A pan in 1/1000 px as far as it shows: round what comes round, so a drift goes on for good, else no further than
// a glide can put it
static int32_t pan_in(const place_t *p, int axis, int32_t v) {
  int32_t round = pan_round(p, axis) * MILLI;
  return round ? wrap(v, round) : LV_CLAMP(INT16_MIN * MILLI, v, INT16_MAX * MILLI);
}

// How many colours its phase takes round: a field's all, else its slot's cycling ones
static int cycling(const slot_t *s) { return s->field ? 16 : __builtin_popcount(s->cycles); }

// What it shows with its channels as v has them, and put there. Returns its box.
static lv_area_t show(place_t *p, const int32_t *at) {
  look_t *l = &p->now;
  int32_t v[FADE + 1];
  for (int c = 0; c <= FADE; c++)
    v[c] = limited(c, at[c]);
  for (int a = 0; a < 2; a++) {
    l->pan[a] = (int16_t) floor_div(pan_in(p, a, v[OVERLAY_PAN_X + a]), MILLI);
    l->box[a] = (int16_t) (LV_CLAMP(0, v[OVERLAY_W + a], OVERLAY_BOX_MAX * MILLI) / MILLI);
    l->size_xy[a] = (int16_t) v[OVERLAY_SIZE_X + a];
    l->pivot[a] = (uint16_t) v[OVERLAY_PIVOT_X + a];
  }
  int n = cycling(&slots[p->slot]);
  l->deg = (int16_t) (wrap(v[OVERLAY_TURN], TURN) / MILLI);
  l->phase = (int16_t) (n ? (int64_t) wrap(v[OVERLAY_PHASE], n * MILLI) * 256 / MILLI : 0);
  l->size = (int16_t) v[OVERLAY_SIZE];
  l->speed = (int16_t) v[OVERLAY_SPEED];
  l->opa = (int16_t) (v[OVERLAY_OPACITY] * v[FADE] / FULL);
  l->light = (int16_t) v[OVERLAY_LIGHT];
  l->level = (int16_t) v[OVERLAY_LEVEL];
  l->depth = (uint8_t) v[OVERLAY_DEPTH];
  l->view = p->view;
  return put(p, v);
}

// What it shows from all its channels together, redrawn if that changed. What's bound to x and y first, so it's
// there for the rest, which may go by where it is.
void update(place_t *p) {
  look_t was = p->now;
  int32_t v[FADE + 1];
  chs_at(p, v);
  if (p->bound)
    script_bound(p, v, OVERLAY_X, OVERLAY_Y);
  lv_area_t b = show(p, v);
  if (p->bound) {
    script_bound(p, v, OVERLAY_Y + 1, OVERLAY_CHANNELS - 1);
    b = show(p, v);
  }
  uint16_t rows = p->bound ? script_rows(p) : 0;
  refresh(p, &was, &b);
  if (rows != p->rows)  // it looks another way in the box it has
    redraw_place(p);
  p->rows = rows;
}

// ---------------------------------------------------------------- moving

// Starts the motion if it's stopped. Running, it goes on as it is: a reset would put its next step off.
void wake_mover(void) {
  if (!mover->paused)
    return;
  lv_timer_reset(mover);  // or the first step comes at once
  lv_timer_resume(mover);
  moved = lv_tick_get();
}

uint32_t since_step(void) { return mover->paused ? 0 : lv_tick_get() - moved; }
uint32_t step_tick(void) { return mover->paused ? lv_tick_get() : moved; }

bool roam(const place_t *p, roam_t *r) {
  if (p->roam == NO_OFF) {
    *r = (roam_t) {0};
    return false;
  }
  memcpy(r, pool + p->roam, sizeof *r);
  return true;
}

// In the pool while it wanders or faces, out of it once it does neither
bool roam_set(place_t *p, const roam_t *r) {
  if (!r->wander.on && !r->face.on) {
    if (p->roam != NO_OFF)
      give(p->roam, sizeof *r);
    p->roam = NO_OFF;
    return true;
  }
  if (p->roam == NO_OFF && (p->roam = take_for(p, sizeof *r)) == NO_OFF)
    return false;
  memcpy(pool + p->roam, r, sizeof *r);
  return true;
}

// Keeps a drifting axis to the area: off the box's corner from x or y, n its size
static void keep(place_t *p, int axis, int off, int n) {
  move_t *m = move_of(p, axis);
  int32_t none = 0, *f = &p->big[axis], *v = m && !m->ms ? &m->d : &none;
  int32_t lo = (axis ? p->area.y0 : p->area.x0) * MILLI, hi = (axis ? p->area.y1 : p->area.x1) * MILLI,
          c = *f + off * MILLI, size = n * MILLI, was = *f;
  if (p->area.edge[axis] == OVERLAY_EDGE_CROSS) {
    int32_t span = hi - lo + size;  // out past one edge is in at the other
    if (*v > 0 && c >= hi)
      *f -= ((c - hi) / span + 1) * span;
    else if (*v < 0 && c + size <= lo)
      *f += ((lo - c - size) / span + 1) * span;
    p->jumped |= *f != was;
  } else if (p->area.edge[axis] == OVERLAY_EDGE_BOUNCE) {
    if (size >= hi - lo) {  // bigger than the area, it stays in the middle
      *f = lo + (hi - lo - size) / 2 - off * MILLI;
      return;
    }
    bool back = (c < lo && *v < 0) || (c + size > hi && *v > 0);
    if (c < lo)
      *f += lo - c;
    else if (c + size > hi)
      *f -= c + size - hi;
    roam_t r;
    if (back)
      *v = -*v;
    if (back && roam(p, &r)) {  // and its wander's next leg goes the way it does now
      r.wander.heading = (int16_t) wrap(axis ? -r.wander.heading : 180 - r.wander.heading, 360);
      roam_set(p, &r);
    }
  }
}

static void leg(place_t *p, roam_t *r) {
  int32_t v = pick(&p->rng, r->wander.speed[0], r->wander.speed[1]) * (MILLI / 10);  // from tenths
  int d = wrap(r->wander.heading + pick(&p->rng, -r->wander.spread, r->wander.spread), 360);
  ch_drift(p, OVERLAY_X, (int32_t) ((int64_t) v * lv_trigo_cos((int16_t) d) / LV_TRIGO_SIN_MAX));
  ch_drift(p, OVERLAY_Y, (int32_t) ((int64_t) v * lv_trigo_sin((int16_t) d) / LV_TRIGO_SIN_MAX));
  r->wander.resting = false;
  r->wander.left = (uint16_t) pick(&p->rng, r->wander.leg[0], r->wander.leg[1]);
}

void new_leg(place_t *p) {
  roam_t r;
  if (!roam(p, &r))
    return;
  leg(p, &r);
  roam_set(p, &r);
}

// The next leg when this one or its rest is over. With no leg times it's one leg.
static void wander_step(place_t *p, uint32_t ms) {
  roam_t r;
  if (!roam(p, &r) || !r.wander.on)
    return;
  bool over = (r.wander.resting || r.wander.leg[0] || r.wander.leg[1]) && run_out(&r.wander.left, ms);
  if (over && !r.wander.resting && (r.wander.rest[0] || r.wander.rest[1])) {
    ch_drift(p, OVERLAY_X, 0);
    ch_drift(p, OVERLAY_Y, 0);
    r.wander.resting = true;
    r.wander.left = (uint16_t) pick(&p->rng, r.wander.rest[0], r.wander.rest[1]);
  } else if (over) {
    leg(p, &r);
  }
  roam_set(p, &r);
}

// Which way the frames face with view f's flips, in 1/1000 degree
static int32_t faces(int32_t facing, uint8_t f) {
  int32_t a = facing * MILLI;
  if (f & OVERLAY_VIEW_FLIP_X)
    a = TURN / 2 - a;
  return f & OVERLAY_VIEW_FLIP_Y ? -a : a;
}

int32_t angle(int32_t dx, int32_t dy) {
  dx = LV_MAX(dx, -INT32_MAX);
  dy = LV_MAX(dy, -INT32_MAX);
  while (LV_ABS(dx) > 1456 || LV_ABS(dy) > 1456) {  // what lv_atan2 takes
    dx /= 2;
    dy /= 2;
  }
  return dx || dy ? lv_atan2(dy, dx) : 0;
}

// Turns toward where it went by d over the last ms, mirrored first if that's nearer. False if it faces as it did.
static bool face_step(place_t *p, const int32_t d[2], uint32_t ms) {
  roam_t r;
  if (!roam(p, &r) || !r.face.on)
    return false;
  int32_t to, was = ch_at(p, OVERLAY_TURN), a = half(was);
  uint8_t view = p->view;
  if (d[0] || d[1]) {
    int32_t h = angle(d[0], d[1]) * MILLI;
    uint8_t other = p->view ^ OVERLAY_VIEW_FLIP_X;
    int32_t alt = half(h - faces(r.face.facing, other));
    to = half(h - faces(r.face.facing, p->view));
    if ((r.face.flags & OVERLAY_FACE_MIRROR) && LV_ABS(alt) + 10 * MILLI < LV_ABS(to)) {
      p->view = other;
      a = -a;  // the mirror image of what it showed
      to = alt;
    }
    to = LV_CLAMP(-r.face.tilt * MILLI, to, r.face.tilt * MILLI);
  } else if (r.face.flags & OVERLAY_FACE_LEVEL) {
    to = 0;
  } else {
    return false;
  }
  int32_t t = half(to - a), most = r.face.rate * (int32_t) ms;
  if (r.face.rate && LV_ABS(t) > most)
    t = t < 0 ? -most : most;
  ch_set(p, OVERLAY_TURN, a + t);
  return a + t != was || p->view != view;
}

bool moving(const place_t *p) {
  if (p->roam != NO_OFF || p->leaving || p->bound || script_moves(p))
    return true;
  for (int i = 0; i < MOVES; i++)
    if (moves[i].place == p - places)
      return true;
  return false;
}

// k steps on of its glides and its drifts: all but turn's at its speed, a box no less than 0
static void moves_step(place_t *p, unsigned k) {
  for (int i = 0; i < MOVES; i++) {
    move_t *m = &moves[i];
    if (m->place != p - places)
      continue;
    int c = m->ch;
    if (m->ms) {
      unsigned steps = m->steps + k;
      if (steps * MOVE_MS >= m->ms)
        m->place = OVERLAY_PLACES;
      else
        m->steps = steps & 0x3FFF;
      continue;
    }
    int64_t v = (int64_t) m->d * (k * MOVE_MS) / 1000,
            far = base(p, c) + (c == OVERLAY_TURN ? v : v * p->now.speed / FULL);
    int32_t to = (int32_t) LV_CLAMP(INT32_MIN / 2, far, INT32_MAX / 2);
    if (c == OVERLAY_TURN)
      to = wrap(to, TURN);
    else if (c == OVERLAY_PHASE)
      to = wrap(to, PHASES * MILLI);
    else if (c == OVERLAY_PAN_X || c == OVERLAY_PAN_Y)
      to = pan_in(p, c - OVERLAY_PAN_X, to);
    else if (c == OVERLAY_W || c == OVERLAY_H)
      to = LV_CLAMP(0, to, OVERLAY_BOX_MAX * MILLI);
    rebase(p, c, to);
  }
}

// Keeps its drifting axes to its area
static void keep_in_area(place_t *p) {
  if (!p->drifting)
    return;
  lv_area_t b = box(p);
  for (int c = OVERLAY_X; c <= OVERLAY_Y; c++)
    if (p->drifting >> c & 1)
      keep(p, c, c ? b.y1 : b.x1, c ? lv_area_get_height(&b) : lv_area_get_width(&b));
}

// Where it is in 1/1000 px
static int32_t fine(const place_t *p, int axis) { return (axis ? p->y : p->x) * MILLI + p->now.sub[axis]; }

// k motion steps on. Its face turns by the way it went as it's shown, whatever moved it, but for a wrap or a crossing.
static void move_place(place_t *p, unsigned k) {
  uint32_t ms = k * MOVE_MS;
  int32_t d[2] = {fine(p, 0), fine(p, 1)};
  wander_step(p, ms);
  moves_step(p, k);
  script_steps(p, ms);
  if (p->slot == OVERLAY_SLOTS)
    return;
  p->jumped = false;
  keep_in_area(p);
  update(p);
  for (int a = 0; a < 2; a++)
    d[a] = fine(p, a) - d[a];
  if (!p->jumped && face_step(p, d, ms))
    update(p);
  if (p->leaving && !ch_moves(p, FADE))
    gone(p);
}

// Everything that moves goes on by the steps since the last time, LVGL skips the ones it was too busy for
static void move_step(lv_timer_t *t) {
  uint32_t now = lv_tick_get(), k = (now - moved) / MOVE_MS;
  if (input_backlight() < 0) {
    moved = now;  // nothing moves in the dark
    return;
  }
  if (!k)
    return;
  moved = k > MOVE_MAX ? now : moved + k * MOVE_MS;  // a long stall isn't all caught up
  k = LV_MIN(k, MOVE_MAX);
  bool any = false;
  for (int i = 0; i < OVERLAY_PLACES && layer; i++) {
    place_t *p = &places[i];
    if (p->slot != OVERLAY_SLOTS && moving(p)) {
      any = true;
      move_place(p, k);
    }
  }
  order();
  if (!any)
    lv_timer_pause(t);
}

void motion_init(void) {
  for (int i = 0; i < MOVES; i++)
    moves[i].place = OVERLAY_PLACES;
  if (!mover) {
    mover = lv_timer_create(move_step, MOVE_MS, NULL);
    lv_timer_pause(mover);
  }
}
