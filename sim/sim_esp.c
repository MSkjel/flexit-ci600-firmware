// Fake ESP at address 1 running the real pbulk code, talking to app/link.c over a fake CX60 bus.
// One transaction per cycle, in the quiet after the status broadcast.
#include "ui.h"
#include "sim.h"
#include "link.h"
#include "plink.h"
#include "pbulk.h"
#include "pdeflate.h"
#include "pjpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_picture.h"

static pbulk_esp_t FL;  // the ESP's end of the link
unsigned long sim_cycles;
static uint32_t now_us;                           // bus time
static uint8_t STATUS[PBUS_STATUS_LEN], POLL[8];  // the CX60's status broadcast and its poll of the panel

// Waits for room in the queue like a real sender, running the link meanwhile.
static void send(uint16_t key, const void *b, int n) {
  while (!pbulk_esp_send(&FL, key, b, n, plink_kind(key) == PLINK_VALUE))
    sim_link_run(1);
}

static void send_number(uint16_t key, uint32_t v) {
  uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
  send(key, b, 4);
}

static void send_text(uint16_t key, const char *s) { send(key, s, (int) strlen(s)); }

static unsigned long got_from_panel;
static uint16_t last_key;
static uint32_t last_val;
static int last_ack_status = -1;

static void esp_got(void *u, uint16_t key, const uint8_t *b, int n) {
  (void) u;
  uint32_t v = plink_number(b, n);
  got_from_panel++;
  last_key = key;
  last_val = v;
  if (key == K_HELLO && (v & HELLO_ASK))
    send_number(K_HELLO, (uint32_t) PLINK_VERSION << 16);
  if (key == K_ITEM_HELP && v < (uint32_t) SIM_N_ITEMS) {  // reply with the item's help text, which may be empty
    uint8_t h[1 + PLINK_HELP_MAX] = {(uint8_t) v};
    sim_text((char *) h + 1, PLINK_HELP_MAX + 1, SIM_ITEMS[v].help);
    send(K_ITEM_HELP, h, 1 + (int) strlen((char *) h + 1));
  }
  if (key == K_PIC_ACK)
    last_ack_status = (int) (v >> 24);
}

static void fin(uint8_t *f, int n) {
  uint16_t c = pbus_crc16(f, n - 2);
  f[n - 2] = c & 0xFF;
  f[n - 1] = c >> 8;
}

// One 110 ms CX60 cycle. Status broadcast, the ESP's frame at most 1.5 ms after it and the panel's answer,
// then the CX60 polls the panel 14 ms after the broadcast.
static void cx60_cycle(void) {
  uint32_t t0 = now_us, end = t0 + PBUS_STATUS_LEN * PBUS_BYTE_US;
  link_bus(STATUS, PBUS_STATUS_LEN, t0, end + PBUS_BYTE_US, (end + PBUS_BYTE_US) / 1000);
  uint8_t f[PBULK_FRAME_MAX], a[PBULK_ANSWER_FRAME_MAX];
  int n = pbulk_esp_window(&FL, pbus_status_left(1000, FL.end_us), f);
  if (n) {
    uint32_t idle = end + 1500 + (uint32_t) (n + 1) * PBUS_BYTE_US;
    int an = link_bulk_frame(f, n, idle, idle / 1000, a);
    if (an)
      pbulk_esp_answer(&FL, a, an, esp_got, NULL);
  }
  link_bus(POLL, 8, end + 14000, end + 14000 + 9 * PBUS_BYTE_US, (end + 14000) / 1000);
  now_us += 110000;
  sim_cycles++;
}

// ---------------------------------------------------------------- sending pictures, as the ESP does
static uint8_t comp[PIC_POOL + 1024], wire[PIC_POOL + 2048];  // the bytes, and how they go in DATA
static unsigned comp_len, comp_pos, wire_len;
static bool pic_sending, place_after;  // place_after: an animation, place it at place_x, place_y after END
static int place_x, place_y;
static uint8_t pic_id;

static unsigned pack_rows(const uint8_t rows[][TP_W / 2], int y, int count) {
  unsigned n = 0;
  for (int r = y; r < y + count; r++)
    n += plink_pack_row(rows[r], TP_W / 2, comp + n);
  return n;
}

// DATA for comp: 16 colours as pzip pieces, deflated only against themselves here, the rest as they are
static void send_data(bool pieces) {
  wire_len = 0;
  if (pieces) {
    static uint16_t table[PZIP_TABLE];
    pzip_t z = {.data = comp, .n = comp_len, .table = table};
    for (unsigned m; (m = pzip(&z, wire + wire_len));)
      wire_len += m;
  } else {
    memcpy(wire, comp, comp_len);
    wire_len = comp_len;
  }
  comp_pos = 0;
  pic_sending = true;
}

