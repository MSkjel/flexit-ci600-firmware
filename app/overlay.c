// Screensaver overlays, see overlay.h. Motion is in overlay_motion.c, drawing in overlay_draw.c.
#include "overlay_int.h"
#include "input.h"
#include "pdeflate.h"
#include "picture.h"
#include "plink.h"
#include <string.h>

// Every slot's frame (w * h / 2 bytes, an animated one's) and then its records, and the frames places of their own,
// packed from the start. Freeing one moves what's behind it down.
static uint8_t pool[OVERLAY_POOL];
static unsigned used;

slot_t slots[OVERLAY_SLOTS];
place_t places[OVERLAY_PLACES];
static uint8_t in = OVERLAY_SLOTS;  // the slot a transfer is coming in for
static lv_timer_t *timer;
static uint8_t gone_bits, gone_tag[OVERLAY_PLACES];  // places gone after their rounds, for OVERLAY_GONE
_Static_assert(OVERLAY_PLACES <= 8, "a bit per place in gone_bits");
_Static_assert(OVERLAY_PAL_LEN - PIC_PAL_LEN == sizeof slots[0].opa, "the opacities as they come");

static uint32_t frame_bytes(const slot_t *s) { return s->still ? 0 : (uint32_t) s->w / 2 * s->h; }
uint8_t *records(const slot_t *s) { return pool + s->off + frame_bytes(s); }
const uint8_t *place_frame(const place_t *p) { return pool + (p->frame == NO_OFF ? slots[p->slot].off : p->frame); }

// ---------------------------------------------------------------- the pool

// Takes n bytes, ending fade-outs early to make room. NO_OFF if they still don't fit.
static uint16_t take(uint32_t n) {
  for (int k = 0; k < OVERLAY_PLACES && n > OVERLAY_POOL - used; k++)
    if (places[k].slot != OVERLAY_SLOTS && places[k].leaving)
      gone(&places[k]);
  if (n > OVERLAY_POOL - used)
    return NO_OFF;
  used += n;
  return (uint16_t) (used - n);
}

// Frees the n bytes at off and moves everything behind down
static void give(uint16_t off, uint32_t n) {
  memmove(pool + off, pool + off + n, used - off - n);
  used -= n;
  for (int i = 0; i < OVERLAY_SLOTS; i++)
    if (slots[i].off != NO_OFF && slots[i].off > off)
      slots[i].off -= (uint16_t) n;
  for (int i = 0; i < OVERLAY_PLACES; i++)
    if (places[i].frame != NO_OFF && places[i].frame > off)
      places[i].frame -= (uint16_t) n;
}

// ---------------------------------------------------------------- places

// How a new place starts: still, upright, full size, opaque, on the whole screen
static void reset(place_t *p) {
  for (int c = 0; c < OVERLAY_CHANNELS; c++)
    ramp_set(&p->ch[c], no_change(c));
  memset(p->rate, 0, sizeof p->rate);
  for (int c = OVERLAY_TURN; c < OVERLAY_CHANNELS; c++)
    for (int i = 0; i < 4; i++)
      p->map[c - OVERLAY_TURN][i] = (int16_t) (no_change(c) / unit(c));
  for (int i = 0; i < OVERLAY_WAVES; i++)
    if (waves[i].place == p - places)
      waves[i].place = OVERLAY_PLACES;
  ramp_set(&p->fade, FULL);
  p->now.deg = 0;
  p->now.size = p->now.speed = p->now.opa = FULL;
  p->now.flip = p->flip = 0;
  p->drifting = 0;
  p->area.x0 = p->area.y0 = 0;
  p->area.x1 = PIC_MAX_W;
  p->area.y1 = PIC_MAX_H;
  p->area.edge[0] = p->area.edge[1] = OVERLAY_EDGE_SCREEN;
  p->wander.on = false;
  p->face.on = false;
  p->rounds = 0;
  p->leaving = STAY;
  p->held = false;
  p->depth = OVERLAY_NEAREST;
}

static void drop_slot(int i);

// Whether slot i has places, or with shown, places that show its frame
static bool has_places(int i, bool shown) {
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i && (!shown || places[k].frame == NO_OFF))
      return true;
  return false;
}

