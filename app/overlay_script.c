// Places' scripts, see K_OVERLAY_SCRIPT: steps the panel runs itself, so a place can play parts of its frames, wait,
// move, shape its box, tell the others and look where they are without the ESP. And what's bound, see K_OVERLAY_BIND:
// values worked out again each motion step for a place's channels, its frame and the rows it's drawn in. A script is
// in the pool behind what's there, its run_t with the registers it has and then its bytes.
#include "overlay_int.h"
#include "plink.h"
#include <stddef.h>
#include <string.h>

enum {
  W_LOADING,  // its bytes are still coming
  W_RUN,      // on with the step at pc
  W_YIELD,    // took its OVERLAY_SCRIPT_STEPS, on at the next motion step
  W_MS,       // a WAIT's left ms, pc after it
  W_UNTIL,    // pc at an UNTIL that didn't hold, looked at again at the next motion step
  W_ROUNDS,   // till rounds is want, pc after the ROUNDS
  W_CLIP,     // pc at a CLIP for the end of this round
  W_DONE,     // past its last step, or ENDed
};

typedef struct {
  uint16_t len;       // its bytes
  uint16_t pc;        // the step it's at, or while it's W_LOADING the bytes in so far
  uint8_t wait;       // W_*
  uint8_t tag;        // its END's
  bool turned;        // a step showed another frame at the end of a round
  uint8_t regs;       // registers of its own
  uint16_t frame;     // the frame its own frame shows
  uint16_t from, to;  // its clip, frames from to to, both in
  uint16_t rate;      // percent its frames go at, 0 holds them
  uint32_t rounds;    // of its clip, so far
  union {
    int32_t left;   // ms a WAIT has left
    uint32_t want;  // rounds a ROUNDS waits for
  };
  int32_t r[OVERLAY_OWN_REGS];  // the pool has the first regs of them
} run_t;
_Static_assert(offsetof(run_t, r) == OVERLAY_SCRIPT_HEAD, "a script's state in what the ESP counts for it");
_Static_assert(OVERLAY_SCRIPT_MAX <= UINT16_MAX, "2 bytes for where a step starts");

// A value bound to what it sets: at off in pool, in its place's script or on its own after a byte with its length
typedef struct {
  uint16_t off;  // NO_OFF = free
  uint8_t place;
  uint8_t what;  // an OVERLAY_BIND_* op | what it binds
} bind_t;
#define TARGET 0x3F  // of what

static int32_t shared[OVERLAY_REGS - OVERLAY_OWN_REGS];
static bind_t binds[OVERLAY_BINDS];
static uint16_t mark_bits;  // marks for the ESP
static uint8_t mark_tag[OVERLAY_PLACES];
static bool ending;  // a round just ended, so a CLIP for the end of one goes at once
static run_t *live;  // the state of the script that's running, which the pool's copy is behind
static const place_t *live_of;

static unsigned kept(unsigned regs) { return OVERLAY_SCRIPT_HEAD + 4 * regs; }  // bytes of a run_t in the pool
static uint8_t head(const place_t *p, size_t at) { return p->script == NO_OFF ? 0 : pool[p->script + at]; }
static void load(const place_t *p, run_t *r) {
  const uint8_t *at = pool + p->script;
  memset(r->r, 0, sizeof r->r);
  memcpy(r, at, kept(at[offsetof(run_t, regs)]));
}
static void save(const place_t *p, const run_t *r) { memcpy(pool + p->script, r, kept(r->regs)); }
static unsigned code_at(const place_t *p) { return p->script + kept(head(p, offsetof(run_t, regs))); }
static const uint8_t *bytes(const place_t *p) { return pool + code_at(p); }
static bool loaded(const place_t *p) { return p->script != NO_OFF && head(p, offsetof(run_t, wait)) != W_LOADING; }

// Its script's state as it is now: the running one's, else the pool's into tmp. NULL without a script.
static run_t *state(const place_t *p, run_t *tmp) {
  if (p == live_of)
    return live;
  if (!loaded(p))
    return NULL;
  load(p, tmp);
  return tmp;
}

// ---------------------------------------------------------------- what a value works out

// 0..65535, smoothly from one at random to another each period of t
static int32_t noise(int64_t t, int32_t period, uint32_t seed) {
  if (period <= 0)
    return 0;
  int64_t k = floor_div64(t, period);
  int32_t f = eased(OVERLAY_EASE_IN | OVERLAY_EASE_OUT, (int32_t) ((t - k * period) * 65536 / period));
  int32_t a = (int32_t) (hash32((uint32_t) k ^ seed) & 0xFFFF);
  int32_t b = (int32_t) (hash32(((uint32_t) k + 1) ^ seed) & 0xFFFF);
  return a + (int32_t) ((int64_t) (b - a) * f >> 16);
}

// Going up, slow at low eases in and slow at high eases out. Going down it's the other way round.
_Static_assert(OVERLAY_WAVE_SLOW_LOW == OVERLAY_EASE_IN && OVERLAY_WAVE_SLOW_HIGH == OVERLAY_EASE_OUT,
               "slow at low eases in");
static unsigned mirror_ease(unsigned e) {
  return (e & OVERLAY_EASE_IN ? OVERLAY_EASE_OUT : 0) | (e & OVERLAY_EASE_OUT ? OVERLAY_EASE_IN : 0);
}

// OVERLAY_OP_WAVE of a, how far up it is in 1/65536: 0 at low. Its jitter bends the time by up to that share of a
// round either way, over three rounds so it goes that much faster or slower at the most and never backwards, and
// starts each place somewhere of its own.
static int32_t wave(const int32_t *a, uint32_t seed) {
  int64_t t = a[0];
  int32_t rise = LV_MAX(a[3], 0), fall = LV_MAX(a[4], 0), hi = LV_MAX(a[5], 0), lo = LV_MAX(a[6], 0);
  int32_t jitter = LV_CLAMP(0, a[7], 100);
  unsigned slow = (unsigned) a[8] & (OVERLAY_WAVE_SLOW_LOW | OVERLAY_WAVE_SLOW_HIGH);
  bool random = a[8] & OVERLAY_WAVE_RANDOM;
  int64_t leg = random ? ((int64_t) rise + fall) / 2 : rise, rest = random ? ((int64_t) hi + lo) / 2 : hi;
  int64_t round = random ? leg + rest : leg + rest + fall + lo;
  if (round <= 0)
    return 0;
  if (jitter)
    t += hash32(seed) % round +
         round * jitter * (noise(t, (int32_t) LV_MIN(3 * round, INT32_MAX), seed) - 32768) / (100 * 32768);
  int64_t k = floor_div64(t, round), f = t - k * round;
  if (random) {  // from one at random to the next, then there for the hold
    int32_t from = (int32_t) (hash32((uint32_t) k ^ ~seed) & 0xFFFF);
    int32_t to = (int32_t) (hash32(((uint32_t) k + 1) ^ ~seed) & 0xFFFF);
    int32_t way = f < leg ? eased(to > from ? slow : mirror_ease(slow), (int32_t) (f * 65536 / leg)) : 65536;
    return from + (int32_t) ((int64_t) (to - from) * way >> 16);
  }
  if (f < leg)
    return eased(slow, (int32_t) (f * 65536 / leg));
  if (f < leg + rest)
    return 65536;
  if (f < leg + rest + fall)
    return 65536 - eased(mirror_ease(slow), (int32_t) ((f - leg - rest) * 65536 / fall));
  return 0;
}

