// Screensaver animations from the ESP (K_ANIM_BEGIN), played over the picture. RAM only. Each record is the
// deflated rectangle that changed since the last frame, so we only redraw that. A slot holds an animation, a place
// shows one somewhere on the screen. The places of a slot show the same frame, unless one has a frame of its own.
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void anim_init(void);
// Transfer header from picture.c. -1 if we take it and *len = record bytes to come, else a PIC_* error.
int anim_open(const uint8_t *b, int n, uint32_t *len);
void anim_bytes(uint32_t at, const uint8_t *b, unsigned n);  // record bytes at offset at
bool anim_close(void);                                       // all bytes are in. false if the records are bad
void anim_abort(void);                                       // the transfer failed, drop the slot
bool anim_clear(const uint8_t *b, int n);                    // K_ANIM_CLEAR. true if it dropped the one coming in
void anim_clear_all(void);
void anim_msg(uint16_t key, const uint8_t *b, int n);  // K_ANIM_FRAMES to K_ANIM_ROUNDS
int32_t anim_gone(void);                               // the next K_ANIM_GONE to send, -1 for none
unsigned anim_used(void);                              // bytes of ANIM_POOL in use
// The object that draws the places on parent, at its top left. They play while it exists.
lv_obj_t *anim_show(lv_obj_t *parent);

// A place as it is now, for the tests
typedef struct {
  int16_t x, y;            // where it is
  int16_t deg, size, opa;  // its turn, and size and opacity in 1/1000
  uint8_t flip;            // ANIM_FLIP_*
  uint8_t tiles;           // bit 0 drawn, then over the right edge, the bottom one, and both
  uint8_t depth;           // drawn after depth others
  lv_area_t box;           // what it's drawn in, from the top left of anim_show()'s object
  const uint8_t *frame;    // the frame it shows, 2 pixels a byte
  const lv_color_t *pal;   // its colours
} anim_look_t;
bool anim_look(int place, anim_look_t *l);  // false if there's no such place