// Takes p away, and its slot with it if that's leaving and this was its last place
static void drop_place(place_t *p) {
  int i = p->slot;
  if (i == OVERLAY_SLOTS)
    return;
  hide(p);
  if (p->frame != NO_OFF)
    give(p->frame, frame_bytes(&slots[i]));
  p->slot = OVERLAY_SLOTS;
  p->frame = NO_OFF;
  reset(p);
  if (slots[i].leaving && !has_places(i, false))
    drop_slot(i);
}

static void drop_slot(int i) {
  slot_t *s = &slots[i];
  if (s->off == NO_OFF)
    return;
  s->leaving = false;  // not again from drop_place()
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i)
      drop_place(&places[k]);
  give(s->off, frame_bytes(s) + s->len);
  s->off = NO_OFF;
  s->ready = false;
  if (i == in)
    in = OVERLAY_SLOTS;
}

// It has faded out and goes, telling the ESP if its rounds ended
void gone(place_t *p) {
  int k = (int) (p - places);
  if (p->leaving == LEAVE_GONE) {
    gone_bits |= (uint8_t) (1u << k);
    gone_tag[k] = p->tag;
  }
  drop_place(p);
}

// Fades out over ms and goes, at the next step at the soonest: not while frames step. One that isn't shown goes
// at once.
static void leave(place_t *p, uint16_t ms, uint8_t how) {
  p->leaving = how;
  p->rounds = 0;
  if (!layer) {
    gone(p);
    return;
  }
  ramp_to(&p->fade, 0, ms ? ms : 1, 0);
  wake_mover();
}

// Its places fade out over ms, and it goes with the last. One already leaving goes on as it was, with its OVERLAY_GONE.
static void clear_slot(int i, uint16_t ms) {
  if (!ms || !has_places(i, false)) {
    drop_slot(i);
    return;
  }
  slots[i].leaving = true;
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i && !places[k].leaving)
      leave(&places[k], ms, LEAVE);
}

void overlay_clear_all(void) {
  for (int i = 0; i < OVERLAY_SLOTS; i++)
    drop_slot(i);
  gone_bits = 0;
}

bool overlay_clear(const uint8_t *b, int n) {
  uint32_t v = plink_number(b, n), what = v & 0xFF;
  uint16_t ms = (uint16_t) (v >> 8);
  uint8_t was = in;
  if (what == OVERLAY_ALL) {
    for (int i = 0; i < OVERLAY_SLOTS; i++)
      clear_slot(i, ms);
  } else if (what & OVERLAY_PLACE_BIT) {
    unsigned k = what & ~OVERLAY_PLACE_BIT;
    if (k >= OVERLAY_PLACES || places[k].slot == OVERLAY_SLOTS)
      return false;
    if (!ms)
      drop_place(&places[k]);
    else if (!places[k].leaving)  // as in clear_slot()
      leave(&places[k], ms, LEAVE);
  } else if (what < OVERLAY_SLOTS) {
    clear_slot((int) what, ms);
  }
  return in != was;
}

unsigned overlay_used(void) { return used; }

// The nibbles of rectangle a at f, rb bytes a row from its top left, into px a byte each, stride bytes a row
static void spread(uint8_t *px, int stride, const uint8_t *f, int rb, const lv_area_t *a) {
  for (int y = a->y1; y <= a->y2; y++, f += rb)
    for (int x = a->x1; x <= a->x2; x++) {
      uint8_t b = f[(x - a->x1) / 2];
      px[y * stride + x] = x & 1 ? b & 15 : b >> 4;
    }
}

bool overlay_pixels(int place, uint8_t *px) {
  if (place < 0 || place >= OVERLAY_PLACES || places[place].slot == OVERLAY_SLOTS)
    return false;
  const slot_t *s = &slots[places[place].slot];
  if (!s->still) {
    spread(px, s->w, place_frame(&places[place]), s->w / 2, &(lv_area_t) {0, 0, s->w - 1, s->h - 1});
    return true;
  }
  uint8_t *piece = lv_mem_alloc(OVERLAY_PIECE_MAX);
  if (!piece)
    return false;
  memset(px, 16, (size_t) s->w * s->h);
  for (uint32_t at = 0, k; at < s->len; at += k) {
    lv_area_t a;
    k = record(s, at, piece, &a);
    spread(px, s->w, piece, lv_area_get_width(&a) / 2, &a);
  }
  lv_mem_free(piece);
  return true;
}

