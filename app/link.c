// ESP link, see link.h
#include "link.h"
#include "ui.h"
#include "clock.h"
#include "input.h"
#include "plat.h"
#include <string.h>

static plink_t L;               // outgoing messages waiting for room in an answer
static pbulk_panel_t B;         // frame state, filled in the RX interrupt and handled in link_poll()
static pbus_panel_t W;          // bus timing, as the RX interrupt sees it
static volatile bool ready;     // link_init() has run. Until then frames go unanswered
static uint16_t quiet_s;        // seconds since the last frame from the ESP
static int sync_next;           // next value id to send after a HELLO, 0 = not syncing
static int32_t pic_ack = -1;    // a K_PIC_ACK waiting for room in the queue, -1 = none
static int8_t help_asked = -1;  // the item we last asked the ESP about
static uint8_t help_next;       // slot the next help goes in

// The last few item help texts from the ESP, so moving up and down a menu doesn't ask again
#define HELP_SLOTS 8
static struct {
  int8_t of;  // item, -1 = free
  char text[PLINK_HELP_MAX + 1];
} helps[HELP_SLOTS];

bool link_compatible(void) { return M.esp_version == PLINK_VERSION; }

// Sent when we start or the ESP is enabled in the menu. The ESP answers with its state.
static void hello(void) { plink_send(&L, K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK); }

void link_send(uint16_t key, uint32_t value) {
  if (HAS(FEAT_ESP) && link_compatible())
    plink_send(&L, key, value);
}

// Item i's help, or all of them for -1
static void forget_helps(int i) {
  for (int k = 0; k < HELP_SLOTS; k++)
    if (i < 0 || helps[k].of == i)
      helps[k].of = -1;
  if (i < 0 || help_asked == i)
    help_asked = -1;
}

// ESP restarted or answered our HELLO. link_poll() sends our state next, unless it's another link version.
static void got_hello(uint32_t v) {
  M.esp_version = (uint16_t) (v >> 16);
  plink_drop_events(&L);  // queued events are about things it no longer has
  pic_ack = -1;
  if (v & HELLO_ASK)
    plink_send(&L, K_HELLO, (uint32_t) PLINK_VERSION << 16);
  pic_reset();
  val_forget_esp();  // its items may have changed, it sends them all again
  forget_helps(-1);
  sync_next = link_compatible() ? V_NONE + 1 : 0;
  ui_layout_changed();
}

// seconds since 2000-01-01 -> the panel clock
static void set_time(uint32_t s) { clock_from_unix(s + SECS_1970_TO_2000); }

static void send_pic_ack(void) {
  if (pic_ack >= 0 && plink_room(&L)) {
    plink_send(&L, K_PIC_ACK, (uint32_t) pic_ack);
    pic_ack = -1;
  }
}

// Send the picture transfer's ack, if any, and update the screen. An ack that doesn't fit waits, a newer one
// replaces it.
static void pic_answer(uint16_t key, int32_t ack) {
  if (ack >= 0) {
    pic_ack = ack;
    send_pic_ack();
  }
  if (key == K_PIC_CLEAR || key == K_ANIM_BEGIN || key == K_ANIM_CLEAR || (ack >= 0 && ack >> 24 == PIC_DONE))
    ui_value_changed(V_SCREENSAVER);  // picture or animation came or went, rebuild the screensaver
  else if (key == K_PIC_BEGIN || key == K_PIC_PATCH)
    ui_value_changed(V_NONE);  // "Receiving ..."
}

// K_ITEM_DEF, layout in proto/plink_keys.h. Can add or remove tiles and rows.
static void item_def(item_t *it, const uint8_t *b, int n) {
  if (n != ITEM_DEF_LEN)
    return;
  it->type = b[0] <= ITEM_TEXT ? b[0] : ITEM_NONE;
  it->flags = it->type == ITEM_TEXT ? b[1] & ~ITEMF_TREND : b[1];  // nothing to chart in a text
  it->icon = b[2];
  it->dec = b[3] & 3;
  it->step = (uint16_t) (b[4] | b[5] << 8);
  it->step = it->step ? it->step : 1;
  it->id = (uint16_t) (b[6] | b[7] << 8) & 0x3FFF;
  it->min = (int32_t) plink_number(b + 8, 4);
  it->max = (int32_t) plink_number(b + 12, 4);
  for (int i = 0; i < 4; i++)
    it->level[i] = (int16_t) (b[16 + 2 * i] | b[17 + 2 * i] << 8);
  if (it->flags & ITEMF_TREND)
    trend_keep(it->id);
  ui_layout_changed();
}

