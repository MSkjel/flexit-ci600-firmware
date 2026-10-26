// ESP link, see link.h
#include "link.h"
#include "ui.h"
#include "anim.h"
#include "clock.h"
#include "input.h"
#include "picture.h"
#include "plat.h"
#include "trend.h"
#include <string.h>

#define ESP_QUIET_S 30  // the ESP sends a frame every CX60 cycle, so this long without one means it's gone
#define SEND_SPARE 8    // queue slots the sync and the settings leave free, so changes still get through

static plink_t queue;             // outgoing messages waiting for room in an answer
static pbulk_panel_t bulk;        // frame state, filled in the RX interrupt and handled in link_poll()
static pbus_panel_t bus;          // bus timing, as the RX interrupt sees it
static volatile bool ready;       // link_init() has run. Until then frames go unanswered
static uint16_t quiet_s;          // seconds since the last frame from the ESP
static int16_t sync_at = -1;      // the last value sent in the sync after a HELLO, V_NONE = none yet, -1 = idle
static int16_t settings_at = -1;  // the last of our settings sent since the ESP asked, V_NONE = none yet, -1 = idle
static uint16_t settings_n;       // sent so far, or taken from the ESP
static bool restoring;            // the ESP's copy of our settings is coming, and we can take it
static int32_t pic_ack = -1;      // a K_PIC_ACK waiting for room in the queue, -1 = none
static int8_t help_asked = -1;    // the item we last asked the ESP about
static uint8_t help_next;         // slot the next help goes in

// The last few item help texts from the ESP, so moving up and down a menu doesn't ask again
#define HELP_SLOTS 8
static struct {
  int8_t of;  // item, -1 = free
  char text[PLINK_HELP_MAX + 1];
} helps[HELP_SLOTS];
_Static_assert(N_ITEMS <= INT8_MAX, "an item in help_asked and of");

bool link_compatible(void) { return M.esp_version == PLINK_VERSION; }

static uint32_t hello_value(void) { return (uint32_t) PLINK_VERSION << 16 | (plat_fresh() ? HELLO_FRESH : 0); }

// Sent when we start or the ESP is enabled in the menu. The ESP answers with its state.
static void hello(void) { plink_send(&queue, K_HELLO, hello_value() | HELLO_ASK); }

void link_send(uint16_t key, uint32_t value) {
  if (HAS(FEAT_ESP) && link_compatible())
    plink_send(&queue, key, value);
}

// Item i's help, or all of them for -1
static void forget_helps(int i) {
  for (int k = 0; k < HELP_SLOTS; k++)
    if (i < 0 || helps[k].of == i)
      helps[k].of = -1;
  if (i < 0 || help_asked == i)
    help_asked = -1;
}

// A CX60 value from an answer the ESP ignored before our HELLO. The sync skips those, so it goes again.
static void resend_cx60(void *u, uint16_t key, const uint8_t *b, int n) {
  (void) u;
  val_t v = val_by_key(key);
  for (int i = 0; i < queue.qn; i++)
    if (queue.key[i] == key)
      return;  // a newer one is waiting
  if (v != V_NONE && (val_def(v).flags & VF_CX60))
    plink_send(&queue, key, plink_number(b, n));
}

// ESP restarted or answered our HELLO. link_poll() sends our state next, unless it's another link version.
static void got_hello(uint32_t v) {
  M.esp_version = (uint16_t) (v >> 16);
  plink_forget(&queue);  // events and item values are about things it no longer has
  pic_ack = -1;
  if (v & HELLO_ASK) {
    plink_send(&queue, K_HELLO, hello_value());  // goes ahead of the waiting values
    if (bulk.out_full)
      pbulk_parse(bulk.out, bulk.out_n, resend_cx60, NULL);
  }
  pic_reset();
  anim_clear_all();  // it sends them again
  val_forget_esp();  // its items may have changed, it sends them all again
  forget_helps(-1);
  sync_at = link_compatible() ? V_NONE : -1;
  ui_layout_changed();
}

// seconds since PLINK_EPOCH -> the panel clock
static void set_time(uint32_t s) { clock_from_unix(s + PLINK_EPOCH); }

static void send_pic_ack(void) {
  if (pic_ack >= 0 && plink_room(&queue)) {
    plink_send(&queue, K_PIC_ACK, (uint32_t) pic_ack);
    pic_ack = -1;
  }
}