int32_t overlay_gone(void) {
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (gone_bits >> k & 1) {
      gone_bits &= (uint8_t) ~(1u << k);
      return k | gone_tag[k] << 8;
    }
  return -1;
}

// ---------------------------------------------------------------- a transfer

void overlay_abort(void) {
  if (in < OVERLAY_SLOTS)
    drop_slot(in);
}

int overlay_open(const uint8_t *b, int n, uint32_t *len) {
  if (n != OVERLAY_BEGIN_HEAD + OVERLAY_PAL_LEN)
    return PIC_BAD;
  uint8_t i = b[OVERLAY_BEGIN_SLOT], kind = b[OVERLAY_BEGIN_KIND];
  if (i >= OVERLAY_SLOTS || kind > OVERLAY_STILL)
    return PIC_BAD;
  slot_t *s = &slots[i];
  drop_slot(i);
  s->w = plink_get16(b + OVERLAY_BEGIN_W);
  s->h = plink_get16(b + OVERLAY_BEGIN_H);
  s->scale = b[OVERLAY_BEGIN_SCALE];
  s->still = kind == OVERLAY_STILL;
  s->rate = 100;
  *len = plink_number(b + OVERLAY_BEGIN_SIZE, 4);
  pic_palette(s->pal, b + OVERLAY_BEGIN_HEAD);
  memcpy(s->opa, b + OVERLAY_BEGIN_HEAD + PIC_PAL_LEN, sizeof s->opa);
  if (!s->w || !s->h || s->w % 2 || s->h > 255 || (s->scale != 1 && s->scale != 2) || s->w * s->scale > PIC_MAX_W ||
      s->h * s->scale > PIC_MAX_H)
    return PIC_BAD_SIZE;
  if (*len > OVERLAY_POOL || (s->off = take(frame_bytes(s) + *len)) == NO_OFF)
    return PIC_TOO_BIG;
  s->len = (uint16_t) *len;
  in = i;
  return -1;
}

void overlay_bytes(uint32_t at, const uint8_t *b, unsigned n) {
  if (in < OVERLAY_SLOTS)
    memcpy(records(&slots[in]) + at, b, n);
}

// h rows of w bytes at p, packed as plink_pack_row packs them, into out, or only checked if out is NULL. Returns the
// bytes they take, 0 if they run past end or a run past the end of a row.
static uint32_t unpack(const uint8_t *p, const uint8_t *end, unsigned w, unsigned h, uint8_t *out) {
  const uint8_t *start = p;
  for (unsigned y = 0; y < h; y++)
    for (unsigned left = w; left;) {
      if (p == end)
        return 0;
      uint8_t c = *p++;
      unsigned n = c < 128 ? c + 1u : c - 126u, k = c < 128 ? n : 1;  // the bytes it makes, and those after it
      if (n > left || (unsigned) (end - p) < k)
        return 0;
      if (out) {
        if (c < 128)
          memcpy(out, p, n);
        else
          memset(out, *p, n);
        out += n;
      }
      p += k;
      left -= n;
    }
  return (uint32_t) (p - start);
}

// A record is OVERLAY_RECORD_*: delay, x / 2, y, w / 2, h, then the rectangle, deflated or a still's packed rows
uint32_t record(const slot_t *s, uint32_t at, uint8_t *frame, lv_area_t *area) {
  const uint8_t *r = records(s) + at, *end = records(s) + s->len, *d = r + OVERLAY_RECORD_HEAD;
  unsigned rb = s->w / 2u;
  if (end - r < OVERLAY_RECORD_HEAD)
    return 0;
  unsigned x = r[OVERLAY_RECORD_X], y = r[OVERLAY_RECORD_Y], w = r[OVERLAY_RECORD_W], h = r[OVERLAY_RECORD_H];
  if (x + w > rb || y + h > s->h || !w != !h || (s->still && w * h > OVERLAY_PIECE_MAX))
    return 0;
  uint32_t n = 0;
  if (w) {
    n = s->still ? unpack(d, end, w, h, frame)
                 : pinflate(d, (uint32_t) (end - d), frame ? frame + y * rb + x : NULL, w, rb, w * h, 0);
    if (!n)
      return 0;
  }
  if (area)
    *area = (lv_area_t) {x * 2, y, (x + w) * 2 - 1, y + h - 1};
  return OVERLAY_RECORD_HEAD + n;
}