// How far x right and y down is
static int32_t hypot_of(int32_t x, int32_t y) {
  x = LV_CLAMP(-32767, x, 32767);
  y = LV_CLAMP(-32767, y, 32767);
  return (int32_t) isqrt((uint32_t) (x * x + y * y));
}

static int32_t sine(int32_t deg, int32_t n, bool cosine) {
  int16_t d = (int16_t) wrap(wrap(deg, 360) + (cosine ? 90 : 0), 360);
  int64_t v = (int64_t) n * lv_trigo_sin(d);
  return (int32_t) ((v + (v < 0 ? -LV_TRIGO_SIN_MAX : LV_TRIGO_SIN_MAX) / 2) / LV_TRIGO_SIN_MAX);  // rounded
}

// What a value is worked out for: a place, its script's state if it has one, where its RANDs come from, the row a
// row's bind is for, and what makes one value's NOISE and WAVE another's. Without a place it's only read.
typedef struct {
  const place_t *p;
  const run_t *r;
  uint32_t *rng;
  int32_t row;
  uint32_t salt;
} env_t;

static const place_t *other(const place_t *p, int32_t k) {
  if (k == OVERLAY_NOBODY)
    return NULL;
  const place_t *q = k == OVERLAY_SELF ? p : &places[k];
  return q->slot == OVERLAY_SLOTS ? NULL : q;
}

// Channel c of q as it's shown, in a GLIDE's units
static int32_t channel(const place_t *q, int c) {
  const look_t *l = &q->now;
  switch (c) {
    case OVERLAY_X:
      return q->x;
    case OVERLAY_Y:
      return q->y;
    case OVERLAY_PAN_X:
    case OVERLAY_PAN_Y:
      return l->pan[c - OVERLAY_PAN_X];
    case OVERLAY_W:
    case OVERLAY_H:
      return l->box[c - OVERLAY_W];
    case OVERLAY_TURN:
      return l->deg;
    case OVERLAY_PHASE:
      return l->phase / 256;
    case OVERLAY_SIZE:
      return l->size / (FULL / 100);
    case OVERLAY_SIZE_X:
    case OVERLAY_SIZE_Y:
      return l->size_xy[c - OVERLAY_SIZE_X] / (FULL / 100);
    case OVERLAY_OPACITY:
      return l->opa / (FULL / 100);
    case OVERLAY_SPEED:
      return l->speed / (FULL / 100);
    case OVERLAY_LIGHT:
      return l->light / (FULL / 100);
    case OVERLAY_LEVEL:
      return l->level / (FULL / 100);
    case OVERLAY_PIVOT_X:
    case OVERLAY_PIVOT_Y:
      return l->pivot[c - OVERLAY_PIVOT_X];
  }
  return l->depth;
}

// Where its pivot is across its area, 0..OVERLAY_AT_MAX from the left or the top
static int32_t across(const place_t *p, int axis) {
  int lo = axis ? p->area.y0 : p->area.x0, hi = axis ? p->area.y1 : p->area.x1;
  return LV_CLAMP(0, (pivot_at(p, axis) - lo) * OVERLAY_AT_MAX / (hi - lo), OVERLAY_AT_MAX);
}

static int32_t get(const env_t *e, unsigned what, unsigned who) {
  const place_t *q = other(e->p, (int32_t) who);
  if (!q)
    return 0;
  if (what < OVERLAY_CHANNELS)
    return channel(q, (int) what);
  const slot_t *s = &slots[q->slot];
  switch (what) {
    case OVERLAY_GET_FRAME:
    case OVERLAY_GET_ROUNDS: {  // of its script as it is, the one that's running ahead of the pool's
      const uint8_t *r = q == live_of ? (const uint8_t *) live : loaded(q) ? pool + q->script : NULL;
      uint16_t frame = 0;
      uint32_t rounds = 0;
      if (r) {
        memcpy(&frame, r + offsetof(run_t, frame), sizeof frame);
        memcpy(&rounds, r + offsetof(run_t, rounds), sizeof rounds);
      }
      return what == OVERLAY_GET_FRAME ? frame : (int32_t) rounds;
    }
    case OVERLAY_GET_FRAMES:
      return LV_MAX(s->frames, 1);
    case OVERLAY_GET_MS:
      return LV_MAX((int32_t) (step_tick() - q->born), 0);
    case OVERLAY_GET_FRAME_W:
      return s->w * s->scale;
    case OVERLAY_GET_FRAME_H:
      return s->h * s->scale;
    case OVERLAY_GET_AT_X:
    case OVERLAY_GET_AT_Y:
      return across(q, what == OVERLAY_GET_AT_Y);
    case OVERLAY_GET_SEED:
      return (int32_t) (seed_of(q) % (OVERLAY_SEED_MAX + 1));
    case OVERLAY_GET_ROW:
      return e->row;
  }
  return 1;  // here
}

// ---------------------------------------------------------------- reading

// What each OP takes of the numbers those before it left. Each leaves one.
const uint8_t OP_TAKES[OVERLAY_OPS] = {
    [OVERLAY_OP_ADD] = 2,   [OVERLAY_OP_SUB] = 2,  [OVERLAY_OP_MUL] = 2,  [OVERLAY_OP_DIV] = 2,
    [OVERLAY_OP_MOD] = 2,   [OVERLAY_OP_MIN] = 2,  [OVERLAY_OP_MAX] = 2,  [OVERLAY_OP_LT] = 2,
    [OVERLAY_OP_LE] = 2,    [OVERLAY_OP_EQ] = 2,   [OVERLAY_OP_NE] = 2,   [OVERLAY_OP_GE] = 2,
    [OVERLAY_OP_GT] = 2,    [OVERLAY_OP_AND] = 2,  [OVERLAY_OP_OR] = 2,   [OVERLAY_OP_NEG] = 1,
    [OVERLAY_OP_ABS] = 1,   [OVERLAY_OP_NOT] = 1,  [OVERLAY_OP_IF] = 3,   [OVERLAY_OP_SCALE] = 3,
    [OVERLAY_OP_WRAP] = 2,  [OVERLAY_OP_SIN] = 2,  [OVERLAY_OP_COS] = 2,  [OVERLAY_OP_ATAN] = 2,
    [OVERLAY_OP_HYPOT] = 2, [OVERLAY_OP_RAND] = 2, [OVERLAY_OP_HASH] = 2, [OVERLAY_OP_NOISE] = 3,
    [OVERLAY_OP_EASE] = 3,  [OVERLAY_OP_WAVE] = 9, [OVERLAY_OP_PICK] = 1, [OVERLAY_OP_CURVE] = 2,
};
_Static_assert(OVERLAY_EXPR_STACK > 9, "room for a WAVE's, and one before them");

