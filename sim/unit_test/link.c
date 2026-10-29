// app/link.c: what the ESP's messages do
#include "unit_test.h"

static uint16_t esp_num;  // number of the ESP's next message

// A message from the ESP in a frame of its own, the way the RX interrupt hands it over, then the main loop. With
// ack the ESP has all of the panel's messages, else it hasn't heard its answers.
static void from_esp(uint16_t key, const uint8_t *b, int n, bool ack) {
  const pbulk_panel_t *p = link_bulk_state();
  uint8_t f[PBULK_FRAME_MAX] = {PBULK_ADDR, PBULK_FC_SEND, (uint8_t) (3 + n), M.bus_addr}, a[PBULK_ANSWER_FRAME_MAX];
  plink_put16(f + PBULK_ESESS, 0xE5);
  plink_put16(f + PBULK_NUM, esp_num++);
  plink_put16(f + PBULK_PSESS, ack ? p->psess : 0);
  plink_put16(f + PBULK_PNEXT, p->pnum);
  plink_put16(f + PBULK_PAYLOAD, key);
  f[PBULK_PAYLOAD + 2] = (uint8_t) n;
  memcpy(f + PBULK_PAYLOAD + 3, b, (size_t) n);
  pbus_put_crc(f, PBULK_PAYLOAD + 3 + n);
  link_bulk_frame(f, PBULK_FRAME(3 + n), 0, 0, a);
  link_poll();
}

static void number_from_esp(uint16_t key, uint32_t v, bool ack) {
  uint8_t b[4];
  plink_put32(b, v);
  from_esp(key, b, 4, ack);
}

// The panel has the ESP's HELLO, so it takes the rest of its messages
static void link_up(void) {
  model_defaults();
  link_init();
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16, false);
}

static int32_t via_link(uint16_t key, const uint8_t *b, int n) {
  uint8_t m[256] = {0};  // DATA in multiples of 4 bytes, the ESP pads its last. Each 4 count as a message.
  memcpy(m, b, (size_t) n);
  from_esp(key, m, key == K_PIC_DATA ? (n + 3) & ~3 : n, false);
  esp_num = (uint16_t) (esp_num + (key == K_PIC_DATA ? (n + 3) / 4 - 1 : 0));
  return -1;
}

static void test_link_picture(void) {
  link_up();
  to_pic = via_link;
  fill(0, H, 21);
  saver_builds = 0;
  send_picture(64);
  CHECK_EQ(pic_state(), PIC_READY);
  CHECK(shows());
  CHECK(saver_builds > 0);
  saver_builds = 0;
  fill(1, 2, 22);
  uint8_t b[512];
  unsigned n = pack(1, 2, b);
  rest_zip(patch(1, 2, n, false), b, n, NULL, 0, 64);
  CHECK(shows());
  CHECK_EQ(saver_builds, 0);
  to_pic = pic_msg;
  pic_msg(K_PIC_CLEAR, NULL, 0);
}

// Our K_SETTINGS as link_poll() queues them, taken out so the next ones fit. Returns how many.
static int settings_out(uint32_t *out, int max) {
  plink_t *l = (plink_t *) link_queue();
  int k = 0;
  for (int i = 0; i < 20; i++) {
    for (int j = 0; j < l->qn && k < max; j++)
      if (l->key[j] == K_SETTINGS)
        out[k++] = l->value[j];
    l->qn = 0;
    link_poll();
  }
  return k;
}

typedef struct {
  int n;
  uint16_t key[64];
  uint32_t value[64];
} msgs_t;

static void add_msg(void *u, uint16_t key, const uint8_t *b, int n) {
  msgs_t *m = u;
  if (m->n < 64) {
    m->key[m->n] = key;
    m->value[m->n++] = plink_number(b, n);
  }
}