// delay is in OVERLAY_TICK_MS at 100 %, scaled by the slot's rate
static uint32_t period(const slot_t *s, uint8_t delay) {
  uint32_t ms = delay * (OVERLAY_TICK_MS * 100u) / LV_MAX(s->rate, 1);
  return ms < OVERLAY_FRAME_MIN_MS ? OVERLAY_FRAME_MIN_MS : ms;
}

// Something new to play: the timer looks soon and then waits for the next frame
void wake(void) {
  lv_timer_set_period(timer, 10);
  lv_timer_reset(timer);
}

// Applies the record at *next to frame and moves *next on, back to loop_at after the last. area gets what it
// changed. Returns its delay.
static uint8_t advance(const slot_t *s, uint8_t *frame, uint16_t *next, lv_area_t *area) {
  uint8_t delay = records(s)[*next + OVERLAY_RECORD_DELAY];
  *next = (uint16_t) (*next + record(s, *next, frame, area));
  if (*next >= s->len)
    *next = s->loop_at;
  return delay;
}

// The first frame into frame, the whole of it in the first record
static void first_frame(const slot_t *s, uint8_t *frame) { record(s, 0, frame, NULL); }

bool overlay_close(void) {
  if (in >= OVERLAY_SLOTS)
    return false;
  int i = in;
  slot_t *s = &slots[i];
  in = OVERLAY_SLOTS;
  s->last = NO_OFF;
  s->frames = 0;
  s->loop_at = s->len;  // a still's pieces don't loop
  bool ok = s->len;
  for (uint32_t at = 0, k; ok && at < s->len; at += k) {
    lv_area_t a;
    if (!(k = record(s, at, NULL, &a))) {
      ok = false;
    } else if (s->still) {
      continue;
    } else if (at) {
      s->last = (uint16_t) at;
      s->frames++;
    } else {
      s->loop_at = (uint16_t) k;
      ok = lv_area_get_width(&a) == s->w && lv_area_get_height(&a) == s->h;
    }
  }
  if (!ok) {
    drop_slot(i);
    return false;
  }
  if (!s->still) {
    first_frame(s, pool + s->off);
    s->next = s->loop_at;
    s->due = lv_tick_get() + period(s, records(s)[OVERLAY_RECORD_DELAY]);
  }
  s->ready = true;
  wake();
  return true;
}

// ---------------------------------------------------------------- playing

// It played to the end of its rounds: fades out, holding the last frame if it's its own
static void rounds_done(place_t *p) {
  p->held = p->frame != NO_OFF;
  leave(p, p->rounds_fade, LEAVE_GONE);
}

// Applies the next record to frame and redraws what it changed on every place that shows that frame. One that
// shows the last frame now has played another round.
static void step(const slot_t *s, uint8_t *frame, uint16_t *next, uint32_t *due) {
  lv_area_t a = {0, 0, -1, -1};
  *due += period(s, advance(s, frame, next, &a));
  for (int k = 0; k < OVERLAY_PLACES; k++) {
    place_t *p = &places[k];
    if (p->slot == OVERLAY_SLOTS || place_frame(p) != frame)
      continue;
    if (a.x2 >= a.x1)
      changed(p, &a);
    if (*next == s->last && p->rounds && !--p->rounds)
      rounds_done(p);
  }
}

// The next record of a frame once it's due. Returns how long until the one after.
static uint32_t play(const slot_t *s, uint8_t *frame, uint16_t *next, uint32_t *due, uint32_t now) {
  if ((int32_t) (*due - now) <= 0)
    step(s, frame, next, due);
  if ((int32_t) (*due - now) <= 0)
    *due = now + OVERLAY_FRAME_MIN_MS;  // fell behind, like after the backlight was off
  return *due - now;
}

