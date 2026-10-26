// The animations' insides, shared by anim.c, anim_motion.c and anim_draw.c. See anim.h.
#pragma once
#include "anim.h"
#include "plink_keys.h"

#define MOVE_MS 50     // a motion step, 20 a second
#define MOVE_MAX 20    // steps the motion catches up at most, after a stall
#define NO_OFF 0xFFFF  // no offset in the pool
#define FULL 1000      // size, opacity and speed are in 1/1000
#define MILLI 1000     // x and y in 1/1000 px, turns in 1/1000 degree
#define TURN (360 * MILLI)
_Static_assert(ANIM_POOL < NO_OFF, "16-bit offsets and lengths in the pool, and NO_OFF");

typedef struct {
  bool ready, leaving;     // leaving: goes with its last place
  uint8_t scale, transp;   // scale 1 or 2. transp = transparent palette index, ANIM_OPAQUE for none
  uint16_t w, h;           // size in animation pixels
  uint16_t off;            // its frame in pool, the records right after. NO_OFF = empty slot
  uint16_t rate;           // frame rate in percent, 0 = paused
  uint16_t len;            // record bytes
  uint16_t loop_at, next;  // loop_at = the second record, where we loop back to. next = record to show next
  uint16_t last;           // the record that takes the last frame back to the first, NO_OFF for a still picture
  uint16_t frames;         // records in the loop
  uint32_t due;            // tick for the next record
  lv_color_t pal[16];
} slot_t;

// A value on its way from one to another
typedef struct {
  int32_t from, to;
  uint16_t ms;          // over ms, 0 = still at to
  uint16_t steps : 14;  // MOVE_MS steps of it gone
  uint16_t ease : 2;    // ANIM_EASE_*
} ramp_t;

// A place's channel going between lo and hi on its own, see K_ANIM_WAVE
typedef struct {
  uint8_t place, ch;  // place ANIM_PLACES = free
  uint8_t jitter, flags;
  bool up;                  // the last leg went up
  int16_t lo, hi;           // in the channel's units
  uint16_t ms[2], hold[2];  // the whole way up and down, the holds at hi and lo
  uint16_t left;            // ms left of the hold
  ramp_t r;                 // what it adds or its share now, see unit()
} wave_t;

enum { STAY, LEAVE, LEAVE_GONE };  // LEAVE_GONE answers ANIM_GONE once it's gone

typedef struct {
  int16_t deg, size, speed, opa;  // size, speed and opacity in 1/1000
  uint8_t flip;
} look_t;

typedef struct {
  uint8_t slot;      // ANIM_SLOTS = empty place
  uint8_t flip;      // ANIM_FLIP_X, ANIM_FLIP_Y
  uint8_t drifting;  // bit per axis, a drift put it there. On ANIM_EDGE_SCREEN what's over an edge shows at the other
  uint8_t tiles;     // bit k: tile k of drawn is on the layer, see tile(). 0 = not drawn
  uint8_t leaving;   // STAY, or fading out to go
  bool held;         // its own frame stays at the last one
  uint8_t tag;       // ANIM_ROUNDS's, for ANIM_GONE
  uint16_t frame;    // a frame of its own in pool, NO_OFF = the slot's
  uint16_t next;     // the next record of its own frame
  uint32_t due;      // tick for that record. A still picture's ms left of its round instead
  uint16_t rounds, rounds_fade;               // rounds to the end of, 0 = on and on
  ramp_t ch[ANIM_CHANNELS];                   // what glides and drifts set, see unit()
  int32_t rate[ANIM_TURN + 1];                // the drifts of x, y and turn, per second
  int16_t map[ANIM_CHANNELS - ANIM_TURN][4];  // turn, size, opacity, speed at the area's left, right, top, bottom
  ramp_t fade;                                // 1/1000, fades it in when placed and out when it leaves
  look_t now;                                 // what it shows, all its channels together
  int16_t x, y;                               // where it is, can be partly off the screen
  lv_area_t drawn;                            // its box on the layer, as drawn
  struct {
    int16_t x0, y0, x1, y1;
    uint8_t edge[2];  // ANIM_EDGE_* for x and y
  } area;
  struct {
    bool on, resting;
    uint16_t speed[2], leg[2], rest[2];  // min, max
    int16_t heading, spread;
    uint16_t left;  // ms of this leg or rest
  } wander;
  struct {
    bool on;
    uint8_t flags;  // ANIM_FACE_*
    int16_t facing, tilt, rate;
  } face;
  uint32_t rng;
} place_t;

extern slot_t slots[ANIM_SLOTS];
extern place_t places[ANIM_PLACES];
extern wave_t waves[ANIM_WAVES];
extern lv_obj_t *layer;  // draws every place on the screensaver, while it's shown
// what size, opacity and speed can be
extern const int32_t SHARE_LO[ANIM_CHANNELS], SHARE_HI[ANIM_CHANNELS];

static inline bool looping(const slot_t *s) { return s->loop_at < s->len; }
static inline int32_t wrap(int32_t v, int32_t m) { return (v % m + m) % m; }  // to 0..m
// 1/1000 degree to -180..180
static inline int32_t half(int32_t a) {
  a = wrap(a, TURN);
  return a > TURN / 2 ? a - TURN : a;
}
static inline bool scales(int c) { return c >= ANIM_SIZE; }
// Ours from the messages' units: 1/1000 px and degree, and 1/1000 of full for percent
static inline int32_t unit(int c) { return scales(c) ? FULL / 100 : MILLI; }
static inline int32_t no_change(int c) { return scales(c) ? FULL : 0; }
static inline bool wraps(const place_t *p, int axis) {
  return (p->drifting >> axis & 1) && p->area.edge[axis] == ANIM_EDGE_SCREEN;
}

// anim.c
const uint8_t *place_frame(const place_t *p);  // the frame it shows
void gone(place_t *p);                         // it faded out, and goes
void still_rounds(place_t *p, uint32_t ms);    // ms more of a still picture's rounds
void wake(void);                               // something new to play

// anim_motion.c
int32_t ramp_at(const ramp_t *r);
void ramp_to(ramp_t *r, int32_t to, uint16_t ms, unsigned ease);
void ramp_set(ramp_t *r, int32_t v);
wave_t *wave_of(const place_t *p, int c);  // NULL if channel c doesn't wave
bool stopping(const wave_t *w);
void wave_settle(wave_t *w);
int32_t mapped(const place_t *p, int c);
void update(place_t *p);  // what it shows, redrawn if that changed
void new_leg(place_t *p);
bool moving(const place_t *p);
void wake_mover(void);
void motion_init(void);

// anim_draw.c
lv_area_t box(const place_t *p);
void refresh(place_t *p, const look_t *was, const lv_area_t *b);
void hide(place_t *p);
void order(void);
void changed(const place_t *p, const lv_area_t *a);
void draw_init(void);