// Bytes being read: a step's, or a value's on its own
typedef struct {
  const uint8_t *s;
  unsigned at, len;
  uint8_t regs;  // registers of its own it can take
  bool bad;
} reader_t;

static uint8_t byte(reader_t *t) {
  if (t->at >= t->len) {
    t->bad = true;
    return 0;
  }
  return t->s[t->at++];
}
static uint16_t word(reader_t *t) {  // low byte first
  uint8_t lo = byte(t);
  return (uint16_t) (lo | byte(t) << 8);
}
static bool is_place(unsigned k) { return k < OVERLAY_PLACES || k == OVERLAY_SELF || k == OVERLAY_NOBODY; }

// A list at t, read: where its numbers start and how many
static unsigned list(reader_t *t, unsigned *n) {
  *n = byte(t);
  unsigned at = t->at;
  t->bad |= !*n || at + 2 * *n > t->len;
  if (!t->bad)
    t->at = at + 2 * *n;
  return at;
}

// The way and the distance from place j's pivot to k's, false if one isn't there
static bool between(const place_t *p, unsigned j, unsigned k, int32_t *dx, int32_t *dy) {
  const place_t *a = other(p, (int32_t) j), *b = other(p, (int32_t) k);
  if (!a || !b)
    return false;
  *dx = pivot_at(b, 0) - pivot_at(a, 0);
  *dy = pivot_at(b, 1) - pivot_at(a, 1);
  return true;
}

static int32_t maths(uint8_t k, const int32_t *a) {
  int32_t b = OP_TAKES[k] > 1 ? a[1] : 0;            // one that takes a single number may have none after it
  uint32_t ua = (uint32_t) a[0], ub = (uint32_t) b;  // wraps round, as signed maths mustn't
  bool whole = b && !(a[0] == INT32_MIN && b == -1);
  switch (k) {
    case OVERLAY_OP_ADD:
      return (int32_t) (ua + ub);
    case OVERLAY_OP_SUB:
      return (int32_t) (ua - ub);
    case OVERLAY_OP_MUL:
      return (int32_t) (ua * ub);
    case OVERLAY_OP_DIV:
      return whole ? a[0] / b : 0;
    case OVERLAY_OP_MOD:
      return whole ? a[0] % b : 0;
    case OVERLAY_OP_MIN:
      return LV_MIN(a[0], b);
    case OVERLAY_OP_MAX:
      return LV_MAX(a[0], b);
    case OVERLAY_OP_LT:
      return a[0] < b;
    case OVERLAY_OP_LE:
      return a[0] <= b;
    case OVERLAY_OP_EQ:
      return a[0] == b;
    case OVERLAY_OP_NE:
      return a[0] != b;
    case OVERLAY_OP_GE:
      return a[0] >= b;
    case OVERLAY_OP_GT:
      return a[0] > b;
    case OVERLAY_OP_AND:
      return a[0] && b;
    case OVERLAY_OP_OR:
      return a[0] || b;
    case OVERLAY_OP_NEG:
      return (int32_t) (0u - ua);
    case OVERLAY_OP_ABS:
      return a[0] == INT32_MIN ? INT32_MAX : LV_ABS(a[0]);
    case OVERLAY_OP_NOT:
      return !a[0];
    case OVERLAY_OP_IF:
      return a[0] ? a[1] : a[2];
    case OVERLAY_OP_SCALE:
      return a[2] ? (int32_t) ((int64_t) a[0] * a[1] / a[2]) : 0;
    case OVERLAY_OP_WRAP:
      return a[1] > 0 ? (int32_t) (((int64_t) a[0] % a[1] + a[1]) % a[1]) : 0;
    case OVERLAY_OP_SIN:
    case OVERLAY_OP_COS:
      return sine(a[0], a[1], k == OVERLAY_OP_COS);
    case OVERLAY_OP_ATAN:
      return angle(a[0], a[1]);
    case OVERLAY_OP_HYPOT:
      return hypot_of(a[0], a[1]);
  }
  // EASE
  int32_t n = a[1];
  return n > 0
             ? (int32_t) ((int64_t) eased((unsigned) a[2] & 3, (int32_t) ((int64_t) LV_CLAMP(0, a[0], n) * 65536 / n)) *
                              n >>
                          16)
             : 0;
}

// What makes the NOISE or WAVE read at t of e's place its own
static uint32_t own_seed(const reader_t *t, const env_t *e) {
  return e->p ? seed_of(e->p) ^ hash32(e->salt + t->at) : 0;
}