// What goes after the answer the ESP has yet to take: the next one link_poll() put behind it, then the queue
static msgs_t after_out(void) {
  msgs_t m = {0};
  const pbulk_panel_t *p = link_bulk_state();
  if (p->next_full)
    pbulk_parse(p->next, p->next_n, add_msg, &m);
  const plink_t *l = link_queue();
  for (int i = 0; i < l->qn && m.n < 64; i++) {
    m.key[m.n] = l->key[i];
    m.value[m.n++] = l->value[i];
  }
  return m;
}

static bool queued(uint16_t key, uint32_t value) {
  msgs_t m = after_out();
  for (int i = 0; i < m.n; i++)
    if (m.key[i] == key && m.value[i] == value)
      return true;
  return false;
}

static void test_link(void) {
  model_defaults();
  forget_ui_calls();
  M.features |= FBIT(FEAT_ESP);
  M.idle_after = 0;  // 15 s
  link_init();
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16, false);
  CHECK(link_compatible());
  // only views that exist, and one wakes the panel
  number_from_esp(K_SHOW_VIEW, VIEW_COUNT, false);
  number_from_esp(K_SHOW_VIEW, UINT32_MAX, false);
  CHECK(!views);
  now_ms += 20000;
  input_tick(now_ms);
  CHECK(input_idle());
  wakes = 0;
  number_from_esp(K_SHOW_VIEW, VIEW_FLOW, false);
  CHECK_EQ(views, 1);
  CHECK_EQ(shown_view, VIEW_FLOW);
  CHECK(!input_idle());
  CHECK_EQ(wakes, 1);
  // Before the ESP flashes us it asks for our settings: the format, each saved value, then how many
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  M.theme = 2;
  plat_save_now();
  int saved_n = 0, theme_at = -1;
  for (val_t s = val_next(V_NONE, VF_SAVED, 0); s != V_COUNT; s = val_next(s, VF_SAVED, 0))
    saved_n++;
  number_from_esp(K_SETTINGS, 0, false);
  uint32_t snap[256];
  int k = settings_out(snap, 256);
  for (int i = 0; i < k; i++)
    if (snap[i] == ((uint32_t) K_THEME << 16 | 2))
      theme_at = i;
  CHECK_EQ(k, saved_n + 2);
  CHECK_EQ(snap[0], SETTINGS_FORMAT);
  CHECK_EQ(snap[k - 1], (uint32_t) SETTINGS_END << 16 | saved_n);
  CHECK(theme_at > 0);
  // Not taken by a panel with settings of its own
  int resets = stub_resets;
  M.theme = 0;
  for (int i = 0; i < k; i++)
    number_from_esp(K_SETTINGS, snap[i], false);
  CHECK_EQ(M.theme, 0);
  CHECK_EQ(stub_resets, resets);
  // After the flash it starts without any and says so, and takes them back and restarts with them saved
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  link_init();
  CHECK(queued(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK | HELLO_FRESH));
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16, false);
  for (int i = 0; i < k; i++)
    number_from_esp(K_SETTINGS, snap[i], false);
  CHECK_EQ(M.theme, 2);
  CHECK_EQ(stub_resets, resets + 1);
  M.theme = 0;
  settings_load();
  CHECK_EQ(M.theme, 2);  // saved before the restart
  CHECK(!plat_fresh());
  // none from another format, nor with one missing
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  M.theme = 0;
  snap[0]++;
  for (int i = 0; i < k; i++)
    number_from_esp(K_SETTINGS, snap[i], false);
  snap[0]--;
  for (int i = 0; i < k; i++)
    if (i != theme_at)
      number_from_esp(K_SETTINGS, snap[i], false);
  CHECK_EQ(M.theme, 0);
  CHECK_EQ(stub_resets, resets + 1);
  plat_save_now();
  settings_load();  // with settings again, for what follows
  // a toggle's words and a text share their room: each only takes its own, and a new type starts empty
  uint8_t def[ITEM_DEF_LEN] = {ITEM_TOGGLE};
  item_t *it = &M.items[1];
  from_esp(K_ITEM_DEF + 1, def, sizeof def, false);
  from_esp(K_ITEM_ON + 1, (const uint8_t *) "Open", 4, false);
  from_esp(K_ITEM_TEXT + 1, (const uint8_t *) "Washing", 7, false);
  CHECK_STR(it->u.on, "Open");
  CHECK(!it->u.off[0]);
  def[0] = ITEM_TEXT;
  from_esp(K_ITEM_DEF + 1, def, sizeof def, false);
  CHECK(!it->u.text[0]);
  from_esp(K_ITEM_ON + 1, (const uint8_t *) "Open", 4, false);
  from_esp(K_ITEM_TEXT + 1, (const uint8_t *) "Washing", 7, false);
  CHECK_STR(it->u.text, "Washing");
  from_esp(K_ITEM_DEF + 1, def, sizeof def, false);  // the same type again keeps it
  CHECK_STR(it->u.text, "Washing");
  // the setpoint goes to the ESP, but not while it's offline
  M.esp_online = false;
  val_set(V_SETPOINT, 230);
  CHECK(!queued(K_SETPOINT, 230));
  M.esp_online = true;
  val_set(V_SETPOINT, 235);
  CHECK(queued(K_SETPOINT, 235));
  // A picture's last ack waits while the queue is full, the ESP hasn't heard the panel's answers
  while (plink_room(link_queue()))
    link_action(A_NONE);
  uint8_t bad = 9;
  from_esp(K_PIC_BEGIN, &bad, 1, false);
  uint32_t ack = (uint32_t) PIC_BAD << 24 | 9u << 16;
  CHECK(!queued(K_PIC_ACK, ack));
  from_esp(0, &bad, 0, true);  // now it has, there's room
  link_poll();
  CHECK(queued(K_PIC_ACK, ack));

  // Item help: not asked while the ESP is away, only the latest waits, asked again if it didn't fit
  link_init();
  M.esp_version = PLINK_VERSION;
  M.esp_online = false;
  CHECK(!link_item_help(1) && !queued(K_ITEM_HELP, 1));
  M.esp_online = true;
  link_item_help(1);
  link_item_help(2);
  CHECK(queued(K_ITEM_HELP, 2) && !queued(K_ITEM_HELP, 1));
  link_init();
  M.esp_version = PLINK_VERSION;
  while (plink_room(link_queue()))
    link_action(A_NONE);
  link_item_help(3);
  CHECK(!queued(K_ITEM_HELP, 3));
  link_poll();  // some go out
  link_item_help(3);
  CHECK(queued(K_ITEM_HELP, 3));

  // ESP restart: our HELLO goes ahead of the waiting values, even with a full queue
  link_init();
  M.esp_version = PLINK_VERSION;
  link_poll();                   // our own HELLO is in the answer
  const uint16_t spare = 0x0D0;  // keys no message has, so they're values
  for (uint16_t k = spare; plink_room(link_queue()); k++)
    link_send(k, 1);
  uint8_t d[512];  // and its animations are gone with it, it sends them again
  dot_frames(true);
  uint32_t n = anim_records(d, 5);
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  place(0, 0, 0, 10, 0, 0);
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK, false);
  msgs_t m = after_out();
  CHECK_EQ(m.key[0], K_HELLO);
  CHECK_EQ(m.value[0], (uint32_t) PLINK_VERSION << 16);
  CHECK(queued(spare, 1));
  CHECK_EQ(m.n, PLINK_QUEUE);
  CHECK(!placed());
  CHECK(!anim_used());
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);  // also after the answer to ours
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16, false);
  CHECK(!anim_used());
  // An animation shows and goes on its own, the screen isn't built again for it
  saver_builds = 0;
  CHECK_EQ(status(anim_send(0, AW, n, d, plink_crc32(0, d, n))), PIC_DONE);
  uint8_t at[10] = {0};
  from_esp(K_ANIM_PLACE, at, sizeof at, false);
  number_from_esp(K_ANIM_CLEAR, ANIM_PLACE_BIT | 0, false);
  number_from_esp(K_ANIM_CLEAR, ANIM_ALL, false);
  CHECK(!saver_builds && !anim_used());
  // A CX60 value in the ignored answer goes again unless a newer one waits. Item values don't.
  link_init();
  M.esp_version = PLINK_VERSION;
  link_send(K_SETPOINT, 215);
  link_send(K_FAN_NORM_SUP, 50);
  link_send(K_ITEM_VALUE + 1, 7);
  link_poll();  // they're in the answer
  link_send(K_FAN_NORM_SUP, 60);
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK, false);
  CHECK_EQ(after_out().key[0], K_HELLO);
  CHECK(queued(K_SETPOINT, 215));
  CHECK(queued(K_FAN_NORM_SUP, 60));
  CHECK(!queued(K_FAN_NORM_SUP, 50) && !queued(K_ITEM_VALUE + 1, 7));
  // and with one in the answer behind it too, the newer of them
  link_init();
  M.esp_version = PLINK_VERSION;
  link_send(K_SETPOINT, 215);
  link_poll();
  link_send(K_SETPOINT, 220);
  link_poll();
  CHECK(link_bulk_state()->next_full);
  number_from_esp(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK, false);
  int setpoints = 0;
  for (int i = 0; i < link_queue()->qn; i++)
    if (link_queue()->key[i] == K_SETPOINT)
      setpoints += link_queue()->value[i] == 220 ? 1 : 100;
  CHECK_EQ(setpoints, 1);

  // ESP switched off: offline at once, and until it's heard again
  M.esp_online = true;
  M.features &= ~FBIT(FEAT_ESP);
  link_second();
  CHECK(!M.esp_online);
  M.features |= FBIT(FEAT_ESP);
  link_second();
  CHECK(!M.esp_online);
  from_esp(0, &bad, 0, false);
  link_second();
  CHECK(M.esp_online);
}

