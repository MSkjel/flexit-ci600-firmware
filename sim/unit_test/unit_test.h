// What the unit tests share. Each file tests a part of the firmware and has a table of its tests, main.c runs them.
#pragma once
#include "board.h"
#include "bus.h"
#include "clock.h"
#include "layout.h"
#include "link.h"
#include "mbserver.h"
#include "pdeflate.h"
#include "pjpeg.h"
#include "plink.h"
#include "settings.h"
#include "trend.h"
#include "check.h"
#include "sim.h"
#include "ui_int.h"
#include "unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const check_test_t PICTURE_TESTS[], OVERLAY_TESTS[], OVERLAY_MOTION_TESTS[], OVERLAY_SCRIPT_TESTS[],
    VALUES_TESTS[], CLOCK_TESTS[], UNIT_TESTS[], SETTINGS_TESTS[], UI_TESTS[], LINK_TESTS[];

// board_stub.c
extern uint32_t stub_ms, stub_rtc;
extern uint8_t stub_flash[];
extern int stub_flash_writes_left, stub_flash_erases, stub_resets;
extern uint32_t stub_flash_bad;
extern bool stub_bus_quiet;

// main.c: what input.c, link.c, the week program and the values call in the UI, see --wrap in the Makefile
extern int keys[64], n_keys, idles, wakes, toasts, lock_notices[2];  // lock_notices[toggled]
extern int layout_changes, saver_builds, value_changes, views, shown_view;
void forget_ui_calls(void);  // all of them back to 0

// picture.c: a W x H picture of 16 colours, px, and the ESP's messages that send it
#define W 10  // pixels, so 5 bytes per row
#define H 6
extern uint8_t id;                                                // id of the last transfer
extern int32_t (*to_pic)(uint16_t key, const uint8_t *b, int n);  // pic_msg, or through the link, see link.c
uint32_t rgb(int i);                                              // palette colour i
const uint32_t *palette(void);                                    // rgb()'s 16 as they are now
uint8_t status(int32_t ack);                                      // of a K_PIC_ACK, 0xFF for none
int32_t patch(int y, int count, uint32_t len, bool pal);
int32_t end(uint32_t crc);
int32_t data(const uint8_t *b, unsigned n);
int32_t rest(int32_t a, const uint8_t *b, unsigned len, unsigned piece, uint32_t crc);
int32_t rest_zip(int32_t a, const uint8_t *b, unsigned n, const uint8_t *hist, unsigned nh, unsigned piece);
unsigned pack(int y, int count, uint8_t *out);
void fill(int y, int count, uint8_t seed);
int32_t send_picture(unsigned piece);
bool shows(void);

// overlay.c: AW x AH frames, sent as the ESP sends them, and the places that show them
#define AW 8
#define AH 4
extern uint8_t an[4][AH][AW / 2];  // the frames, colour 15 is transparent
extern lv_obj_t *overlays;         // overlay_show()'s, at the top left of the screen
void no_overlays(void);            // none, and nothing on the screen
void dot_frames(bool moves);       // a dot that moves, and stays put for a frame unless moves
uint32_t overlay_records(uint8_t *d, int delay);
int32_t overlay_send(int slot, int w, uint32_t len, const uint8_t *data, uint32_t crc);
// A value in a script, see K_OVERLAY_SCRIPT
#define V16(v) OVERLAY_V16, (uint8_t) (v), (uint8_t) ((uint16_t) (v) >> 8)
#define REG(k) (OVERLAY_VREG + (k))
#define AT(at) (uint8_t) (at), (uint8_t) ((at) >> 8)  // where a step goes on, or a SCRIPT's at or len
#define WHOLE AT(0), AT(0)                            // a CLIP's size: all of its frame
void script_to(int p, const uint8_t *s, int n);       // place p's script, in as many messages as the ESP sends it in
void rounds_end(int p, int rounds, int fade, int tag);
void place_in(int p, int slot, int x, int y, int own, int start, int fade);
void place(int p, int slot, int x, int y, int own, int start);
void overlay_clear_msg(unsigned v);
void frames_msg(int slot, int rate);
lv_obj_t *show_overlays(void);
bool placed(void);
bool overlay_pos(int k, int16_t *x, int16_t *y);
int look_opa(int place);
bool drawn(int k, overlay_look_t *l, int *px);
bool overlay_shows(int k, int f);

// unit.c: the CX60's frames
void relay(uint16_t reg, uint16_t v);
void block(uint16_t reg, const uint16_t *v, int n);
void cx60(uint16_t reg, uint16_t v);

// ui.c
extern uint32_t now_ms;  // input.c's clock