// OP k of a value at t, with the n numbers on st: takes its own of them and leaves one. Returns how many there are
// then.
static int op(reader_t *t, const env_t *e, uint8_t k, int32_t *st, int n) {
  if (k == OVERLAY_VEXPR || k == OVERLAY_OP_END || k >= OVERLAY_OPS || OP_TAKES[k] > n ||
      n - OP_TAKES[k] >= OVERLAY_EXPR_STACK) {
    t->bad = true;
    return n;
  }
  int32_t *a = st + n - OP_TAKES[k], v = 0;
  if (k >= OVERLAY_VREG && k < OVERLAY_V8) {
    unsigned r = k - OVERLAY_VREG;
    t->bad |= r < OVERLAY_OWN_REGS && r >= t->regs;
    v = r >= OVERLAY_OWN_REGS ? shared[r - OVERLAY_OWN_REGS] : e->r && r < e->r->regs ? e->r->r[r] : 0;
  } else {
    switch (k) {
      case OVERLAY_V8:
        v = (int8_t) byte(t);
        break;
      case OVERLAY_V16:
        v = (int16_t) word(t);
        break;
      case OVERLAY_V32: {
        uint32_t lo = word(t);
        v = (int32_t) (lo | (uint32_t) word(t) << 16);
        break;
      }
      case OVERLAY_OP_GET:
      case OVERLAY_OP_OWN: {
        unsigned what = byte(t), who = k == OVERLAY_OP_GET ? byte(t) : OVERLAY_SELF;
        t->bad |= what >= OVERLAY_GETS || !is_place(who);
        v = t->bad || !e->p ? 0 : get(e, what, who);
        break;
      }
      case OVERLAY_OP_DIST:
      case OVERLAY_OP_ANGLE: {
        unsigned j = byte(t), i = byte(t);
        int32_t dx = 0, dy = 0;
        t->bad |= !is_place(j) || !is_place(i);
        if (!t->bad && e->p && between(e->p, j, i, &dx, &dy))
          v = k == OVERLAY_OP_ANGLE ? angle(dx, dy) : hypot_of(dx, dy);
        break;
      }
      case OVERLAY_OP_PICK:
      case OVERLAY_OP_CURVE: {
        unsigned m, at = list(t, &m);
        if (t->bad)
          break;
        const uint8_t *l = t->s + at;
        if (k == OVERLAY_OP_PICK || m == 1 || a[1] <= 0) {
          v = plink_i16(l + 2 * LV_CLAMP(0, k == OVERLAY_OP_PICK ? a[0] : 0, (int32_t) m - 1));
        } else {  // how far along it is in 1/65536 of the way from one number to the next
          int64_t x = (int64_t) LV_CLAMP(0, a[0], a[1]) * (m - 1) * 65536 / a[1];
          unsigned i = (unsigned) LV_MIN(x >> 16, (int64_t) m - 2);
          int32_t from = plink_i16(l + 2 * i), to = plink_i16(l + 2 * i + 2);
          v = from + (int32_t) ((to - from) * (x - ((int64_t) i << 16)) >> 16);
        }
        break;
      }
      case OVERLAY_OP_RAND:
        v = e->rng ? pick(e->rng, a[0], a[1]) : 0;
        break;
      case OVERLAY_OP_HASH:
        v = a[1] > 0 && e->p ? (int32_t) (hash32((uint32_t) a[0] ^ seed_of(e->p)) % (uint32_t) a[1]) : 0;
        break;
      case OVERLAY_OP_NOISE:
        v = (int32_t) ((int64_t) noise(a[0], a[1], own_seed(t, e)) * a[2] >> 16);
        break;
      case OVERLAY_OP_WAVE:
        v = (int32_t) (a[1] + (((int64_t) a[2] - a[1]) * wave(a, own_seed(t, e)) >> 16));
        break;
      default:
        v = maths(k, a);
    }
  }
  a[0] = v;
  return (int) (a - st) + 1;
}

// A value at t worked out for e
static int32_t value(reader_t *t, const env_t *e) {
  int32_t st[OVERLAY_EXPR_STACK] = {0};
  int n = 0;
  bool many = t->at < t->len && t->s[t->at] == OVERLAY_VEXPR;
  t->at += many;
  do {
    uint8_t k = byte(t);
    if (many && k == OVERLAY_OP_END)
      break;
    n = op(t, e, k, st, n);
  } while (many && !t->bad);
  t->bad |= n != 1;
  return t->bad ? 0 : st[0];
}

// ---------------------------------------------------------------- what's bound

static bool in_script(const place_t *p, unsigned off) {
  return p->script != NO_OFF && off >= p->script &&
         off < code_at(p) + plink_get16(pool + p->script + offsetof(run_t, len));
}

// It goes, and its bytes with it if they're its own
static void unbind(bind_t *b) {
  place_t *p = &places[b->place];
  uint16_t off = b->off;
  b->off = NO_OFF;
  p->bound--;
  if (!in_script(p, off))
    give((uint16_t) (off - 1), pool[off - 1] + 1u);
}

void binds_moved(uint16_t off, uint32_t n) {
  for (int i = 0; i < OVERLAY_BINDS; i++)
    if (binds[i].off != NO_OFF && binds[i].off > off)
      binds[i].off -= (uint16_t) n;
}

// What a bind can be: all three ops on a channel, the rest only set
static bool bindable(unsigned what, unsigned regs) {
  unsigned to = what & TARGET, o = what & OVERLAY_BIND_OP;
  if (to < OVERLAY_CHANNELS)
    return true;
  bool reg = to >= OVERLAY_BIND_REG && to < OVERLAY_BIND_REG + OVERLAY_REGS &&
             (to - OVERLAY_BIND_REG >= OVERLAY_OWN_REGS || to - OVERLAY_BIND_REG < regs);
  return (to < OVERLAY_BIND_TARGETS || reg) && (o == OVERLAY_BIND_SET || o == OVERLAY_BIND_FREE);
}

// p's binds of what's target go, all of them for a FREE, else the one of its op
static void unbind_like(const place_t *p, uint8_t what) {
  for (int i = 0; i < OVERLAY_BINDS; i++) {
    bind_t *b = &binds[i];
    if (b->off != NO_OFF && b->place == p - places && !((b->what ^ what) & TARGET) &&
        ((what & OVERLAY_BIND_OP) == OVERLAY_BIND_FREE || b->what == what))
      unbind(b);
  }
}

// What says is bound to the value at off from now on. False with all OVERLAY_BINDS taken.
static bool bind(place_t *p, uint8_t what, unsigned off) {
  for (int i = 0; i < OVERLAY_BINDS; i++)
    if (binds[i].off == NO_OFF) {
      binds[i] = (bind_t) {(uint16_t) off, (uint8_t) (p - places), what};
      p->bound++;
      return true;
    }
  return false;
}

void bind_msg(place_t *p, const uint8_t *b, int n) {
  uint8_t what = b[OVERLAY_BIND_WHAT];
  bool frees = (what & OVERLAY_BIND_OP) == OVERLAY_BIND_FREE;
  reader_t t = {b, OVERLAY_BIND_VALUE, (unsigned) n, OVERLAY_OWN_REGS, false};
  if (!bindable(what, OVERLAY_OWN_REGS) || (frees ? n != OVERLAY_BIND_VALUE : n - OVERLAY_BIND_VALUE > 255))
    return;
  if (!frees && (value(&t, &(env_t) {0}), t.bad || t.at != t.len))
    return;
  unbind_like(p, what);
  if (frees)
    return;
  unsigned len = (unsigned) n - OVERLAY_BIND_VALUE, off = take_for(p, 1 + len);
  if (off == NO_OFF)
    return;
  pool[off] = (uint8_t) len;
  memcpy(pool + off + 1, b + OVERLAY_BIND_VALUE, len);
  if (!bind(p, what, off + 1))
    give((uint16_t) off, 1 + len);
}

void binds_drop(place_t *p) {
  for (int i = 0; i < OVERLAY_BINDS && p->bound; i++)
    if (binds[i].off != NO_OFF && binds[i].place == p - places)
      unbind(&binds[i]);
}

// What b is bound to, worked out for e
static int32_t bound(const bind_t *b, env_t *e) {
  reader_t t = {pool + b->off, 0, OVERLAY_POOL - b->off, OVERLAY_OWN_REGS, false};
  e->salt = (uint32_t) (b - binds) << 16;
  return value(&t, e);
}

// The most a bind makes a channel, either way: far past what any shows, and whole in all that's worked out from it
#define BOUND_FAR (1 << 30)

