// Moves the places, see anim_int.h: ramps, waves, drifts and the wander, combined into what a place shows.
#include "anim_int.h"
#include "input.h"

wave_t waves[ANIM_WAVES];
static lv_timer_t *mover;
static uint32_t moved;  // the tick the motion has got to

const int32_t SHARE_LO[ANIM_CHANNELS] = {[ANIM_SIZE] = ANIM_SIZE_MIN * FULL / 100},
              SHARE_HI[ANIM_CHANNELS] = {[ANIM_SIZE] = ANIM_SIZE_MAX * FULL / 100,
                                         [ANIM_OPACITY] = ANIM_OPACITY_MAX * FULL / 100,
                                         [ANIM_SPEED] = ANIM_SPEED_MAX * FULL / 100};

// Counts *left down by ms, true once it has run out
static bool run_out(uint16_t *left, uint32_t ms) {
  if (*left > ms) {
    *left = (uint16_t) (*left - ms);
    return false;
  }
  *left = 0;
  return true;
}

// ---------------------------------------------------------------- values over time

// f and the result in 1/65536 of the way
static int32_t eased(unsigned e, int32_t f) {
  int64_t g = f;
  switch (e) {
    case ANIM_EASE_IN:
      return (int32_t) (g * g >> 16);
    case ANIM_EASE_OUT:
      return 65536 - (int32_t) ((65536 - g) * (65536 - g) >> 16);
    case ANIM_EASE_IN | ANIM_EASE_OUT:
      return (int32_t) (g * g * (3 * 65536 - 2 * g) >> 32);
  }
  return f;
}

int32_t ramp_at(const ramp_t *r) {
  if (!r->ms)
    return r->to;
  uint32_t t = LV_MIN((uint32_t) r->steps * MOVE_MS, r->ms);
  int64_t span = (int64_t) r->to - r->from;
  return r->from + (int32_t) (r->ease ? span * eased(r->ease, (int32_t) (t * 65536u / r->ms)) >> 16 : span * t / r->ms);
}

void ramp_to(ramp_t *r, int32_t to, uint16_t ms, unsigned ease) {
  r->from = ramp_at(r);
  r->to = to;
  r->ms = ms;
  r->steps = 0;
  r->ease = ease & 3;
}

void ramp_set(ramp_t *r, int32_t v) { ramp_to(r, v, 0, 0); }