// Text from the ESP into dst, size counts the 0. Cut on a UTF-8 character boundary
static void text(char *dst, int size, const uint8_t *b, int n) {
  if (n > size - 1) {
    n = size - 1;
    while (n > 0 && (b[n] & 0xC0) == 0x80)  // first byte cut off is mid-character
      n--;
  }
  memcpy(dst, b, n);
  dst[n] = 0;
}

// number message, a command or a value by its key
static void number(uint16_t key, uint32_t v) {
  switch (key) {
    case K_TIME:
      set_time(v);
      plat_time_set();
      ui_value_changed(V_NONE);
      return;
    case K_ACTION:
      do_action_remote((int) v);
      return;
    case K_SHOW_VIEW:
      if (v < VIEW_COUNT) {
        input_activity();  // wakes it, or the view would sit at the idle brightness
        ui_show_view((int) v);
      }
      return;
  }
  val_t id = val_by_key(key);
  if (id != V_NONE)
    val_set_remote(id, (int32_t) v);
}

// A message from the ESP
static void got(void *u, uint16_t key, const uint8_t *b, int n) {
  (void) u;
  if (key == K_HELLO) {
    got_hello(plink_number(b, n));
    return;
  }
  if (!link_compatible())
    return;  // different link version, the message can't be trusted
  switch (key) {
    case K_PIC_BEGIN:
    case K_PIC_PATCH:
    case K_PIC_DATA:
    case K_PIC_END:
    case K_PIC_CLEAR:
    case K_ANIM_BEGIN:
    case K_ANIM_CLEAR:
    case K_ANIM_MOVE:
      pic_answer(key, pic_msg(key, b, n));
      return;
    case K_NOTIFY:
      if (n < 2)
        return;
      M.note_icon = b[0];
      M.note_level = b[1] <= NOTE_ALARM ? b[1] : NOTE_INFO;
      text(M.note, NOTE_LEN, b + 2, n - 2);
      M.note_new = true;
      return;
  }
  if (key >= K_ITEM_DEF && key < K_ITEM_DEF + N_ITEMS) {
    item_def(&M.items[key - K_ITEM_DEF], b, n);
  } else if (key >= K_ITEM_LABEL && key < K_ITEM_LABEL + N_ITEMS) {
    int i = key - K_ITEM_LABEL;
    text(M.items[i].label, ITEM_LABEL, b, n);
    forget_helps(i);      // labels change with the language, so the help is stale too
    ui_layout_changed();  // a new label can change the layout
  } else if (key >= K_ITEM_UNIT && key < K_ITEM_UNIT + N_ITEMS) {
    text(M.items[key - K_ITEM_UNIT].unit, ITEM_UNIT, b, n);
    ui_layout_changed();
  } else if (key >= K_ITEM_ON && key < K_ITEM_ON + N_ITEMS) {
    text(M.items[key - K_ITEM_ON].on, ITEM_STATE, b, n);
    ui_layout_changed();
  } else if (key >= K_ITEM_OFF && key < K_ITEM_OFF + N_ITEMS) {
    text(M.items[key - K_ITEM_OFF].off, ITEM_STATE, b, n);
    ui_layout_changed();
  } else if (key >= K_ITEM_TEXT && key < K_ITEM_TEXT + N_ITEMS) {
    text(M.items[key - K_ITEM_TEXT].text, ITEM_TEXT_LEN, b, n);
    ui_value_changed((val_t) (V_ITEM_FIRST + key - K_ITEM_TEXT));
  } else if (key == K_ITEM_HELP) {
    if (n >= 1 && b[0] < N_ITEMS) {
      forget_helps(b[0]);
      text(helps[help_next].text, sizeof helps[0].text, b + 1, n - 1);
      helps[help_next].of = (int8_t) b[0];
      help_next = (uint8_t) ((help_next + 1) % HELP_SLOTS);
      ui_value_changed(V_NONE);
    }
  } else if (plink_kind(key) != PLINK_BYTES) {
    number(key, plink_number(b, n));
  }
}

void link_bus(const uint8_t *b, int n, uint32_t start_us, uint32_t idle_us, uint32_t now_ms) {
  if (ready)
    pbus_panel_burst(&W, b, n, start_us, idle_us, now_ms);
}

