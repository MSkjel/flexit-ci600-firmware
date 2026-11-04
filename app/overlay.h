// Screensaver overlays from the ESP (K_OVERLAY_BEGIN), drawn over the picture. RAM only. Each record is the
// deflated rectangle that changed since the last frame, so we only redraw that. A slot holds an overlay, a place
// shows one somewhere on the screen. The places of a slot show the same frame, unless one has a frame of its own.
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void overlay_init(void);
// Transfer header from picture.c. -1 if we take it and *len = record bytes to come, else a PIC_* error.
int overlay_open(const uint8_t *b, int n, uint32_t *len);
void overlay_bytes(uint32_t at, const uint8_t *b, unsigned n);  // record bytes at offset at
bool overlay_close(void);                                       // all bytes are in. false if the records are bad
void overlay_abort(void);                                       // the transfer failed, drop the slot
bool overlay_clear(const uint8_t *b, int n);                    // K_OVERLAY_CLEAR. true if it dropped the one coming in
void overlay_clear_all(void);
void overlay_msg(uint16_t key, const uint8_t *b, int n);  // K_OVERLAY_FRAMES to K_OVERLAY_DEPTH
void overlay_part(const uint8_t *b, int n);               // K_PIC_PART
void overlay_parts_clear(void);                           // the picture went, and its parts with it
int32_t overlay_gone(void);                               // the next K_OVERLAY_GONE to send, -1 for none
unsigned overlay_used(void);                              // bytes of OVERLAY_POOL in use
// The object that draws the places on parent, at its top left. They play while it exists.
lv_obj_t *overlay_show(lv_obj_t *parent);

// A place as it is now, for the tests
typedef struct {
  int16_t x, y;            // where it is
  int16_t deg, size, opa;  // its turn, and size and opacity in 1/1000
  uint8_t flip;            // OVERLAY_FLIP_*
  uint8_t tiles;           // bit 0 drawn, then over the right edge, the bottom one, and both
  uint8_t order;           // drawn after order others
  uint8_t depth;           // K_OVERLAY_DEPTH's
  lv_area_t box;           // what it's drawn in, from the top left of overlay_show()'s object
  const uint8_t *frame;    // the frame it shows, 2 pixels a byte
  const lv_color_t *pal;   // its colours
} overlay_look_t;
bool overlay_look(int place, overlay_look_t *l);  // false if there's no such place
bool overlay_covered(int place, int x, int y);    // a part of the picture is in front of place at x, y, as box is