// k steps on, false if it's still
static bool ramp_step(ramp_t *r, unsigned k) {
  if (!r->ms)
    return false;
  k += r->steps;
  if (k * MOVE_MS >= r->ms)
    r->ms = 0;
  else
    r->steps = k & 0x3FFF;
  return true;
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

static uint16_t jittered(place_t *p, uint32_t ms, uint8_t jitter) {
  int32_t j = (int32_t) (ms * jitter / 100);
  return (uint16_t) LV_CLAMP(0, (int32_t) ms + pick(p, -j, j), 65535);
}

wave_t *wave_of(const place_t *p, int c) {
  for (int i = 0; i < ANIM_WAVES; i++)
    if (waves[i].place == p - places && waves[i].ch == c)
      return &waves[i];
  return NULL;
}

// What channel c's wave adds, or its share
static int32_t waved(const place_t *p, int c) {
  const wave_t *w = wave_of(p, c);
  return w ? ramp_at(&w->r) : no_change(c);
}

// x, y or turn: what glides and drifts set, and its wave added
static int32_t sum(const place_t *p, int c) { return ramp_at(&p->ch[c]) + waved(p, c); }

// Not settled at lo = hi
static bool wave_busy(const wave_t *w) {
  return w->r.ms || w->left || w->lo != w->hi || ramp_at(&w->r) != w->lo * unit(w->ch);
}

// Stopped: on its way back to no change, and gone once it's there
bool stopping(const wave_t *w) { return w->lo == w->hi && w->lo * unit(w->ch) == no_change(w->ch); }

void wave_settle(wave_t *w) {
  if (stopping(w) && !wave_busy(w))
    w->place = ANIM_PLACES;
}

// Going up, slow at low eases in and slow at high eases out. Going down it's the other way round.
_Static_assert(ANIM_WAVE_SLOW_LOW == ANIM_EASE_IN && ANIM_WAVE_SLOW_HIGH == ANIM_EASE_OUT, "slow at low eases in");
static unsigned mirror_ease(unsigned e) {
  return (e & ANIM_EASE_IN ? ANIM_EASE_OUT : 0) | (e & ANIM_EASE_OUT ? ANIM_EASE_IN : 0);
}

// k steps on: the next leg once the last one and its hold are over
static void wave_step(wave_t *w, unsigned k) {
  place_t *p = &places[w->place];
  if (ramp_step(&w->r, k)) {
    if (!w->r.ms)
      w->left = jittered(p, w->hold[!w->up], w->jitter);
  } else if (run_out(&w->left, k * MOVE_MS)) {
    int32_t c = unit(w->ch), lo = w->lo * c, hi = w->hi * c, now = ramp_at(&w->r);
    if (lo != hi || now != lo) {
      int32_t to = w->flags & ANIM_WAVE_RANDOM ? pick(p, lo, hi) : w->up ? lo : hi;
      w->up = to > now || (to == now && !w->up);
      uint32_t ms = hi > lo ? (uint32_t) ((uint64_t) w->ms[!w->up] * (uint32_t) LV_ABS(to - now) / (uint32_t) (hi - lo))
                            : w->ms[!w->up];  // part of the way in part of the time
      unsigned slow = w->flags & (ANIM_WAVE_SLOW_LOW | ANIM_WAVE_SLOW_HIGH);
      ramp_to(&w->r, to, jittered(p, ms, w->jitter), w->up ? slow : mirror_ease(slow));
      ramp_step(&w->r, k);  // this step is its first
      if (!w->r.ms)
        w->left = jittered(p, w->hold[!w->up], w->jitter);
    }
  }
  wave_settle(w);
}

// ---------------------------------------------------------------- what a place shows

// Where its middle is across its area, 0..FULL from the left or the top
static int32_t across(const place_t *p, int axis) {
  const slot_t *s = &slots[p->slot];
  int lo = axis ? p->area.y0 : p->area.x0, hi = axis ? p->area.y1 : p->area.x1;
  int mid = (axis ? p->y + s->h * s->scale / 2 : p->x + s->w * s->scale / 2) - lo;
  return LV_CLAMP(0, mid * FULL / (hi - lo), FULL);
}

// Channel c's map where it is, left to right and top to bottom added, or multiplied for a share
int32_t mapped(const place_t *p, int c) {
  const int16_t *m = p->map[c - ANIM_TURN];
  int32_t a = m[0] * MILLI + (m[1] - m[0]) * across(p, 0), b = m[2] * MILLI + (m[3] - m[2]) * across(p, 1);
  return scales(c) ? (int32_t) ((int64_t) a * b / (100LL * 100 * MILLI * MILLI / FULL)) : a + b;  // from 1/1000 percent
}

// A share: what glides set, times its wave and its map
static int32_t share(const place_t *p, int c) {
  int64_t v = (int64_t) ramp_at(&p->ch[c]) * waved(p, c) / FULL * mapped(p, c) / FULL;
  return (int32_t) LV_CLAMP(SHARE_LO[c], v, SHARE_HI[c]);
}

// Puts it where x and y and their waves say, round the screen on a drifting ANIM_EDGE_SCREEN axis. Returns its box.
static lv_area_t put(place_t *p) {
  lv_area_t b = box(p);
  for (int c = ANIM_X; c <= ANIM_Y; c++) {
    int32_t span = (c ? PIC_MAX_H : PIC_MAX_W) * MILLI, off = (c ? b.y1 : b.x1) * MILLI;
    if (wraps(p, c))  // a drifting axis doesn't glide
      p->ch[c].to = wrap(p->ch[c].to + off, span) - off;
    int32_t v = sum(p, c);
    if (wraps(p, c))
      v = wrap(v + off, span) - off;
    *(c ? &p->y : &p->x) = (int16_t) ((v >= 0 ? v : v - (MILLI - 1)) / MILLI);
  }
  return b;
}

// What it shows from all its channels together, redrawn if that changed
void update(place_t *p) {
  look_t was = p->now;
  lv_area_t b = put(p);
  p->now.deg = (int16_t) (wrap(sum(p, ANIM_TURN) + mapped(p, ANIM_TURN), TURN) / MILLI);
  p->now.size = (int16_t) share(p, ANIM_SIZE);
  p->now.speed = (int16_t) share(p, ANIM_SPEED);
  p->now.opa = (int16_t) (share(p, ANIM_OPACITY) * ramp_at(&p->fade) / FULL);
  p->now.flip = p->flip;
  if (p->now.deg != was.deg || p->now.size != was.size)
    b = put(p);  // the box's corner goes with its size
  refresh(p, &was, &b);
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

// Keeps a drifting axis to the area: off the box's corner from x or y, n its size
static void keep(place_t *p, int axis, int off, int n) {
  int32_t *f = &p->ch[axis].to, *v = &p->rate[axis];
  int32_t lo = (axis ? p->area.y0 : p->area.x0) * MILLI, hi = (axis ? p->area.y1 : p->area.x1) * MILLI,
          c = *f + off * MILLI, size = n * MILLI;
  if (p->area.edge[axis] == ANIM_EDGE_CROSS) {
    int32_t span = hi - lo + size;  // out past one edge is in at the other
    if (*v > 0 && c >= hi)
      *f -= ((c - hi) / span + 1) * span;
    else if (*v < 0 && c + size <= lo)
      *f += ((lo - c - size) / span + 1) * span;
  } else if (p->area.edge[axis] == ANIM_EDGE_BOUNCE) {
    if (size >= hi - lo) {  // bigger than the area, it stays in the middle
      *f = lo + (hi - lo - size) / 2 - off * MILLI;
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
      p->wander.heading = (int16_t) wrap(h, 360);
    }
  }
}

void new_leg(place_t *p) {
  int32_t v = pick(p, p->wander.speed[0], p->wander.speed[1]) * (MILLI / 10);  // from tenths
  int d = p->wander.heading + pick(p, -p->wander.spread, p->wander.spread);
  d = wrap(d, 360);
  p->rate[ANIM_X] = (int32_t) ((int64_t) v * lv_trigo_cos((int16_t) d) / LV_TRIGO_SIN_MAX);
  p->rate[ANIM_Y] = (int32_t) ((int64_t) v * lv_trigo_sin((int16_t) d) / LV_TRIGO_SIN_MAX);
  p->wander.resting = false;
  p->wander.left = (uint16_t) pick(p, p->wander.leg[0], p->wander.leg[1]);
}

// The next leg when this one or its rest is over. With no leg times it's one leg.
static void wander_step(place_t *p, uint32_t ms) {
  if ((!p->wander.resting && !p->wander.leg[0] && !p->wander.leg[1]) || !run_out(&p->wander.left, ms))
    return;
  if (!p->wander.resting && (p->wander.rest[0] || p->wander.rest[1])) {
    p->rate[ANIM_X] = p->rate[ANIM_Y] = 0;
    p->wander.resting = true;
    p->wander.left = (uint16_t) pick(p, p->wander.rest[0], p->wander.rest[1]);
    return;
  }
  new_leg(p);
}

// Which way the frames face with flips f, in 1/1000 degree
static int32_t faces(const place_t *p, uint8_t f) {
  int32_t a = p->face.facing * MILLI;
  if (f & ANIM_FLIP_X)
    a = TURN / 2 - a;
  return f & ANIM_FLIP_Y ? -a : a;
}

// Which way it went by d, false when it stood still
static bool heading(const int32_t d[2], int32_t *h) {
  int32_t dx = d[0], dy = d[1];
  while (LV_ABS(dx) > 1456 || LV_ABS(dy) > 1456) {  // what lv_atan2 takes
    dx /= 2;
    dy /= 2;
  }
  if (!dx && !dy)
    return false;
  *h = lv_atan2(dy, dx) * MILLI;
  return true;
}

// Turns toward where it went over the last ms, mirrored first if that's nearer
static void face_step(place_t *p, const int32_t d[2], uint32_t ms) {
  int32_t h, to, a = half(ramp_at(&p->ch[ANIM_TURN]));
  if (heading(d, &h)) {
    uint8_t other = p->flip ^ ANIM_FLIP_X;
    int32_t alt = half(h - faces(p, other));
    to = half(h - faces(p, p->flip));
    if ((p->face.flags & ANIM_FACE_MIRROR) && LV_ABS(alt) + 10 * MILLI < LV_ABS(to)) {
      p->flip = other;
      a = -a;  // the mirror image of what it showed
      to = alt;
    }
    to = LV_CLAMP(-p->face.tilt * MILLI, to, p->face.tilt * MILLI);
  } else if (p->face.flags & ANIM_FACE_LEVEL) {
    to = 0;
  } else {
    return;
  }
  int32_t t = half(to - a), most = p->face.rate * (int32_t) ms;
  if (p->face.rate && LV_ABS(t) > most)
    t = t < 0 ? -most : most;
  ramp_set(&p->ch[ANIM_TURN], a + t);
}

bool moving(const place_t *p) {
  if (p->wander.on || p->face.on || p->fade.ms || p->leaving || (p->rounds && !looping(&slots[p->slot])))
    return true;
  for (int c = 0; c < ANIM_CHANNELS; c++)
    if (p->ch[c].ms || (c <= ANIM_TURN && p->rate[c]))
      return true;
  for (int i = 0; i < ANIM_WAVES; i++)
    if (waves[i].place == p - places && wave_busy(&waves[i]))
      return true;
  return false;
}

// k steps on of its glides, and of its drifts where nothing glides
static void drift_step(place_t *p, unsigned k) {
  uint32_t ms = k * MOVE_MS;
  for (int c = ANIM_X; c < ANIM_CHANNELS; c++) {
    ramp_t *r = &p->ch[c];
    if (ramp_step(r, k) || c > ANIM_TURN || !p->rate[c])
      continue;
    int64_t v = (int64_t) p->rate[c] * ms / 1000;
    r->to = c == ANIM_TURN ? wrap(r->to + (int32_t) v, TURN) : r->to + (int32_t) (v * p->now.speed / FULL);
  }
}

// Keeps its drifting axes to its area
static void keep_in_area(place_t *p) {
  if (!p->drifting)
    return;
  lv_area_t b = box(p);
  for (int c = ANIM_X; c <= ANIM_Y; c++)
    if (p->drifting >> c & 1)
      keep(p, c, c ? b.y1 : b.x1, c ? lv_area_get_height(&b) : lv_area_get_width(&b));
}

// k motion steps on
static void move_place(place_t *p, unsigned k) {
  uint32_t ms = k * MOVE_MS;
  int32_t d[2];  // how far it went, for face_step()
  for (int c = ANIM_X; c <= ANIM_Y; c++)
    d[c] = sum(p, c);
  if (p->wander.on)
    wander_step(p, ms);
  drift_step(p, k);
  for (int i = 0; i < ANIM_WAVES; i++)
    if (waves[i].place == p - places)
      wave_step(&waves[i], k);
  for (int c = ANIM_X; c <= ANIM_Y; c++)
    d[c] = sum(p, c) - d[c];
  if (p->face.on)
    face_step(p, d, ms);
  ramp_step(&p->fade, k);
  if (p->rounds && !looping(&slots[p->slot]))
    still_rounds(p, ms);
  keep_in_area(p);
  update(p);
  if (p->leaving && !p->fade.ms)
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
  for (int i = 0; i < ANIM_PLACES && layer; i++) {
    place_t *p = &places[i];
    if (p->slot != ANIM_SLOTS && moving(p)) {
      any = true;
      move_place(p, k);
    }
  }
  order();
  if (!any)
    lv_timer_pause(t);
}

void motion_init(void) {
  if (!mover) {
    mover = lv_timer_create(move_step, MOVE_MS, NULL);
    lv_timer_pause(mover);
  }
}