void script_bound(place_t *p, int32_t *v, int from, int last) {
  static const uint8_t ORDER[] = {OVERLAY_BIND_SET, OVERLAY_BIND_MUL, OVERLAY_BIND_ADD};
  run_t tmp, *r = state(p, &tmp);
  env_t e = {p, r, &p->rng, 0, 0};
  bool set = false;
  for (int pass = from ? 0 : -1; pass < 3; pass++)  // the registers first, for the rest to take
    for (int i = 0; i < OVERLAY_BINDS; i++) {
      const bind_t *b = &binds[i];
      int to = b->what & TARGET;
      if (b->off == NO_OFF || b->place != p - places)
        continue;
      if (pass < 0 && to >= OVERLAY_BIND_REG) {
        unsigned k = (unsigned) to - OVERLAY_BIND_REG;
        int32_t x = bound(b, &e);
        if (k >= OVERLAY_OWN_REGS && !p->leaving)  // those are the ones that stay's
          shared[k - OVERLAY_OWN_REGS] = x;
        else if (k < OVERLAY_OWN_REGS && r && k < r->regs)
          set = true, r->r[k] = x;
      } else if (pass >= 0 && to >= from && to <= last && (b->what & OVERLAY_BIND_OP) == ORDER[pass]) {
        int64_t x = bound(b, &e);
        x = pass == 0 ? x : pass == 1 ? v[to] * x / FULL : v[to] + x;
        v[to] = (int32_t) LV_CLAMP(-BOUND_FAR, x, BOUND_FAR);
      }
    }
  if (set && r == &tmp)
    save(p, r);
}

// Whether b binds a row of p's
static bool binds_row(const bind_t *b, const place_t *p) {
  unsigned to = b->what & TARGET;
  return b->off != NO_OFF && b->place == p - places && to >= OVERLAY_BIND_ROW_SHIFT && to <= OVERLAY_BIND_ROW_PHASE;
}

void script_row(const place_t *p, int row, row_t *out) {
  run_t tmp;
  uint32_t rng = p->rng + (uint32_t) row;  // its RANDs are the row's, and the place's stay as they were
  env_t e = {p, state(p, &tmp), &rng, row, 0};
  int32_t v[] = {0, FULL, FULL, FULL, 0};
  for (int i = 0; i < OVERLAY_BINDS; i++)
    if (binds_row(&binds[i], p))
      v[(binds[i].what & TARGET) - OVERLAY_BIND_ROW_SHIFT] = bound(&binds[i], &e);
  // a shift to the pixel, a quarter of one when it's smooth, as a place is where it is: less doesn't draw it again
  int32_t px = p->now.view & OVERLAY_VIEW_SMOOTH ? MILLI / 4 : MILLI, far = OVERLAY_BOX_MAX * MILLI;
  *out = (row_t) {floor_div(LV_CLAMP(-far, v[0], far) + px / 2, px) * px,
                  LV_CLAMP(OVERLAY_SIZE_MIN * FULL / 100, v[1], OVERLAY_SIZE_MAX * FULL / 100), LV_MAX(v[2], 0),
                  LV_MAX(v[3], 0), v[4]};
}

uint16_t script_rows(const place_t *p) {
  bool any = false;
  for (int i = 0; i < OVERLAY_BINDS && !any; i++)
    any = binds_row(&binds[i], p);
  if (!any)
    return 0;
  uint32_t sum = 0;
  for (int row = 0; row < shown(p, 1); row += 4) {  // every fourth says enough
    row_t r;
    script_row(p, row, &r);
    sum = hash32(sum + (uint32_t) (r.shift + 3 * r.size + 5 * r.opa + 7 * r.light + 11 * r.phase));
  }
  return (uint16_t) (sum | 1);
}

// ---------------------------------------------------------------- reading a step

// What each step takes, see OVERLAY_DO_*: r a register, v a value, p a place, a where to go on, m an OVERLAY_CLIP_*,
// t a tag, s a shape and f soft edges, read as where they start, z a size, b what a BIND binds, its value after it
// read as where it starts. SEND's message is read on its own after its place, and GLIDE's made of what it has.
const char *const SCRIPT_FORMS[OVERLAY_DOS] = {
    [OVERLAY_DO_SET] = "rv",  [OVERLAY_DO_GOTO] = "a",   [OVERLAY_DO_IF] = "va",      [OVERLAY_DO_WAIT] = "v",
    [OVERLAY_DO_UNTIL] = "v", [OVERLAY_DO_ROUNDS] = "v", [OVERLAY_DO_CLIP] = "vvmzz", [OVERLAY_DO_FRAME] = "v",
    [OVERLAY_DO_RATE] = "v",  [OVERLAY_DO_GLIDE] = "",   [OVERLAY_DO_SEND] = "p",     [OVERLAY_DO_BIND] = "b",
    [OVERLAY_DO_MARK] = "t",  [OVERLAY_DO_END] = "vt",   [OVERLAY_DO_SHAPE] = "s",    [OVERLAY_DO_SOFT] = "f",
};

// A step being read, and what it says
typedef struct {
  reader_t t;
  uint8_t op;
  int32_t a[5];                   // in order, a value as it is now, five for the longest form, CLIP
  uint16_t key;                   // SEND's
  uint8_t n;                      // its message's length
  uint8_t msg[OVERLAY_SEND_MAX];  // with its values in
} step_t;

// The keys a SEND can have: what a place takes but SCRIPT
static bool sendable(uint16_t key) { return key >= K_OVERLAY_AREA && key <= K_OVERLAY_TINT; }

// What a message takes of a value: its low 16 bits, past those it's at the end
static uint16_t low16(int32_t v) { return (uint16_t) LV_CLAMP(-32768, v, 65535); }

// SEND after its place: key, n, the message and k values for it, each at its byte
static void read_send(step_t *s, const env_t *e) {
  reader_t *t = &s->t;
  s->key = (uint16_t) (K_OVERLAY_FRAMES + byte(t));
  s->n = byte(t);
  if (!sendable(s->key) || s->n < 2 || s->n > OVERLAY_SEND_MAX) {
    t->bad = true;
    return;
  }
  for (unsigned i = 0; i < s->n; i++)
    s->msg[i] = byte(t);
  if (msg_len(s->key, s->n) != (int) s->n)
    t->bad = true;
  for (unsigned k = byte(t); k && !t->bad; k--) {
    unsigned at = byte(t);
    uint16_t v = low16(value(t, e));
    if (!at || at + 2 > s->n)
      t->bad = true;
    else
      plink_put16(s->msg + at, v);
  }
}