int link_bulk_frame(const uint8_t *f, int n, uint32_t idle_us, uint32_t now_ms, uint8_t *answer) {
  if (!ready || !HAS(FEAT_ESP))
    return 0;
  uint8_t say;
  uint32_t left = pbus_panel_window(&W, idle_us, now_ms, &say);
  return pbulk_panel_frame(&B, f, n, left, say, answer);
}

const pbulk_panel_t *link_bulk_state(void) { return &B; }
const pbus_panel_t *link_bus_state(void) { return &W; }
const plink_t *link_queue(void) { return &L; }

void link_init(void) {
  ready = false;
  uint32_t e = plat_entropy();
  plink_init(&L);
  pic_ack = -1;
  forget_helps(-1);
  pbulk_panel_init(&B, (uint16_t) (e ^ e >> 16));
  pbus_panel_init(&W);
  M.esp_version = 0;
  sync_next = 0;
  ready = true;
  if (HAS(FEAT_ESP))
    hello();
}

void link_poll(void) {
  if (!HAS(FEAT_ESP))
    return;
  uint8_t p[PBULK_MAX];
  int n;
  while ((n = pbulk_panel_take(&B, p)) > 0)
    pbulk_parse(p, n, got, NULL);
  send_pic_ack();
  // After a HELLO, send our own settings a few at a time so user changes still get through. Not the CU60's
  // values or the ESP's items. From us those mean "change it", and right after a start we may not know them yet.
  while (sync_next && plink_room(&L) > 8) {
    int v = sync_next++;
    if (v >= V_ITEM_FIRST) {
      sync_next = 0;
      break;
    }
    val_def_t d = val_def((val_t) v);
    if ((d.flags & VF_LINK_R) && !(d.flags & (VF_CU60 | VF_ESP)))
      plink_send(&L, d.key, (uint32_t) val_get((val_t) v));
  }
  if (!B.out_full) {  // answer buffer is free, fill it
    n = plink_pack(&L, p, PBULK_ANSWER_MAX);
    if (n)
      pbulk_panel_put(&B, p, n);
  }
  n = plink_waiting(&L);
  B.waiting = n > 255 ? 255 : (uint8_t) n;  // what's left, so the ESP can leave room for it
}

void link_second(void) {
  static uint8_t room_s;
  static uint32_t frames_seen;
  if (!HAS(FEAT_ESP))
    return;
  if (++room_s >= 60) {  // once a minute, the panel's own temperature sensor for Home Assistant
    static int16_t room_sent = T_NONE;
    room_s = 0;
    if (M.t_room != room_sent && M.esp_online) {
      room_sent = M.t_room;
      link_send(K_ROOM_TEMP, (uint32_t) (int32_t) M.t_room);
    }
  }
  // The ESP sends a frame every CU60 cycle, so 30 s without one means it's gone. Refused frames don't count,
  // since nothing gets through while we refuse them.
  uint32_t frames = B.frames - B.bad - B.refused;
  if (frames != frames_seen) {
    frames_seen = frames;
    quiet_s = 0;
  } else if (quiet_s < 30) {
    quiet_s++;
  }
  bool online = quiet_s < 30 && link_compatible();
  if (online != M.esp_online) {
    M.esp_online = online;
    ui_layout_changed();  // some views show other things without the ESP
  }
}

// User changed v. Of the CU60 values we only send VF_MASTER ones. The CU60 takes those from the lowest panel
// address only, so the ESP may have to write them for us. It sees the other CU60 writes on the bus. Not while
// it's offline, it would write them late.
void link_value_changed(val_t v) {
  val_def_t d = val_def(v);
  if (v == V_FEAT_FIRST + FEAT_ESP && HAS(FEAT_ESP)) {
    hello();
    return;
  }
  if (!(d.flags & VF_LINK_R))
    return;
  if ((d.flags & VF_CU60) && (!(d.flags & VF_MASTER) || !M.esp_online))
    return;
  link_send(d.key, (uint32_t) val_get(v));
}

void link_action(int a) { link_send(K_ACTION, (uint32_t) a); }

const char *link_item_help(int i) {
  for (int k = 0; k < HELP_SLOTS; k++)
    if (helps[k].of == i)
      return helps[k].text;
  if (help_asked != i && link_compatible()) {
    help_asked = (int8_t) i;
    plink_send(&L, K_ITEM_HELP, (uint32_t) i);
  }
  return NULL;
}
