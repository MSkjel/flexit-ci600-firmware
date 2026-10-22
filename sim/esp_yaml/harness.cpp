// Runs the ESP's panel packages, turned into C++ by gen.py, against the panel's own link code on a fake CU60.
// micros() and millis() run on simulated time.
//   build/esp_test OUTDIR [PICTURE.ppm]
#include <fstream>
#include <sstream>
#include "gen_panel.cpp"

extern "C" {
void pn_init(void);
void pn_restart(void);
void pn_poll(void);
void pn_second(void);
int pn_online(void);
int pn_get(uint16_t key);
int pn_known(uint16_t key);
void pn_set(uint16_t key, int v);
void pn_action(int a);
int pn_idle(void);
const char *pn_note(int *icon, int *level, int *fresh);
void pn_clock(int *y, int *mo, int *d, int *h, int *mi);
int pn_item(int i, int *flags, int *icon, int *dec, int *step, int *id, int *min, int *max, int *value,
            const char **label, const char **unit);
int pn_item_level(int i, int k);
const char *pn_item_text(int i);
const char *pn_item_help(int i);
int sim_item_id(const char *name);
int pn_pic(int *w, int *h);
int pn_pic_format(void);
int pn_pic_line(int y, uint16_t *out);
int pn_anim_line(int y, uint8_t *out);
void pn_anim_step(int ms);
int pn_anim_ready(void);
void pn_anim_pos(int *x, int *y);
void pn_bus(const uint8_t *b, int n, uint32_t start_us);
int pn_bulk(const uint8_t *f, int n, uint32_t idle_us, uint8_t *answer);
void pn_shot(const char *dir, const char *name);
void sim_mkdir(const char *dir);
}

int log_level = 3;

// Index of the ESP's item called name, the same on both ends. -1 if there's none.
static int item(const char *name) {
  for (size_t i = 0; i < flexit::panel::items.size(); i++)
    if (flexit::panel::items[i].name_ == name)
      return (int) i;
  return -1;
}

// Item i as the panel has it.
struct PanelItem {
  int type = 0, flags, icon, dec, step, id, min, max, value;
  const char *label = "", *unit = "";
};
static PanelItem pitem(int i) {
  PanelItem p;
  if (i >= 0)
    p.type = pn_item(i, &p.flags, &p.icon, &p.dec, &p.step, &p.id, &p.min, &p.max, &p.value, &p.label, &p.unit);
  return p;
}
static PanelItem pitem(const char *name) { return pitem(item(name)); }
static uint16_t value_key(const char *name) { return (uint16_t) (K_ITEM_VALUE + item(name)); }
std::string log_all;
static uint32_t now_us;
uint32_t millis() { return now_us / 1000; }
uint32_t micros() { return now_us; }
static uint32_t waited_us;
void delayMicroseconds(uint32_t us) {
  now_us += us;
  waited_us += us;
}
static Server server_obj;
Server *server = &server_obj;

static int fails, checks;
#define CHECK(cond, ...) \
  do { \
    checks++; \
    if (!(cond)) { \
      fails++; \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__); \
      printf("\n"); \
    } \
  } while (0)

static void step(const char *what) { printf("%s\n", what); }

// ---------------------------------------------------------------- the fake CU60
// Takes whatever the ESP flags for it, which is the setpoint and fan speeds it sets for the panel. Every 110 ms it
// sends the status broadcast and the ESP and the panel talk in the quiet after it. The component calls the ESP
// 150 us to 1.5 ms after the status frame ended. 30 ms after the status started the CU60 polls the panel, the
// panel replies, and the ESP may send more in the gap after that.
static unsigned long cycles, link_frames, gap_frames;
static bool link_windows = true;     // false while the CU60 restarts and sends nothing
static int link_lost_frames;         // the panel doesn't hear this many of the ESP's frames
static int link_lost_answers;        // the ESP doesn't hear this many of the panel's answers
static uint32_t link_quiet = 14400;  // end of the status broadcast to the poll, the panel refuses under 13 ms
static int link_early, link_late;    // ESP frames too soon after the status, answers that ended after the quiet
static uint8_t STATUS[PBUS_STATUS_LEN], POLL[8], REPLY[13];

static void fin(uint8_t *f, int n) {
  uint16_t c = pbus_crc16(f, n - 2);
  f[n - 2] = c & 0xFF;
  f[n - 1] = c >> 8;
}

// A frame on the bus from now. The panel hears it unless it's its own. The ESP's component reads it
// frame_age_us after it ends.
static void on_bus(const uint8_t *f, int n, bool panel_hears) {
  uint32_t start = now_us;
  now_us += (uint32_t) n * PBUS_BYTE_US;
  if (panel_hears)
    pn_bus(f, n, start);
  now_us += 300;
  for (auto &cb : server->frame_cbs)
    cb(f, (size_t) n);
}

// The ESP's frame, if it sent one, goes to the panel and the answer comes back.
static void link_frame() {
  if (server->sent.empty())
    return;
  std::vector<uint8_t> f = std::move(server->sent);
  server->sent.clear();
  int n = (int) f.size();
  link_frames++;
  CHECK(server->frame_length && server->frame_length(f.data(), f.size()) == f.size(), "the component can't size %d", n);
  now_us += 300 + (uint32_t) n * PBUS_BYTE_US;  // the ESP starting up, then its frame
  uint32_t idle = now_us + PBUS_BYTE_US;
  if (link_lost_frames > 0) {
    link_lost_frames--;
    return;
  }
  uint8_t a[PBULK_ANSWER_FRAME_MAX];
  int an = pn_bulk(f.data(), n, idle, a);
  if (an && link_lost_answers > 0) {
    link_lost_answers--;
    return;
  }
  if (an) {
    now_us = idle + PBUS_PANEL_WAIT_US;
    on_bus(a, an, false);
  }
}

static void cs60_cycle() {
  if (!link_windows)
    return;
  uint32_t t0 = now_us, end = t0 + PBUS_STATUS_LEN * PBUS_BYTE_US;
  on_bus(STATUS, PBUS_STATUS_LEN, true);
  server->age_us = 1500;
  server->min_age_us = 150;
  waited_us = 0;
  for (auto &f : server->status_cbs)
    f();
  server->age_us = server->min_age_us = UINT32_MAX;
  if (!server->sent.empty() && 150 + waited_us < PBUS_START_MIN_US)
    link_early++;
  link_frame();
  if (now_us > end + PBUS_END_US)
    link_late++;
  now_us = end + link_quiet;  // the poll, and the panel's reply
  on_bus(POLL, sizeof POLL, true);
  now_us += 500;
  on_bus(REPLY, sizeof REPLY, true);
  for (auto &f : server->read_cbs)  // the gap after it
    f();
  if (!server->sent.empty())
    gap_frames++;
  link_frame();
}

static void run(int n) {  // n CU60 cycles of 110 ms
  for (int c = 0; c < n; c++) {
    uint32_t t0 = now_us;
    cs60_cycle();
    for (int k = 0; k < 3; k++) {
      now_us += 20000;
      yaml_loop();
      yaml_interval();
    }
    pn_poll();
    for (int r = 0; r < 0x200; r++)
      server->coil[r] = false;  // the CU60 has read everything flagged
    if (++cycles % 9 == 0)
      pn_second();
    now_us = t0 + 110000;
  }
}

static int run_until(const std::function<bool()> &done, int max) {
  for (int c = 0; c < max; c++) {
    if (done())
      return c;
    run(1);
  }
  return -1;
}

static bool settled() { return !flexit::panel::link::waiting() && pn_idle(); }