// GLIDE: the message it is, to its own place
static void read_glide(step_t *s, const env_t *e) {
  reader_t *t = &s->t;
  unsigned k = byte(t), n = k & 63;
  t->bad |= !n || n > OVERLAY_GLIDE_STEP;
  s->a[0] = OVERLAY_SELF;
  s->key = K_OVERLAY_GLIDE;
  s->n = (uint8_t) (OVERLAY_GLIDE_HEAD + OVERLAY_GLIDE_EACH * n);
  s->msg[OVERLAY_GLIDE_EASE] = (uint8_t) (k >> 6);
  plink_put16(s->msg + OVERLAY_GLIDE_MS, low16(value(t, e)));
  for (unsigned at = OVERLAY_GLIDE_HEAD; at < s->n && !t->bad; at += OVERLAY_GLIDE_EACH) {
    s->msg[at] = byte(t);
    plink_put16(s->msg + at + 1, low16(value(t, e)));
  }
}

// A shape's runs, all of them read. Where it starts, at its n.
static int32_t read_shape(reader_t *t) {
  int32_t at = (int32_t) t->at;
  unsigned n = byte(t);
  t->bad |= n % 3 != 0 || n > 3 * OVERLAY_SHAPE_RUNS;
  for (unsigned i = 0; i < n && !t->bad; i += 3) {
    int16_t w = (int16_t) word(t), top = (int16_t) word(t), bottom = (int16_t) word(t);
    t->bad |= w < 0 || bottom < top;
  }
  return at;
}

// Soft edges, all four read. Where they start.
static int32_t read_soft(reader_t *t) {
  int32_t at = (int32_t) t->at;
  for (int i = 0; i < 4 && !t->bad; i++)
    t->bad |= word(t) > OVERLAY_BOX_MAX;
  return at;
}

// The step at at of the len bytes at code into s, its values worked out for e. False if it's bad.
static bool read_step(step_t *s, const uint8_t *code, unsigned len, unsigned at, unsigned regs, const env_t *e) {
  reader_t *t = &s->t;
  *t = (reader_t) {code, at, len, (uint8_t) regs, false};
  s->op = byte(t);
  if (s->op >= OVERLAY_DOS)
    return false;
  int i = 0;
  for (const char *f = SCRIPT_FORMS[s->op]; *f && !t->bad; f++, i++) {
    int32_t v = *f == 'v'                ? value(t, e)
                : *f == 's'              ? read_shape(t)
                : *f == 'f'              ? read_soft(t)
                : *f == 'a' || *f == 'z' ? word(t)
                                         : byte(t);
    bool ok = *f == 'r'   ? v < OVERLAY_REGS && (v >= OVERLAY_OWN_REGS || v < (int32_t) regs)
              : *f == 'p' ? is_place((unsigned) v)
              : *f == 'm' ? v <= OVERLAY_CLIP_NEXT
              : *f == 'b' ? bindable((unsigned) v, regs)
                          : true;
    t->bad |= !ok;
    s->a[i] = v;
  }
  if (s->op == OVERLAY_DO_SEND && !t->bad)
    read_send(s, e);
  if (s->op == OVERLAY_DO_GLIDE)
    read_glide(s, e);
  if (s->op == OVERLAY_DO_BIND && !t->bad && (s->a[0] & OVERLAY_BIND_OP) != OVERLAY_BIND_FREE) {
    s->a[1] = (int32_t) t->at;
    value(t, &(env_t) {0});  // read past, it's worked out when it's time
  }
  return !t->bad;
}

// Whether a script for p is whole: each step can be read, each goes on at the start of a step or the end, and only
// a place with a frame of its own plays frames. On its own, so what it reads with is off the stack when it runs.
__attribute__((noinline)) static bool check(const place_t *p, const uint8_t *code, unsigned len, unsigned regs) {
  uint8_t starts[(OVERLAY_SCRIPT_MAX + 8) / 8] = {0}, to[sizeof starts] = {0};
  step_t s;
  for (unsigned at = 0; at < len; at = s.t.at) {
    if (!read_step(&s, code, len, at, regs, &(env_t) {0}))
      return false;
    starts[at / 8] |= (uint8_t) (1u << at % 8);
    if (s.op == OVERLAY_DO_GOTO || s.op == OVERLAY_DO_IF) {
      unsigned go = (unsigned) s.a[s.op == OVERLAY_DO_IF];
      if (go > len)
        return false;
      to[go / 8] |= (uint8_t) (1u << go % 8);
    }
    bool frames = s.op == OVERLAY_DO_CLIP || s.op == OVERLAY_DO_FRAME || s.op == OVERLAY_DO_RATE ||
                  (s.op == OVERLAY_DO_BIND && (s.a[0] & TARGET) == OVERLAY_BIND_FRAME);
    if (frames && !p->own)
      return false;
  }
  starts[len / 8] |= (uint8_t) (1u << len % 8);
  for (unsigned i = 0; i < sizeof starts; i++)
    if (to[i] & ~starts[i])
      return false;
  return true;
}

// ---------------------------------------------------------------- frames of its own

// ms a record's delay is at the slot's rate and the script's, OVERLAY_FRAME_MIN_MS at least
static uint32_t frame_ms(const slot_t *s, const run_t *r, uint8_t delay) {
  uint32_t ms = delay * (OVERLAY_TICK_MS * 100u * 100u) / (LV_MAX(s->rate, 1) * LV_MAX(r->rate, 1));
  return ms < OVERLAY_FRAME_MIN_MS ? OVERLAY_FRAME_MIN_MS : ms;
}

// Frame f, 0..frames - 1: on from the one it shows if that's next, else from the first
static void to_frame(place_t *p, run_t *r, unsigned f) {
  const slot_t *s = &slots[p->slot];
  uint8_t *frame = s->still ? NULL : pool + p->frame, delay;
  lv_area_t a = {0, 0, (lv_coord_t) (s->w - 1), (lv_coord_t) (s->h - 1)};
  if (f == (r->frame + 1u) % s->frames && !more(s, p->next)) {
    a = (lv_area_t) {0, 0, -1, -1};
    delay = advance(s, frame, &p->at, &p->next, &a);
  } else {
    first_frame(s, frame, &p->at, &p->next);
    delay = records(s)[OVERLAY_RECORD_DELAY];
    for (unsigned k = 0; k < f;) {
      bool starts = !more(s, p->next);
      uint8_t d = advance(s, frame, &p->at, &p->next, NULL);
      if (starts) {
        k++;
        delay = d;
      }
    }
  }
  r->frame = (uint16_t) f;
  r->turned = true;
  p->due = lv_tick_get() + frame_ms(s, r, delay);
  if (a.x2 >= a.x1)
    changed(p, &a);
  wake();
}

// v as one of its frames, round them
static unsigned frame_of(const place_t *p, int32_t v) {
  int32_t n = slots[p->slot].frames;
  return n ? (unsigned) wrap(v, n) : 0;
}

static bool plays(const place_t *p) { return looping(&slots[p->slot]); }

