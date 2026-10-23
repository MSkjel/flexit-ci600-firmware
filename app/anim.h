// Screensaver animations from the ESP (K_ANIM_BEGIN), played over the picture. RAM only. Each record is the
// deflated rectangle that changed since the last frame, so we only redraw that. A slot holds an animation, a place
// shows one somewhere on the screen. The places of a slot show the same frame, unless one has a frame of its own.
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void anim_init(void);  // registers the LVGL decoder
// Transfer header from picture.c. -1 if we take it and *len = record bytes to come, else a PIC_* error.
int anim_open(const uint8_t *b, int n, uint32_t *len);
void anim_bytes(uint32_t at, const uint8_t *b, unsigned n);  // record bytes at offset at
bool anim_close(void);                                       // all bytes are in. false if the records are bad
void anim_abort(void);                                       // the transfer failed, drop the slot
void anim_clear(const uint8_t *b, int n);                    // K_ANIM_CLEAR
void anim_clear_all(void);
bool anim_ready(void);                             // anything placed
void anim_place(const uint8_t *b, int n);          // K_ANIM_PLACE
void anim_move(const uint8_t *b, int n);           // K_ANIM_MOVE
bool anim_pos(int place, int16_t *x, int16_t *y);  // where a place is now, false if there's none
unsigned anim_used(void);                          // bytes of ANIM_POOL in use
lv_obj_t *anim_show(lv_obj_t *parent);  // creates the images, which play while they exist. The first, or NULL