// Every frame that's due goes on, then the timer waits for the next one. Only what's on the screen plays, a paused
// slot holds its frame.
static void tick(lv_timer_t *t) {
  if (input_backlight() < 0)
    return;
  uint32_t now = lv_tick_get(), wait = 1000;
  for (int i = 0; i < OVERLAY_SLOTS && layer; i++) {
    slot_t *s = &slots[i];
    if (s->ready && looping(s) && s->rate && has_places(i, true))
      wait = LV_MIN(wait, play(s, pool + s->off, &s->next, &s->due, now));
  }
  for (int k = 0; k < OVERLAY_PLACES && layer; k++) {
    place_t *p = &places[k];
    if (p->slot != OVERLAY_SLOTS && p->frame != NO_OFF && !p->held && looping(&slots[p->slot]) && slots[p->slot].rate)
      wait = LV_MIN(wait, play(&slots[p->slot], pool + p->frame, &p->next, &p->due, now));
  }
  lv_timer_set_period(t, wait < 10 ? 10 : wait);
}

// A still picture's rounds go by at its frame time, ms more of it
void still_rounds(place_t *p, uint32_t ms) {
  const slot_t *s = &slots[p->slot];
  while (s->rate && p->rounds && ms >= p->due) {
    ms -= p->due;
    p->due = period(s, records(s)[OVERLAY_RECORD_DELAY]);
    if (!--p->rounds)
      rounds_done(p);
  }
  if (s->rate && p->rounds)
    p->due -= ms;
}

// ---------------------------------------------------------------- the messages

static void place_msg(const uint8_t *b) {
  uint8_t k = b[OVERLAY_MSG_PLACE], slot = b[OVERLAY_PLACE_SLOT];
  if (k >= OVERLAY_PLACES || slot >= OVERLAY_SLOTS || !slots[slot].ready || slots[slot].leaving)
    return;
  place_t *p = &places[k];
  const slot_t *s = &slots[slot];
  int16_t x = plink_i16(b + OVERLAY_PLACE_X),
          y = plink_i16(b + OVERLAY_PLACE_Y);  // can be partly or all off the screen
  uint16_t fade = plink_get16(b + OVERLAY_PLACE_FADE);
  drop_place(p);
  gone_bits &= (uint8_t) ~(1u << k);
  if (b[OVERLAY_PLACE_OWN] && !s->still) {  // a frame of its own, started b[OVERLAY_PLACE_START] frames in
    if ((p->frame = take(frame_bytes(s))) == NO_OFF)
      return;
    first_frame(s, pool + p->frame);
    p->next = s->loop_at;
    uint8_t delay = records(s)[OVERLAY_RECORD_DELAY];  // of the frame it shows
    for (int i = looping(s) ? b[OVERLAY_PLACE_START] % s->frames : 0; i > 0; i--)
      delay = advance(s, pool + p->frame, &p->next, NULL);
    p->due = lv_tick_get() + period(s, delay);
  }
  p->slot = slot;
  p->rng = (lv_tick_get() + 1) * 2654435761u ^ (uint32_t) (k + 1) * 0x9E3779B9u;
  if (!p->rng)
    p->rng = 1;
  ramp_set(&p->ch[OVERLAY_X], (int32_t) x * MILLI);
  ramp_set(&p->ch[OVERLAY_Y], (int32_t) y * MILLI);
  if (fade) {
    ramp_set(&p->fade, 0);
    ramp_to(&p->fade, FULL, fade, 0);
    wake_mover();
  }
  update(p);
  order();
  wake();
}

// A glide or drift of channel c stops what else moved it
static void take_over(place_t *p, int c) {
  ramp_set(&p->ch[c], ramp_at(&p->ch[c]));
  if (c <= OVERLAY_TURN)
    p->rate[c] = 0;
  if (c <= OVERLAY_Y && p->wander.on) {
    p->wander.on = false;
    p->rate[OVERLAY_X] = p->rate[OVERLAY_Y] = 0;
  }
  if (c == OVERLAY_TURN)
    p->face.on = false;
}