// Its box w x h of its frame's px at once, the frame's width or height for 0
static void to_size(place_t *p, int32_t w, int32_t h) {
  const slot_t *s = &slots[p->slot];
  int32_t n[2] = {w ? w : s->w, h ? h : s->h};
  for (int i = 0; i < 2; i++)
    ch_set(p, OVERLAY_W + i, LV_MIN(n[i] * s->scale, OVERLAY_BOX_MAX) * MILLI);
  update(p);
}

static void set_rate(place_t *p, run_t *r, int32_t v) {
  uint16_t was = r->rate, rate = (uint16_t) LV_CLAMP(0, v, OVERLAY_RATE_MAX);
  uint32_t now = lv_tick_get();
  if (!was)
    p->due = now;
  else if (rate && (int32_t) (p->due - now) > 0)
    p->due = now + (p->due - now) * was / rate;
  r->rate = rate;
  wake();
}

// The frame it's bound to, if it is: there once that's another, and held
static void bound_frame(place_t *p) {
  run_t tmp, *r = state(p, &tmp);
  if (!r || !p->own || !plays(p))
    return;
  for (int i = 0; i < OVERLAY_BINDS; i++) {
    const bind_t *b = &binds[i];
    if (b->off == NO_OFF || b->place != p - places || (b->what & TARGET) != OVERLAY_BIND_FRAME)
      continue;
    env_t e = {p, r, &p->rng, 0, 0};
    unsigned f = frame_of(p, bound(b, &e));
    if (r->rate)
      set_rate(p, r, 0);
    if (f != r->frame)
      to_frame(p, r, f);
    if (r == &tmp)
      save(p, r);
    return;
  }
}

// ---------------------------------------------------------------- running it

// The step at r->pc, which can be the one it waits at. False when it's over: the script ended, or waits.
static bool do_step(place_t *p, run_t *r) {
  step_t s;
  env_t e = {p, r, &p->rng, 0, r->pc};
  if (!read_step(&s, bytes(p), r->len, r->pc, r->regs, &e)) {  // check() saw it whole, so it can't be
    r->pc = r->len;
    return true;
  }
  int32_t *a = s.a;
  uint16_t next = (uint16_t) s.t.at;
  switch (s.op) {
    case OVERLAY_DO_SET:
      if (a[0] < OVERLAY_OWN_REGS)
        r->r[a[0]] = a[1];
      else if (!p->leaving)  // those are the ones that stay's
        shared[a[0] - OVERLAY_OWN_REGS] = a[1];
      break;
    case OVERLAY_DO_GOTO:
      next = (uint16_t) a[0];
      break;
    case OVERLAY_DO_IF:
      if (a[0])
        next = (uint16_t) a[1];
      break;
    case OVERLAY_DO_WAIT:
      if (a[0] > 0) {
        r->left = (int32_t) LV_MIN((uint32_t) a[0] + since_step(), INT32_MAX);  // the next step takes those too
        r->wait = W_MS;
      }
      break;
    case OVERLAY_DO_UNTIL:
      if (!a[0]) {
        r->wait = W_UNTIL;
        return false;
      }
      break;
    case OVERLAY_DO_ROUNDS:
      if (a[0] > 0) {
        r->want = r->rounds + (uint32_t) a[0];
        r->wait = W_ROUNDS;
      }
      break;
    case OVERLAY_DO_CLIP:
      if (!plays(p))
        break;
      if (a[2] == OVERLAY_CLIP_NEXT && r->wait != W_CLIP && !ending) {
        r->wait = W_CLIP;
        return false;  // done from here at the end of the round
      }
      r->from = (uint16_t) frame_of(p, a[0]);
      r->to = (uint16_t) LV_MAX(r->from, frame_of(p, a[1]));
      r->rounds = 0;
      to_size(p, a[3], a[4]);
      to_frame(p, r, r->from);
      break;
    case OVERLAY_DO_FRAME:
      if (plays(p))
        to_frame(p, r, frame_of(p, a[0]));
      break;
    case OVERLAY_DO_RATE:
      if (plays(p))
        set_rate(p, r, a[0]);
      break;
    case OVERLAY_DO_GLIDE:
    case OVERLAY_DO_SEND:
      s.msg[OVERLAY_MSG_PLACE] = (uint8_t) (a[0] == OVERLAY_SELF ? p - places : a[0]);
      overlay_msg(s.key, s.msg, s.n);
      break;
    case OVERLAY_DO_BIND:
      unbind_like(p, (uint8_t) a[0]);  // which can move the script, so where the value is comes after
      if ((a[0] & OVERLAY_BIND_OP) != OVERLAY_BIND_FREE)
        bind(p, (uint8_t) a[0], code_at(p) + (unsigned) a[1]);
      break;
    case OVERLAY_DO_SHAPE:
      p->shape = (uint16_t) a[0];
      redraw_place(p);
      break;
    case OVERLAY_DO_SOFT: {
      const uint8_t *k = bytes(p) + a[0];
      p->soft =
          plink_get16(k) | plink_get16(k + 2) | plink_get16(k + 4) | plink_get16(k + 6) ? (uint16_t) a[0] : NO_OFF;
      redraw_place(p);
      break;
    }
    case OVERLAY_DO_MARK:
      mark_bits |= (uint16_t) (1u << (p - places));
      mark_tag[p - places] = (uint8_t) a[0];
      break;
    case OVERLAY_DO_END:
      r->tag = (uint8_t) a[1];
      r->rate = 0;
      r->wait = W_DONE;
      save(p, r);
      if (!p->leaving)
        leave(p, (uint16_t) LV_CLAMP(0, a[0], 65535), LEAVE_GONE);
      return false;
  }
  r->pc = next;
  if (r->wait == W_CLIP)
    r->wait = W_RUN;
  return r->wait == W_RUN;
}

// On from r->pc until it waits or ends, OVERLAY_SCRIPT_STEPS steps at most
static void run(place_t *p) {
  run_t r;
  load(p, &r);
  live = &r;
  live_of = p;
  for (int n = 0; p->script != NO_OFF; n++) {
    if (r.pc >= r.len) {
      r.wait = W_DONE;
      break;
    }
    if (n == OVERLAY_SCRIPT_STEPS) {
      r.wait = W_YIELD;
      break;
    }
    if (!do_step(p, &r))
      break;
  }
  live = NULL;
  live_of = NULL;
  if (p->script == NO_OFF)
    return;  // a step ended the place, and it wasn't shown or made room
  save(p, &r);
  if (r.wait == W_MS || r.wait == W_UNTIL || r.wait == W_YIELD)
    wake_mover();
}

// A round of its clip ended: a ROUNDS or CLIP that waited for it goes on
static void round_over(place_t *p) {
  run_t r;
  load(p, &r);
  r.rounds++;
  bool on = (r.wait == W_ROUNDS && r.rounds == r.want) || r.wait == W_CLIP;
  if (r.wait == W_ROUNDS && on)
    r.wait = W_RUN;
  save(p, &r);
  if (on) {
    ending = true;
    run(p);
    ending = false;
  }
}