// Our saved value of v in a settings dump, -1 if it isn't there
static int32_t dumped(val_t v) {
  uint32_t snap[256];
  number_from_esp(K_SETTINGS, 0, false);
  int k = settings_out(snap, 256);
  for (int i = 1; i < k - 1; i++)  // between the format and the count
    if (snap[i] >> 16 == val_def(v).key)
      return (int32_t) (snap[i] & 0xFFFF);
  return -1;
}

// A setting on trial goes to flash and to the ESP's copy as 0, 72 MHz or off, until it's kept. The rest are saved.
static void test_link_trial(void) {
  link_up();
  memset(stub_flash, 0xFF, FLASH_SETTINGS_PAGES * FLASH_PAGE);
  settings_load();
  M.cpu_speed = 2;  // a trial at 88 MHz, see hw/main.c
  val_trial(V_CPU_SPEED);
  M.brightness = 42;
  plat_save_now();  // as a restart does
  CHECK_EQ(dumped(V_CPU_SPEED), 0);
  CHECK_EQ(dumped(V_BRIGHTNESS), 42);
  link_up();  // the defaults, and the ESP's HELLO again
  settings_load();
  CHECK_EQ(M.cpu_speed, 0);
  CHECK_EQ(M.brightness, 42);
  // kept
  M.cpu_speed = 2;
  val_trial(V_NONE);
  plat_save_now();
  CHECK_EQ(dumped(V_CPU_SPEED), 2);
  link_up();
  settings_load();
  CHECK_EQ(M.cpu_speed, 2);
  // Fast display at 80 MHz
  M.cpu_speed = 1;
  M.fast_display = true;
  val_trial(V_FAST_DISPLAY);
  plat_save_now();
  CHECK_EQ(dumped(V_FAST_DISPLAY), 0);
  link_up();
  settings_load();
  CHECK(!M.fast_display);
  CHECK_EQ(M.cpu_speed, 1);
  val_trial(V_NONE);
}

const check_test_t LINK_TESTS[] = {
    {"link", test_link},
    {"link_picture", test_link_picture},
    {"link_trial", test_link_trial},
    {0},
};