// ---------------------------------------------------------------- test pictures (drawn here, no files)
static void draw_dusk(Image &img, int moon_x, bool noise) {
  img.w = 320;
  img.h = 240;
  img.px.assign(320 * 240, 0);
  uint32_t seed = 12345;
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      double t = y / 240.0;
      int r = (int) (20 + 60 * t), g = (int) (30 + 40 * t), b = (int) (80 + 60 * (1 - t));
      double hill = 160 + 18 * sin(x / 37.0) + 8 * sin(x / 11.0);
      if (y > hill) {
        r = 20;
        g = 55 + (x / 40 % 3) * 10;
        b = 40;
      }
      int dx = x - moon_x, dy = y - 55;
      if (dx * dx + dy * dy < 25 * 25) {
        r = 240;
        g = 230;
        b = 190;
      }
      if (x >= 60 && x <= 120 && y >= 170 && y <= 220) {
        r = 90;
        g = 60;
        b = 40;
      }
      if (x >= 80 && x <= 95 && y >= 185 && y <= 200) {
        r = 250;
        g = 210;
        b = 90;
      }
      if (noise) {
        seed = seed * 1103515245 + 12345;
        r = seed >> 16 & 255;
        g = seed >> 8 & 255;
        b = seed & 255;
      }
      img.px[(size_t) y * 320 + x] = (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) b;
    }
}

// Something like a photo, smooth gradients with grain and thousands of colours
static void draw_photo(Image &img, int w, int h) {
  img.w = w;
  img.h = h;
  img.px.assign((size_t) w * h, 0);
  uint32_t seed = 99;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      seed = seed * 1103515245 + 12345;
      int grain = (int) (seed >> 16 & 7) - 4, glow = std::max(0, 120 - abs(x * 320 / w - 200) - abs(y * 240 / h - 90));
      int r = 40 + y * 150 / h + glow + grain, g = 60 + y * 90 / h + glow * 3 / 4 + grain,
          b = 150 - y * 60 / h + glow / 3 + grain;
      img.px[(size_t) y * w + x] =
          (uint32_t) std::min(r, 255) << 16 | (uint32_t) std::min(g, 255) << 8 | (uint32_t) std::min(b, 255);
    }
}

// A drawing in 6 flat colours
static void draw_flat(Image &img) {
  draw_dusk(img, 200, false);
  for (auto &c : img.px)
    c &= 0xC0C0C0;
}

static bool load_ppm(Image &img, const char *path) {  // P6, 8 bits
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  int w, h, mx;
  if (fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3) {
    fclose(f);
    return false;
  }
  fgetc(f);
  img.w = w;
  img.h = h;
  img.px.assign((size_t) w * h, 0);
  for (auto &p : img.px) {
    int r = fgetc(f), g = fgetc(f), b = fgetc(f);
    p = (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) b;
  }
  fclose(f);
  return true;
}

// A JPEG on the panel should be about as far off the ESP's picture as the encoder reckoned
static bool same_jpeg() {
  namespace pic = flexit::panel::picture;
  const auto &p = pic::S.shown;
  std::vector<uint16_t> line(p.w);
  double err = 0;
  for (int y = 0; y < p.h; y++) {
    if (!pn_pic_line(y, line.data()))
      return false;
    for (int x = 0; x < p.w; x++) {
      uint16_t c = line[x];
      uint32_t got = (uint32_t) (c >> 11) * 255 / 31 << 16 | (uint32_t) (c >> 5 & 63) * 255 / 63 << 8 |
                     (uint32_t) (c & 31) * 255 / 31,
               want = x < pic::S.fw && y < pic::S.fh ? pic::px(x, y) : 0;
      err += pic::ycc_dist(got, want);
    }
  }
  err /= (double) p.w * p.h;
  printf("  JPEG at quality %d: error %.1f on the panel, the encoder reckoned %.1f (+ RGB565)\n", p.quality, err,
         p.err);
  return err <= p.err * 1.3 + 15;
}