// A GLIDE's or DRIFT's values after its head, one per channel, into v. False if one is out of range.
_Static_assert((int) OVERLAY_GLIDE_CHANNELS == OVERLAY_DRIFT_CHANNELS, "GLIDE's channels where DRIFT has them");
static bool values(const uint8_t *b, int head, int32_t v[OVERLAY_CHANNELS]) {
  const uint8_t *q = b + head;
  for (int c = 0; c < OVERLAY_CHANNELS; c++)
    if (b[OVERLAY_GLIDE_CHANNELS] >> c & 1) {
      v[c] = plink_i16(q);
      q += 2;
      if (scales(c) && (v[c] * unit(c) < SHARE_LO[c] || v[c] * unit(c) > SHARE_HI[c]))
        return false;
    }
  return true;
}

static void glide_msg(place_t *p, const uint8_t *b) {
  int32_t v[OVERLAY_CHANNELS];
  if (b[OVERLAY_GLIDE_EASE] > (OVERLAY_EASE_IN | OVERLAY_EASE_OUT) || !values(b, OVERLAY_GLIDE_HEAD, v))
    return;
  for (int c = 0; c < OVERLAY_CHANNELS; c++) {
    if (!(b[OVERLAY_GLIDE_CHANNELS] >> c & 1))
      continue;
    take_over(p, c);
    ramp_t *r = &p->ch[c];
    int32_t to = v[c] * unit(c);
    if (c <= OVERLAY_Y)
      p->drifting &= (uint8_t) ~(1u << c);  // a glide can end partly off the screen, and that's all
    if (c == OVERLAY_TURN) {
      ramp_set(r, wrap(r->to, TURN));
      to = r->to + half(to - r->to);  // the short way round
    }
    ramp_to(r, to, plink_get16(b + OVERLAY_GLIDE_MS), b[OVERLAY_GLIDE_EASE]);
  }
}

static void drift_msg(place_t *p, const uint8_t *b) {
  int32_t v[OVERLAY_CHANNELS];
  if (!values(b, OVERLAY_DRIFT_HEAD, v))
    return;
  for (int c = OVERLAY_X; c <= OVERLAY_TURN; c++) {
    if (!(b[OVERLAY_DRIFT_CHANNELS] >> c & 1))
      continue;
    take_over(p, c);
    p->rate[c] = v[c] * (MILLI / 10);  // from tenths
    if (c <= OVERLAY_Y && p->rate[c])
      p->drifting |= (uint8_t) (1u << c);
  }
}

static void wander_msg(place_t *p, const uint8_t *b) {
  take_over(p, OVERLAY_X);
  take_over(p, OVERLAY_Y);
  for (int i = 0; i < 2; i++) {
    p->wander.speed[i] = LV_MIN(plink_get16(b + OVERLAY_WANDER_SPEED + 2 * i), 10000);
    p->wander.leg[i] = plink_get16(b + OVERLAY_WANDER_LEG + 2 * i);
    p->wander.rest[i] = plink_get16(b + OVERLAY_WANDER_REST + 2 * i);
  }
  p->wander.heading = (int16_t) wrap(plink_i16(b + OVERLAY_WANDER_HEADING), 360);
  p->wander.spread = (int16_t) LV_CLAMP(0, plink_i16(b + OVERLAY_WANDER_SPREAD), 180);
  p->wander.on = true;
  p->drifting = 3;
  new_leg(p);
}

static void face_msg(place_t *p, const uint8_t *b) {
  int16_t tilt = plink_i16(b + OVERLAY_FACE_TILT), rate = plink_i16(b + OVERLAY_FACE_RATE);
  if (b[OVERLAY_FACE_FLAGS] > (OVERLAY_FACE_MIRROR | OVERLAY_FACE_LEVEL) || tilt < 0 || tilt > 180 || rate < 0)
    return;
  take_over(p, OVERLAY_TURN);
  p->face.on = true;
  p->face.flags = b[OVERLAY_FACE_FLAGS];
  p->face.facing = plink_i16(b + OVERLAY_FACE_FACING);
  p->face.tilt = tilt;
  p->face.rate = rate;
}