// Hands a picture message to picture.c and sends its ack. One that doesn't fit waits, a newer one replaces it.
static void pic_answer(uint16_t key, const uint8_t *b, int n) {
  pic_state_t was = pic_state();
  int32_t ack = pic_msg(key, b, n);
  if (ack >= 0) {
    pic_ack = ack;
    send_pic_ack();
  }
  if (was == PIC_READY && pic_state() == PIC_RECEIVING)
    ui_picture_coming();
  // Picture came or went, rebuild the screensaver. A new or failed picture drops the old. A patch redraws its own rows
  // and the animations show and go on their own.
  if (pic_state() != was || key == K_PIC_CLEAR)
    ui_value_changed(V_SCREENSAVER);
  else if (key == K_PIC_BEGIN || key == K_PIC_PATCH)
    ui_value_changed(V_NONE);  // "Receiving ..."
}

// K_ITEM_DEF, layout in proto/plink_keys.h. Can add or remove tiles and rows.
static void item_def(item_t *it, const uint8_t *b, int n) {
  if (n != ITEM_DEF_LEN)
    return;
  uint8_t type = b[ITEM_DEF_TYPE] <= ITEM_TEXT ? b[ITEM_DEF_TYPE] : ITEM_NONE;
  if (type != it->type)
    memset(&it->u, 0, sizeof it->u);  // the toggle's words and the text share their room
  it->type = type;
  uint8_t flags = b[ITEM_DEF_FLAGS];
  it->flags = it->type == ITEM_TEXT ? flags & ~ITEMF_TREND : flags;  // nothing to chart in a text
  it->icon = b[ITEM_DEF_ICON];
  it->dec = b[ITEM_DEF_DEC] & 3;
  it->step = plink_get16(b + ITEM_DEF_STEP);
  it->step = it->step ? it->step : 1;
  it->id = plink_get16(b + ITEM_DEF_ID) & ITEM_ID_MASK;
  it->min = (int32_t) plink_number(b + ITEM_DEF_MIN, 4);
  it->max = (int32_t) plink_number(b + ITEM_DEF_MAX, 4);
  for (int i = 0; i < 4; i++)
    it->level[i] = plink_i16(b + ITEM_DEF_LEVELS + 2 * i);
  if (it->flags & ITEMF_TREND)
    trend_keep(it->id);
  ui_layout_changed();
}

// Text from the ESP into dst, size counts the 0. Cut on a UTF-8 character boundary
static void copy_text(char *dst, int size, const uint8_t *b, int n) {
  if (n > size - 1) {
    n = size - 1;
    while (n > 0 && (b[n] & 0xC0) == 0x80)  // first byte cut off is mid-character
      n--;
  }
  memcpy(dst, b, n);
  dst[n] = 0;
}

// K_SETTINGS from the ESP: it asks for ours, or sends them back after a flash. Once they've all come we save them and
// restart, as if they'd been in flash all along.
static void settings_in(uint32_t v) {
  uint16_t key = (uint16_t) (v >> 16), x = (uint16_t) v;
  if (!v) {
    settings_at = V_NONE;
  } else if (!key) {
    restoring = x == SETTINGS_FORMAT && plat_fresh();
    settings_n = 0;
  } else if (key == SETTINGS_END) {
    if (restoring && x == settings_n) {
      plat_save_now();
      plat_action(A_RESTART);
    }
    restoring = false;
  } else if (restoring) {
    val_t id = val_by_key(key);
    if (val_def(id).flags & VF_SAVED)
      val_load(id, (int16_t) x);
    settings_n++;
  }
}

// After a HELLO, our own settings a few at a time so user changes still get through. Not the CX60's values or the
// ESP's items. From us those mean "change it", and right after a start we may not know them yet.
static void send_sync(void) {
  while (sync_at >= 0 && plink_room(&queue) > SEND_SPARE) {
    val_t v = val_next((val_t) sync_at, VF_LINK_R, VF_CX60 | VF_ESP);
    if (v == V_COUNT) {
      sync_at = -1;
      return;
    }
    plink_send(&queue, val_def(v).key, (uint32_t) val_get(v));
    sync_at = (int16_t) v;
  }
}