static void put16(uint8_t *b, unsigned v) {
  b[0] = (uint8_t) v;
  b[1] = (uint8_t) (v >> 8);
}

static void put32(uint8_t *b, uint32_t v) {
  put16(b, v & 0xFFFF);
  put16(b + 2, v >> 16);
}

static void put_palette(uint8_t *b, const uint32_t *pal) {
  for (int i = 0; i < 16; i++, b += 3) {
    b[0] = (uint8_t) (pal[i] >> 16);
    b[1] = (uint8_t) (pal[i] >> 8);
    b[2] = (uint8_t) pal[i];
  }
}

unsigned sim_esp_send_picture(void) {
  comp_len = pack_rows(tp_base, 0, TP_H);
  // BEGIN: id, format, w, h, len, palette
  uint8_t h[58] = {++pic_id, PIC_PALETTE};
  put16(h + 2, TP_W);
  put16(h + 4, TP_H);
  put32(h + 6, comp_len);
  put_palette(h + 10, tp_palette);
  send(K_PIC_BEGIN, h, sizeof h);
  send_data(true);
  return comp_len;
}

static int tri(int x, int period, int amp) { return amp * (abs(x % period * 4 - 2 * period) - period) / period; }

// A photo-like picture for the JPEG path, a sunset over hills and a lake with some grain. Integer maths only,
// so it's the same on every machine.
static uint32_t photo_px(void *ctx, int x, int y) {
  (void) ctx;
  uint32_t n = (uint32_t) x * 73856093u ^ (uint32_t) y * 19349663u;
  n ^= n >> 13;
  n *= 0x5bd1e995u;
  n ^= n >> 15;
  const int horizon = 150, ridge = 122 + tri(x, 97, 16) + tri(x + 40, 53, 8);
  int sy = y < horizon ? y : 2 * horizon - y + (int) (n >> 8 & 3), r, g, b;  // the lake mirrors, with ripples
  if (sy < ridge) {  // sky: blue at the top, orange at the horizon, bright round the sun
    r = 40 + 200 * sy / ridge;
    g = 50 + 120 * sy / ridge;
    b = 150 - 70 * sy / ridge;
    int dx = x - 220, dy = sy - 104, d2 = dx * dx + 2 * dy * dy, glow = d2 < 9000 ? (9000 - d2) / 50 : 0;
    r += glow;
    g += glow * 3 / 4;
    b += glow / 4;
  } else {  // hills: dark, lighter where the light catches them
    int lit = (sy - ridge) * 2 + tri(x, 31, 6);
    r = 35 + lit / 2;
    g = 55 + lit;
    b = 45 + lit / 3;
  }
  if (y >= horizon) {  // water: darker and bluer
    r = r * 3 / 4;
    g = g * 3 / 4;
    b = b * 4 / 5 + 12;
  }
  int grain = (int) (n & 7) - 4;
  r = LV_CLAMP(0, r + grain, 255);
  g = LV_CLAMP(0, g + grain, 255);
  b = LV_CLAMP(0, b + grain, 255);
  return (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) b;
}

// The photo as a JPEG at quality q, into comp. Returns the length, 0 if it doesn't fit in PIC_POOL.
static unsigned photo_jpeg(int q) {
  pjpeg_t j;
  pjpeg_begin(&j, 320, 240, q, comp, PIC_POOL);
  while (pjpeg_mcu(&j, photo_px, NULL)) {
  }
  return pjpeg_end(&j);
}

unsigned sim_esp_send_photo(int *quality) {
  int lo = 1, hi = 95;  // the best quality that fits, as the ESP finds it
  while (lo < hi) {
    int q = (lo + hi + 1) / 2;
    if (photo_jpeg(q))
      lo = q;
    else
      hi = q - 1;
  }
  *quality = lo;
  comp_len = photo_jpeg(lo);
  uint8_t h[10] = {++pic_id, PIC_JPEG};  // BEGIN: id, format, w, h, len
  put16(h + 2, 320);
  put16(h + 4, 240);
  put32(h + 6, comp_len);
  send(K_PIC_BEGIN, h, sizeof h);
  send_data(false);
  return comp_len;
}

// A PATCH with only the rows where tp_moon differs from tp_base.
unsigned sim_esp_send_moon(int *y_out, int *count_out) {
  int y0 = -1, y1 = -1;
  for (int y = 0; y < TP_H; y++)
    if (memcmp(tp_base[y], tp_moon[y], TP_W / 2)) {
      if (y0 < 0)
        y0 = y;
      y1 = y;
    }
  comp_len = pack_rows(tp_moon, y0, y1 - y0 + 1);
  uint8_t h[9] = {++pic_id};  // id, y, count, len, no palette
  put16(h + 1, (unsigned) y0);
  put16(h + 3, (unsigned) (y1 - y0 + 1));
  put32(h + 5, comp_len);
  send(K_PIC_PATCH, h, sizeof h);
  send_data(true);
  *y_out = y0;
  *count_out = y1 - y0 + 1;
  return comp_len;
}