static void area_msg(place_t *p, const uint8_t *b) {
  int16_t x0 = plink_i16(b + OVERLAY_AREA_X0), y0 = plink_i16(b + OVERLAY_AREA_Y0), x1 = plink_i16(b + OVERLAY_AREA_X1),
          y1 = plink_i16(b + OVERLAY_AREA_Y1);
  const uint8_t *edge = b + OVERLAY_AREA_EDGES;  // x, y
  if (x1 <= x0 || y1 <= y0 || edge[0] > OVERLAY_EDGE_BOUNCE || edge[1] > OVERLAY_EDGE_BOUNCE)
    return;
  p->area.x0 = x0;
  p->area.y0 = y0;
  p->area.x1 = x1;
  p->area.y1 = y1;
  p->area.edge[0] = edge[0];
  p->area.edge[1] = edge[1];
}

// With all OVERLAY_WAVES taken, one that was stopped and is still on its way back is there at once and makes room
static void wave_msg(place_t *p, const uint8_t *b) {
  int c = b[OVERLAY_WAVE_CHANNEL];
  int16_t lo = plink_i16(b + OVERLAY_WAVE_LO), hi = plink_i16(b + OVERLAY_WAVE_HI);
  if (c >= OVERLAY_CHANNELS || hi < lo || b[OVERLAY_WAVE_JITTER] > 100 ||
      b[OVERLAY_WAVE_FLAGS] > (OVERLAY_WAVE_SLOW_LOW | OVERLAY_WAVE_SLOW_HIGH | OVERLAY_WAVE_RANDOM))
    return;
  wave_t *w = wave_of(p, c);
  if (!w && lo == hi && lo * unit(c) == no_change(c))
    return;  // nothing to stop
  for (int i = 0; i < OVERLAY_WAVES && !w; i++)
    if (waves[i].place == OVERLAY_PLACES)
      w = &waves[i];
  for (int i = 0; i < OVERLAY_WAVES && !w; i++)
    if (stopping(&waves[i])) {
      w = &waves[i];
      place_t *q = &places[w->place];
      w->place = OVERLAY_PLACES;
      update(q);
    }
  if (!w)
    return;  // all taken
  if (w->place == OVERLAY_PLACES)
    ramp_set(&w->r, no_change(c));
  w->place = (uint8_t) (p - places);
  w->ch = (uint8_t) c;
  w->lo = lo;
  w->hi = hi;
  for (int i = 0; i < 2; i++) {
    w->ms[i] = plink_get16(b + OVERLAY_WAVE_MS + 2 * i);
    w->hold[i] = plink_get16(b + OVERLAY_WAVE_HOLD + 2 * i);
  }
  w->jitter = b[OVERLAY_WAVE_JITTER];
  w->flags = b[OVERLAY_WAVE_FLAGS];
  ramp_set(&w->r, ramp_at(&w->r));  // on from where it is
  w->left = 0;
  w->up = false;
  wave_settle(w);
}

static void map_msg(place_t *p, const uint8_t *b) {
  int c = b[OVERLAY_MAP_CHANNEL];
  if (c < OVERLAY_TURN || c >= OVERLAY_CHANNELS)
    return;
  for (int i = 0; i < 4; i++)
    p->map[c - OVERLAY_TURN][i] = plink_i16(b + OVERLAY_MAP_VALUES + 2 * i);
}

static void rounds_msg(place_t *p, const uint8_t *b) {
  const slot_t *s = &slots[p->slot];
  p->rounds = plink_get16(b + OVERLAY_ROUNDS_COUNT);
  p->rounds_fade = plink_get16(b + OVERLAY_ROUNDS_FADE);
  p->tag = b[OVERLAY_ROUNDS_TAG];
  p->held = false;
  if (!looping(s))
    p->due = period(s, records(s)[OVERLAY_RECORD_DELAY]);  // counted while it's shown
}

// The ms left of a frame at rate was, at rate
static uint32_t rescaled(uint32_t left, uint16_t was, uint16_t rate) { return was && rate ? left * was / rate : left; }

// A frame due at tick *due at rate was, at rate. From paused it goes on at once.
static void retime(uint32_t *due, uint16_t was, uint16_t rate, uint32_t now) {
  if (!was)
    *due = now;
  else if ((int32_t) (*due - now) > 0)
    *due = now + rescaled(*due - now, was, rate);
}