// After the ESP asked: SETTINGS_FORMAT, each saved value, then how many. A few at a time, like the sync.
static void send_settings(void) {
  while (settings_at >= 0 && plink_room(&queue) > SEND_SPARE) {
    if (settings_at == V_NONE) {
      plink_send(&queue, K_SETTINGS, SETTINGS_FORMAT);
      settings_n = 0;
    }
    val_t v = val_next((val_t) settings_at, VF_SAVED, 0);
    if (v == V_COUNT) {
      plink_send(&queue, K_SETTINGS, (uint32_t) SETTINGS_END << 16 | settings_n);
      settings_at = -1;
      return;
    }
    plink_send(&queue, K_SETTINGS, (uint32_t) val_def(v).key << 16 | (uint16_t) val_saved(v));
    settings_n++;
    settings_at = (int16_t) v;
  }
}

// number message, a command or a value by its key
static void got_number(uint16_t key, uint32_t v) {
  switch (key) {
    case K_SETTINGS:
      settings_in(v);
      return;
    case K_TIME:
      set_time(v);
      plat_time_set();
      ui_value_changed(V_NONE);
      return;
    case K_ACTION:
      do_action_remote((int) v);
      return;
    case K_CX60_REG:
      plat_cx60_reg((uint16_t) (v >> 16), (int16_t) v);
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

// An item's DEF, LABEL, UNIT, ON, OFF or TEXT. kind = the K_ITEM_* key, i = the item
static void item_msg(uint16_t kind, int i, const uint8_t *b, int n) {
  if (i >= N_ITEMS)
    return;
  item_t *it = &M.items[i];
  switch (kind) {
    case K_ITEM_DEF:
      item_def(it, b, n);
      return;
    case K_ITEM_LABEL:
      copy_text(it->label, ITEM_LABEL_LEN, b, n);
      forget_helps(i);  // labels change with the language, so the help is stale too
      break;
    case K_ITEM_UNIT:
      copy_text(it->unit, ITEM_UNIT_LEN, b, n);
      break;
    case K_ITEM_ON:
    case K_ITEM_OFF:
      if (it->type != ITEM_TOGGLE)
        return;
      copy_text(kind == K_ITEM_ON ? it->u.on : it->u.off, ITEM_STATE_LEN, b, n);
      break;
    case K_ITEM_TEXT:
      if (it->type == ITEM_TEXT) {
        copy_text(it->u.text, ITEM_TEXT_LEN, b, n);
        ui_value_changed((val_t) (V_ITEM_FIRST + i));
      }
      return;
    default:
      return;
  }
  ui_layout_changed();  // new words can change the layout
}

// A message from the ESP
static void got_msg(void *u, uint16_t key, const uint8_t *b, int n) {
  (void) u;
  if (key == K_HELLO) {
    got_hello(plink_number(b, n));
    return;
  }
  if (!link_compatible())
    return;  // different link version, the message can't be trusted
  if (key >= K_ANIM_FRAMES && key <= K_ANIM_ROUNDS) {
    anim_msg(key, b, n);  // a running animation's, no picture data or ack
    return;
  }
  if ((key >= K_PIC_BEGIN && key <= K_PIC_CLEAR) || key == K_ANIM_BEGIN || key == K_ANIM_CLEAR) {
    pic_answer(key, b, n);
    return;
  }
  switch (key) {
    case K_NOTIFY:
      if (n < 2)
        return;
      M.note_icon = b[0];
      M.note_level = b[1] <= NOTE_ALARM ? b[1] : NOTE_INFO;
      copy_text(M.note, NOTE_LEN, b + 2, n - 2);
      M.note_new = true;
      return;
    case K_ITEM_HELP:
      if (n >= 1 && b[0] < N_ITEMS) {
        forget_helps(b[0]);
        copy_text(helps[help_next].text, sizeof helps[0].text, b + 1, n - 1);
        helps[help_next].of = (int8_t) b[0];
        help_next = (uint8_t) ((help_next + 1) % HELP_SLOTS);
        ui_value_changed(V_NONE);
      }
      return;
  }
  if (plink_kind(key) != PLINK_BYTES)
    got_number(key, plink_number(b, n));
  else if (key >= K_ITEM_DEF)
    item_msg(K_ITEM_KIND(key), K_ITEM_INDEX(key), b, n);
}

void link_bus(const uint8_t *b, int n, uint32_t start_us, uint32_t idle_us, uint32_t now_ms) {
  if (ready)
    pbus_panel_burst(&bus, b, n, start_us, idle_us, now_ms);
}

int link_bulk_frame(const uint8_t *f, int n, uint32_t idle_us, uint32_t now_ms, uint8_t *answer) {
  if (!ready || !HAS(FEAT_ESP))
    return 0;
  uint8_t say;
  uint32_t left = pbus_panel_window(&bus, idle_us, now_ms, &say);
  return pbulk_panel_frame(&bulk, M.bus_addr, f, n, left, say, bus.end_us, answer);
}

const pbulk_panel_t *link_bulk_state(void) { return &bulk; }
const pbus_panel_t *link_bus_state(void) { return &bus; }
const plink_t *link_queue(void) { return &queue; }

void link_init(void) {
  ready = false;
  uint32_t e = plat_entropy();
  plink_init(&queue);
  pic_ack = -1;
  forget_helps(-1);
  pbulk_panel_init(&bulk, (uint16_t) (e ^ e >> 16));
  pbus_panel_init(&bus);
  M.esp_version = 0;
  sync_at = -1;
  settings_at = -1;
  restoring = false;
  ready = true;
  if (HAS(FEAT_ESP))
    hello();
}

void link_poll(void) {
  if (!HAS(FEAT_ESP))
    return;
  uint8_t p[PBULK_MAX];
  int n;
  while ((n = pbulk_panel_take(&bulk, p)) > 0)
    pbulk_parse(p, n, got_msg, NULL);
  send_pic_ack();
  int32_t gone = link_compatible() && plink_room(&queue) ? anim_gone() : -1;
  if (gone >= 0)
    plink_send(&queue, K_ANIM_GONE, (uint32_t) gone);
  send_sync();
  send_settings();
  if (!bulk.out_full) {  // answer buffer is free, fill it
    n = plink_pack(&queue, p, PBULK_ANSWER_MAX);
    if (n)
      pbulk_panel_put(&bulk, p, n);
  }
  n = plink_waiting(&queue);
  bulk.waiting = n > 255 ? 255 : (uint8_t) n;  // what's left, so the ESP can leave room for it
}

void link_second(void) {
  static uint8_t room_s;
  static uint32_t frames_seen;
  if (HAS(FEAT_ESP) && ++room_s >= 60) {  // once a minute, the panel's own temperature sensor for Home Assistant
    static int16_t room_sent = T_NONE;
    room_s = 0;
    if (M.t_room != room_sent && M.esp_online) {
      room_sent = M.t_room;
      link_send(K_ROOM_TEMP, (uint32_t) (int32_t) M.t_room);
    }
  }
  // Refused frames don't count, since nothing gets through while we refuse them
  uint32_t frames = bulk.frames - bulk.bad - bulk.refused;
  if (!HAS(FEAT_ESP)) {
    frames_seen = frames;
    quiet_s = ESP_QUIET_S;  // switched off, offline until it's heard again
  } else if (frames != frames_seen) {
    frames_seen = frames;
    quiet_s = 0;
  } else if (quiet_s < ESP_QUIET_S) {
    quiet_s++;
  }
  bool online = quiet_s < ESP_QUIET_S && link_compatible();
  if (online != M.esp_online) {
    M.esp_online = online;
    ui_layout_changed();  // some views show other things without the ESP
  }
}

// User changed v. Of the CX60 values we only send VF_MASTER ones. The CX60 takes those from the lowest panel
// address only, so the ESP may have to write them for us. It sees the other CX60 writes on the bus. Not while
// it's offline, it would write them late.
void link_value_changed(val_t v) {
  val_def_t d = val_def(v);
  if (v == V_FEAT_FIRST + FEAT_ESP && HAS(FEAT_ESP)) {
    hello();
    return;
  }
  if (!(d.flags & VF_LINK_R))
    return;
  if ((d.flags & VF_CX60) && (!(d.flags & VF_MASTER) || !M.esp_online))
    return;
  link_send(d.key, (uint32_t) val_get(v));
}

void link_action(int a) { link_send(K_ACTION, (uint32_t) a); }

const char *link_item_help(int i) {
  for (int k = 0; k < HELP_SLOTS; k++)
    if (helps[k].of == i)
      return helps[k].text;
  if (help_asked != i && M.esp_online && plink_send(&queue, K_ITEM_HELP, (uint32_t) i))
    help_asked = (int8_t) i;
  return NULL;
}