// A PATCH with no rows, just a warmer palette for a dawn sky.
void sim_esp_send_dawn(void) {
  uint32_t pal[16];
  for (int i = 0; i < 16; i++) {
    uint32_t c = tp_palette[i];
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (b > r && b > 40 && g < 200) {
      r = r + (230 - r) * 2 / 3;
      g = g + (120 - g) / 2;
      b = b * 2 / 3;
    }
    pal[i] = (uint32_t) (r << 16 | g << 8 | b);
  }
  uint8_t h[57] = {++pic_id};
  put_palette(h + 9, pal);
  send(K_PIC_PATCH, h, sizeof h);
  send_number(K_PIC_END, plink_crc32(0, comp, 0));
}

// A ball bouncing on a transparent background, so the picture shows through. 48x48, 8 frames of 100 ms.
#define AN_W 48
#define AN_H 48
#define AN_FRAMES 8
unsigned sim_esp_send_animation(int x, int y) {
  static const uint32_t PAL[16] = {0xF08020, 0xA04010, 0xFFF0C0};
  static const uint8_t HEIGHT[AN_FRAMES] = {0, 3, 10, 20, 28, 20, 10, 3};  // ball height above the ground
  static uint8_t frames[AN_FRAMES][AN_H][AN_W / 2];
  memset(frames, 0xFF, sizeof frames);  // colour 15 is transparent
  for (int f = 0; f < AN_FRAMES; f++)
    for (int py = 0; py < AN_H; py++)
      for (int px = 0; px < AN_W; px++) {
        int dx = px - AN_W / 2, dy = py - (AN_H - 11 - HEIGHT[f]), d = dx * dx + dy * dy;
        if (d > 100)
          continue;
        int c = d > 70 ? 1 : dx < -2 && dy < -2 && d < 30 ? 2 : 0;
        uint8_t *b = &frames[f][py][px / 2];
        *b = px & 1 ? (uint8_t) ((*b & 0xF0) | c) : (uint8_t) ((*b & 0x0F) | c << 4);
      }
  comp_len = 0;
  // frame 0 whole, then changes from the frame before, and a last record back to frame 0
  for (int f = 0; f <= AN_FRAMES; f++)
    comp_len += plink_anim_record(f ? &frames[f - 1][0][0] : NULL, &frames[f % AN_FRAMES][0][0], AN_W, AN_H, 10,
                                  comp + comp_len);
  // ANIM_BEGIN: id, slot, w, h, scale, transparent colour, len, palette. Then place 0 at x, y once it's in.
  uint8_t h[60] = {++pic_id, 0};
  put16(h + 2, AN_W);
  put16(h + 4, AN_H);
  h[6] = 1;
  h[7] = 15;
  put32(h + 8, comp_len);
  put_palette(h + 12, PAL);
  send(K_ANIM_BEGIN, h, sizeof h);
  send_data(false);
  place_x = x;
  place_y = y;
  place_after = true;
  return comp_len;
}

// ANIM_PLACE: place, slot, x, y, own, start, fade
void sim_esp_place_animation(int place, int slot, int x, int y, int own, int start) {
  uint8_t m[10] = {(uint8_t) place, (uint8_t) slot};
  put16(m + 2, (unsigned) x);
  put16(m + 4, (unsigned) y);
  m[6] = (uint8_t) own;
  m[7] = (uint8_t) start;
  send(K_ANIM_PLACE, m, sizeof m);
}

void sim_esp_move_animation(int mode, int a, int b, int ms) {
  uint8_t m[8] = {0, (uint8_t) mode};
  put16(m + 2, (unsigned) a);
  put16(m + 4, (unsigned) b);
  put16(m + 6, (unsigned) ms);
  send(K_ANIM_MOVE, m, mode == ANIM_GLIDE ? 8 : mode == ANIM_DRIFT ? 6 : 2);
}

static void esp_pump(void) {
  while (pic_sending && pbulk_esp_room(&FL) > 128) {
    if (comp_pos >= wire_len) {
      send_number(K_PIC_END, plink_crc32(0, comp, comp_len));
      pic_sending = false;
      if (place_after)
        sim_esp_place_animation(0, 0, place_x, place_y, 0, 0);
      place_after = false;
      break;
    }
    uint8_t b[64] = {0};  // DATA is padded to a multiple of 4 bytes
    unsigned n = wire_len - comp_pos < sizeof b ? wire_len - comp_pos : sizeof b;
    memcpy(b, wire + comp_pos, n);
    send(K_PIC_DATA, b, (int) (n + 3) & ~3);
    comp_pos += n;
  }
}