static void frames_msg(const uint8_t *b) {
  uint8_t i = b[OVERLAY_FRAMES_SLOT];
  uint16_t rate = plink_get16(b + OVERLAY_FRAMES_RATE);
  if (i >= OVERLAY_SLOTS || rate > OVERLAY_RATE_MAX)
    return;
  slot_t *s = &slots[i];
  uint32_t now = lv_tick_get();
  retime(&s->due, s->rate, rate, now);
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (places[k].slot == i && !looping(s))
      places[k].due = rescaled(places[k].due, s->rate, rate);  // ms left of the round
    else if (places[k].slot == i)
      retime(&places[k].due, s->rate, rate, now);
  s->rate = rate;
  wake();
}

void overlay_msg(uint16_t key, const uint8_t *b, int n) {
  // FRAMES to DEPTH, GLIDE and DRIFT 2 a channel more. GONE is ours.
  static const uint8_t LEN[] = {OVERLAY_FRAMES_LEN, OVERLAY_PLACE_LEN,  OVERLAY_AREA_LEN,   OVERLAY_GLIDE_HEAD,
                                OVERLAY_DRIFT_HEAD, OVERLAY_WANDER_LEN, OVERLAY_FACE_LEN,   OVERLAY_FLIP_LEN,
                                OVERLAY_WAVE_LEN,   OVERLAY_MAP_LEN,    OVERLAY_ROUNDS_LEN, 0,
                                OVERLAY_DEPTH_LEN};
  _Static_assert(sizeof LEN == K_OVERLAY_DEPTH - K_OVERLAY_FRAMES + 1, "a length for each");
  if (key < K_OVERLAY_FRAMES || key > K_OVERLAY_DEPTH || n < 2)
    return;
  unsigned moves = key == K_OVERLAY_GLIDE   ? (1u << OVERLAY_CHANNELS) - 1
                   : key == K_OVERLAY_DRIFT ? (1u << (OVERLAY_TURN + 1)) - 1
                                            : 0;
  unsigned channels = b[OVERLAY_GLIDE_CHANNELS];  // DRIFT's too
  if (moves && (!channels || channels & ~moves))
    return;  // no channel, or one it doesn't take
  if (n != LEN[key - K_OVERLAY_FRAMES] + (moves ? 2 * __builtin_popcount(channels) : 0))
    return;
  if (key == K_OVERLAY_FRAMES) {
    frames_msg(b);
    return;
  }
  if (key == K_OVERLAY_PLACE) {
    place_msg(b);
    return;
  }
  uint8_t k = b[OVERLAY_MSG_PLACE];
  place_t *p = k < OVERLAY_PLACES && places[k].slot != OVERLAY_SLOTS ? &places[k] : NULL;
  if (!p)
    return;
  switch (key) {
    case K_OVERLAY_AREA:
      area_msg(p, b);
      break;
    case K_OVERLAY_GLIDE:
      glide_msg(p, b);
      break;
    case K_OVERLAY_DRIFT:
      drift_msg(p, b);
      break;
    case K_OVERLAY_WANDER:
      wander_msg(p, b);
      break;
    case K_OVERLAY_FACE:
      face_msg(p, b);
      break;
    case K_OVERLAY_FLIP:
      if (b[OVERLAY_FLIP_FLAGS] <= (OVERLAY_FLIP_X | OVERLAY_FLIP_Y))
        p->flip = b[OVERLAY_FLIP_FLAGS];
      break;
    case K_OVERLAY_WAVE:
      wave_msg(p, b);
      break;
    case K_OVERLAY_MAP:
      map_msg(p, b);
      break;
    case K_OVERLAY_ROUNDS:
      rounds_msg(p, b);
      break;
    case K_OVERLAY_DEPTH:
      set_depth(p, b[OVERLAY_DEPTH_DEPTH]);
      break;
  }
  update(p);
  order();
  if (moving(p))
    wake_mover();
}

void overlay_init(void) {
  for (int i = 0; i < OVERLAY_SLOTS; i++)
    slots[i].off = NO_OFF;
  for (int i = 0; i < OVERLAY_WAVES; i++)
    waves[i].place = OVERLAY_PLACES;
  for (int k = 0; k < OVERLAY_PLACES; k++) {
    places[k].slot = OVERLAY_SLOTS;
    places[k].frame = NO_OFF;
    reset(&places[k]);
  }
  draw_init();
  if (!timer)
    timer = lv_timer_create(tick, 100, NULL);
  motion_init();
}