// Whether the panel draws the ESP's picture. A 16-colour one must match pixel for pixel, which shows the patches
// went in at the right rows. A JPEG must be within its error.
static bool same_picture() {
  if (pn_pic_format() == PIC_JPEG)
    return same_jpeg();
  const auto &p = flexit::panel::picture::S.shown;
  std::vector<uint8_t> row((p.w + 1) / 2);
  std::vector<uint16_t> line(p.w);
  for (int y = 0; y < p.h; y++) {
    // unpack the ESP's row. PackBits, two pixels per byte, left one in the high nibble
    const uint8_t *q = p.data.data() + p.rows[y];
    for (size_t o = 0; o < row.size();) {
      uint8_t c = *q++;
      size_t n = std::min<size_t>(c < 128 ? c + 1u : c - 126u, row.size() - o);
      if (c < 128) {
        memcpy(&row[o], q, n);
        q += c + 1u;
      } else
        memset(&row[o], *q++, n);
      o += n;
    }
    if (!pn_pic_line(y, line.data()))
      return false;
    for (int x = 0; x < p.w; x++) {
      uint32_t rgb = p.pal[x & 1 ? row[x >> 1] & 15 : row[x >> 1] >> 4];
      uint16_t want = (rgb >> 8 & 0xF800) | (rgb >> 5 & 0x07E0) | (rgb >> 3 & 0x001F);  // RGB565
      if (line[x] != want) {
        printf("  pixel %d,%d: the panel draws %04x, the ESP's picture has %04x\n", x, y, line[x], want);
        return false;
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------- test animations (drawn here, no files)
// A ball bouncing on a transparent background, or noise, which is too big at full size.
static void draw_ball(Animation &a, int size, int frames, bool noise) {
  static const int HEIGHT[8] = {0, 3, 10, 20, 28, 20, 10, 3};
  a.w = a.h = size;
  a.frames.assign(frames, std::vector<uint32_t>((size_t) size * size, 0));
  uint32_t seed = 7;
  for (int f = 0; f < frames; f++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++) {
        seed = seed * 1103515245 + 12345;
        int dx = x - size / 2, dy = y - (size - 12 - HEIGHT[f % 8]);
        if (noise)
          a.frames[f][(size_t) y * size + x] = 0xFF000000u | seed >> 8;
        else if (dx * dx + dy * dy <= 100)
          a.frames[f][(size_t) y * size + x] = dx * dx + dy * dy > 70 ? 0xFFA04010u : 0xFFF08020u;
      }
}

// Whether the panel draws frame f as the ESP made it, transparent in the same places and palette colours elsewhere.
static bool same_anim(int f) {
  namespace anim = flexit::panel::anim;
  auto &A = anim::A;
  for (int y = 0; y < A.h * A.scale; y++) {
    uint8_t line[320 * 3];
    if (pn_anim_line(y, line) != A.w * A.scale)
      return false;
    for (int x = 0; x < A.w * A.scale; x++) {
      uint32_t c = anim::px(f, x / A.scale, y / A.scale), rgb = A.pal[A.lut[flexit::panel::picture::bin(c)]];
      uint16_t want = (rgb >> 8 & 0xF800) | (rgb >> 5 & 0x07E0) | (rgb >> 3 & 0x001F),
               got = line[3 * x] | line[3 * x + 1] << 8;
      bool see = c >> 24 < 128;
      if (see != (line[3 * x + 2] == 0) || (!see && got != want)) {
        printf("  frame %d pixel %d,%d: the panel draws %04x/%d, the ESP made %04x/%d\n", f, x, y, got, line[3 * x + 2],
               want, !see);
        return false;
      }
    }
  }
  return true;
}

static int picture(const char *name, int max_cycles) {
  unsigned long c0 = cycles;
  int w, h;
  run(3);  // so an earlier "Shown" isn't taken for this one
  int r =
      run_until([&] { return panel_picture_status.state.rfind("Shown", 0) == 0 && settled() && pn_pic(&w, &h) == 2; },
                max_cycles);
  printf("  %s: %s after %lu cycles (%.1f s on the bus)\n", name, panel_picture_status.state.c_str(), cycles - c0,
         (cycles - c0) * 0.11);
  CHECK(r < 0 || same_picture(), "%s: the panel draws something else than the ESP's picture", name);
  return r;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: esp_test OUTDIR [PICTURE.ppm]\n");
    return 2;
  }
  const char *outdir = argv[1];
  sim_mkdir(outdir);
  memcpy(STATUS, (const uint8_t[]) {0x00, 0x10, 0x00, 0xBE, 0x00, 0x55, 0xAA}, 7);
  for (int i = 7; i < PBUS_STATUS_LEN - 2; i++)
    STATUS[i] = (uint8_t) i;
  fin(STATUS, PBUS_STATUS_LEN);
  memcpy(POLL, (const uint8_t[]) {3, 0x01, 0x00, 0x00, 0x00, 0x40}, 6);  // the settings poll of panel 3
  fin(POLL, sizeof POLL);
  memcpy(REPLY, (const uint8_t[]) {3, 0x01, 8}, 3);  // nothing flagged
  fin(REPLY, sizeof REPLY);
  pn_init();
  yaml_wire();
  // like the real comfort packages, set_action sets the global and then the number publishes the value
  fireplace_duration.set_action = [](float v) { fireplace_duration_minutes = (int) v; };
  inside_humidity.publish_state(54.6f);
  co2.publish_state(912);
  heater_enabled.publish_state(true);
  temperature.publish_state(20.0f);
  ha_washer.publish_state("Washing|Vasker");
  draw_ball(panel_animation_ball, 48, 8, false);
  draw_ball(panel_animation_noise, 64, 8, true);  // too big at full size
  yaml_boot();
  CHECK(!panel_brightness.has_state() && !panel_idle_brightness.has_state() && !panel_screensaver.has,
        "panel settings known before the panel reported them: brightness %g, idle %g, screensaver '%s'",
        panel_brightness.state, panel_idle_brightness.state, panel_screensaver.state.c_str());

  step("hello, both ways");
  int c = run_until([] { return pn_online() && flexit::panel::link::connected && settled(); }, 2000);
  CHECK(c >= 0, "no link after 2000 cycles");
  int hellos = 0;  // times the ESP sent the panel its state
  for (size_t at = 0; (at = log_all.find("Panel says hello", at)) != std::string::npos; at++)
    hellos++;
  CHECK(hellos == 1, "the ESP sent its state %d times", hellos);
  CHECK(!link_early, "%d frames sent sooner than %d us after the status", link_early, PBUS_START_MIN_US);
  CHECK(!link_late, "%d answers ended later than %d us after the status", link_late, PBUS_END_US);
  CHECK(gap_frames > 0, "nothing sent in the gaps after polls");
  CHECK(pn_get(K_SETPOINT) == 200 && pn_get(K_FAN_MAX_SUP) != 20,
        "the ESP sent unit settings it doesn't have: setpoint %d, max supply %d", pn_get(K_SETPOINT),
        pn_get(K_FAN_MAX_SUP));
  printf("  linked and settled after %d cycles\n", c);
  CHECK(panel_screensaver.state == panel_screensaver.options[pn_get(K_SCREENSAVER)], "screensaver %s vs panel %d",
        panel_screensaver.state.c_str(), pn_get(K_SCREENSAVER));
  CHECK(panel_brightness.state == pn_get(K_BRIGHTNESS), "brightness %g vs %d", panel_brightness.state,
        pn_get(K_BRIGHTNESS));
  CHECK(panel_idle_brightness.state == pn_get(K_IDLE_BRIGHTNESS), "idle brightness %g vs %d",
        panel_idle_brightness.state, pn_get(K_IDLE_BRIGHTNESS));
  CHECK(fabsf(panel_temperature.state * 10 - pn_get(K_ROOM_TEMP)) < 0.5f, "temperature %g vs %d",
        panel_temperature.state, pn_get(K_ROOM_TEMP));
  CHECK(panel_firmware.state == "0.1.0", "firmware version '%s'", panel_firmware.state.c_str());
  printf("  panel firmware %s, temperature %.1f C, screensaver %s, brightness %.0f %%\n", panel_firmware.state.c_str(),
         panel_temperature.state, panel_screensaver.state.c_str(), panel_brightness.state);
  int y, mo, d, h, mi;
  pn_clock(&y, &mo, &d, &h, &mi);
  CHECK(y == 2026 && mo == 10 && d == 18 && h == 21 && mi == 30, "clock %d-%d-%d %d:%d", y, mo, d, h, mi);
  // the local hour changes, say summer time starts. The panel must follow within a few cycles
  flexit_panel_time.t.hour = 22;
  tz_offset_s = 3600;
  int cycles_to_clock = run_until(
      [&] {
        pn_clock(&y, &mo, &d, &h, &mi);
        return h == 22;
      },
      20);
  CHECK(cycles_to_clock >= 0, "the panel's clock didn't follow the hour: %d:%d", h, mi);
  flexit_panel_time.t.hour = 21;
  tz_offset_s = 0;
  run_until(
      [&] {
        pn_clock(&y, &mo, &d, &h, &mi);
        return h == 21;
      },
      20);
  for (const char *n : {"fireplace", "fireplace_minutes", "humidity", "humidity_boost", "humidity_trigger", "co2",
                        "co2_control", "co2_target", "summer_cooling"}) {
    CHECK(pitem(n).type && (pitem(n).flags & ITEMF_COMFORT), "the panel doesn't have comfort item %s", n);
    CHECK(pitem(n).id == sim_item_id(n), "%s: id %d on the panel, the simulator's %d", n, pitem(n).id, sim_item_id(n));
  }
  CHECK(!pitem(N_ITEMS - 1).type, "an item nothing sent");
  CHECK(pitem("humidity").value == 55 && pitem("co2").value == 912, "humidity %d, co2 %d", pitem("humidity").value,
        pitem("co2").value);

  if (argc > 2) {  // esp_test OUTDIR PICTURE.ppm: only show how the panel draws that picture
    if (!load_ppm(panel_picture_dusk, argv[2])) {
      printf("can't read %s\n", argv[2]);
      return 1;
    }
    flexit::panel::picture::show("dusk");
    CHECK(picture(argv[2], 6000) >= 0, "picture not shown");
    pn_shot(outdir, "esp_custom");
    printf("  %s/esp_custom.ppm\n", outdir);
    return fails ? 1 : 0;
  }

  step("items");
  static const char *const NAME[] = {"inside_humidity", "heater_enabled", "heater",
                                     "temperature",     "clear_alarms",   "washer"};
  static const char *const LABEL[] = {"Bathroom", "Heater on", "Heater", "Setpoint", "Clear alarms", "Washer"};
  static const int TYPE[] = {ITEM_SENSOR, ITEM_TOGGLE, ITEM_TOGGLE, ITEM_NUMBER, ITEM_BUTTON, ITEM_TEXT};
  for (int i = 0; i < 6; i++) {
    PanelItem p = pitem(NAME[i]);
    CHECK(p.type == TYPE[i] && !strcmp(p.label, LABEL[i]), "%s: type %d label '%s'", NAME[i], p.type, p.label);
    printf("  %s: type %d flags 0x%02X icon %d '%s' %d%s (%d..%d step %d, %d decimals)\n", NAME[i], p.type, p.flags,
           p.icon, p.label, p.value, p.unit, p.min, p.max, p.step, p.dec);
  }
  PanelItem p = pitem("temperature");
  CHECK(p.dec == 1 && p.step == 5 && p.min == 100 && p.max == 300 && p.value == 200 && (p.flags & ITEMF_COMFORT),
        "number %d %d %d %d %d 0x%02X", p.dec, p.step, p.min, p.max, p.value, p.flags);
  p = pitem("inside_humidity");
  CHECK(p.value == 55 && (p.flags & (ITEMF_RO | ITEMF_TREND | ITEMF_LEVELS)) == (ITEMF_RO | ITEMF_TREND | ITEMF_LEVELS),
        "sensor value %d flags 0x%02X", p.value, p.flags);
  CHECK(pitem("heater_enabled").flags & ITEMF_RO, "a binary sensor the panel may change");
  CHECK(!(pitem("heater").flags & ITEMF_RO), "a switch the panel can't change");
  CHECK((pitem("washer").flags & ITEMF_RO) && !strcmp(pn_item_text(item("washer")), "Washing"),
        "text item: flags 0x%02X, shows '%s'", pitem("washer").flags, pn_item_text(item("washer")));
  CHECK(!(pitem("clear_alarms").flags & ITEMF_VIEW) && (pitem("clear_alarms").flags & ITEMF_MENU),
        "button flags 0x%02X", pitem("clear_alarms").flags);

  step("item decimals and levels as the panel can take them");
  {
    using flexit::panel::Item;
    int many = Item("many_decimals", ITEM_SENSOR).view(false).menu(false).decimals(5).range(0, 10).add();
    int ppm =
        Item("ppm", ITEM_SENSOR).view(false).menu(false).decimals(2).levels("0, 0, 800, 1200").range(0, 2000).add();
    flexit::panel::item_value(many, 1.5f);
    flexit::panel::item_value(ppm, 912);
    run_until(settled, 200);
    run(3);
    PanelItem m = pitem("many_decimals"), q = pitem("ppm");
    CHECK(m.dec == 3 && m.value == 1500, "5 decimals asked for: %d on the panel, value %d", m.dec, m.value);
    // 900 ppm is between good and bad, so its levels have to fit the panel's 16 bits
    CHECK(q.dec == 1 && q.value == 9120 && pn_item_level(item("ppm"), 2) == 8000 &&
              pn_item_level(item("ppm"), 3) == 12000 && (q.flags & ITEMF_LEVELS),
          "ppm with 2 decimals: %d decimals, value %d, levels %d %d", q.dec, q.value, pn_item_level(item("ppm"), 2),
          pn_item_level(item("ppm"), 3));
  }

  step("their texts in the panel's language");
  int fp = item("fireplace");
  CHECK(!pn_item_help(fp), "help before the ESP sent it");
  run(10);
  const char *help = pn_item_help(fp);
  CHECK(help && strstr(help, "fireplace draws"), "fireplace help: %s", help ? help : "(none)");
  pn_set(K_LANGUAGE, 1);
  run(20);
  CHECK(!strcmp(pitem("inside_humidity").label, "Bad") && !strcmp(pitem("fireplace").label, "Peis") &&
            !strcmp(pitem("temperature").label, "Setpoint"),
        "in Norwegian: '%s', '%s', '%s'", pitem("inside_humidity").label, pitem("fireplace").label,
        pitem("temperature").label);
  CHECK(!strcmp(pn_item_text(item("washer")), "Vasker"), "the text item in Norwegian: '%s'",
        pn_item_text(item("washer")));
  CHECK(!pn_item_help(fp), "the English help kept");
  run(10);
  help = pn_item_help(fp);
  CHECK(help && strstr(help, "peisen trekker"), "fireplace help in Norwegian: %s", help ? help : "(none)");
  int hb = item("humidity_boost");
  pn_item_help(hb);  // another item's help doesn't push the fireplace's out
  run(10);
  CHECK(pn_item_help(hb) && pn_item_help(fp), "the fireplace help wasn't kept after another item's");
  pn_set(K_LANGUAGE, 0);
  run(20);
  CHECK(!strcmp(pitem("fireplace").label, "Fireplace"), "back in English: '%s'", pitem("fireplace").label);

  step("Home Assistant changes the panel's settings");
  panel_screensaver.control(1);
  panel_brightness.control(60);
  panel_led_filter.control(3);
  flexit::panel::set("theme", 2);  // the panel_set action
  run_until(settled, 200);
  run(3);
  CHECK(pn_get(K_SCREENSAVER) == 1, "screensaver %d", pn_get(K_SCREENSAVER));
  CHECK(pn_get(K_BRIGHTNESS) == 60, "brightness %d", pn_get(K_BRIGHTNESS));
  CHECK(pn_get(K_LEDS) == (3 << 2), "leds %d", pn_get(K_LEDS));
  CHECK(pn_get(K_THEME) == 2, "theme %d", pn_get(K_THEME));
  // the panel doesn't echo what the ESP sets, so the ESP shows it itself
  CHECK(panel_screensaver.state == panel_screensaver.options[1] && panel_brightness.state == 60,
        "Home Assistant shows screensaver '%s', brightness %g", panel_screensaver.state.c_str(),
        panel_brightness.state);
  flexit::panel::set("theme", 0);
  flexit::panel::set("screensaver", 3);  // the panel_set action
  flexit::panel::set("idle_brightness", 25);
  flexit::panel::set("night", 1);
  flexit::panel::set("key_lock", 1);
  flexit::panel::set("language", 1);
  size_t leds_at = log_all.size();
  flexit::panel::set("leds", 5);  // the LED selects do those
  run_until(settled, 200);
  run(20);
  CHECK(pn_get(K_SCREENSAVER) == 3 && pn_get(K_IDLE_BRIGHTNESS) == 25 && pn_get(K_NIGHT) == 1 &&
            pn_get(K_KEY_LOCK) == 1 && pn_get(K_LEDS) == (3 << 2),
        "panel_set: screensaver %d, idle brightness %d, night %d, key lock %d, leds %d", pn_get(K_SCREENSAVER),
        pn_get(K_IDLE_BRIGHTNESS), pn_get(K_NIGHT), pn_get(K_KEY_LOCK), pn_get(K_LEDS));
  CHECK(panel_screensaver.idx == 3 && panel_idle_brightness.state == 25 && panel_night.state && panel_key_lock.state,
        "panel_set not in Home Assistant: screensaver '%s', idle brightness %g, night %d, key lock %d",
        panel_screensaver.state.c_str(), panel_idle_brightness.state, panel_night.state, panel_key_lock.state);
  CHECK(!strcmp(pitem("fireplace").label, "Peis"), "panel_set language: '%s'", pitem("fireplace").label);
  CHECK(log_all.find("No panel setting called 'leds'", leds_at) != std::string::npos, "panel_set took leds");
  flexit::panel::set("night", 0);
  flexit::panel::set("key_lock", 0);
  flexit::panel::set("language", 0);
  run_until(settled, 200);
  run(20);

  step("the panel's menu changes them");
  pn_set(K_KEY_LOCK, 1);
  pn_set(K_SCREENSAVER, 4);
  run_until(settled, 200);
  run(3);
  CHECK(panel_key_lock.state, "key lock not on in Home Assistant");
  CHECK(panel_screensaver.state == "Picture and clock", "screensaver %s", panel_screensaver.state.c_str());

  step("items from the panel");
  pn_set(value_key("heater"), 1);
  pn_set(value_key("temperature"), 225);
  pn_set(value_key("clear_alarms"), 1);
  run(30);
  CHECK(heater.state, "heater not on");
  CHECK(!strcmp(pn_item_text(item("heater")), "Heating"), "a switch with words shows '%s'",
        pn_item_text(item("heater")));
  CHECK(fabsf(temperature.state - 22.5f) < 0.01f, "temperature %g", temperature.state);
  CHECK(clear_alarms.presses == 1, "button pressed %d times", clear_alarms.presses);
  inside_humidity.publish_state(61.4f);
  heater_enabled.publish_state(false);
  run(10);
  CHECK(pitem("inside_humidity").value == 61, "sensor %d", pitem("inside_humidity").value);
  CHECK(pitem("heater_enabled").value == 0, "binary sensor %d", pitem("heater_enabled").value);
  CHECK(!strcmp(pn_item_text(item("heater_enabled")), "Blocked"), "binary sensor shown as '%s', not its off_text",
        pn_item_text(item("heater_enabled")));
  pn_set(value_key("heater_enabled"), 1);  // read only: nothing happens
  run(10);
  CHECK(!heater_enabled.state, "binary sensor changed from the panel");

  step("a text item");
  int wi = item("washer");
  ha_washer.publish_state("Vasker ferdig!\xC3\xA5");  // 16 bytes, the 15th byte is half a character
  run(10);
  CHECK(!strcmp(pn_item_text(wi), "Vasker ferdig!"), "a long text shows as '%s'", pn_item_text(wi));
  ha_washer.publish_state("Dishwasher done");  // 15 bytes fit
  run(10);
  CHECK(!strcmp(pn_item_text(wi), "Dishwasher done"), "a 15 byte text shows as '%s'", pn_item_text(wi));
  ha_washer.publish_state("");
  run(10);
  CHECK(!strcmp(pn_item_text(wi), "\xE2\x80\x93"), "an empty text shows as '%s'", pn_item_text(wi));
  ha_washer.publish_state("Washing");
  run(10);
  pn_set(value_key("washer"), 1);  // read only: the panel keeps the text and the ESP hears nothing
  run(10);
  CHECK(!strcmp(pn_item_text(wi), "Washing") && pn_get(value_key("washer")) == ITEM_UNKNOWN,
        "a text item set from the panel: '%s', value %d", pn_item_text(wi), pn_get(value_key("washer")));
  pn_restart();
  run_until([] { return pn_online() && settled(); }, 2000);
  CHECK(!strcmp(pn_item_text(wi), "Washing"), "the text after a panel restart: '%s'", pn_item_text(wi));

  step("comfort");
  pn_set(value_key("fireplace"), 1);
  run(30);
  CHECK(fireplace_mode_switch.state, "fireplace not on");
  pn_set(value_key("fireplace_minutes"), 45);
  run(10);
  CHECK(fireplace_duration_minutes == 45, "fireplace minutes %d", fireplace_duration_minutes);
  fireplace_mode_switch.publish_state(false);
  fireplace_mode_switch.refuse = true;  // like when the unit is stopped
  run(10);
  pn_set(value_key("fireplace"), 1);
  run(5);
  CHECK(pn_get(value_key("fireplace")) == 1, "the panel should show its request at first");
  run(30);
  CHECK(pn_get(value_key("fireplace")) == 0, "the panel still shows fireplace on after the ESP refused");
  pn_set(value_key("humidity_trigger"), 120);
  run(10);
  CHECK(fabsf(humidity_trigger_delta.state - 12.0f) < 0.01f, "humidity trigger %g", humidity_trigger_delta.state);
  pn_set(value_key("co2_control"), 1);
  run(10);
  CHECK(co2_control.state == "Stepped", "co2 control %s", co2_control.state.c_str());
  co2_control.control(3);
  run(10);
  pn_set(value_key("co2_control"), 0);
  run(10);
  CHECK(co2_control.state == "Off", "co2 control %s", co2_control.state.c_str());
  pn_set(value_key("co2_control"), 1);
  run(10);
  CHECK(co2_control.state == "PI", "co2 control %s, should be back to PI", co2_control.state.c_str());
  co2.publish_state(1250);
  run(10);
  CHECK(pitem("co2").value == 1250, "co2 %d", pitem("co2").value);
  co2.publish_state(NAN);  // unavailable
  run(10);
  CHECK(pitem("co2").value == ITEM_UNKNOWN, "co2 %d when unavailable", pitem("co2").value);
  co2.publish_state(1250);
  run(10);
  CHECK(pitem("co2").value == 1250, "co2 %d once it's back", pitem("co2").value);

  step("what only the master panel may do");
  pn_set(K_SETPOINT, 215);
  run(10);
  bool sent = false;
  for (auto &m : server->cmds)
    sent |= m.first == 0x00C && m.second == 215;
  CHECK(sent, "setpoint not written to 0x00C");

  step("actions and notifications");
  flexit::panel::notify("Filter change due in a week", "filter", "warning");
  run_until(settled, 300);
  run(3);
  int icon, level, fresh;
  const char *note = pn_note(&icon, &level, &fresh);
  CHECK(!strcmp(note, "Filter change due in a week") && icon == ICN_FILTER && level == NOTE_WARNING && fresh,
        "note '%s' icon %d level %d", note, icon, level);
  printf("  note: '%s' icon %d level %d\n", note, icon, level);
  pn_action(A_RESET_FILTER);  // Filter > Reset on the panel
  run_until(settled, 100);
  run(5);
  CHECK(panel_action.last == "reset_filter", "event %s", panel_action.last.c_str());

  step("the CU60 restarts: no quiet for the link for a while");
  link_windows = false;
  panel_brightness.control(40);  // ESP -> panel
  pn_set(K_NIGHT, 1);            // panel -> ESP
  run(60);
  CHECK(pn_get(K_BRIGHTNESS) != 40 && !panel_night.state, "something got through without a status broadcast");
  link_windows = true;
  run(20);
  CHECK(pn_get(K_BRIGHTNESS) == 40, "brightness %d after the CU60 came back", pn_get(K_BRIGHTNESS));
  CHECK(panel_night.state, "night mode not on after the CU60 came back");
  CHECK(settled(), "something still waiting after the CU60 came back");

  step("frames and answers lost");
  link_lost_frames = 4;
  link_lost_answers = 4;
  pn_set(K_NIGHT, 0);
  panel_brightness.control(60);
  run(30);
  CHECK(pn_get(K_BRIGHTNESS) == 60, "brightness %d after lost frames", pn_get(K_BRIGHTNESS));
  CHECK(!panel_night.state, "night mode still on after lost answers");
  CHECK(settled(), "something still waiting after lost frames");

  CHECK(panel_picture_status.state == "None", "picture status at the start: '%s'", panel_picture_status.state.c_str());

  step("the panel refuses the link while the CU60 comes back too soon, then lets it back in");
  link_quiet = 10000;  // the CU60 polls 10 ms after its status broadcast
  run(40);
  CHECK(flexit::panel::link::tx.refused, "the ESP didn't take the refusal");
  CHECK(log_all.find("refuses the link") != std::string::npos, "the refusal wasn't logged");
  link_quiet = 14400;
  size_t logged = log_all.size();
  int back = run_until([] { return !flexit::panel::link::tx.refused && flexit::panel::link::online(); }, 4000);
  CHECK(back >= PBUS_REARM, "back after %d cycles, the panel wants %d clean ones", back, PBUS_REARM);
  CHECK(back >= 0 && back < PBUS_REARM + 600, "the link didn't come back in time: %d cycles", back);
  CHECK(log_all.find("lets the link back in", logged) != std::string::npos, "its return wasn't logged");
  CHECK(log_all.find("refuses the link", logged) == std::string::npos, "the refusal was logged again");
  printf("  back after %d cycles (%.0f s)\n", back, back * 0.11);
  step("pictures");
  unsigned long frames_before = link_frames;
  namespace pic = flexit::panel::picture;
  pic::Source &dusk = pic::sources["dusk"];
  dusk.encoding = pic::PALETTE;  // 16 colours first, to test their patches
  draw_dusk(panel_picture_dusk, 250, false);
  panel_picture.control(1);  // the select: "dusk"
  CHECK(picture("whole picture", 6000) >= 0, "picture not shown");
  int pw, ph;
  CHECK(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "panel picture %dx%d", pw, ph);
  pn_shot(outdir, "esp_picture");

  draw_dusk(panel_picture_dusk, 225, false);  // the moon moved
  flexit::panel::picture::show("dusk");
  CHECK(picture("moon moved", 3000) >= 0, "patch not shown");
  pn_shot(outdir, "esp_picture_moon");

  draw_dusk(panel_picture_dusk, 240, false);
  flexit::panel::picture::show("dusk");
  run(5);
  link_lost_answers = 3;  // the ESP misses three of the panel's answers
  CHECK(picture("moved a bit, 3 answers lost", 3000) >= 0, "not shown after lost answers");

  draw_dusk(panel_picture_dusk, 230, false);
  flexit::panel::picture::show("dusk");
  run(5);
  link_lost_frames = 3;  // the panel misses three of the ESP's frames
  CHECK(picture("moved again, 3 frames lost", 3000) >= 0, "not shown after lost frames");

  // Two pieces, the moon and a box lower down. Back to the picture before once the first is in: the panel has
  // neither now, so all of it has to go.
  draw_dusk(panel_picture_dusk, 250, false);
  for (int y = 205; y < 215; y++)
    for (int x = 150; x < 170; x++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = 0x5A3C28;
  flexit::panel::picture::show("dusk");
  int half = run_until([&] { return pic::S.phase == pic::SEND && pic::S.jobs.size() == 1; }, 3000);
  CHECK(half >= 0, "not sent as two pieces");
  draw_dusk(panel_picture_dusk, 230, false);
  flexit::panel::picture::show("dusk");
  CHECK(picture("changed back halfway through", 3000) >= 0, "not shown after a change while sending");

  draw_dusk(panel_picture_dusk, 200, false);
  flexit::panel::picture::show("dusk");
  run(5);
  link_windows = false;  // the CU60 restarts mid-picture
  run(60);
  link_windows = true;
  CHECK(picture("the CU60 restarted mid-picture", 3000) >= 0, "not shown after the CU60 restarted");

  pn_restart();
  CHECK(picture("panel restarted", 6000) >= 0, "picture not sent again after a panel restart");
  CHECK(pn_pic(&pw, &ph) == 2, "no picture after the restart");

  step("pictures: 16 colours or JPEG");
  auto as = [&](const char *name, int format) {
    CHECK(picture(name, 8000) >= 0, "%s not shown", name);
    CHECK(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "%s drawn at %dx%d", name, pw, ph);
    CHECK(pn_pic_format() == format, "%s went as %s", name, pn_pic_format() == PIC_JPEG ? "a JPEG" : "16 colours");
    CHECK(pic::S.shown.data.size() <= PIC_POOL, "%s: %u bytes sent", name, (unsigned) pic::S.shown.data.size());
    CHECK(pic::S.shown.data.capacity() == pic::S.shown.data.size(), "%s: %u bytes kept for %u", name,
          (unsigned) pic::S.shown.data.capacity(), (unsigned) pic::S.shown.data.size());
  };
  size_t warned = log_all.size();
  draw_dusk(panel_picture_dusk, 200, true);  // noise doesn't fit in 16 colours, so it's a JPEG anyway
  flexit::panel::picture::show("dusk");
  as("noise, 16 colours asked for", PIC_JPEG);
  CHECK(log_all.find("too detailed for 16 colours", warned) != std::string::npos, "no warning about the JPEG");
  dusk.encoding = pic::AUTO;
  draw_photo(panel_picture_dusk, 320, 240);
  flexit::panel::picture::show("dusk");
  as("a photo", PIC_JPEG);
  int best = pic::S.shown.quality;
  CHECK(best > 30 && best < 95, "the photo's quality %d", best);
  pn_shot(outdir, "esp_photo");
  dusk.quality = 30;  // no higher than 30
  flexit::panel::picture::show("dusk");
  as("a photo, quality 30 at most", PIC_JPEG);
  CHECK(pic::S.shown.quality == 30, "quality %d", pic::S.shown.quality);
  dusk.quality = 0;
  draw_photo(panel_picture_dusk, 160, 120);  // a small one is blown up to fill the screen
  flexit::panel::picture::show("dusk");
  as("a 160x120 photo", PIC_JPEG);
  draw_flat(panel_picture_dusk);
  flexit::panel::picture::show("dusk");
  as("a flat drawing", PIC_PALETTE);
  dusk.encoding = pic::JPEG;
  flexit::panel::picture::show("dusk");
  as("a flat drawing, JPEG asked for", PIC_JPEG);
  dusk.encoding = pic::AUTO;
  draw_dusk(panel_picture_dusk, 200, false);
  flexit::panel::picture::show("dusk");
  CHECK(picture("dusk, automatic", 6000) >= 0, "dusk not shown");
  printf("  %lu frames for the pictures\n", link_frames - frames_before);

  step("animations");
  panel_animation.control(1);  // the select: "ball"
  auto shown = [] { return panel_animation_status.state.rfind("Shown: ", 0) == 0 && settled(); };
  int ar = run_until(shown, 2000);
  printf("  ball: %s after %d cycles\n", panel_animation_status.state.c_str(), ar);
  CHECK(ar >= 0 && same_anim(0), "the panel's first frame isn't the ESP's");
  for (int f = 1; f <= 8; f++) {
    pn_anim_step(100);
    CHECK(same_anim(f % 8), "frame %d isn't the ESP's", f % 8);
  }
  pn_shot(outdir, "esp_animation");
  // moving it: the actions reach the panel, a step of the panel's motion timer per pn_anim_step
  int ax, ay;
  pn_anim_pos(&ax, &ay);
  CHECK(ax == 250 && ay == 20, "ball at %d,%d, not where the yaml put it", ax, ay);
  panel_move_animation.execute(100, 40, 0);
  ar = run_until(
      [&] {
        pn_anim_pos(&ax, &ay);
        return ax == 100 && ay == 40 && settled();
      },
      100);
  CHECK(ar >= 0, "panel_move_animation didn't move it: at %d,%d", ax, ay);
  CHECK(panel_animation_status.state == "Shown: ball, moved to 100,40", "status '%s'",
        panel_animation_status.state.c_str());
  panel_drift_animation.execute(-20, 0);
  ar = run_until([] { return panel_animation_status.state == "Shown: ball, drifting" && settled(); }, 100);
  pn_anim_step(100);
  pn_anim_pos(&ax, &ay);
  CHECK(ar >= 0 && ax == 99 && ay == 40, "panel_drift_animation: status '%s', at %d,%d after a step",
        panel_animation_status.state.c_str(), ax, ay);
  panel_drift_animation.execute(0, 0);
  ar = run_until([] { return panel_animation_status.state == "Shown: ball" && settled(); }, 100);
  pn_anim_step(100);
  pn_anim_pos(&ax, &ay);
  CHECK(ar >= 0 && ax == 99, "stopping the drift: status '%s', at %d", panel_animation_status.state.c_str(), ax);
  panel_animation.control(0);
  run(3);
  CHECK(!pn_anim_ready(), "the animation is still there after None");
  panel_animation.control(2);  // noise
  ar = run_until(shown, 3000);
  CHECK(ar >= 0 && flexit::panel::anim::A.half && flexit::panel::anim::A.scale == 2 && same_anim(0),
        "noise not shown at half size: %s", panel_animation_status.state.c_str());
  // its drift_x from the yaml, 10 px/s left: half a pixel a step
  pn_anim_step(100);
  pn_anim_pos(&ax, &ay);
  CHECK(panel_animation_status.state == "Shown: noise, drifting" && ax == 15 && ay == 16,
        "noise's drift_x: status '%s', at %d,%d after a step", panel_animation_status.state.c_str(), ax, ay);
  CHECK(flexit::panel::anim::A.data.capacity() == flexit::panel::anim::A.data.size(), "%u bytes kept for %u",
        (unsigned) flexit::panel::anim::A.data.capacity(), (unsigned) flexit::panel::anim::A.data.size());
  pn_restart();
  ar = run_until([&] { return pn_anim_ready() && shown(); }, 3000);
  CHECK(ar >= 0 && same_anim(0), "the animation not sent again after a panel restart");
  pn_anim_pos(&ax, &ay);
  pn_anim_step(100);
  CHECK(ax == 16 && ay == 16 && panel_animation_status.state == "Shown: noise, drifting",
        "after the restart: at %d,%d, status '%s'", ax, ay, panel_animation_status.state.c_str());
  pn_anim_pos(&ax, &ay);
  CHECK(ax == 15, "the drift wasn't sent again after a panel restart: at %d", ax);
  printf("  noise at half size, and again after a panel restart after %d cycles\n", ar);
  // a frame alone is over ANIM_POOL at full size, so it's made at half size right away
  flexit::panel::anim::add("big", {128, 100, 4, 100, 16, 16, 1, [](int f, int x, int y) -> uint32_t {
                                     return 0xFF000000u | (uint32_t) ((x / 8 + y / 8 + f) % 3) * 0x405060u;
                                   }});
  size_t big_at = log_all.size();
  flexit::panel::anim::show("big");
  ar = run_until([] { return panel_animation_status.state == "Shown: big" && settled(); }, 3000);
  CHECK(ar >= 0 && flexit::panel::anim::A.half && same_anim(0) &&
            log_all.find("making it at half size", big_at) == std::string::npos,
        "big: %s, half %d, tried at full size first: %d", panel_animation_status.state.c_str(),
        flexit::panel::anim::A.half, log_all.find("making it at half size", big_at) != std::string::npos);
  panel_animation.control(0);
  run(3);

  step("the weather picture (packages/panel/met.yaml)");
  {
    namespace met = flexit::panel::met;
    std::ifstream in(MET_SAMPLE);  // the first 16 KB of a real answer from api.met.no
    std::stringstream body;
    body << in.rdbuf();
    flexit_panel_time.t.timestamp = 1792409400;  // 2026-10-19 11:30 UTC, half an hour into the first forecast hour
    met::setup(59.9139, 10.7522, "weather", "auto", "auto");
    CHECK(!strcmp(met::url, "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=59.9139&lon=10.7522") &&
              met::modified.empty() && met::due(),
          "first request: %s, If-Modified-Since '%s'", met::url, met::modified.c_str());
    std::string status;
    met::on_status = [&](const std::string &s) { status = s; };
    met::unreachable();  // right after boot the network may not be up: try again at the next check
    CHECK(met::backoff_s == 0 && met::due(), "backed off before MET Norway ever answered");
    for (int i = 0; i < 3; i++)
      met::unreachable();
    CHECK(met::backoff_s == 3600 && !met::due(), "kept trying without backing off");
    met::fetched(200, body.str(), "Mon, 19 Oct 2026 11:34:38 GMT", "Mon, 19 Oct 2026 12:00:00 GMT");
    CHECK(met::not_before == 1792411200 && !met::due(), "asks again before Expires: %u", (unsigned) met::not_before);
    CHECK(
        met::weather.ok && !strcmp(met::weather.symbol, "partlycloudy_day") && fabsf(met::weather.temp - 6.6f) < 0.01f,
        "forecast read as %s, %.1f", met::weather.symbol, met::weather.temp);
    CHECK(met::modified == "Mon, 19 Oct 2026 11:34:38 GMT", "Last-Modified not kept: %s", met::modified.c_str());
    CHECK(met::weather.hours == 12 && met::weather.hour[2].time == met::weather.hour[0].time + 7200,
          "forecast hours: %d", met::weather.hours);
    CHECK(status.find("partlycloudy_day") == 0, "status: %s", status.c_str());
    flexit::panel::picture::show("weather");
    CHECK(picture("weather", 6000) >= 0, "weather picture not shown");
    CHECK(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "weather picture %dx%d", pw, ph);
    run(10);
    PanelItem wx = pitem("weather");
    CHECK(wx.value == 66 && wx.icon == ICN_SUN && !strcmp(wx.label, "Fair") && !(wx.flags & (ITEMF_VIEW | ITEMF_MENU)),
          "the weather now for a tile: %d, icon %d, '%s', flags 0x%02X (want 66, sun, Fair)", wx.value, wx.icon,
          wx.label, wx.flags);
    pn_shot(outdir, "esp_weather");
    size_t changed = log_all.size();
    flexit_panel_time.t.timestamp += 600;  // ten minutes later, the sun and the clouds have moved
    met::redraw();
    CHECK(picture("weather, ten minutes on", 3000) >= 0, "weather picture not shown again");
    CHECK(log_all.find("Picture weather:", changed) != std::string::npos &&
              log_all.find("pieces", changed) != std::string::npos,
          "not sent as changed rows");
    met::with_forecast = true;  // the Panel Weather Forecast switch
    met::redraw();
    CHECK(picture("weather with the forecast", 3000) >= 0, "weather picture with the forecast not shown");
    pn_shot(outdir, "esp_weather_forecast");
    met::with_forecast = false;
    met::fetched(304, "", "", "Mon, 19 Oct 2026 13:00:00 GMT");  // not modified: nothing changes
    CHECK(met::weather.ok && met::modified == "Mon, 19 Oct 2026 11:34:38 GMT", "304 changed the forecast");
    CHECK(met::not_before == 1792414800, "the 304's Expires not kept");
    met::fetched(500, "", "", "");
    CHECK(met::weather.ok && status.find("HTTP 500") != std::string::npos, "an error lost the forecast: %s",
          status.c_str());
    // errors back off an hour, then twice as long, and an answer starts over
    uint32_t now = (uint32_t) flexit_panel_time.t.timestamp;
    CHECK(met::not_before == now + 3600, "no back-off after an error");
    met::fetched(429, "", "", "");
    CHECK(met::not_before == now + 7200 && !met::due(), "back-off not doubled after a 429");
    flexit_panel_time.t.timestamp += 7200;
    CHECK(met::due(), "not due after the back-off");
    met::fetched(304, "", "", "");  // without Expires, an hour
    CHECK(met::backoff_s == 0 && met::not_before == now + 7200 + 3600, "an answer didn't reset the back-off");
    met::unreachable();  // no answer at all backs off too
    CHECK(met::backoff_s == 3600 && status == "Couldn't reach MET Norway", "no back-off without an answer: %s",
          status.c_str());
    met::weather.ok = false;
    met::fetched(203, body.str(), "", "");  // a deprecated product, its data is still good
    CHECK(met::weather.ok && met::backoff_s == 0, "a 203 not taken: %s", status.c_str());
    CHECK(met::http_date("Fri, 31 Dec 1999 23:59:59 GMT") == 946684799 && !met::http_date("soon") &&
              !met::http_date("Mon, 19 Foo 2026 11:34:38 GMT"),
          "HTTP dates");
  }

  step("clearing the picture while an animation is on its way");
  {
    namespace anim = flexit::panel::anim;
    draw_dusk(panel_picture_dusk, 200, false);
    panel_picture.control(1);
    CHECK(picture("dusk before the animation", 6000) >= 0, "dusk not shown");
    panel_animation.control(2);  // noise, big enough to take a while
    int r = run_until([] { return anim::sending() && anim::A.pos > 0 && anim::A.pos < anim::A.data.size(); }, 3000);
    CHECK(r >= 0, "the animation never went out");
    panel_picture.control(0);
    r = run_until([] { return panel_animation_status.state.rfind("Shown: ", 0) == 0 && settled(); }, 3000);
    run(3);
    CHECK(r >= 0 && pn_anim_ready() && pn_pic(&pw, &ph) == 0,
          "after clearing the picture on the animation's way: '%s', animation %d, picture %d",
          panel_animation_status.state.c_str(), pn_anim_ready(), pn_pic(&pw, &ph));
    panel_picture.control(1);
    CHECK(picture("dusk again", 3000) >= 0, "the picture is stuck behind the animation");
  }

  step("the panel's answer to END gets lost");
  {
    static int lose;  // DONE acks to lose
    auto &h = flexit::panel::handlers[K_PIC_ACK];
    auto real = h;
    h = [real](uint32_t a) {
      if (a >> 24 == PIC_DONE && lose > 0)
        lose--;
      else
        real(a);
    };
    lose = 1;
    draw_dusk(panel_picture_dusk, 210, false);
    flexit::panel::picture::show("dusk");
    CHECK(picture("its answer lost", 3000) >= 0 && !lose, "the picture not sent again");
    lose = 1;
    panel_animation.control(1);  // ball
    int r = run_until([] { return panel_animation_status.state == "Shown: ball" && settled(); }, 3000);
    CHECK(r >= 0 && !lose && same_anim(0), "the animation not sent again: %s", panel_animation_status.state.c_str());
    printf("  ball: %s after %d cycles\n", panel_animation_status.state.c_str(), r);
    h = real;
  }

  step("names that aren't there, and none");
  flexit::panel::picture::show("nope");
  flexit::panel::anim::show("nope");
  run(10);
  CHECK(log_all.find("No picture called 'nope'") != std::string::npos &&
            log_all.find("No animation called 'nope'") != std::string::npos,
        "no warnings about the names");
  CHECK(panel_picture.state == "dusk" && panel_picture_status.state.rfind("Shown: dusk", 0) == 0 &&
            pn_pic(&pw, &ph) == 2 && panel_animation.state == "ball" && pn_anim_ready(),
        "an unknown name changed what's shown: '%s' '%s', '%s' '%s'", panel_picture.state.c_str(),
        panel_picture_status.state.c_str(), panel_animation.state.c_str(), panel_animation_status.state.c_str());
  flexit::panel::anim::show("");  // panel_show_animation without a name
  flexit::panel::picture::show("");
  run(20);
  CHECK(panel_picture.state == "None" && panel_picture_status.state == "None" && pn_pic(&pw, &ph) == 0,
        "picture '%s' '%s' after no name", panel_picture.state.c_str(), panel_picture_status.state.c_str());
  CHECK(panel_animation.state == "None" && panel_animation_status.state == "None" && !pn_anim_ready(),
        "animation '%s' '%s' after no name", panel_animation.state.c_str(), panel_animation_status.state.c_str());

  panel_picture.control(1);
  CHECK(picture("dusk", 6000) >= 0, "dusk not shown");
  panel_picture.control(0);  // None
  run(20);
  CHECK(pn_pic(&pw, &ph) == 0, "picture still there after clearing");

  step("the same picture cleared and shown again before the loop runs");
  panel_picture.control(1);
  CHECK(picture("dusk", 6000) >= 0, "dusk not shown");
  pic::clear();
  pic::show("dusk");
  CHECK(picture("dusk after clear and show", 3000) >= 0, "the panel has no picture after clear and show: '%s'",
        panel_picture_status.state.c_str());

  step("an online picture downloaded again while it's being made");
  {
    // Like online_picture.yaml's source: size() is false while a download runs or after one failed, and the pixels
    // are gone. The sim converts a picture in one go, so the download starts from inside pixel().
    static int trigger = 0, asked = 0;  // pixel() calls until the download starts, size() calls while it runs
    static bool downloading = false, failing = false;
    pic::Source flaky;
    flaky.size = [](int &w, int &h) {
      w = panel_picture_dusk.w;
      h = panel_picture_dusk.h;
      if (downloading)
        asked++;
      return !downloading;
    };
    flaky.pixel = [](int x, int y) -> uint32_t {
      if (trigger && !--trigger) {
        downloading = true;
        if (failing)
          pic::failed("flaky");
      }
      return downloading ? 0 : panel_picture_dusk.px[(size_t) y * 320 + x];
    };
    flaky.encoding = pic::PALETTE;
    pic::add("flaky", flaky);
    draw_dusk(panel_picture_dusk, 220, false);
    for (int pass = 0; pass < 2; pass++) {
      failing = pass == 1;
      trigger = 5000;
      asked = 0;
      pic::show("flaky");
      run(30);
      CHECK(panel_picture_status.state.rfind("flaky:", 0) == 0 && asked > 0,
            "while the download %s: '%s', asked for it %d times", failing ? "failed" : "ran",
            panel_picture_status.state.c_str(), asked);
      downloading = false;  // the download came
      pic::changed("flaky");
      CHECK(picture(failing ? "flaky, downloaded again after a failure" : "flaky, downloaded again", 3000) >= 0,
            "not shown after the download came");
    }
  }

  step("a number whose entity answers late");
  {
    static float set_to = 0;
    temperature.optimistic = false;  // like a number whose set_action goes to the unit and reads back later
    temperature.set_action = [](float v) { set_to = v; };
    pn_set(value_key("temperature"), 230);
    run(20);  // 2.2 s, nothing from the entity yet
    CHECK(set_to == 23.0f && pitem("temperature").value == 230, "the panel shows %d after setting 23.0, entity got %g",
          pitem("temperature").value, set_to);
    temperature.publish_state(23.0f);
    run(40);
    CHECK(pitem("temperature").value == 230, "the panel shows %d once the entity answered", pitem("temperature").value);
    temperature.optimistic = true;
    temperature.set_action = nullptr;
  }

  step("the ESP restarts while the panel has a newer unit setting");
  server->hold[0x00D] = 150;  // the ESP's flash copy of the supply min
  pn_set(K_SUPPLY_MIN, 160);  // changed on the panel while the ESP was down
  run(20);
  flexit::panel::link::send(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK);  // what panel::setup() sends
  run(100);
  CHECK(pn_get(K_SUPPLY_MIN) == 160, "the ESP's hello put its old supply min %d on the panel", pn_get(K_SUPPLY_MIN));
  pn_restart();  // the panel asks, and has nothing newer
  c = run_until([] { return pn_online() && flexit::panel::link::connected && settled(); }, 2000);
  run(20);
  CHECK(c >= 0 && pn_get(K_SUPPLY_MIN) == 150, "the panel restarted and has supply min %d, the ESP's is 150",
        pn_get(K_SUPPLY_MIN));

  printf("\n%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