// ---------------------------------------------------------------- the ESP's messages
// Send all of item i, with the label and unit in the panel's language.
static void item(int i) {
  item_t it;
  sim_item(i, &it);
  uint8_t d[ITEM_DEF_LEN] = {it.type, it.flags, it.icon, it.dec};
  put16(d + 4, it.step);
  put16(d + 6, it.id);
  put32(d + 8, (uint32_t) it.min);
  put32(d + 12, (uint32_t) it.max);
  for (int k = 0; k < 4; k++)
    put16(d + 16 + 2 * k, (uint16_t) it.level[k]);
  send(K_ITEM_DEF + i, d, sizeof d);
  send_text(K_ITEM_LABEL + i, it.label);
  send_text(K_ITEM_UNIT + i, it.unit);
  if (it.type == ITEM_TEXT)
    send_text(K_ITEM_TEXT + i, it.text);
  else
    send_number(K_ITEM_VALUE + i, (uint32_t) it.value);
}

void sim_link_run(int cycles) {
  for (int i = 0; i < cycles; i++) {
    link_poll();
    esp_pump();
    cx60_cycle();
  }
}

// Settled once both ends have been idle for 3 cycles in a row.
int sim_link_settle(void) {
  unsigned long start = sim_cycles;
  int quiet = 0;
  while (quiet < 3 && sim_cycles - start < 200000) {
    sim_link_run(1);
    bool idle = !pbulk_esp_pending(&FL) && !link_queue()->qn && !link_bulk_state()->out_full && !pic_sending;
    quiet = idle ? quiet + 1 : 0;
  }
  return quiet < 3 ? -1 : (int) (sim_cycles - start);  // -1 = still talking after 200000 cycles
}

void sim_esp_init(void) {
  memcpy(STATUS, (const uint8_t[]) {0x00, 0x10, 0x00, 0xBE, 0x00, 0x55, 0xAA}, 7);
  for (int i = 7; i < PBUS_STATUS_LEN - 2; i++)
    STATUS[i] = (uint8_t) i;
  fin(STATUS, PBUS_STATUS_LEN);
  memcpy(POLL, (const uint8_t[]) {3, 0x01, 0x00, 0x00, 0x00, 0x40}, 6);
  fin(POLL, 8);
  pbulk_esp_init(&FL, 0x2B, 3);
  link_init();
  send_number(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK);
}

void sim_esp_items(void) {
  for (int i = 0; i < SIM_N_ITEMS; i++)
    item(i);
}

// Fill the remaining item slots on the smart home page, the most the panel ever has to show.
void sim_esp_fill(void) {
  static const uint8_t TYPE[] = {ITEM_SENSOR, ITEM_TOGGLE, ITEM_NUMBER}, ICON[] = {ICN_THERMO, ICN_BULB, ICN_PLUG};
  for (int i = SIM_N_ITEMS; i < N_ITEMS; i++) {
    int k = i - SIM_N_ITEMS, t = TYPE[k % 3];
    char name[24], label[ITEM_LABEL];
    snprintf(name, sizeof name, "extra_%d", k);
    uint8_t d[ITEM_DEF_LEN] = {(uint8_t) t, ITEMF_VIEW | ITEMF_MENU, ICON[k % 3], t == ITEM_TOGGLE ? 0 : 1};
    put16(d + 4, 5);
    put16(d + 6, (uint16_t) sim_item_id(name));
    put32(d + 12, t == ITEM_TOGGLE ? 1 : 1000);
    send(K_ITEM_DEF + i, d, sizeof d);
    snprintf(label, sizeof label, "Basement sensor %2u", (unsigned) (k + 1) % 100u);  // about as long as labels get
    send_text(K_ITEM_LABEL + i, label);
    send_text(K_ITEM_UNIT + i, t == ITEM_TOGGLE ? "" : "kWh");
    send_number(K_ITEM_VALUE + i, t == ITEM_TOGGLE ? (uint32_t) k % 2 : 123u + (uint32_t) k);
  }
}

void sim_esp_notify(const char *text, int icon, int level) {
  uint8_t b[2 + PLINK_TEXT_MAX] = {(uint8_t) icon, (uint8_t) level};
  int n = (int) strlen(text);
  memcpy(b + 2, text, n);
  send(K_NOTIFY, b, 2 + n);
}

void sim_esp_set(uint16_t key, uint32_t value) { send_number(key, value); }

void sim_esp_report(const char *what) {
  printf("  %-30s %6lu cycles so far, panel sent %lu / got %lu, ESP got %lu (last key 0x%03X = %lu), pic ack %d\n",
         what, sim_cycles, (unsigned long) link_queue()->sent, (unsigned long) link_bulk_state()->taken, got_from_panel,
         last_key, (unsigned long) last_val, last_ack_status);
}