// ---------------------------------------------------------------- from overlay.c and overlay_motion.c

void script_drop(place_t *p) {
  if (p->script == NO_OFF)
    return;
  for (int i = 0; i < OVERLAY_BINDS && p->bound; i++)
    if (binds[i].off != NO_OFF && binds[i].place == p - places && in_script(p, binds[i].off))
      unbind(&binds[i]);
  give(p->script, code_at(p) - p->script + plink_get16(pool + p->script + offsetof(run_t, len)));
  p->script = NO_OFF;
  mark_bits &= (uint16_t) ~(1u << (p - places));
  if (p->shape != NO_OFF || p->soft != NO_OFF)
    redraw_place(p);
  p->shape = p->soft = NO_OFF;
}

const uint8_t *script_shape(const place_t *p, int *runs) {
  if (p->shape == NO_OFF || !bytes(p)[p->shape])
    return NULL;
  *runs = bytes(p)[p->shape] / 3;
  return bytes(p) + p->shape + 1;
}

const uint8_t *script_soft(const place_t *p) { return p->soft == NO_OFF ? NULL : bytes(p) + p->soft; }

void script_msg(place_t *p, const uint8_t *b, int n) {
  if (n < OVERLAY_SCRIPT_BYTES) {
    script_drop(p);
    return;
  }
  unsigned at = plink_get16(b + OVERLAY_SCRIPT_AT), len = plink_get16(b + OVERLAY_SCRIPT_LEN),
           regs = b[OVERLAY_SCRIPT_REGS], k = (unsigned) n - OVERLAY_SCRIPT_BYTES;
  run_t r;
  if (len > OVERLAY_SCRIPT_MAX || at + k > len || regs > OVERLAY_OWN_REGS) {
    script_drop(p);
    return;
  }
  if (!at) {
    script_drop(p);
    if (!len || (p->script = take_for(p, kept(regs) + len)) == NO_OFF)
      return;
    memset(&r, 0, sizeof r);
    r.len = (uint16_t) len;
    r.regs = (uint8_t) regs;
  } else {
    if (p->script == NO_OFF)
      return;
    load(p, &r);
    if (r.wait != W_LOADING || at != r.pc || len != r.len || regs != r.regs) {  // not the one coming in
      script_drop(p);
      return;
    }
  }
  memcpy(pool + p->script + kept(regs) + at, b + OVERLAY_SCRIPT_BYTES, k);
  r.pc = (uint16_t) (at + k);
  save(p, &r);
  if (r.pc < r.len)
    return;
  if (!check(p, bytes(p), len, regs)) {
    script_drop(p);
    return;
  }
  // From its first step and the first frame, all of them round and round
  const slot_t *s = &slots[p->slot];
  r.pc = 0;
  r.wait = W_RUN;
  r.rate = 100;
  r.to = (uint16_t) LV_MAX(s->frames, 1) - 1;
  if (p->own && plays(p)) {
    first_frame(s, s->still ? NULL : pool + p->frame, &p->at, &p->next);
    p->due = lv_tick_get() + frame_ms(s, &r, records(s)[OVERLAY_RECORD_DELAY]);
    lv_area_t a = {0, 0, (lv_coord_t) (s->w - 1), (lv_coord_t) (s->h - 1)};
    changed(p, &a);
    wake();
  } else if (!plays(p)) {
    p->due = period(s, round_delay(s));  // its rounds from now
  }
  save(p, &r);
  run(p);
}

bool script_plays(const place_t *p) { return loaded(p); }

bool script_moves(const place_t *p) {
  uint8_t w = head(p, offsetof(run_t, wait));
  return p->script != NO_OFF && (w == W_MS || w == W_UNTIL || w == W_YIELD || (!plays(p) && w != W_DONE));
}

void script_steps(place_t *p, uint32_t ms) {
  if (p->bound)
    bound_frame(p);
  if (!loaded(p))
    return;
  const slot_t *s = &slots[p->slot];
  if (!plays(p) && s->rate) {  // a round each time its first record's delay goes by
    uint32_t left = ms;
    while (p->script != NO_OFF && left >= p->due) {
      left -= p->due;
      p->due = period(s, round_delay(s));
      round_over(p);
    }
    p->due -= left;
  }
  if (p->script == NO_OFF)
    return;
  run_t r;
  load(p, &r);
  if (r.wait == W_MS) {
    r.left -= (int32_t) LV_MIN(ms, INT32_MAX);
    if (r.left > 0) {
      save(p, &r);
      return;
    }
  } else if (r.wait != W_YIELD && r.wait != W_UNTIL) {
    return;
  }
  r.wait = W_RUN;
  save(p, &r);
  run(p);
}

void script_round(place_t *p) {
  if (loaded(p))
    round_over(p);
}

uint32_t script_play(place_t *p, uint32_t now) {
  run_t r;
  load(p, &r);
  if (!r.rate)
    return 1000;
  if ((int32_t) (p->due - now) > 0)
    return p->due - now;
  const slot_t *s = &slots[p->slot];
  if (!more(s, p->next) && r.frame == r.to) {  // its clip's last frame is over
    r.turned = false;
    save(p, &r);
    round_over(p);
    if (p->script == NO_OFF)
      return 1000;  // a step ended the place, or its script
    load(p, &r);
    if (!r.turned && r.rate)
      to_frame(p, &r, r.from);
  } else {
    lv_area_t a = {0, 0, -1, -1};
    bool starts = !more(s, p->next);
    p->due += frame_ms(s, &r, advance(s, s->still ? NULL : pool + p->frame, &p->at, &p->next, &a));
    if (starts)
      r.frame = (uint16_t) ((r.frame + 1u) % s->frames);
    if (a.x2 >= a.x1)
      changed(p, &a);
  }
  save(p, &r);
  if (!r.rate)
    return 1000;
  if ((int32_t) (p->due - now) <= 0)
    p->due = now + OVERLAY_FRAME_MIN_MS;  // fell behind, like after the backlight was off
  return p->due - now;
}

uint8_t script_tag(const place_t *p) { return head(p, offsetof(run_t, tag)); }

void script_clear(void) {
  memset(shared, 0, sizeof shared);
  mark_bits = 0;
}

void script_init(void) {
  for (int i = 0; i < OVERLAY_BINDS; i++)
    binds[i].off = NO_OFF;
}

int32_t overlay_mark(void) {
  for (int k = 0; k < OVERLAY_PLACES; k++)
    if (mark_bits >> k & 1) {
      mark_bits &= (uint16_t) ~(1u << k);
      return k | mark_tag[k] << 8;
    }
  return -1;
}
