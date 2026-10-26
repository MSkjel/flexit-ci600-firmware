// CI600 replacement UI (LVGL 8.3, 320x240, five keys), what the platform calls. Internals in ui_int.h.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lang.h"
// icons.h declares the fonts too. Without lvgl.h they're the bare struct.
#ifdef LV_FONT_DECLARE
#include "icons.h"
#else
struct _lv_font_t;
#define LV_FONT_DECLARE(name) extern const struct _lv_font_t name
#include "icons.h"
#undef LV_FONT_DECLARE
#endif

#define SCR_W 320
#define SCR_H 240

// Key codes. HOME is BACK held, OK_LONG is OK held for 2 s.
enum { KEY_UP = 1, KEY_DOWN, KEY_OK, KEY_BACK, KEY_HELP, KEY_HOME, KEY_OK_LONG };

void ui_init(void);
void ui_key(int key);
void ui_idle(void);     // no keys for a while, back to the start view and the screensaver if they're on
void ui_wake(void);     // leaving idle, closes the screensaver
void ui_refresh(void);  // model changed
void ui_second(void);   // 1 Hz: night theme, new alarms and messages, redraw
// The OK light as the top screen wants it, -1 for its usual meaning
int ui_ok_led(uint32_t ms);
// A whole new picture is coming in over the one shown
void ui_picture_coming(void);
bool ui_held(void);           // the flush sends nothing to the LCD
void ui_show_view(int view);  // back to the home screen on this view
void ui_toast(const char *icon, str_t msg);
void ui_toast_text(const char *icon, const char *text);  // the same with a text made at run time
// V_CPU_SPEED just went over 72 MHz or V_FAST_DISPLAY on, and may garble the screen. OK keeps it, BACK or 20 s
// without an answer set it back to 0, 72 MHz or off, after telling plat_trial_end().
void ui_trial(int v);
// Toasts locked or unlocked after a toggle, else how to unlock
void ui_key_lock_notice(bool toggled);
int ui_backlight_preview(void);  // brightness an open editor is previewing, -1 if none
