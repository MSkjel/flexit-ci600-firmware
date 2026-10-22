// Screensaver picture from the ESP, kept compressed in RAM and unpacked as LVGL draws it. 16 colours with each row
// packed on its own, or JPEG decoded 8 lines at a time. Lost on restart, the ESP resends it after our HELLO.
#pragma once
#include "lvgl.h"
#include "plink_keys.h"  // PIC_POOL, PIC_PALETTE, PIC_JPEG

#define PIC_MAX_W 320
#define PIC_MAX_H 240

typedef enum { PIC_NONE, PIC_RECEIVING, PIC_READY } pic_state_t;

// K_PIC_* and K_ANIM_* messages. Returns the K_PIC_ACK value to send back, -1 for none.
int32_t pic_msg(uint16_t key, const uint8_t *b, int n);
void pic_reset(void);  // ESP restarted, drop the transfer in progress. It sends it again
// The picture as an object, centered in parent. NULL if there's none.
lv_obj_t *pic_show(lv_obj_t *parent);
// Draws area a, in picture coordinates, into buf with stride pixels per line. False if no picture or a is outside.
bool pic_read(const lv_area_t *a, lv_color_t *buf, int stride);
pic_state_t pic_state(void);
uint8_t pic_format(void);   // PIC_PALETTE or PIC_JPEG
uint8_t pic_percent(void);  // of the current transfer
uint16_t pic_width(void);
uint16_t pic_height(void);
