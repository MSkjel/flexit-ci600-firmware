// Runs the ESP's panel packages, turned into C++ by gen.py, against the panel's own link code on a fake CX60.
// micros() and millis() run on simulated time.
//   build/esp_test OUTDIR [PICTURE.ppm]
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>
#include "gen_panel.cpp"
#include "cx60.h"
#include "met_case.h"
#include "panel_side.h"
#include "sim.h"

namespace panel = flexit::panel;
namespace pic = flexit::panel::picture;
namespace anim = flexit::panel::anim;
namespace met = flexit::panel::met;
namespace live = flexit::panel::met::live;

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

static const char *outdir;    // the screenshots
static std::string live_dir;  // the weather shows', not in the goldens: they move by chance

// ---------------------------------------------------------------- stubs
int log_level = 3;
std::string log_all;
static uint32_t now_us;
static uint32_t micros_step;  // each micros() this much later, to see a loop give way after its slice
uint32_t millis() { return now_us / 1000; }
uint32_t micros() { return now_us += micros_step; }
static uint32_t waited_us;
void delayMicroseconds(uint32_t us) {
  now_us += us;
  waited_us += us;
}
// FreeRTOS: a new task waits until run_task() runs it, to its end. Its stack comes from the fake heap.
static std::vector<std::pair<TaskFunction_t, void *>> tasks;
BaseType_t xTaskCreate(TaskFunction_t f, const char *, uint32_t stack, void *arg, UBaseType_t, TaskHandle_t *) {
  if (stack > ram_block)
    return 0;
  tasks.push_back({f, arg});
  return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 0; }
static void run_task() {
  auto t = tasks.front();
  tasks.erase(tasks.begin());
  t.first(t.second);
}

// ESP-IDF's HTTP client, answering from panel_http
struct HttpClient {
  esp_http_client_config_t config;
  std::string url;  // config's, or where it was moved to
  std::vector<http_request::Header> headers;
  std::shared_ptr<HttpContainer> c;
};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
  return new HttpClient{*config, config->url, {}, nullptr};
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value) {
  c->headers.push_back({key, value});
  return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int) {
  c->c = panel_http.get(c->url, c->headers, {});
  return c->c ? ESP_OK : ESP_FAIL;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) {
  for (const char *h : {"Last-Modified", "Expires"}) {  // as servers write them
    std::string k = h, v = c->c->get_response_header(h[0] == 'L' ? "last-modified" : "expires");
    esp_http_client_event_t e = {HTTP_EVENT_ON_HEADER, c->config.user_data, k.data(), v.data()};
    if (!v.empty())
      c->config.event_handler(&e);
  }
  return (int64_t) c->c->content_length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return c->c->status_code; }
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t c) {
  if (c->c->location.empty())
    return ESP_FAIL;
  c->url = c->c->location;
  return ESP_OK;
}
int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len) {
  return c->c->read((uint8_t *) buf, (size_t) len);
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { return c->c->is_read_complete(); }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) {
  delete c;
  return ESP_OK;
}
esp_err_t esp_crt_bundle_attach(void *) { return ESP_OK; }

static Server server_obj;
Server *server = &server_obj;

// ---------------------------------------------------------------- the fake CX60
// Takes whatever the ESP flags for it, which is the setpoint and fan speeds it sets for the panel. Every 110 ms it
// sends the status broadcast and the ESP and the panel talk in the quiet after it. The component calls the ESP
// 150 us to 1.5 ms after the status frame ended. 30 ms after the status started the CX60 polls the panel, the
// panel replies, and the ESP may send more in the gap after that.
static unsigned long cycles, link_frames, gap_frames;
static bool link_windows = true;     // false while the CX60 restarts and sends nothing
static int link_lost_frames;         // the panel doesn't hear this many of the ESP's frames
static int link_lost_answers;        // the ESP doesn't hear this many of the panel's answers
static uint32_t link_quiet = 14400;  // end of the status broadcast to the poll, the panel refuses under 13 ms
// The latest the panel's deadline after a status broadcast can be with that quiet time
static uint32_t link_end() {
  uint32_t e = link_quiet - PBUS_END_MARGIN_US;
  return e > PBUS_END_MAX_US ? PBUS_END_MAX_US : e < PBUS_END_US ? PBUS_END_US : e;
}
static int link_early, link_late;  // ESP frames too soon after the status, answers that ended after the quiet
static uint8_t STATUS[PBUS_STATUS_LEN], POLL[8], REPLY[13];

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

static void cx60_cycle() {
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
  if (now_us > end + link_end())
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

static void run(int n) {  // n CX60 cycles of 110 ms
  for (int c = 0; c < n; c++) {
    uint32_t t0 = now_us;
    cx60_cycle();
    for (int k = 0; k < 3; k++) {
      now_us += 20000;
      yaml_loop();
      yaml_interval();
    }
    pn_poll();
    for (int r = 0; r < 0x200; r++)
      server->coil[r] = false;  // the CX60 has read everything flagged
    if (++cycles % 9 == 0)
      pn_second();
    now_us = t0 + PBUS_CYCLE_US;
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

static bool settled() { return !panel::link::waiting() && pn_idle(); }

// ---------------------------------------------------------------- helpers
// Whether s was logged since log_all was at from
static bool logged(const char *s, size_t from = 0) { return log_all.find(s, from) != std::string::npos; }

static int times_logged(const char *s, size_t from = 0) {
  int n = 0;
  for (size_t at = from; (at = log_all.find(s, at)) != std::string::npos; at++)
    n++;
  return n;
}

// The panel restarts and the link comes back, then 5 cycles more. The cycles the link took, -1 if it didn't.
static int restart_panel() {
  pn_restart();
  int c = run_until([] { return pn_online() && panel::link::connected && settled(); }, 3000);
  run(5);
  return c;
}

// Index of the ESP's item called name, the same on both ends. -1 if there's none.
static int item(const char *name) {
  for (size_t i = 0; i < panel::items.size(); i++)
    if (panel::items[i].name_ == name)
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

// Place p of the animations as the panel has it, not shown if there's no such place
struct Look : pn_look_t {
  bool shown;
};
static Look look(int p) {
  Look l{};
  l.shown = pn_anim(p, &l);
  return l;
}
static bool anim_ready() {  // the panel shows an animation somewhere
  for (int p = 0; p < ANIM_PLACES; p++)
    if (look(p).shown)
      return true;
  return false;
}
static std::string places() {  // what the panel shows where
  std::string s;
  for (int p = 0; p < ANIM_PLACES; p++) {
    Look l = look(p);
    if (l.shown)
      s += "[" + std::to_string(p) + " " + std::to_string(l.x) + "," + std::to_string(l.y) + "]";
  }
  return s;
}
static bool same_pool() { return pn_anim_used() == anim::used_by_others(nullptr); }

// The animations play for ms, in the panel's motion steps
static void steps(int ms) {
  for (int t = 0; t < ms; t += 50)
    pn_anim_step(50);
}

static bool anim_shown() { return panel_animation_status.state.rfind("Shown: ", 0) == 0 && settled(); }

// Runs until the ESP says the animations are want and the link is quiet. The cycles it took, -1 if never.
static int until_anim_status(const char *want, int max = 3000) {
  return run_until([want] { return panel_animation_status.state == want && settled(); }, max);
}

// Every animation the ESP wants is on the panel
static bool anims_sent() {
  for (auto &a : anim::anims)
    if (!a.name.empty() && a.state == anim::WANT)
      return false;
  return settled();
}

// The next of a random sequence from seed, the same on every machine
static uint32_t lcg(uint32_t &seed) { return seed = seed * 1103515245 + 12345; }

// Pixel x, y of frame f, a hash of them, so every frame differs and none packs small
static uint32_t noise_px(int f, int x, int y) {
  return 0xFF000000u | ((uint32_t) (f * 4096 + y * 64 + x + 1) * 2654435761u) >> 8;
}

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
        uint32_t n = lcg(seed);
        r = n >> 16 & 255;
        g = n >> 8 & 255;
        b = n & 255;
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
      int grain = (int) (lcg(seed) >> 16 & 7) - 4,
          glow = std::max(0, 120 - abs(x * 320 / w - 200) - abs(y * 240 / h - 90));
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

// picks dusk after online_pick_after pixels, mid-way through making the picture
static int online_pick_after;
struct PickingImage : OnlineImage {
  Color get_pixel(int x, int y) const {
    if (online_pick_after && !--online_pick_after)
      pic::show("dusk");
    return Image::get_pixel(x, y);
  }
};

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

// squared difference in Y, Cb and Cr, the units of pjpeg's error
static double ycc_dist(uint32_t a, uint32_t b) {
  double d[3] = {(double) (a >> 16) - (b >> 16), (double) (a >> 8 & 255) - (b >> 8 & 255),
                 (double) (a & 255) - (b & 255)};
  return pic::ycc(d, d);
}

// A JPEG on the panel should be about as far off the ESP's picture as the encoder reckoned
static bool same_jpeg() {
  const auto &p = pic::S.shown;
  std::vector<uint16_t> line(p.w);
  double err = 0;
  for (int y = 0; y < p.h; y++) {
    if (!pn_pic_line(y, line.data()))
      return false;
    for (int x = 0; x < p.w; x++)
      err += ycc_dist(sim_rgb888(line[x]), pic::jpx(x, y));
  }
  err /= (double) pic::S.fw * pic::S.fh;
  printf("  JPEG at quality %d: error %.1f on the panel, the encoder reckoned %.1f (+ RGB565)\n", p.quality, err,
         p.err);
  return err <= p.err * 1.3 + 15;
}

// Row y of a 16-colour picture in RGB565. PackBits, two pixels per byte, left one in the high nibble
static void row565(const pic::Pic &p, int y, uint16_t *out) {
  std::vector<uint8_t> row((p.w + 1) / 2);
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
  for (int x = 0; x < p.w; x++)
    out[x] = sim_rgb565(p.pal[x & 1 ? row[x >> 1] & 15 : row[x >> 1] >> 4]);
}

// The size of the shown picture's JPEG at quality q, made as the ESP makes it
static uint32_t jpeg_size(int q) {
  pjpeg_t e;
  pjpeg_begin(&e, pic::S.shown.w, pic::S.shown.h, q, nullptr, 0);
  while (pjpeg_mcu(&e, [](void *, int x, int y) { return pic::jpx(x, y); }, nullptr)) {
  }
  return pjpeg_end(&e);
}

// Whether the panel draws the ESP's picture. A 16-colour one must match pixel for pixel, which shows the patches
// went in at the right rows. A JPEG must be within its error.
static bool same_picture() {
  if (pn_pic_format() == PIC_JPEG)
    return same_jpeg();
  const auto &p = pic::S.shown;
  std::vector<uint16_t> line(p.w), want(p.w);
  for (int y = 0; y < p.h; y++) {
    row565(p, y, want.data());
    if (!pn_pic_line(y, line.data()))
      return false;
    for (int x = 0; x < p.w; x++)
      if (line[x] != want[x]) {
        printf("  pixel %d,%d: the panel draws %04x, the ESP's picture has %04x\n", x, y, line[x], want[x]);
        return false;
      }
  }
  return true;
}

// Rows the panel draws that are neither a's nor b's, two 16-colour pictures
static int rows_off(const pic::Pic &a, const pic::Pic &b) {
  int off = 0;
  std::vector<uint16_t> line(a.w), ra(a.w), rb(a.w);
  for (int y = 0; y < a.h; y++) {
    pn_pic_line(y, line.data());
    row565(a, y, ra.data());
    row565(b, y, rb.data());
    off += line != ra && line != rb;
  }
  return off;
}

// Runs until the panel shows the ESP's picture, checks that it's the same and says how long it took
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

static int panel_pic() {  // pn_pic()'s state alone
  int w, h;
  return pn_pic(&w, &h);
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
        uint32_t n = lcg(seed);
        int dx = x - size / 2, dy = y - (size - 12 - HEIGHT[f % 8]);
        if (noise)
          a.frames[f][(size_t) y * size + x] = 0xFF000000u | n >> 8;
        else if (dx * dx + dy * dy <= 100)
          a.frames[f][(size_t) y * size + x] = dx * dx + dy * dy > 70 ? 0xFFA04010u : 0xFFF08020u;
      }
}

// The delays of animation name's records, as the panel plays them
static std::vector<int> anim_delays(const char *name) {
  std::vector<int> d;
  const anim::Anim *a = anim::find(name);
  for (size_t at = 0; a && at + 5 <= a->data.size();) {
    const uint8_t *r = a->data.data() + at;
    uint32_t n =
        r[3] ? pinflate(r + 5, (uint32_t) (a->data.size() - at - 5), nullptr, r[3], a->w / 2, (uint32_t) r[3] * r[4], 0)
             : 0;
    if (r[3] && !n)
      break;
    d.push_back(r[0]);
    at += 5 + n;
  }
  return d;
}

// Whether the panel draws frame f of animation `name` at place p as the ESP made it: transparent in the same
// places and palette colours elsewhere. Says where not unless quiet.
static bool same_anim(const char *name, int f, int p = 0, bool quiet = false) {
  anim::Anim *a = anim::find(name);
  if (!a)
    return false;
  auto &A = *a;
  anim::Anim *was = anim::W.a;
  anim::W.a = a;  // px() goes by it
  struct Back {
    anim::Anim *was;
    ~Back() { anim::W.a = was; }
  } back{was};
  // its colour map, made again as count() makes it, since the ESP frees it
  std::vector<uint32_t> hist(4096);
  for (int g = 0; g < A.src->frames; g++)
    for (int y = 0; y < A.h; y++)
      for (int x = 0; x < A.w; x++) {
        uint32_t c = anim::px(g, x, y);
        if (c >> 24 >= 128)
          hist[panel::bin(c)]++;
      }
  uint32_t pal[16] = {};
  std::vector<uint16_t> bins;
  int colours = pic::median_cut(hist, A.transp == 15 ? 15 : 16, pal, bins);
  if (memcmp(pal, A.pal, sizeof pal))
    return false;
  std::vector<uint8_t> lut(4096);
  for (int i = 0; i < 4096; i++)
    lut[i] = (uint8_t) panel::nearest(panel::bin_rgb(i), A.pal, colours);
  for (int y = 0; y < A.h * A.scale; y++) {
    uint8_t line[320 * 3];
    if (pn_anim_line(p, y, A.w, A.scale, A.transp, line) != A.w * A.scale)
      return false;
    for (int x = 0; x < A.w * A.scale; x++) {
      uint32_t c = anim::px(f, x / A.scale, y / A.scale);
      uint16_t want = sim_rgb565(A.pal[lut[panel::bin(c)]]), got = line[3 * x] | line[3 * x + 1] << 8;
      bool see = c >> 24 < 128;
      if (see != (line[3 * x + 2] == 0) || (!see && got != want)) {
        if (!quiet)
          printf("  frame %d pixel %d,%d: the panel draws %04x/%d, the ESP made %04x/%d\n", f, x, y, got,
                 line[3 * x + 2], want, !see);
        return false;
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------- the weather picture and its shows
static const std::string &met_status = panel_weather.state;  // what met.h said last, in met.yaml's Panel Weather

// The first 16 KB of a real answer from api.met.no
static std::string met_sample() {
  std::ifstream in(MET_SAMPLE);
  std::stringstream body;
  body << in.rdbuf();
  return body.str();
}

// The scene of the weather picture the panel has, made again as met.h made it
static const met::Scene &panel_scene() {
  static SceneBuf sc(false);
  met::build(sc.s, met::weather, met::lat, met::lon, (uint32_t) flexit_panel_time.t.timestamp, false, met::utc_offset,
             met::options);
  return sc.s;
}

// A show's case: the show it should be and MET's symbol, the high, middle and low clouds (%), rain (mm), wind (m/s),
// temperature (C) and fog (%), the time, the landscape and the heater (%), how long it runs (s) and how many shots of
// it to take, and the place
struct Live {
  const char *want, *symbol;
  float high, mid, low, rain, wind, temp, fog;
  uint32_t t;
  met::Ground ground;
  int heat, seconds, shots;
  double lat = OSLO_LAT, lon = OSLO_LON;
};

// What a show must never log
static const char *const LIVE_BAD[] = {
    "waves at most", "No room for animation", "copies at most",   "too big",  "no room in the", "No weather animation",
    "no copy",       "doesn't fit",           "the panel didn't", "half size"};

static const char *show_on() { return live::R.show >= 0 ? live::SHOWS[live::R.show].name : "none"; }

// The show's chance events due now
static void chances_now() {
  for (auto &t : live::R.timers)
    if (t.mean)
      t.at = millis();
}

// n cycles, each with one run of met.yaml's 50 ms interval, the shows' timers
static void tick(int n) {
  for (int k = 0; k < n; k++) {
    run(1);
    yaml_every(50);
  }
}

// seconds of the show with the screensaver up, so it plays, and shots of it spread over them as live/NAME_n.ppm
static void play(int seconds, int shots, const std::string &name) {
  static std::set<std::string> taken;
  int every = shots ? seconds * 9 / shots : 0;  // 9 cycles a second
  pn_saver(1);
  for (int k = 0, n = 0; k < seconds * 9; k++) {
    tick(1);
    if (every && k % every == every - 1) {
      std::string shot;
      for (char c : name + "_" + std::to_string(n++))
        shot += isalnum((unsigned char) c) ? (char) tolower(c) : '_';
      CHECK(taken.insert(shot).second, "live/%s.ppm taken twice", shot.c_str());
      sim_shot(live_dir.c_str(), shot.c_str());
    }
  }
  pn_saver(0);
}

// Nothing a show must never log since log_all was at from
static void live_quiet(const std::string &name, size_t from) {
  for (const char *bad : LIVE_BAD)
    CHECK(!logged(bad, from), "%s: '%s' in the log", name.c_str(), bad);
}

// The show for c's weather from its start, c.seconds of it with c.shots shots as live/WANT[_TAG]_n.ppm, and its
// chance events due at once with chances. It's the one c wants, still on at the end, and asks the panel for no more
// than it has.
static void show(const Live &c, const char *tag = "", bool chances = false) {
  size_t from = log_all.size();
  std::string name = std::string(c.want) + (*tag ? "_" : "") + tag;
  met::animate(false);
  met::Weather w;
  w.ok = true;
  w.time = c.t - c.t % 3600;
  snprintf(w.symbol, sizeof w.symbol, "%s", c.symbol);
  w.cloud_high = c.high;
  w.cloud_mid = c.mid;
  w.cloud_low = c.low;
  w.cloud = std::max({c.high, c.mid, c.low});
  w.rain = c.rain;
  w.wind = c.wind;
  w.wind_from = 200;
  w.temp = c.temp;
  w.fog = c.fog;
  w.hour[0] = {w.time, c.temp, met::icon_of(c.symbol)};
  w.hours = 1;
  met::weather = w;
  met::lat = c.lat;
  met::lon = c.lon;
  flexit_panel_time.t.timestamp = c.t;
  met::options.ground = c.ground;
  met::options.heater_pct = c.heat;
  pic::show("weather");
  met::animate(true);
  bool drawn = picture(name.c_str(), 6000) >= 0;
  CHECK(drawn && !strcmp(show_on(), c.want), "%s: the weather shows %s", name.c_str(), show_on());
  if (chances)
    chances_now();
  play(c.seconds, c.shots, name);
  live_quiet(name, from);
  CHECK(!strcmp(show_on(), c.want), "%s: not running on", name.c_str());
}

// ---------------------------------------------------------------- online pictures
// A webcam, and its server: it answers with code, size and type, and counts how often it was asked
static PickingImage webcam;
static struct {
  int code = 200, heads = 0;
  size_t size = 30000;
  std::string type = "image/jpeg";
  std::shared_ptr<HttpContainer> get(const std::string &, const std::vector<http_request::Header> &,
                                     const std::vector<std::string> &) {
    heads++;
    auto c = std::make_shared<HttpContainer>();
    c->status_code = code;
    c->content_length = size;
    c->type = type;
    return c;
  }
} cam_server;

// ---------------------------------------------------------------- the steps
// The ESP boots with a picture and an animation it restored
static void boot() {
  cx60_status(STATUS);
  cx60_poll(POLL, 3);
  cx60_reply(REPLY);
  pn_init();
  yaml_wire();
  inside_humidity.publish_state(54.6f);
  co2.publish_state(912);
  heater_enabled.publish_state(true);
  temperature.publish_state(20.0f);
  ha_washer.publish_state("Washing|Vasker");
  draw_ball(panel_animation_ball, 48, 8, false);
  draw_ball(panel_animation_noise, 64, 8, true);  // too big at full size
  panel_picture.publish_state("dusk");            // restored, shown once on_boot has registered them all
  panel_anim_ball.publish_state(true);            // restored
  yaml_boot();
  CHECK(pic::S.want == "dusk" && anim::find("ball"),
        "the restored picture and animation aren't shown after boot: '%s', %d", pic::S.want.c_str(),
        anim::find("ball") != nullptr);
  pic::clear();
  panel_clear_animation.execute("");
  CHECK(!panel_brightness.has_state() && !panel_idle_brightness.has_state() && !panel_screensaver.has,
        "panel settings known before the panel reported them: brightness %g, idle %g, screensaver '%s'",
        panel_brightness.state, panel_idle_brightness.state, panel_screensaver.state.c_str());
}

static void hello() {
  step("hello, both ways");
  int c = run_until([] { return pn_online() && panel::link::connected && settled(); }, 2000);
  CHECK(c >= 0, "no link after 2000 cycles");
  int hellos = times_logged("Panel says hello");  // times the ESP sent the panel its state
  CHECK(hellos == 1, "the ESP sent its state %d times", hellos);
  CHECK(!link_early, "%d frames sent sooner than %d us after the status", link_early, PBUS_START_MIN_US);
  CHECK(!link_late, "%d answers ended later than %u us after the status", link_late, (unsigned) link_end());
  CHECK(gap_frames > 0, "nothing sent in the gaps after polls");
  CHECK(pn_get(K_SETPOINT) == 200 && pn_reg(0x004) != 20,
        "the ESP sent unit settings it doesn't have: setpoint %d, max supply %d", pn_get(K_SETPOINT), pn_reg(0x004));
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
    CHECK(pitem(n).id == plink_item_id(n), "%s: id %d on the panel, its hash %d", n, pitem(n).id, plink_item_id(n));
  }
  CHECK(!pitem(N_ITEMS - 1).type, "item %d, which nothing sent, has type %d", N_ITEMS - 1, pitem(N_ITEMS - 1).type);
  CHECK(pitem("humidity").value == 55 && pitem("co2").value == 912, "humidity %d, co2 %d", pitem("humidity").value,
        pitem("co2").value);
}

// esp_test OUTDIR PICTURE.ppm: only how the panel draws that picture
static int custom_picture(const char *path) {
  if (!load_ppm(panel_picture_dusk, path)) {
    printf("can't read %s\n", path);
    return 1;
  }
  pic::show("dusk");
  CHECK(picture(path, 6000) >= 0, "picture not shown");
  pn_shot(outdir, "esp_custom");
  printf("  %s/esp_custom.ppm\n", outdir);
  return fails ? 1 : 0;
}

static void items() {
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
        "temperature: %d decimals, step %d, %d..%d, value %d, flags 0x%02X", p.dec, p.step, p.min, p.max, p.value,
        p.flags);
  p = pitem("inside_humidity");
  CHECK(p.value == 55 && (p.flags & (ITEMF_RO | ITEMF_TREND | ITEMF_LEVELS)) == (ITEMF_RO | ITEMF_TREND | ITEMF_LEVELS),
        "sensor value %d flags 0x%02X", p.value, p.flags);
  CHECK(pitem("heater_enabled").flags & ITEMF_RO, "a binary sensor the panel may change");
  CHECK(!(pitem("heater").flags & ITEMF_RO), "a switch the panel can't change");
  CHECK((pitem("washer").flags & ITEMF_RO) && !strcmp(pn_item_text(item("washer")), "Washing"),
        "text item: flags 0x%02X, shows '%s'", pitem("washer").flags, pn_item_text(item("washer")));
  CHECK(!(pitem("clear_alarms").flags & ITEMF_VIEW) && (pitem("clear_alarms").flags & ITEMF_MENU),
        "button flags 0x%02X", pitem("clear_alarms").flags);
}

static void item_decimals() {
  step("item decimals and levels as the panel can take them");
  using panel::Item;
  int many = Item("many_decimals", ITEM_SENSOR).view(false).menu(false).decimals(5).range(0, 10).add();
  int ppm = Item("ppm", ITEM_SENSOR).view(false).menu(false).decimals(2).levels("0, 0, 800, 1200").range(0, 2000).add();
  panel::item_value(many, 1.5f);
  panel::item_value(ppm, 912);
  run_until(settled, 200);
  run(3);
  PanelItem m = pitem("many_decimals"), q = pitem("ppm");
  CHECK(m.dec == 3 && m.value == 1500, "5 decimals asked for: %d on the panel, value %d", m.dec, m.value);
  // 900 ppm is between good and bad, so its levels have to fit the panel's 16 bits
  CHECK(q.dec == 1 && q.value == 9120 && pn_item_level(item("ppm"), 2) == 8000 &&
            pn_item_level(item("ppm"), 3) == 12000 && (q.flags & ITEMF_LEVELS),
        "ppm with 2 decimals: %d decimals, value %d, levels %d %d", q.dec, q.value, pn_item_level(item("ppm"), 2),
        pn_item_level(item("ppm"), 3));
  // the step goes as 16 bits, the value as 32
  int big = Item("big", ITEM_NUMBER).view(false).menu(false).decimals(2).range(0, 1e8f, 1000).add();
  panel::item_value(big, 3e8f);
  run_until(settled, 200);
  run(3);
  PanelItem b = pitem("big");
  CHECK(b.dec == 1 && b.step == 10000 && b.value == INT32_MAX,
        "step 1000 with 2 decimals: %d decimals, step %d, 3e8 is %d", b.dec, b.step, b.value);
  panel::item_value(big, -3e8f);
  run(5);
  CHECK(pitem("big").value == -INT32_MAX, "-3e8 is %d", pitem("big").value);
  size_t at = log_all.size();
  Item("long_label", ITEM_SENSOR).view(false).menu(false).label("Living room temperature|Stue").add();
  run_until(settled, 200);
  run(3);
  CHECK(!strcmp(pitem("long_label").label, "Living room tempera") &&
            logged("'long_label': the panel shows 19 bytes of its label, 'Living room temperature'", at) &&
            !logged("'Stue'", at),
        "a 23 byte label: '%s' on the panel", pitem("long_label").label);
}

static void item_language() {
  step("their texts in the panel's language");
  int fp = item("fireplace");
  CHECK(!pn_item_help(fp), "help before the ESP sent it");
  run(10);
  const char *help = pn_item_help(fp);
  CHECK(help && strstr(help, "fireplace draws"), "fireplace help: %s", help ? help : "(none)");
  pn_set(K_LANGUAGE, 1);
  run(20);
  CHECK(panel_language == 1, "the panel's language not kept for a restart: %d", panel_language);
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
  CHECK(!strcmp(pitem("fireplace").label, "Fireplace") && panel_language == 0, "back in English: '%s', kept %d",
        pitem("fireplace").label, panel_language);
}

static void ha_sets_settings() {
  step("Home Assistant changes the panel's settings");
  panel_screensaver.control(1);
  panel_brightness.control(60);
  panel_led_filter.control(3);
  panel::set("theme", 2);  // the panel_set action
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
  panel::set("theme", 0);
  panel::set("screensaver", 3);  // the panel_set action
  panel::set("idle_brightness", 25);
  panel::set("night", 1);
  panel::set("key_lock", 1);
  panel::set("language", 1);
  size_t leds_at = log_all.size();
  panel::set("leds", 5);  // the LED selects do those
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
  CHECK(logged("No panel setting called 'leds'", leds_at), "panel_set took leds");
  panel::set("night", 0);
  panel::set("key_lock", 0);
  panel::set("language", 0);
  run_until(settled, 200);
  run(20);
}

static void menu_sets_settings() {
  step("the panel's menu changes them");
  pn_set(K_KEY_LOCK, 1);
  pn_set(K_SCREENSAVER, 4);
  run_until(settled, 200);
  run(3);
  CHECK(panel_key_lock.state, "key lock not on in Home Assistant");
  CHECK(panel_screensaver.state == "Picture and clock", "screensaver %s", panel_screensaver.state.c_str());
}

static void items_from_panel() {
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
}

static void ha_switch() {
  step("a Home Assistant switch");
  int dl = item("door_lock");
  CHECK(pitem(dl).value == ITEM_UNKNOWN, "a lock Home Assistant hasn't told us about shows '%s'", pn_item_text(dl));
  pn_set(value_key("door_lock"), 1);  // HA isn't there yet
  run(150);
  CHECK(pitem(dl).value == ITEM_UNKNOWN, "locked before Home Assistant had a state shows '%s'", pn_item_text(dl));
  ha_door_lock_state.publish_state("unlocked");
  run(10);
  CHECK(pitem(dl).value == 0 && !strcmp(pn_item_text(dl), "Unlocked"), "unlocked shows '%s'", pn_item_text(dl));
  pn_set(value_key("door_lock"), 1);  // locked on the panel, the lock takes its time
  run(5);
  ha_door_lock_state.publish_state("locking");
  run(60);
  CHECK(pitem(dl).value == 1, "the panel went back to '%s' while the lock was locking", pn_item_text(dl));
  ha_door_lock_state.publish_state("locked");
  run(10);
  CHECK(pitem(dl).value == 1 && !strcmp(pn_item_text(dl), "Locked"), "locked shows '%s'", pn_item_text(dl));
  pn_set(value_key("door_lock"), 0);  // the lock never follows
  run(150);
  CHECK(pitem(dl).value == 1, "an unlock the lock never did shows '%s'", pn_item_text(dl));
  ha_door_lock_state.publish_state("unavailable");
  run(10);
  CHECK(pitem(dl).value == ITEM_UNKNOWN, "an unavailable lock shows '%s'", pn_item_text(dl));
}

static void text_item() {
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
  pn_restart();  // without restart_panel()'s 5 cycles more, which would move the frames of the animation shots
  run_until([] { return pn_online() && settled(); }, 2000);
  CHECK(!strcmp(pn_item_text(wi), "Washing"), "the text after a panel restart: '%s'", pn_item_text(wi));
}

static void ha_entities() {
  step("Home Assistant entities (ha_sensor, ha_binary_sensor and ha_switch)");
  ha_hot_water.publish_state(61.4f);
  ha_front_door.publish_state(true);
  ha_door_lock_state.publish_state("locked");
  run(20);
  CHECK(pitem("hot_water").value == 61 && pitem("front_door").value == 1 && pitem("door_lock").value == 1,
        "hot water %d, front door %d, door lock %d", pitem("hot_water").value, pitem("front_door").value,
        pitem("door_lock").value);
  ha_actions.clear();
  pn_set(value_key("door_lock"), 0);
  run(20);
  CHECK(ha_actions.size() == 1 && ha_actions[0].action == "lock.unlock" &&
            ha_actions[0].data["entity_id"] == "lock.front_door",
        "unlocking from the panel asked Home Assistant for %s",
        ha_actions.empty() ? "nothing" : ha_actions[0].action.c_str());
  ha_door_lock_state.publish_state("unlocked");
  run(10);
  CHECK(pitem("door_lock").value == 0, "the lock shows %d once it's unlocked", pitem("door_lock").value);
}

static void comfort() {
  step("comfort");
  pn_set(value_key("fireplace"), 1);
  run(30);
  CHECK(fireplace_mode_switch.state, "fireplace not on");
  pn_set(value_key("fireplace_minutes"), 45);
  run(10);
  CHECK(fireplace_duration.state == 45, "fireplace minutes %g", fireplace_duration.state);
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
}

static void master_only() {
  step("what only the master panel may do");
  pn_set(K_SETPOINT, 215);
  run(10);
  bool sent = false;
  for (auto &m : server->cmds)
    sent |= m.first == 0x00C && m.second == 215;
  CHECK(sent, "setpoint not written to 0x00C");
}

static void actions_and_notes() {
  step("actions and notifications");
  panel::notify("Filter change due in a week", "filter", "warning");
  panel::loop();  // queues it
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
  CHECK(panel_action.last == "reset_filter", "Filter > Reset on the panel is event '%s' in Home Assistant",
        panel_action.last.c_str());
}

static void cx60_restarts() {
  step("the CX60 restarts: no quiet for the link for a while");
  link_windows = false;
  panel_brightness.control(40);  // ESP -> panel
  pn_set(K_NIGHT, 1);            // panel -> ESP
  run(60);
  CHECK(pn_get(K_BRIGHTNESS) != 40 && !panel_night.state, "something got through without a status broadcast");
  link_windows = true;
  run(20);
  CHECK(pn_get(K_BRIGHTNESS) == 40, "brightness %d after the CX60 came back", pn_get(K_BRIGHTNESS));
  CHECK(panel_night.state, "night mode not on after the CX60 came back");
  CHECK(settled(), "something still waiting after the CX60 came back");
}

static void frames_lost() {
  step("frames and answers lost");
  link_lost_frames = 4;
  link_lost_answers = 4;
  pn_set(K_NIGHT, 0);
  panel_brightness.control(60);
  run(30);
  CHECK(pn_get(K_BRIGHTNESS) == 60, "brightness %d after lost frames", pn_get(K_BRIGHTNESS));
  CHECK(!panel_night.state, "night mode still on after lost answers");
  CHECK(settled(), "something still waiting after lost frames");
}

static void link_refused() {
  step("the panel refuses the link while the CX60 comes back too soon, then lets it back in");
  link_quiet = 10000;  // the CX60 polls 10 ms after its status broadcast
  run(40);
  CHECK(panel::link::tx.refused, "the ESP didn't take the refusal");
  CHECK(logged("refuses the link"), "the refusal wasn't logged");
  link_quiet = 14400;
  size_t from = log_all.size();
  int back = run_until([] { return !panel::link::tx.refused && panel::link::online(); }, 4000);
  CHECK(back >= PBUS_REARM, "back after %d cycles, the panel wants %d clean ones", back, PBUS_REARM);
  CHECK(back >= 0 && back < PBUS_REARM + 600, "the link didn't come back in time: %d cycles", back);
  CHECK(logged("lets the link back in", from), "its return wasn't logged");
  CHECK(!logged("refuses the link", from), "the refusal was logged again");
  printf("  back after %d cycles (%.0f s)\n", back, back * 0.11);
}

static void pictures() {
  step("pictures");
  CHECK(panel_picture_status.state == "None", "picture status at the start: '%s'", panel_picture_status.state.c_str());
  unsigned long frames_before = link_frames;
  pic::Source &dusk = pic::sources["dusk"];
  dusk.encoding = pic::PALETTE;  // 16 colours first, to test their patches
  draw_dusk(panel_picture_dusk, 250, false);
  panel_picture.control(1);  // the select: "dusk"
  CHECK(picture("whole picture", 6000) >= 0, "picture not shown");
  int pw, ph;
  CHECK(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "panel picture %dx%d", pw, ph);
  pn_shot(outdir, "esp_picture");

  draw_dusk(panel_picture_dusk, 225, false);  // the moon moved
  pic::show("dusk");
  CHECK(picture("moon moved", 3000) >= 0, "patch not shown");
  pn_shot(outdir, "esp_picture_moon");

  draw_dusk(panel_picture_dusk, 240, false);
  pic::show("dusk");
  run(5);
  link_lost_answers = 3;  // the ESP misses three of the panel's answers
  CHECK(picture("moved a bit, 3 answers lost", 3000) >= 0, "not shown after lost answers");

  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  run(5);
  link_lost_frames = 3;  // the panel misses three of the ESP's frames
  CHECK(picture("moved again, 3 frames lost", 3000) >= 0, "not shown after lost frames");

  // The moon and a box just below it go as one piece, the rows between along
  draw_dusk(panel_picture_dusk, 250, false);
  for (int y = 88; y < 94; y++)
    for (int x = 150; x < 170; x++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = 0x5A3C28;
  size_t one = log_all.size();
  pic::show("dusk");
  CHECK(picture("the moon and a box below it", 3000) >= 0 && logged("Picture dusk: 1 pieces", one),
        "the moon and a box below it not sent as one piece");

  // Two pieces, the box gone and a box lower down. Back to the picture before once the first is in.
  draw_dusk(panel_picture_dusk, 250, false);
  for (int y = 205; y < 215; y++)
    for (int x = 150; x < 170; x++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = 0x5A3C28;
  pic::show("dusk");
  int half = run_until([&] { return pic::S.phase == pic::SEND && pic::S.jobs.size() == 1; }, 3000);
  CHECK(half >= 0, "not sent as two pieces");
  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  CHECK(picture("changed back halfway through", 3000) >= 0, "not shown after a change while sending");

  // Two pieces again, the second a long one. Another picture while it's on its way: the panel has the first and drops
  // the rest once another comes, so only rows go, no BEGIN.
  draw_dusk(panel_picture_dusk, 250, false);
  uint32_t seed = 1;
  for (int y = 160; y < 200; y++)
    for (int x = 0; x < 320; x++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = pic::S.shown.pal[lcg(seed) >> 16 & 1];
  size_t two = log_all.size();
  pic::show("dusk");
  half = run_until([&] { return pic::S.jobs.size() == 1 && pic::S.t.head && !pic::S.t.ended; }, 3000);
  CHECK(half >= 0 && logged("Picture dusk: 2 pieces", two), "not sent as two pieces");
  size_t rows_only = log_all.size();
  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  CHECK(picture("changed between pieces", 3000) >= 0, "not shown after a change between pieces");
  CHECK(logged("Picture dusk: 1 pieces", rows_only), "a change between pieces sent it all");
  auto planned = [&] { return run_until([&] { return pic::S.phase == pic::SEND && !pic::S.redo; }, 100) >= 0; };

  // A BEGIN on its way when it changes: the panel has dropped its picture for it, so all of it goes again
  panel_picture_dusk.h = 200;
  panel_picture_dusk.px.resize(320 * 200);
  pic::show("dusk");
  CHECK(run_until([&] { return pic::S.phase == pic::SEND && pic::S.t.head; }, 100) >= 0 && pic::S.jobs.front().full,
        "a picture of another size not sent whole");
  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  CHECK(planned() && pic::S.jobs.front().full, "a change during a BEGIN sends rows the panel doesn't have");
  CHECK(picture("changed during a BEGIN", 3000) >= 0, "not shown after a change during a BEGIN");

  // New colours in rows far apart, so the palette changes. The panel takes it at the piece's END, so they go as one
  // piece and it never draws a row in colours that aren't its own.
  {
    pic::Pic was = pic::S.shown;
    for (int y = 0; y < 240; y++)
      for (int x = 100; x < 200; x++)
        if ((y >= 20 && y < 30) || (y >= 200 && y < 210))
          panel_picture_dusk.px[(size_t) y * 320 + x] = 0xE01020;
    pic::show("dusk");
    CHECK(planned() && pic::S.jobs.size() == 1 && pic::S.jobs[0].pal && !pic::S.jobs[0].full &&
              memcmp(was.pal, pic::S.made.pal, sizeof was.pal),
          "new colours in rows far apart: %d pieces", (int) pic::S.jobs.size());
    pic::Pic made = pic::S.made;
    int off = 0;
    for (int c = 0; c < 100 && pic::S.phase == pic::SEND; c++) {
      run(1);
      off = std::max(off, rows_off(was, made));
    }
    CHECK(!off, "new colours in rows far apart: the panel drew %d rows that are neither picture", off);
    CHECK(picture("new colours in rows far apart", 3000) >= 0, "not shown");
  }

  // A picture in 16 colours that aren't on the 4-bit grid shows them as they are
  {
    static const uint32_t COLOURS[4] = {0xFFF6D2, 0x123456, 0x7A1B9C, 0x0D0E0F};
    for (int y = 0; y < 240; y++)
      for (int x = 0; x < 320; x++)
        panel_picture_dusk.px[(size_t) y * 320 + x] = COLOURS[(x / 80 + y / 60) % 4];
    pic::show("dusk");
    CHECK(picture("4 colours off the grid", 3000) >= 0, "not shown");
    int exact = 0;
    for (uint32_t c : COLOURS)
      exact += std::count(pic::S.shown.pal, pic::S.shown.pal + 16, c) > 0;
    CHECK(exact == 4 && pic::S.shown.err < 1e-6, "%d of the 4 colours exactly, error %g", exact, pic::S.shown.err);
  }

  draw_dusk(panel_picture_dusk, 200, false);
  pic::show("dusk");
  run(5);
  link_windows = false;  // the CX60 restarts mid-picture
  run(60);
  link_windows = true;
  CHECK(picture("the CX60 restarted mid-picture", 3000) >= 0, "not shown after the CX60 restarted");

  pn_restart();
  CHECK(picture("panel restarted", 6000) >= 0, "picture not sent again after a panel restart");
  CHECK(panel_pic() == 2, "no picture after the restart");
  printf("  %lu frames for these pictures\n", link_frames - frames_before);
}

static void pictures_jpeg() {
  step("pictures: 16 colours or JPEG");
  unsigned long frames_before = link_frames;
  pic::Source &dusk = pic::sources["dusk"];
  int pw, ph;
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
  pic::show("dusk");
  as("noise, 16 colours asked for", PIC_JPEG);
  CHECK(logged("too detailed for 16 colours", warned), "no warning about the JPEG");
  dusk.encoding = pic::AUTO;
  draw_photo(panel_picture_dusk, 320, 240);
  size_t tries = log_all.size();
  pic::show("dusk");
  as("a photo", PIC_JPEG);
  int best = pic::S.shown.quality;
  CHECK(best > 30 && best < 95, "the photo's quality %d", best);
  CHECK(jpeg_size(best) == pic::S.shown.data.size() && jpeg_size(best + 1) > PIC_POOL,
        "quality %d isn't the best that fits: %u bytes, %u at %d", best, (unsigned) jpeg_size(best),
        (unsigned) jpeg_size(best + 1), best + 1);
  int encodes = 0;
  tries = log_all.find("as a JPEG at quality", tries);
  CHECK(tries != std::string::npos && sscanf(log_all.c_str() + tries, "as a JPEG at quality %*d (%d", &encodes) == 1 &&
            encodes <= 3,
        "the photo took %d JPEGs to find its quality", encodes);
  pn_shot(outdir, "esp_photo");
  draw_photo(panel_picture_dusk, 320, 180);  // 16:9, padded to whole MCUs: 4 rows above, 8 below
  pic::show("dusk");
  CHECK(picture("a 16:9 photo", 8000) >= 0 && pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 192 && pic::S.search.oy == 4,
        "a 16:9 photo drawn at %dx%d, %d rows down", pw, ph, pic::S.search.oy);
  draw_photo(panel_picture_dusk, 640, 480);  // averaged 2x2 for the JPEG
  pic::show("dusk");
  as("a 640x480 photo", PIC_JPEG);
  int avg = 0;
  for (int y = 0; y < 240; y += 7)
    for (int x = 0; x < 320; x += 5) {
      uint32_t sum[3] = {};
      for (int k = 0; k < 4; k++) {
        uint32_t c = panel_picture_dusk.px[(size_t) (2 * y + k / 2) * 640 + 2 * x + k % 2];
        for (int i = 0; i < 3; i++)
          sum[i] += c >> (16 - 8 * i) & 255;
      }
      avg += pic::jpx(x, y) == ((sum[0] + 2) / 4 << 16 | (sum[1] + 2) / 4 << 8 | (sum[2] + 2) / 4);
    }
  CHECK(avg == 35 * 64, "%d of %d pixels of a 640x480 photo the mean of the 4 they cover", avg, 35 * 64);
  draw_photo(panel_picture_dusk, 320, 240);
  dusk.quality = 30;  // no higher than 30
  pic::show("dusk");
  as("a photo, quality 30 at most", PIC_JPEG);
  CHECK(pic::S.shown.quality == 30, "quality %d", pic::S.shown.quality);
  dusk.quality = 0;
  draw_photo(panel_picture_dusk, 160, 120);  // a small one is blown up to fill the screen
  pic::show("dusk");
  as("a 160x120 photo", PIC_JPEG);
  draw_flat(panel_picture_dusk);
  pic::show("dusk");
  as("a flat drawing", PIC_PALETTE);
  dusk.encoding = pic::JPEG;
  pic::show("dusk");
  as("a flat drawing, JPEG asked for", PIC_JPEG);
  dusk.encoding = pic::AUTO;
  draw_dusk(panel_picture_dusk, 200, false);
  pic::show("dusk");
  CHECK(picture("dusk, automatic", 6000) >= 0, "dusk not shown");
  printf("  %lu frames for these pictures\n", link_frames - frames_before);
}

// ---- animations
static void animations() {
  step("animations");
  panel_anim_ball.turn_on();  // the switch
  int ar = run_until(anim_shown, 2000);
  printf("  ball: %s after %d cycles\n", panel_animation_status.state.c_str(), ar);
  CHECK(ar >= 0 && same_anim("ball", 0), "the panel's first frame isn't the ESP's");
  for (int f = 1; f <= 8; f++) {
    pn_anim_step(100);
    CHECK(same_anim("ball", f % 8), "frame %d isn't the ESP's", f % 8);
  }
  pn_shot(outdir, "esp_animation");
}

// The actions reach the panel, pn_anim_step(100) is two steps of the panel's motion timer
static void anim_moving() {
  step("animations moved and drifting");
  Look l = look(0);
  CHECK(l.x == 250 && l.y == 20, "ball at %d,%d, not where the yaml put it", l.x, l.y);
  panel_move_animation.execute("ball", 0, 100, 40, 0, "none");
  int ar = run_until(
      [&] {
        l = look(0);
        return l.x == 100 && l.y == 40 && settled();
      },
      100);
  CHECK(ar >= 0, "panel_move_animation didn't move it: at %d,%d", l.x, l.y);
  CHECK(panel_animation_status.state == "Shown: ball, moved to 100,40", "status '%s'",
        panel_animation_status.state.c_str());
  size_t clamped = log_all.size();
  panel_move_animation.execute("ball", 0, 100, 40, 70000, "none");  // ms only has 16 bits
  run(3);
  CHECK(logged("over 70000 ms is out of range, it goes to 100,40 over 65535 ms", clamped),
        "no warning about the glide time");
  panel_drift_animation.execute("ball", 0, -20, 0);
  ar = until_anim_status("Shown: ball, drifting", 100);
  pn_anim_step(100);
  l = look(0);
  CHECK(ar >= 0 && l.x == 98 && l.y == 40, "panel_drift_animation: status '%s', at %d,%d after a step",
        panel_animation_status.state.c_str(), l.x, l.y);
  panel_drift_animation.execute("ball", 0, 0, 0);
  ar = until_anim_status("Shown: ball", 100);
  pn_anim_step(100);
  l = look(0);
  CHECK(ar >= 0 && l.x == 98, "stopping the drift: status '%s', at %d", panel_animation_status.state.c_str(), l.x);
  panel_move_animation.execute("ball", 1, 0, 0, 0, "none");  // no such copy
  run(3);
  CHECK(logged("panel_move_animation: no copy 1 of animation 'ball' shown"), "no warning about the copy");
}

// More places: a copy in step, and one with a frame of its own two frames ahead
static void anim_copies() {
  step("animation copies");
  panel_place_animation.execute("ball", 1, 10, 150, false, 0);
  panel_place_animation.execute("ball", 2, 200, 150, true, 5);  // the ball is on frame 2 now, so 3 frames apart
  int ar = until_anim_status("Shown: ball (3 copies)", 100);
  CHECK(ar >= 0, "the copies: status '%s'", panel_animation_status.state.c_str());
  Look a = look(1), b = look(2);
  CHECK(a.x == 10 && a.y == 150 && b.x == 200 && b.y == 150, "copies at %d,%d and %d,%d", a.x, a.y, b.x, b.y);
  const anim::Anim *ball = anim::find("ball");
  CHECK(pn_anim_used() == ball->frame_bytes() * 2 + ball->data.size(),
        "the panel's pool holds %u bytes, the ESP thinks %u", pn_anim_used(),
        (unsigned) (ball->frame_bytes() * 2 + ball->data.size()));
  // copy 1 shows what copy 0 shows, copy 2 started on frame 5 when it was placed, so it's out of step
  int apart = 0;
  for (int i = 0; i < 8; i++, pn_anim_step(100))
    for (int f = 0; f < 8; f++)
      if (same_anim("ball", f, 0)) {
        CHECK(same_anim("ball", f, 1), "copy 1 isn't in step at frame %d", f);
        apart += !same_anim("ball", f, 2);
        break;
      }
  CHECK(apart, "copy 2 is in step with copy 0");
  panel_drift_animation.execute("ball", 2, 0, 30);
  run(3);
  pn_anim_step(100);
  a = look(0);
  b = look(2);
  CHECK(b.x == 200 && b.y == 153 && a.x == 98 && a.y == 40, "only copy 2 drifts: %d,%d and %d,%d", b.x, b.y, a.x, a.y);
  pn_shot(outdir, "esp_animation_copies");
}

// Copy 2 turned and flipped as it drifts, a flip keeps a turn going, a spin and a stop, and a panel restart puts it
// back as it was. The panel turns it while it's shown.
static void anim_turning() {
  step("an animation turned and flipped");
  panel_glide_animation.execute("ball", 2, "turn", 450, 0, "none");  // 90
  panel_flip_animation.execute("ball", 2, true, false);
  run(3);
  Look l = look(2), l0 = look(0);
  CHECK(l.shown && l.deg == 90 && l.flip == ANIM_FLIP_X && l0.shown && !l0.deg && !l0.flip,
        "turned and flipped: %d degrees, flip %d", l.deg, l.flip);
  panel_glide_animation.execute("ball", 2, "turn", 0, 2000, "none");
  run(3);
  steps(1000);
  l = look(2);
  CHECK(l.shown && l.deg > 30 && l.deg < 60, "halfway through a turn: %d degrees", l.deg);
  panel_flip_animation.execute("ball", 2, false, true);
  run(3);
  steps(2100);
  l = look(2);
  CHECK(l.shown && l.deg == 0 && l.flip == ANIM_FLIP_Y, "a flip halfway through a turn: %d, flip %d", l.deg, l.flip);
  panel_spin_animation.execute("ball", 2, -180);
  run(3);
  steps(500);
  l = look(2);
  CHECK(l.shown && l.deg > 180 && l.deg < 360, "spinning back: %d degrees", l.deg);
  panel_spin_animation.execute("ball", 2, 0);
  run(3);
  int stopped = look(2).deg;
  steps(1000);
  l = look(2);
  CHECK(l.shown && l.deg == stopped, "the spin didn't stop: %d then %d", stopped, l.deg);
  panel_glide_animation.execute("ball", 2, "turn", 180, 0, "none");
  run(3);
  restart_panel();
  l = look(2);
  CHECK(l.shown && l.deg == 180 && l.flip == ANIM_FLIP_Y, "after a panel restart: %d, flip %d", l.deg, l.flip);
  CHECK(l.shown && l.opa == 0, "placed again it fades in: opacity %d", l.opa);
  steps(400);
  l = look(2);
  CHECK(l.shown && l.opa == 1000, "faded in to %d", l.opa);
}

// Copy 2 sized, faded and wandering, and back as it was after a restart
static void anim_wandering() {
  step("an animation sized, faded and wandering");
  panel_glide_animation.execute("ball", 2, "size", 150, 0, "none");
  panel_glide_animation.execute("ball", 2, "opacity", 50, 1000, "none");
  run(3);
  steps(500);
  Look l = look(2);
  CHECK(l.shown && l.size == 1500 && l.opa > 500 && l.opa < 1000, "size %d, opacity %d halfway", l.size, l.opa);
  steps(600);
  l = look(2);
  CHECK(l.shown && l.opa == 500, "opacity %d after the fade", l.opa);
  panel_area_animation.execute("ball", 2, 100, 100, 300, 230, "bounce", "bounce");
  panel_wander_animation.execute("ball", 2, 20, 60, 0, 180, 300, 900, 0, 0);
  panel_face_animation.execute("ball", 2, 0, true, 0, 0, false);
  run(3);
  const anim::Anim *ball = anim::find("ball");
  int bw = ball->w * ball->scale, bh = ball->h * ball->scale, zw = (bw * 1500 + 500) / 1000,
      zh = (bh * 1500 + 500) / 1000;
  Look at;  // where it was last
  auto wanders = [&](int n) {
    bool inside = true;
    int lo = 999, hi = -999;
    for (int i = 0; i < n; i++) {
      steps(50);
      at = look(2);
      int cx = at.x + (bw - zw) / 2, cy = at.y + (bh - zh) / 2;
      inside &= cx >= 100 && cx + zw <= 300 && cy >= 100 && cy + zh <= 230;
      lo = std::min(lo, cx);
      hi = std::max(hi, cx);
    }
    return inside && hi - lo > 40;
  };
  CHECK(wanders(200) && at.shown && at.deg == 0 && ball->copies[2].doing == ", wandering",
        "wandering in its area: at %d,%d, %d degrees", at.x, at.y, at.deg);
  restart_panel();
  steps(400);
  l = look(2);
  CHECK(l.shown && l.size == 1500 && l.opa == 500 && wanders(100), "after a panel restart: size %d, opacity %d", l.size,
        l.opa);
}

// Copy 1 waved, mapped and its frames paused, and all of it back after a panel restart
static void anim_waved() {
  step("an animation waved, mapped and paused");
  int p1 = anim::find("ball")->copies[1].place;
  panel_wave_animation.execute("ball", 1, "x", 5, 5, 0, 0, 0, 0, 0, false, "none");
  panel_map_animation.execute("ball", 1, "opacity", 50, 50, 100, 100);
  panel_frame_rate_animation.execute("ball", 0);
  run(3);
  steps(100);
  int f0 = -1;
  for (int f = 0; f < 8 && f0 < 0; f++)
    if (same_anim("ball", f, p1))
      f0 = f;
  steps(500);
  Look l = look(p1);
  CHECK(l.shown && l.x == 15 && l.opa == 500 && f0 >= 0 && same_anim("ball", f0, p1),
        "waved, mapped and paused: at %d, opacity %d, frame %d", l.x, l.opa, f0);
  restart_panel();
  steps(400);
  l = look(p1);
  CHECK(l.shown && l.x == 15 && l.opa == 500 && same_anim("ball", 0, p1), "after a panel restart: at %d, opacity %d",
        l.x, l.opa);
  steps(500);
  CHECK(same_anim("ball", 0, p1), "the frames went on after a restart");
}

static void anim_turned_down() {
  step("animation actions the ESP turns down");
  const anim::Anim *ball = anim::find("ball");
  int p1 = ball->copies[1].place;
  size_t warned = log_all.size();
  panel_wave_animation.execute("ball", 1, "colour", 0, 1, 0, 0, 0, 0, 0, false, "none");
  panel_glide_animation.execute("ball", 1, "size", 500, 0, "none");
  panel_map_animation.execute("ball", 1, "x", 0, 0, 0, 0);
  for (int i = 0; i < ANIM_WAVES; i++)  // small ones, a pixel, a degree or 99 %
    panel_wave_animation.execute("ball", i / ANIM_CHANNELS, anim::CHANNELS[i % ANIM_CHANNELS],
                                 anim::no_change(i % ANIM_CHANNELS) ? 99 : 1,
                                 anim::no_change(i % ANIM_CHANNELS) ? 99 : 1, 0, 0, 0, 0, 0, false, "none");
  panel_wave_animation.execute("ball", 2, "size", 90, 90, 0, 0, 0, 0, 0, false, "none");
  run(3);
  for (const char *w :
       {"'colour' isn't one of x, y, turn, size, opacity, speed", "size 500 over 0 ms, it takes 10..400",
        "x and y don't follow where it is", "the panel runs 12 waves at most"})
    CHECK(logged(w, warned), "no warning '%s'", w);
  for (int i = 0; i < ANIM_WAVES; i++) {
    int c = i % ANIM_CHANNELS;
    panel_wave_animation.execute("ball", i / ANIM_CHANNELS, anim::CHANNELS[c], anim::no_change(c), anim::no_change(c),
                                 0, 0, 0, 0, 0, false, "none");
  }
  panel_map_animation.execute("ball", 1, "opacity", 100, 100, 100, 100);
  panel_frame_rate_animation.execute("ball", 100);
  run(3);
  steps(100);
  Look l = look(p1);
  CHECK(l.shown && l.x == 10 && l.size == 1000 && l.opa == 1000, "back to no change: at %d, size %d, opacity %d", l.x,
        l.size, l.opa);
  l = look(ball->copies[0].place);
  CHECK(l.shown && l.size == 1000 && l.opa == 1000 && l.deg == 0,
        "copy 0 back to no change: size %d, opacity %d, %d degrees", l.size, l.opa, l.deg);
}

// Copy 3 plays one round from its first frame and goes, and the ESP hears so
static void anim_one_round() {
  step("an animation copy that plays one round");
  panel_place_animation.execute("ball", 3, 20, 20, true, 0);
  panel_rounds_animation.execute("ball", 3, 1);
  run(3);
  CHECK(look(3).shown && panel_animation_status.state == "Shown: ball (4 copies)", "copy 3: '%s'",
        panel_animation_status.state.c_str());
  steps(3000);
  run(5);
  CHECK(
      !look(3).shown && !anim::find("ball")->copies[3].used && panel_animation_status.state == "Shown: ball (3 copies)",
      "after its round: '%s'", panel_animation_status.state.c_str());
  panel_glide_animation.execute("ball", 2, "size", 100, 0, "none");
  panel_glide_animation.execute("ball", 2, "opacity", 100, 0, "none");
  panel_move_animation.execute("ball", 2, 200, 150, 0, "none");  // still, for what follows
  run(3);
}

// A second animation alongside, placed by the action. A frame alone is over ANIM_POOL at full size, so it's made at
// half size right away.
static void anim_two() {
  step("two animations");
  anim::add("big", {128, 100, 4, 100, 16, 16, 1, [](int f, int x, int y) -> uint32_t {
                      return 0xFF000000u | (uint32_t) ((x / 8 + y / 8 + f) % 3) * 0x405060u;
                    }});
  size_t big_at = log_all.size();
  anim::place("big", 0, 16, 16, false, 0);
  int ar = until_anim_status("Shown: ball (3 copies), big");
  CHECK(ar >= 0 && same_anim("big", 0, 3), "two animations: '%s'", panel_animation_status.state.c_str());
  CHECK(anim::find("big")->half && !logged("making it at half size", big_at),
        "big: half %d, tried at full size first: %d", anim::find("big") && anim::find("big")->half,
        logged("making it at half size", big_at));
  unsigned used = pn_anim_used();
  CHECK(used == anim::used_by_others(nullptr) && used <= ANIM_POOL,
        "the panel's pool holds %u bytes, the ESP thinks %u", used, (unsigned) anim::used_by_others(nullptr));
  panel_remove_animation.execute("ball", 2);
  run(3);
  CHECK(!look(2).shown && pn_anim_used() == used - anim::find("ball")->frame_bytes() &&
            panel_animation_status.state == "Shown: ball (2 copies), big",
        "removing a copy: '%s', %u bytes", panel_animation_status.state.c_str(), pn_anim_used());
  panel_clear_animation.execute("big");
  run(3);
  CHECK(
      !anim::find("big") && look(0).shown && !look(3).shown && panel_animation_status.state == "Shown: ball (2 copies)",
      "clearing one animation: '%s'", panel_animation_status.state.c_str());
  anim::clear("");
  run(3);
  CHECK(!anim_ready() && !pn_anim_used() && !panel_anim_ball.state, "the animations are still there after clearing");
}

static void anim_half_size() {
  step("an animation at half size");
  panel_anim_noise.turn_on();
  int ar = run_until(anim_shown, 3000);
  CHECK(ar >= 0 && anim::find("noise")->half && anim::find("noise")->scale == 2 && same_anim("noise", 0),
        "noise not shown at half size: %s", panel_animation_status.state.c_str());
  // its drift_x from the yaml, 10 px/s left: half a pixel a step
  pn_anim_step(100);
  Look l = look(0);
  CHECK(panel_animation_status.state == "Shown: noise, drifting" && l.x == 15 && l.y == 16,
        "noise's drift_x: status '%s', at %d,%d after a step", panel_animation_status.state.c_str(), l.x, l.y);
  CHECK(anim::find("noise")->data.capacity() == anim::find("noise")->data.size(), "%u bytes kept for %u",
        (unsigned) anim::find("noise")->data.capacity(), (unsigned) anim::find("noise")->data.size());
  pn_restart();
  ar = run_until([] { return anim_ready() && anim_shown(); }, 3000);
  CHECK(ar >= 0 && same_anim("noise", 0), "the animation not sent again after a panel restart");
  l = look(0);
  pn_anim_step(100);
  CHECK(l.x == 16 && l.y == 16 && panel_animation_status.state == "Shown: noise, drifting",
        "after the restart: at %d,%d, status '%s'", l.x, l.y, panel_animation_status.state.c_str());
  l = look(0);
  CHECK(l.x == 15, "the drift wasn't sent again after a panel restart: at %d", l.x);
  printf("  noise at half size, and again after a panel restart after %d cycles\n", ar);
  panel_anim_noise.turn_off();
  anim::show("big");  // no yaml, no switch
  ar = until_anim_status("Shown: big");
  CHECK(ar >= 0 && anim::find("big")->half && same_anim("big", 0) && !anim::find("noise"), "big: %s, half %d",
        panel_animation_status.state.c_str(), anim::find("big") && anim::find("big")->half);
}

static void anim_not_fitting() {
  step("animations that don't fit");
  // flush with the right and bottom edges, half size rounds up, so it moves in
  anim::add("edge", {62, 45, 8, 100, 258, 195, 1, noise_px});
  anim::clear("big");
  anim::show("edge");
  int ar = until_anim_status("Shown: edge");
  Look l = look(0);
  CHECK(ar >= 0 && anim::find("edge")->half && l.x == 256 && l.y == 194 && same_anim("edge", 0),
        "edge: %s, half %d, at %d,%d", panel_animation_status.state.c_str(), anim::find("edge")->half, l.x, l.y);
  // the panel draws 2x at most, so scale 2 can't go to half size
  anim::add("noise2", {64, 64, 4, 100, 16, 16, 2, noise_px});
  anim::clear("edge");
  anim::show("noise2");
  ar = run_until([] { return anim::find("noise2") && anim::find("noise2")->state == anim::FAILED; }, 100);
  CHECK(ar >= 0 && panel_animation_status.state.rfind("noise2: too big at scale 2 (over ", 0) == 0 &&
            anim::find("noise2")->scale == 2,
        "noise2 at scale 2: %s, scale %d", panel_animation_status.state.c_str(), anim::find("noise2")->scale);
  anim::add("scale3", {8, 8, 1, 100, 0, 0, 3, noise_px});
  anim::clear("noise2");
  anim::show("scale3");
  run(1);
  CHECK(panel_animation_status.state == "scale3: scale has to be 1 or 2", "scale3: %s",
        panel_animation_status.state.c_str());
  anim::add("left", {8, 8, 1, 100, -4, 0, 1, noise_px});
  anim::clear("scale3");
  anim::show("left");
  run(1);
  CHECK(panel_animation_status.state == "left: doesn't fit on the screen at -4,0", "left: %s",
        panel_animation_status.state.c_str());
  size_t slow_at = log_all.size();
  anim::add("slow", {8, 8, 2, 5000, 0, 0, 1, noise_px});
  CHECK(anim::sources["slow"].frame_ms == 2550 && logged("frame_ms 5000 is outside 40..2550", slow_at),
        "frame_ms 5000 taken as %d", anim::sources["slow"].frame_ms);
}

// Clearing it while it's being made frees its buffers
static void anim_cleared_while_made() {
  step("an animation cleared while it's made");
  anim::clear("left");
  anim::show("edge");
  anim::W.a = anim::find("edge");
  anim::start();
  while (anim::W.phase == anim::COUNT)
    anim::count();
  anim::make();
  CHECK(anim::W.phase == anim::MAKE && anim::W.prev.capacity(), "edge isn't being made");
  anim::clear("");
  CHECK(!anim::W.hist.capacity() && !anim::W.frame.capacity() && !anim::W.prev.capacity() && !anim::W.first.capacity(),
        "clearing kept the buffers");
}

// No room next to the others: ball and three big ones, the last one has to go at half size and is refused
static void anim_no_room() {
  step("no room for another animation");
  anim::show("ball");
  anim::place("big", 0, 16, 16, false, 0);
  int ar = until_anim_status("Shown: ball, big", 6000);
  CHECK(ar >= 0, "ball and big: '%s'", panel_animation_status.state.c_str());
  anim::add("big2", anim::sources["big"]);
  anim::add("big3", anim::sources["big"]);
  anim::place("big2", 0, 16, 16, false, 0);
  anim::place("big3", 0, 16, 16, false, 0);
  ar = run_until([] { return anim::find("big3") && anim::find("big3")->state != anim::WANT && settled(); }, 9000);
  CHECK(
      ar >= 0 && anim::find("big2")->state == anim::SENT && anim::find("big3")->state == anim::FAILED &&
          panel_animation_status.state.rfind("Shown: ball, big, big2; big3: too big even at half size (over ", 0) == 0,
      "four animations: '%s'", panel_animation_status.state.c_str());
  CHECK(pn_anim_used() == anim::used_by_others(nullptr) && pn_anim_used() <= ANIM_POOL,
        "the panel's pool holds %u bytes, the ESP thinks %u", pn_anim_used(), (unsigned) anim::used_by_others(nullptr));
  anim::add("big4", anim::sources["big"]);
  anim::place("big4", 0, 16, 16, false, 0);  // no fifth slot
  run(1);
  CHECK(!anim::find("big4") && logged("No room for animation big4: the panel holds 4 at a time"),
        "a fifth animation: '%s'", panel_animation_status.state.c_str());
  anim::clear("");
  run(3);
}

// A GIF that repeats itself goes once round, and a frame shown twice in a row goes as one twice as long
static void anim_repeats() {
  step("animation frames that repeat");
  anim::add("cycle", {16, 16, 9, 100, 0, 0, 1, [](int f, int x, int y) { return noise_px(f % 3, x, y); }});
  anim::add("held", {16, 16, 4, 100, 0, 40, 1, [](int f, int x, int y) { return noise_px(f == 2 ? 1 : f, x, y); }});
  size_t made = log_all.size();
  anim::show("cycle");
  anim::show("held");
  int ar = until_anim_status("Shown: cycle, held");
  CHECK(ar >= 0 && logged("Animation cycle: 16x16, 3 frames (the rest repeat them)", made) &&
            anim_delays("cycle") == std::vector<int>({10, 10, 10, 10}) && same_anim("cycle", 0),
        "cycle: '%s', %u records", panel_animation_status.state.c_str(), (unsigned) anim_delays("cycle").size());
  CHECK(ar >= 0 && anim_delays("held") == std::vector<int>({10, 20, 10, 10}) && same_anim("held", 0, 1),
        "held: %u records", (unsigned) anim_delays("held").size());
  anim::clear("");
  run(3);
}

// ---- what the panel shows is what the ESP thinks it shows
// The ESP restarts. The panel drops what it had for the old one, the copies Home Assistant placed too, and shows what
// the switches do.
static void anim_esp_restart() {
  step("animations after the ESP restarts");
  anim::add("a1", {16, 16, 2, 100, 10, 10, 1, noise_px});
  anim::add("a2", {16, 16, 2, 100, 100, 10, 1, noise_px});
  anim::show("a1");
  anim::show("a2");
  anim::place("a2", 1, 100, 100, false, 0);
  run_until(anims_sent, 3000);
  anim::clear("a1");  // so a2 gets another slot and place after the restart
  run(3);
  anim::stop();
  for (auto &a : anim::anims)
    a = anim::Anim();
  // what panel::setup() sends, and on_boot shows what the switches have on
  panel::link::send(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK);
  anim::show("a2");
  int r = run_until(anims_sent, 3000);
  CHECK(r >= 0 && places() == "[0 100,10]" && same_pool(),
        "after an ESP restart the panel shows %s in %u bytes, the ESP has a2 at 100,10 in %u", places().c_str(),
        pn_anim_used(), (unsigned) anim::used_by_others(nullptr));
  anim::clear("");
}

// A hello while one is made: it's made again from the start, not sent half made. A loop makes a few rows, and a
// frame's record on its own.
static void anim_hello_while_made() {
  step("a hello while an animation is made");
  static int reads;
  anim::add("six", {128, 40, 6, 100, 50, 50, 1, [](int f, int x, int y) -> uint32_t {
                      reads++;
                      return 0xFF000000u | (uint32_t) ((x / 8 + y / 4 + 2 * f) % 7) * 0x203040u;
                    }});
  anim::show("six");
  anim::Anim *six = anim::find("six");
  micros_step = 1000;  // 4 calls in a loop's 5 ms
  int most = 0;
  bool moved = false;  // the records, which have their RAM from the start
  const uint8_t *at = nullptr;
  for (int k = 0; k < 1000 && !(anim::W.phase == anim::MAKE && anim::W.f == 2); k++) {
    reads = 0;
    anim::loop();
    most = std::max(most, reads);
    if (anim::W.phase == anim::MAKE) {
      moved |= at && six->data.data() != at;
      at = six->data.data();
    }
  }
  micros_step = 0;
  CHECK(anim::W.f == 2 && most == 4 * anim::ROWS_PX && !moved && six->data.capacity() == anim::data_max(),
        "%d pixels read in a loop, the records moved %d, %u bytes for them", most, moved,
        (unsigned) six->data.capacity());
  pn_restart();  // its hello comes before the next loop
  int r = run_until([&] { return six->state == anim::SENT && settled(); }, 3000);
  CHECK(r >= 0 && anim_delays("six").size() == 7 && same_anim("six", 0), "made again after a hello: %u records",
        (unsigned) anim_delays("six").size());
  anim::clear("");
}

// A copy can start off the screen, to come in from there. One that isn't a copy takes nothing.
static void anim_off_screen() {
  step("an animation copy off the screen");
  size_t warned = log_all.size();
  anim::place("ball", 8, 10, 10, false, 0);
  run(5);
  CHECK(!anim::find("ball") && !anim_ready() && logged("Animation ball: copy 8 isn't one of 0 to 7", warned),
        "copy 8: ball %d, the panel shows %s", anim::find("ball") != nullptr, places().c_str());
  anim::place("ball", 1, -40, 300, false, 0);
  bool sent =
      run_until([] { return anim::find("ball") && anim::find("ball")->state == anim::SENT && settled(); }, 3000) >= 0;
  Look l = sent ? look(anim::find("ball")->copies[1].place) : Look{};
  CHECK(sent && l.shown && l.x == -40 && l.y == 300, "a copy off the screen: the panel shows %s", places().c_str());
  anim::clear("");
  run(5);
}

// panel_show_animation puts copy 0 back where the yaml has it, drifting, once it was removed or played its rounds
static void anim_shown_again() {
  step("an animation shown again");
  auto back = [] {  // at 16,16 or just left of it, and going left
    Look was = look(0);
    bool at = was.shown && was.x > 10 && was.x <= 16 && was.y == 16;
    steps(500);
    Look l = look(0);
    return at && l.shown && l.x < was.x && panel_animation_status.state == "Shown: noise, drifting";
  };
  panel_anim_noise.turn_on();
  run_until(anims_sent, 3000);
  panel_remove_animation.execute("noise", 0);
  run(3);
  CHECK(!anim_ready() && panel_animation_status.state == "Shown: noise", "copy 0 removed: %s, '%s'", places().c_str(),
        panel_animation_status.state.c_str());
  panel_show_animation.execute("noise");
  run(3);
  CHECK(back(), "shown again: %s, '%s'", places().c_str(), panel_animation_status.state.c_str());
  panel_rounds_animation.execute("noise", 0, 1);
  run(3);
  steps(2000);
  run(5);
  CHECK(!anim_ready() && panel_animation_status.state == "Shown: noise", "after its round: %s, '%s'", places().c_str(),
        panel_animation_status.state.c_str());
  panel_show_animation.execute("noise");
  run(3);
  CHECK(back(), "shown again after its round: %s, '%s'", places().c_str(), panel_animation_status.state.c_str());
  anim::clear("");
}

static void anim_timing() {
  step("animation timing");
  // A GIF that ends on its first frame. The record back to it stays its own, so a round ends there.
  anim::add("aba", {16, 16, 3, 100, 0, 0, 1, [](int f, int x, int y) { return noise_px(f == 1, x, y); }});
  anim::show("aba");
  anim::place("aba", 1, 100, 100, true, 0);
  anim::rounds("aba", 1, 1);
  run_until(anims_sent, 3000);
  std::string seen;  // the frames it shows, 2 is like 0
  for (int t = 0; t < 100 && look(1).shown; t++, pn_anim_step(10)) {
    char f = same_anim("aba", 0, 1, true) ? '0' : same_anim("aba", 1, 1, true) ? '1' : '?';
    if (seen.empty() || seen.back() != f)
      seen += f;
  }
  CHECK(anim_delays("aba") == std::vector<int>({10, 10, 10, 10}) && seen == "010",
        "a round of F0 F1 F0 shows %s, %u records", seen.c_str(), (unsigned) anim_delays("aba").size());
  anim::clear("");

  // Each frame its own time, past a record's 2550 ms in empty records after it: F0 4 s, F1 0.1 s, F2 3 s. In a
  // loop F0 shows for the record back to it and the empty one after the first record.
  anim::Source slow{16, 16, 3, 100, 0, 0, 1, noise_px};
  slow.delays = {4000, 100, 3000};
  anim::add("slow", slow);
  anim::show("slow");
  run_until(anims_sent, 3000);
  std::vector<std::pair<int, int>> turns;  // when the panel shows another frame, and which
  int was = 0;
  for (int t = 0; t <= 16000; t += 10, pn_anim_step(10)) {
    int f = same_anim("slow", 0, 0, true) ? 0 : same_anim("slow", 1, 0, true) ? 1 : 2;
    if (f != was)
      turns.push_back({t, f});
    was = f;
  }
  int want[3] = {4000, 100, 3000}, timed = 0;  // how long each frame shows, every time it's seen whole
  for (size_t i = 0; i + 1 < turns.size(); i++)
    timed += abs(turns[i + 1].first - turns[i].first - want[turns[i].second]) <= 60 ? 1 : -100;
  CHECK(anim_delays("slow") == std::vector<int>({255, 145, 10, 255, 45, 255}) && timed >= 4,
        "frames of 4 s, 0.1 s and 3 s: %u records, %u changes, %d timed right", (unsigned) anim_delays("slow").size(),
        (unsigned) turns.size(), timed);
  anim::clear("");
}

// What making one takes, pdeflate and all. Short of it at full size it's made at half size, short of that too it fails
// until it's shown again or a hello.
static void anim_without_ram() {
  step("animations without the RAM");
  ram_free = 45 * 1024;
  size_t warned = log_all.size();
  panel_anim_ball.turn_on();
  run_until(anims_sent, 3000);
  CHECK(anim::find("ball")->half && same_anim("ball", 0) &&
            logged("Animation ball: the ESP has the RAM to make it at half size only", warned),
        "ball without the RAM for full size: half %d", anim::find("ball")->half);
  anim::add("ball2x", {32, 32, 4, 100, 0, 100, 2, noise_px});  // scale 2 can't halve
  ram_free = 30 * 1024;
  anim::show("ball2x");
  run(3);
  CHECK(panel_animation_status.state == "Shown: ball; ball2x: the ESP doesn't have the RAM to make it", "'%s'",
        panel_animation_status.state.c_str());
  ram_free = 200 * 1024;
  pn_restart();
  int r = run_until(anims_sent, 3000);
  CHECK(r >= 0 && panel_animation_status.state == "Shown: ball, ball2x", "not made after a hello: '%s'",
        panel_animation_status.state.c_str());
  // panel_place_animation tries a failed one again too
  ram_free = 30 * 1024;
  anim::clear("ball2x");
  anim::show("ball2x");
  run(3);
  ram_free = 200 * 1024;
  anim::place("ball2x", 1, 200, 100, false, 0);
  r = run_until(anims_sent, 3000);
  CHECK(r >= 0 && panel_animation_status.state == "Shown: ball, ball2x (2 copies)", "placed after it failed: '%s'",
        panel_animation_status.state.c_str());
  anim::clear("");
  // one that would only fail again isn't made again after a hello, until it's shown again
  static int making;
  panel_animation_status.add_on_state_callback([](const std::string &s) { making += s == "Making noise2"; });
  anim::show("noise2");
  run(3);
  restart_panel();
  CHECK(making == 1 && anim::find("noise2")->state == anim::FAILED, "noise2 made %d times, through a hello", making);
  anim::show("noise2");
  run(3);
  CHECK(making == 2, "noise2 not tried again when shown");
  anim::clear("");
}

// Copies with a frame of their own past the panel's pool aren't kept, placed before it's sent or after. One placed
// again elsewhere has its own frame back first.
static void anim_past_pool() {
  step("animation copies past the panel's pool");
  anim::show("ball");  // 1825 bytes, and 1152 for each frame of its own
  for (int i = 1; i <= 4; i++)
    anim::place("ball", i, 50 * i, 150, true, 0);
  run_until(anims_sent, 3000);
  anim::Anim *ball = anim::find("ball");
  CHECK(!ball->copies[4].used && panel_animation_status.state == "Shown: ball (4 copies)" && same_pool() &&
            !look(4).shown,
        "four copies with frames of their own: '%s', the panel has %u bytes, the ESP thinks %u",
        panel_animation_status.state.c_str(), pn_anim_used(), (unsigned) anim::used_by_others(nullptr));
  size_t warned = log_all.size();
  anim::place("ball", 4, 150, 150, true, 0);
  anim::place("ball", 2, 100, 50, true, 0);
  run(3);
  Look l = look(ball->copies[2].place);
  CHECK(!ball->copies[4].used && panel_animation_status.state == "Shown: ball (4 copies)" && same_pool() && l.shown &&
            l.x == 100 && l.y == 50 &&
            logged("no room in the panel's 6144 bytes for a copy with a frame of its own", warned),
        "placed with no room: '%s', copy 2 at %d,%d", panel_animation_status.state.c_str(), l.x, l.y);
  // all eight places taken: no slot for another
  for (int i = 4; i < ANIM_PLACES; i++)
    anim::place("ball", i, 20 * i, 10, false, 0);
  warned = log_all.size();
  anim::show("noise");
  CHECK(!anim::find("noise") && logged("the panel shows 8 copies at most", warned),
        "a slot taken with no place for it");
  anim::clear("");
}

// The panel lets a wave that's settling go for a new one, so only those that aren't count
static void anim_waves_settling() {
  step("animation waves let go for new ones");
  anim::show("ball");
  anim::place("ball", 1, 10, 150, false, 0);
  anim::place("ball", 2, 100, 150, false, 0);
  for (int i = 0; i < ANIM_WAVES; i++) {
    int c = i % ANIM_CHANNELS;
    panel_wave_animation.execute("ball", i / ANIM_CHANNELS, anim::CHANNELS[c], anim::no_change(c) ? 50 : 1,
                                 anim::no_change(c) ? 90 : 9, 2000, 2000, 0, 0, 0, false, "none");
  }
  run_until(anims_sent, 3000);
  steps(500);
  size_t warned = log_all.size();
  panel_wave_animation.execute("ball", 0, "x", 0, 0, 0, 0, 0, 0, 0, false, "none");
  panel_wave_animation.execute("ball", 2, "x", 30, 30, 0, 0, 0, 0, 0, false, "none");
  run(3);
  steps(100);
  int x = look(anim::find("ball")->copies[2].place).x;
  CHECK(x == 130 && !logged("waves at most", warned), "a wave in place of one stopped: copy 2 at %d", x);
  panel_wave_animation.execute("ball", 2, "y", 5, 5, 0, 0, 0, 0, 0, false, "none");
  CHECK(logged("the panel runs 12 waves at most", warned), "a 13th wave");
}

// Taking a copy off or clearing another while one is on its way: it goes on, and the panel has what the ESP thinks
static void anim_on_its_way() {
  step("animations changed while one is on its way");
  anim::add("mid", {32, 32, 6, 100, 200, 100, 1, noise_px});
  anim::show("mid");
  run_until([] { return anim::W.t.head && anim::W.t.pos > 512; }, 3000);
  CHECK(anim::W.t.head && same_pool(), "on its way: the panel has %u bytes, the ESP thinks %u", pn_anim_used(),
        (unsigned) anim::used_by_others(nullptr));
  size_t warned = log_all.size();
  anim::remove("ball", 1);
  int r = run_until(anims_sent, 3000);
  CHECK(r >= 0 && !logged("sending it again", warned) && same_anim("mid", 0, anim::find("mid")->copies[0].place),
        "mid sent again after a copy of ball went");
  anim::clear("mid");
  anim::show("mid");
  run_until([] { return anim::W.t.head && anim::W.t.pos > 512; }, 3000);
  anim::clear("mid");
  run(5);
  CHECK(same_pool() && !anim::find("mid"), "cleared on its way: the panel has %u bytes, the ESP thinks %u",
        pn_anim_used(), (unsigned) anim::used_by_others(nullptr));
  anim::clear("");
  run(3);
}

// ---- the weather picture, packages/panel/met.yaml and packages/code/met/met.h
static void met_requests() {
  step("MET Norway's forecast, asked for and backed off");
  flexit_panel_time.t.timestamp = 1792409400;  // 2026-10-19 11:30 UTC, half an hour into the first forecast hour
  tz_offset_s = 7200;                          // Oslo in summer time
  // met.yaml's on_boot set it up and asked, its request waits in its task
  CHECK(!strcmp(met::url, "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=59.9139&lon=10.7522") &&
            met::timeout_ms == 4500 && met::modified.empty() && met::due() && met::fetching && tasks.size() == 1,
        "first request: %s, If-Modified-Since '%s', %d requests", met::url, met::modified.c_str(), (int) tasks.size());
  network::up = false;
  CHECK(!met::due(), "asks MET Norway while offline");
  network::up = true;
  // its scene and aurora options are met_scene.h's names, in their order
  bool same = panel_weather_scene.options.size() == std::size(met::GROUNDS) &&
              panel_weather_aurora.options.size() == std::size(met::AURORAS);
  for (size_t i = 0; same && i < std::size(met::GROUNDS); i++) {
    panel_weather_scene.control(i);
    same = panel_weather_scene.options[i] == met::GROUNDS[i] && met::options.ground == i;
  }
  for (size_t i = 0; same && i < std::size(met::AURORAS); i++) {
    panel_weather_aurora.control(i);
    same = panel_weather_aurora.options[i] == met::AURORAS[i] && met::options.aurora == i;
  }
  panel_weather_scene.control(0);
  panel_weather_aurora.control(0);
  CHECK(same && met::ground_of("moon") == met::G_HILLS && logged("No weather option called 'moon'"),
        "met.yaml's options aren't met_scene.h's names");
  met::unreachable();  // right after boot the network may not be up: try again at the next check
  CHECK(met::backoff_s == 0 && met::due(), "backed off before MET Norway ever answered");
  for (int i = 0; i < 3; i++)
    met::unreachable();
  CHECK(met::backoff_s == 600 && !met::due(), "kept trying without backing off");
  // the request, in a task of its own: no answer, then the forecast in 512-byte reads, then without the RAM
  auto ask = [] {
    panel_weather_fetch.execute();  // met.yaml's, every update_interval
    if (!tasks.empty())
      run_task();
    yaml_every(1000);  // and its poll()
  };
  panel_http.down = true;
  met::not_before = 0;
  ask();
  CHECK(met::backoff_s == 1200 && met_status == "Couldn't reach MET Norway", "no answer: %s, back-off %u",
        met_status.c_str(), (unsigned) met::backoff_s);
  panel_http.down = false;
  panel_http.body = met_sample();
  panel_http.last_modified = "Mon, 19 Oct 2026 11:34:38 GMT";
  panel_http.expires = "Mon, 19 Oct 2026 12:00:00 GMT";
  met::not_before = 0;
  met::fetch();
  met::fetch();  // still running, it waits for that one
  met::poll();   // nothing yet
  CHECK(tasks.size() == 1 && met::fetching && !met::weather.ok, "%u requests at once", (unsigned) tasks.size());
  run_task();
  met::poll();
  CHECK(!met::fetching && met::weather.ok, "the answer wasn't taken from the request's task");
  CHECK(panel_http.sent.empty(), "If-Modified-Since sent before any Last-Modified");
  CHECK(met::not_before == 1792411200 && !met::due(), "asks again before Expires: %u", (unsigned) met::not_before);
  for (size_t block : {sizeof(met::Fetch) - 1, sizeof(met::Fetch) + 100}) {  // the request, then its task's stack
    met::not_before = 0;
    ram_block = block;
    met::fetch();
    CHECK(met_status == "No RAM for the forecast" && met::not_before == 1792410000 && !met::fetching && tasks.empty(),
          "without %u bytes of RAM: %s, next at %u", (unsigned) block, met_status.c_str(), (unsigned) met::not_before);
  }
  ram_block = 100 * 1024;
  met::not_before = 0;
  ask();
  CHECK(panel_http.sent.size() == 1 && panel_http.sent[0].name == "If-Modified-Since" &&
            panel_http.sent[0].value == "Mon, 19 Oct 2026 11:34:38 GMT",
        "If-Modified-Since not sent with the Last-Modified we have");
  CHECK(met::weather.ok && !strcmp(met::weather.symbol, "partlycloudy_day") && fabsf(met::weather.temp - 6.6f) < 0.01f,
        "forecast read as %s, %.1f", met::weather.symbol, met::weather.temp);
  CHECK(met::modified == "Mon, 19 Oct 2026 11:34:38 GMT", "Last-Modified not kept: %s", met::modified.c_str());
  CHECK(met::weather.hours == 12 && met::weather.hour[2].time == met::weather.hour[0].time + 7200, "forecast hours: %d",
        met::weather.hours);
  CHECK(met_status.find("partlycloudy_day") == 0, "status: %s", met_status.c_str());
  panel_http.status = 304;  // not modified, through the task too
  met::not_before = 0;
  ask();
  CHECK(met::weather.ok && met_status.find("partlycloudy_day") == 0 && met::not_before == 1792411200,
        "a 304: %s, next at %u", met_status.c_str(), (unsigned) met::not_before);
  panel_http.status = 200;
  // a 301 is followed, three times at most
  panel_http.location = std::string(met::url) + "&moved=1";
  panel_http.moves = 1;
  met::not_before = 0;
  ask();
  CHECK(panel_http.url == panel_http.location && met::weather.ok && met::backoff_s == 0,
        "after a 301: asked for %s, %s", panel_http.url.c_str(), met_status.c_str());
  panel_http.moves = 9;
  met::not_before = 0;
  ask();
  CHECK(panel_http.moves == 5 && met_status == "No forecast (HTTP 301)", "moved on and on: %d requests, '%s'",
        9 - panel_http.moves, met_status.c_str());
  panel_http.moves = 0;
  // A cut-off answer counts as none, wherever it's cut: the forecast and its Last-Modified stay, and it's asked
  // again in minutes
  panel_http.last_modified = "Tue, 20 Oct 2026 08:00:00 GMT";
  for (size_t cut : {6000, 300})
    for (int with : {0, -1}) {  // the connection closed, or failed
      panel_http.cut = cut;
      panel_http.cut_with = with;
      met::backoff_s = 0;
      met::not_before = 0;
      ask();
      CHECK(met_status == "Couldn't reach MET Norway" && met::backoff_s == 600 && met::weather.hours == 12 &&
                met::modified == "Mon, 19 Oct 2026 11:34:38 GMT",
            "cut off after %u bytes, read() %d: '%s', back-off %u, %d hours, Last-Modified '%s'", (unsigned) cut, with,
            met_status.c_str(), (unsigned) met::backoff_s, met::weather.hours, met::modified.c_str());
    }
  // but the reader can have all it needs before the cut
  std::string b = met_sample();
  size_t e = b.find("{\"time\""), f = b.find("{\"time\"", e + 1);
  panel_http.body = b.substr(0, f);
  for (int i = 0; i < 30; i++)
    panel_http.body += b.substr(e, f - e);
  panel_http.cut = panel_http.body.size() - 100;
  met::not_before = 0;
  ask();
  CHECK(met::weather.hours == met::HOURS && met::modified == "Tue, 20 Oct 2026 08:00:00 GMT",
        "cut off once it had %d hours: %d taken, '%s'", met::HOURS, met::weather.hours, met_status.c_str());
  panel_http.cut = SIZE_MAX;
  panel_http.body = b;
  panel_http.last_modified = "Mon, 19 Oct 2026 11:34:38 GMT";
  met::not_before = 0;
  ask();
  CHECK(met::weather.hours == 12 && met::backoff_s == 0, "the forecast not back: %d hours", met::weather.hours);
}

// The reader with the answer cut anywhere, more hours than it needs, and an hour too big for its buffer
static void met_reading() {
  step("MET Norway's answer read in pieces");
  std::string b = met_sample();
  bool stopped;
  met::Weather w1 = read_met(b, 1, &stopped);
  CHECK(!stopped && w1.hours == met::weather.hours && w1.temp == met::weather.temp && w1.cloud == met::weather.cloud &&
            !strcmp(w1.symbol, met::weather.symbol) && w1.hour[5].time == met::weather.hour[5].time,
        "a byte at a time: %d hours, %s", w1.hours, w1.symbol);
  size_t e = b.find("{\"time\""), f = b.find("{\"time\"", e + 1);
  std::string many = b.substr(0, f);
  for (int i = 0; i < 30; i++)
    many += b.substr(e, f - e);
  met::Weather w2 = read_met(many, 512, &stopped);
  CHECK(stopped && w2.ok && w2.hours == met::HOURS, "30 hours: %d read, stopped %d", w2.hours, stopped);
  std::string big = b.substr(0, f) + "{\"time\":\"" + std::string(5000, 'x') + "\"}," + b.substr(e, f - e);
  met::Weather w3 = read_met(big, 512, &stopped);
  CHECK(stopped && w3.ok && w3.hours == 1, "an hour over 4 KB: %d hours, stopped %d", w3.hours, stopped);
  // MET's longest symbols, and one longer still, which is cut short
  auto symbols = [&](const char *day, const char *night) {
    std::string s = b;
    auto put = [&s](const std::string &from, const char *to) {
      for (size_t at; (at = s.find(from)) != std::string::npos;)
        s.replace(at, from.size(), std::string("\"") + to + "\"");
    };
    put("\"partlycloudy_day\"", day);
    put("\"partlycloudy_night\"", night);
    return read_met(s);
  };
  met::Weather w4 = symbols("lightssleetshowersandthunder_polartwilight", "heavysnowshowersandthunder_polartwilight");
  met::Options o;
  met::Now n(w4, o, OSLO_LAT, OSLO_LON, w4.time, 0);
  int thunder = 0, partly = 0;  // the hours that were partly cloudy
  for (int i = 0; i < w4.hours; i++) {
    thunder += w4.hour[i].icon == met::I_THUNDER;
    partly += w1.hour[i].icon == met::I_PARTLY_SUN || w1.hour[i].icon == met::I_PARTLY_MOON;
  }
  CHECK(!strcmp(w4.symbol, "lightssleetshowersandthunder_polartwilight") && thunder == partly && partly > 6 &&
            n.thunder && n.snow && n.rain,
        "MET's longest symbols: '%s', %d of %d hours with thunder, thunder %d snow %d rain %d", w4.symbol, thunder,
        partly, n.thunder, n.snow, n.rain);
  met::Weather w5 = symbols("lightssleetshowersandthunder_polartwilight_and_then_some", "fog");
  CHECK(strlen(w5.symbol) == met::SYMBOL - 1 && !strncmp(w5.symbol, "lightssleetshowersandthunder", 28) &&
            w5.hour[0].icon == met::I_THUNDER,
        "a symbol too long: '%s', icon %d", w5.symbol, w5.hour[0].icon);
}

static void met_picture() {
  step("the weather picture");
  pic::show("weather");
  CHECK(picture("weather", 6000) >= 0, "weather picture not shown");
  int pw, ph;
  CHECK(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "weather picture %dx%d", pw, ph);
  CHECK(!met::drawing && met::land, "the scene kept its RAM once the picture was made, or not the land");
  CHECK(!memcmp(pic::S.shown.pal, met::pal, sizeof met::pal), "the picture isn't in the scene's own colours");
  run(10);
  PanelItem wx = pitem("weather");
  CHECK(wx.value == 66 && wx.icon == ICN_SUN && !strcmp(wx.label, "Fair") && !(wx.flags & (ITEMF_VIEW | ITEMF_MENU)),
        "the weather now for a tile: %d, icon %d, '%s', flags 0x%02X (want 66, sun, Fair)", wx.value, wx.icon, wx.label,
        wx.flags);
  pn_shot(outdir, "esp_weather");
  size_t changed = log_all.size();
  uint32_t was_pal[16];
  memcpy(was_pal, met::pal, sizeof was_pal);
  flexit_panel_time.t.timestamp += 600;  // ten minutes later, the sun and the clouds have moved
  met::redraw();
  CHECK(picture("weather, ten minutes on", 3000) >= 0, "weather picture not shown again");
  CHECK(logged("Picture weather:", changed) && logged("pieces", changed), "not sent as changed rows");
  for (int k = 0; k < 16; k++)  // the colours both have kept their places
    for (int j = 0; j < 16; j++)
      CHECK(met::pal[j] != was_pal[k] || met::pal[k] == was_pal[k] || std::count(was_pal, was_pal + 16, was_pal[k]) > 1,
            "colour %06X moved from %d to %d", (unsigned) was_pal[k], k, j);
  panel_weather_forecast.turn_on();
  CHECK(picture("weather with the forecast", 3000) >= 0, "weather picture with the forecast not shown");
  pn_shot(outdir, "esp_weather_forecast");
  panel_weather_forecast.turn_off();
  ram_block = sizeof(met::Drawing) - 1;  // the scene's biggest piece
  met::redraw();
  run(20);
  CHECK(!met::drawing && panel_picture_status.state == "weather: no RAM to draw it", "without the RAM to draw it: '%s'",
        panel_picture_status.state.c_str());
  ram_block = 100 * 1024;
  CHECK(picture("weather, with the RAM back", 3000) >= 0 && !met::drawing, "not drawn with the RAM back");
}

// Refusals back off an hour, then twice as long, and an answer starts over. No answer at all is ten minutes and twice
// that up to an hour, after a refusal still an hour.
static void met_answers() {
  step("MET Norway's answers");
  const met::Weather sample = met::weather;                                    // the forecast as read from the sample
  auto fetched = [](int status, const met::Weather &w, const char *expires) {  // an answer from the request
    met::Fetch f;
    f.status = status;
    f.w = w;
    f.expires = expires;
    met::fetched(f);
  };
  fetched(304, {}, "Mon, 19 Oct 2026 13:00:00 GMT");  // not modified: nothing changes
  CHECK(met::weather.ok && met::modified == "Mon, 19 Oct 2026 11:34:38 GMT", "304 changed the forecast");
  CHECK(met::not_before == 1792414800, "the 304's Expires not kept");
  fetched(500, {}, "");
  CHECK(met::weather.ok && met_status.find("HTTP 500") != std::string::npos, "an error lost the forecast: %s",
        met_status.c_str());
  uint32_t now = (uint32_t) flexit_panel_time.t.timestamp;
  CHECK(met::not_before == now + 3600, "no back-off after an error");
  fetched(429, {}, "");
  CHECK(met::not_before == now + 7200 && !met::due(), "back-off not doubled after a 429");
  met::unreachable();
  CHECK(met::backoff_s == 3600, "no answer after a 429 waited %u s", (unsigned) met::backoff_s);
  flexit_panel_time.t.timestamp += 7200;
  CHECK(met::due(), "not due after the back-off");
  fetched(304, {}, "");  // without Expires, an hour
  CHECK(met::backoff_s == 0 && met::not_before == now + 7200 + 3600, "an answer didn't reset the back-off");
  for (uint32_t want : {600, 1200, 2400, 3600, 3600}) {
    met::unreachable();  // a short drop costs minutes, not hours
    CHECK(met::backoff_s == want && met_status == "Couldn't reach MET Norway", "no answer backed off %u s, not %u",
          (unsigned) met::backoff_s, (unsigned) want);
  }
  fetched(403, {}, "");
  CHECK(met::backoff_s == 7200, "a refusal after no answers backed off %u s", (unsigned) met::backoff_s);
  met::weather.ok = false;
  fetched(203, sample, "");  // a deprecated product, its data is still good
  CHECK(met::weather.ok && met::backoff_s == 0, "a 203 not taken: %s", met_status.c_str());
  static const struct {
    const char *date;
    uint32_t t;
  } DATES[] = {{"Fri, 31 Dec 1999 23:59:59 GMT", 946684799}, {"soon", 0}, {"Mon, 19 Foo 2026 11:34:38 GMT", 0}};
  for (auto d : DATES)
    CHECK(met::http_date(d.date) == d.t, "HTTP date '%s' read as %u, not %u", d.date, (unsigned) met::http_date(d.date),
          (unsigned) d.t);
}

// Through 304s and refusals the tile follows the forecast's hours, and the forecast is dropped once it runs out
static void met_forecast_hours() {
  step("the weather tile through the forecast's hours");
  const met::Weather was = met::weather;
  auto &tile = panel::items[met::now_item];
  time_t then = flexit_panel_time.t.timestamp;
  flexit_panel_time.t.timestamp = met::weather.hour[4].time + 1200;
  met::redraw();
  CHECK(tile.value_ == lroundf(met::weather.hour[4].temp * 10) && met::weather.ok,
        "4 hours on the tile shows %d, the hour has %.1f", (int) tile.value_, met::weather.hour[4].temp);
  flexit_panel_time.t.timestamp = met::ends(met::weather);
  met::redraw();
  CHECK(!met::weather.ok && tile.value_ == ITEM_UNKNOWN && met::modified.empty() &&
            met_status == "The forecast has run out",
        "a forecast that ran out: ok %d, tile %d, '%s'", met::weather.ok, (int) tile.value_, met_status.c_str());
  run(3);
  met::weather = was;
  flexit_panel_time.t.timestamp = then;
}

// UTC+13, already the 1st of June (summer) at noon UTC on the 31st of May
static void met_date_line() {
  step("the weather picture at UTC+13");
  ESPTime was = flexit_panel_time.t;
  flexit_panel_time.t = {2026, 6, 1, 1, 0, 0};
  flexit_panel_time.t.timestamp = 1780228800;  // 2026-05-31 12:00 UTC
  tz_offset_s = 13 * 3600;
  met::redraw();
  CHECK(met::draw(), "the scene not drawn");
  static SceneBuf want(false);
  met::build(
      want.s, met::weather, met::lat, met::lon, 1780228800, false, [](uint32_t) { return 13 * 3600; }, met::options);
  want.s.live = met::drawing->scene.live;
  met::palette(want.s, nullptr);
  bool same = true;
  for (int y = 0; y < met::H && same; y++) {
    uint8_t r1[met::W], r2[met::W];
    met::row(want.s, y, r1);
    met::row(met::drawing->scene, y, r2);
    for (int x = 0; x < met::W; x++)
      same = same && want.s.pal[r1[x]] == met::drawing->scene.pal[r2[x]];
  }
  CHECK(same, "UTC+13 drawn for the day before");
  flexit_panel_time.t = was;
  tz_offset_s = 7200;
  met::redraw();
  CHECK(picture("weather, back in Oslo", 3000) >= 0 && !met::drawing, "weather picture not shown again");
  CHECK(!logged("refused a piece"), "the panel refused a piece");
  tz_offset_s = 0;
}

#include "live_sky.inc"
#include "live_land.inc"
#include "live_town.inc"
#include "live_days.inc"

static void weather_scene() {
  step("the weather scene (packages/code/met/met_scene.h)");
  static SceneBuf sb(true);
  met::Scene &s = sb.s;
  auto wx = [](const char *sym, float cloud, float temp, float wind, float from) {
    met::Weather w;
    w.ok = true;
    snprintf(w.symbol, sizeof w.symbol, "%s", sym);
    w.cloud = w.cloud_low = w.cloud_mid = w.cloud_high = cloud;
    w.temp = temp;
    w.wind = wind;
    w.wind_from = from;
    return w;
  };
  auto draw = [&](const met::Weather &w, uint32_t t, met::Options o) {
    met::Weather v = w;
    v.time = t / 3600 * 3600;
    met::build(s, v, OSLO_LAT, OSLO_LON, t, false, oslo_offset, o);
    met::palette(s, nullptr);
  };
  // never more than 16 colours, and a flag day on the coast's sprites keep theirs
  draw(wx("fair_day", 25, 14, 5, 200), utc(2026, 5, 17, 11), {met::G_COAST});
  bool in_pal = true, under_16 = true;
  for (int i = 0; i < s.nsprites; i++)
    in_pal = in_pal && s.pal[s.sprite[i].ix] == s.sprite[i].c;
  for (int y = 0; y < met::H; y++) {
    uint8_t r[met::W];
    met::row(s, y, r);
    for (int x = 0; x < met::W; x++)
      under_16 = under_16 && r[x] < 16;
  }
  CHECK(s.folded && in_pal && under_16, "a flag day on the coast: %d folded, sprites' colours kept %d", s.folded,
        in_pal);
  // the wind blows things downwind. The picture faces south, so a wind from the east goes to the right.
  for (float from : {90.0f, 270.0f}) {
    met::Weather w = wx("lightrain", 90, 5, 8, from);
    w.rain = 1;
    met::Options o{};
    o.heater_pct = 60;
    draw(w, utc(2026, 5, 17, 10), o);
    int dir = from < 180 ? 1 : -1, flag = s.sprite[s.nsprites - 1].x - s.pole_x,
        smoke = s.sprite[0].x + s.sprite[0].w / 2 - (s.cabin_x + 9);
    CHECK(s.slant * dir > 0 && flag * dir > 0 && smoke * dir > 0, "wind from %.0f: rain %d, flag %d, smoke %d", from,
          s.slant, flag, smoke);
  }
  // the coast's trees stay put in any weather, with or without the boat
  draw(wx("fair_day", 10, 18, 3, 200), utc(2026, 7, 10, 12), {met::G_COAST});
  auto tree_xs = [&] {
    std::vector<int16_t> v;
    for (int i = 0; i < s.ntrees; i++)
      v.push_back(s.tree[i].x);
    return v;
  };
  std::vector<int16_t> trees = tree_xs();
  draw(wx("rain", 90, 12, 3, 200), utc(2026, 7, 10, 12), {met::G_COAST});
  CHECK(trees == tree_xs(), "the coast's trees moved with the weather");
  // the flag goes up at 9 in December even before the sun is, and down at sunset. Up north it's 10 to 15.
  met::Options o{};
  met::Weather clear = wx("clearsky_day", 0, -2, 2, 200);
  for (auto f : {std::make_tuple(OSLO_LAT, OSLO_LON, utc(2026, 12, 25, 8, 5), 3600, true),
                 std::make_tuple(OSLO_LAT, OSLO_LON, utc(2026, 12, 25, 15, 30), 3600, false),
                 std::make_tuple(69.65, 18.96, utc(2027, 1, 21, 9, 30), 3600, true),
                 std::make_tuple(69.65, 18.96, utc(2027, 1, 21, 14, 30), 3600, false)}) {
    met::Now n(clear, o, std::get<0>(f), std::get<1>(f), std::get<2>(f), std::get<3>(f));
    CHECK(n.flag_up == std::get<4>(f), "the flag at %.2f N at %02d local with the sun at %.1f: up %d", std::get<0>(f),
          n.hour, n.sun.el, n.flag_up);
  }
  // the forecast's hours are labelled in the local time of each, across the end of summer time
  met::Weather w = wx("cloudy", 50, 8, 3, 200);
  w.time = utc(2026, 10, 25);
  for (int i = 0; i < met::HOURS; i++)
    w.hour[w.hours++] = {w.time + i * 3600u, 8, met::I_CLOUD};
  int at[4], n = met::marks(w, w.time + 1800, oslo_offset, at);
  CHECK(n == 4 && at[0] == 2 && met::local_hour(w.hour[at[0]].time, oslo_offset) == 3 && at[1] == 5,
        "forecast marks after the change: %d of them, the first hour %d", n, at[0]);
  // the lighthouse blinks every other redraw, whatever the redraw interval
  int lamps[2];
  for (uint32_t frame : {0, 1}) {
    met::Options c{met::G_COAST};
    c.frame = frame;
    draw(wx("clearsky_night", 0, 8, 2, 200), utc(2026, 9, 10, 21, 30), c);
    lamps[frame] = s.nsprites;
  }
  CHECK(lamps[0] == lamps[1] + 1, "the lamp doesn't blink: %d and %d sprites", lamps[0], lamps[1]);
  // the moon wraps around the edges like the sun
  draw(wx("clearsky_night", 0, 2, 2, 200), utc(2026, 10, 26, 21), {});
  CHECK(s.moon, "no moon to wrap");
  s.moon_x = 2;
  s.moon_phase = 0.5f;
  uint8_t r[met::W];
  met::row(s, s.moon_y, r);
  CHECK(r[met::W - 4] == s.ix[met::C_MOON], "the moon isn't drawn on the other edge");
  // the middle clouds' end lobes are there in a narrow cloud
  int smallest = 99;
  for (int k = 0; k < 200; k++) {
    draw(wx("partlycloudy_day", 90, 8, 3, 200), utc(2026, 10, 20) + k * 3600, {});
    for (int i = 0; i < s.nclouds; i++) {
      if (!s.cloud_layer[i])
        continue;
      for (int j = 0; j < s.cloud[i].n; j++)
        smallest = std::min<int>(smallest, s.cloud[i].e[j].rx);
    }
  }
  CHECK(smallest >= 4, "a cloud lobe %d wide", smallest);
  // the Christmas tree has lights on both sides
  draw(wx("cloudy", 95, -3, 2, 200), utc(2026, 12, 24, 16), {});
  int left = 0, right = 0, tree = -1;
  for (int i = 0; i < s.ntrees; i++)
    if (s.tree[i].layer == 2 && s.tree[i].kind == 0 &&
        (tree < 0 || abs(s.tree[i].x - s.home_x) < abs(s.tree[tree].x - s.home_x)))
      tree = i;
  for (int i = 0; i < s.nsprites; i++)
    if (s.sprite[i].w == 2 && s.sprite[i].h == 2)
      (s.sprite[i].x < s.tree[tree].x ? left : right)++;
  printf("  Christmas lights: %d on the left, %d on the right\n", left, right);
  CHECK(left >= 5 && right >= 5, "Christmas lights: %d on the left, %d on the right", left, right);
}

static void picture_cleared_mid_anim() {
  step("clearing the picture while an animation is on its way");
  draw_dusk(panel_picture_dusk, 200, false);
  panel_picture.control(1);
  CHECK(picture("dusk before the animation", 6000) >= 0, "dusk not shown");
  panel_anim_noise.turn_on();  // big enough to take a while
  int r = run_until([] { return anim::W.t.head && anim::W.t.pos > 0 && anim::W.t.pos < anim::W.a->data.size(); }, 3000);
  CHECK(r >= 0, "the animation never went out");
  panel_picture.control(0);
  r = run_until(anim_shown, 3000);
  run(3);
  CHECK(r >= 0 && anim_ready() && panel_pic() == 0,
        "after clearing the picture on the animation's way: '%s', animation %d, picture %d",
        panel_animation_status.state.c_str(), anim_ready(), panel_pic());
  panel_picture.control(1);
  CHECK(picture("dusk again", 3000) >= 0, "the picture is stuck behind the animation");
}

static void end_lost() {
  step("the panel's answer to END gets lost");
  static int lose;  // DONE acks to lose
  auto &h = panel::handlers[K_PIC_ACK];
  auto real = h;
  h = [real](uint32_t a) {
    if (a >> 24 == PIC_DONE && lose > 0)
      lose--;
    else
      real(a);
  };
  lose = 1;
  draw_dusk(panel_picture_dusk, 210, false);
  pic::show("dusk");
  CHECK(picture("its answer lost", 3000) >= 0 && !lose, "the picture not sent again");
  CHECK(pic::S.t.tries == 0, "%d tries left over for the next transfer", pic::S.t.tries);
  lose = 1;
  panel_anim_noise.turn_off();
  panel_anim_ball.turn_on();
  int r = until_anim_status("Shown: ball");
  CHECK(r >= 0 && !lose && same_anim("ball", 0), "the animation not sent again: %s",
        panel_animation_status.state.c_str());
  printf("  ball: %s after %d cycles\n", panel_animation_status.state.c_str(), r);
  lose = 3;  // gives up after three tries, tries again after a panel restart
  panel_anim_ball.turn_off();
  run(3);
  panel_anim_ball.turn_on();
  r = run_until([] { return anim::find("ball") && anim::find("ball")->state == anim::FAILED; }, 3000);
  CHECK(r >= 0 && !lose && panel_animation_status.state == "ball: the panel didn't answer", "lost answers: %s",
        panel_animation_status.state.c_str());
  pn_restart();
  r = until_anim_status("Shown: ball");
  CHECK(r >= 0 && anim_ready() && same_anim("ball", 0), "not tried again after a panel restart: %s",
        panel_animation_status.state.c_str());
  h = real;
}

static void unknown_names() {
  step("names that aren't there, and none");
  pic::show("nope");
  anim::show("nope");
  run(10);
  CHECK(logged("No picture called 'nope'") && logged("No animation called 'nope'"), "no warnings about the names");
  CHECK(panel_picture.state == "dusk" && panel_picture_status.state.rfind("Shown: dusk", 0) == 0 && panel_pic() == 2 &&
            panel_anim_ball.state && anim_ready(),
        "an unknown name changed what's shown: '%s' '%s', %d '%s'", panel_picture.state.c_str(),
        panel_picture_status.state.c_str(), panel_anim_ball.state, panel_animation_status.state.c_str());
  anim::clear("");  // panel_clear_animation without a name
  pic::show("");
  run(20);
  CHECK(panel_picture.state == "None" && panel_picture_status.state == "None" && panel_pic() == 0,
        "picture '%s' '%s' after no name", panel_picture.state.c_str(), panel_picture_status.state.c_str());
  CHECK(!panel_anim_ball.state && panel_animation_status.state == "None" && !anim_ready(),
        "animation %d '%s' after no name", panel_anim_ball.state, panel_animation_status.state.c_str());
  panel_picture.control(1);
  CHECK(picture("dusk", 6000) >= 0, "dusk not shown");
  panel_picture.control(0);  // None
  run(20);
  CHECK(panel_pic() == 0, "picture still there after clearing");
}

static void cleared_and_shown() {
  step("the same picture cleared and shown again before the loop runs");
  panel_picture.control(1);
  CHECK(picture("dusk", 6000) >= 0, "dusk not shown");
  pic::clear();
  pic::show("dusk");
  CHECK(picture("dusk after clear and show", 3000) >= 0, "the panel has no picture after clear and show: '%s'",
        panel_picture_status.state.c_str());
}

// Like online_picture.yaml's source: size() is false while a download runs or after one failed, and the pixels are
// gone. The sim converts a picture in one go, so the download starts from inside pixel().
static void online_again() {
  step("an online picture downloaded again while it's being made");
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

static void online_grew() {
  step("a shown online picture that grew too big for the ESP");
  // 200 KB on the server
  static int asked = 0, fetched = 0;
  auto o = std::make_shared<pic::Online>();
  o->every = 1000;
  o->format = "JPEG";
  o->head = [](const std::string &, size_t &n, std::string &) {
    asked++;
    n = 200 * 1024;
    return 200;
  };
  o->busy = [] { return false; };
  o->fetch = [] {
    fetched++;
    return true;
  };
  pic::online["flaky"] = o;
  run(30);
  CHECK(asked >= 3 && asked <= 5 && !fetched && panel_picture_status.state.rfind("flaky: too big", 0) == 0,
        "asked %d times, fetched %d, status '%s'", asked, fetched, panel_picture_status.state.c_str());
  pic::online.erase("flaky");
}

static void online_too_big() {
  step("online pictures: one too big for the ESP, and a Home Assistant camera's token");
  panel_http.length = 120 * 1024;  // a JPEG, kept whole
  panel_http.type = "image/jpeg";
  pic::show("radar");
  run(10);
  CHECK(!panel_picture_radar.updates && panel_picture_status.state.rfind("radar: too big for the ESP", 0) == 0,
        "a download too big for the ESP: %d tries, '%s'", panel_picture_radar.updates,
        panel_picture_status.state.c_str());
  ram_free = 400 * 1024;
  ram_block = 200 * 1024;
  run(300);  // retried after 30 s
  CHECK(panel_picture_radar.updates == 1, "%d downloads once it fits", panel_picture_radar.updates);
  draw_dusk(panel_picture_radar, 180, false);
  panel_picture_radar.decoding = false;
  panel_picture_radar.on_download_finished();
  CHECK(picture("radar", 6000) >= 0, "radar not shown after its download");
  ram_free = 200 * 1024;
  ram_block = 100 * 1024;
  ha_front_door_token.publish_state("t0k3n");
  CHECK(panel_picture_front_door.url ==
            "http://homeassistant.local:8123/api/camera_proxy/camera.front_door?width=320&token=t0k3n",
        "the camera's URL with its token: %s", panel_picture_front_door.url.c_str());
  panel_picture.control(0);
  run(20);
}

static void online_downloads() {
  step("an online picture's downloads and refreshes");
  pic::add_online_image("webcam", webcam, cam_server, "http://x/webcam", "10min", "auto", "160x120", "auto", "auto");
  pic::Online &o = *pic::online["webcam"];
  CHECK(o.every == 600000 && o.pixels == 160 * 120 * 2 && o.format == "AUTO", "webcam: every %u, %u bytes, %s",
        (unsigned) o.every, (unsigned) o.pixels, o.format.c_str());
  static const struct {
    const char *every;
    uint32_t ms;
  } EVERY[] = {{"1d", 86400000}, {"1.5h", 5400000}, {"2 min", 120000}, {"500ms", 500},
               {"30s", 30000},   {"never", 0},      {"5", 0},          {"5 parsecs", 0}};
  for (auto e : EVERY)
    CHECK(pic::interval_ms(e.every) == e.ms, "update_interval '%s' read as %u ms, not %u", e.every,
          (unsigned) pic::interval_ms(e.every), (unsigned) e.ms);
  auto shown = [](int max) {
    run(3);
    return run_until([] { return panel_picture_status.state.rfind("Shown: webcam", 0) == 0 && settled(); }, max);
  };
  pic::show("webcam");
  run(3);
  CHECK(cam_server.heads == 1 && webcam.updates == 1 && panel_picture_status.state == "webcam: waiting for it",
        "not downloaded when shown: asked %d, %d downloads, '%s'", cam_server.heads, webcam.updates,
        panel_picture_status.state.c_str());
  draw_photo(webcam, 160, 120);  // the download came
  webcam.decoding = false;
  pic::changed("webcam");
  CHECK(shown(3000) >= 0 && pn_pic_format() == PIC_JPEG, "webcam not shown as a JPEG: '%s'",
        panel_picture_status.state.c_str());
  CHECK(o.kept.buffer == 30000 && o.kept.decoder == 'J' && !webcam.w,
        "download buffer %u, decoder %c, pixels %dx%d kept", (unsigned) o.kept.buffer, o.kept.decoder, webcam.w,
        webcam.h);

  // the same picture downloaded again: made again, nothing sent
  size_t mark = log_all.size();
  uint8_t ids = pic::transfer_ids;
  o.fetched = millis() - o.every;
  run(1);
  CHECK(cam_server.heads == 2 && webcam.updates == 2, "not downloaded again after update_interval");
  draw_photo(webcam, 160, 120);
  webcam.decoding = false;
  pic::changed("webcam");
  bool ok = shown(3000) >= 0;
  int made = times_logged("Picture webcam: 320x240 as a JPEG", mark);
  CHECK(ok && made == 1 && pic::transfer_ids == ids, "the same JPEG: made %d times, %d transfers", made,
        (uint8_t) (pic::transfer_ids - ids));

  // a failed refresh: the panel keeps the picture until the next one
  o.fetched = millis() - o.every;
  run(1);
  webcam.decoding = false;
  pic::failed("webcam");
  run(20);
  CHECK(cam_server.heads == 3 && webcam.updates == 3 && panel_picture_status.state.rfind("Shown: webcam", 0) == 0 &&
            panel_pic() == 2,
        "after a failed refresh: asked %d, %d downloads, '%s'", cam_server.heads, webcam.updates,
        panel_picture_status.state.c_str());

  // a failed first download: the reason stays, the next try waits
  pic::show("dusk");
  CHECK(picture("dusk", 6000) >= 0, "dusk not shown");
  o.at = millis();
  pic::show("webcam");
  run(3);
  CHECK(webcam.updates == 4, "webcam not downloaded");
  webcam.decoding = false;
  pic::failed("webcam");
  run(20);
  CHECK(webcam.updates == 4 && panel_picture_status.state == "webcam: couldn't download it, trying again later",
        "after a failed download: %d downloads, '%s'", webcam.updates, panel_picture_status.state.c_str());
}

// What the server says before a download
static void online_fits() {
  step("an online picture's RAM checks");
  pic::Online &o = *pic::online["webcam"];
  auto fits = [&](int c, size_t n, const char *t) {
    cam_server.code = c;
    cam_server.size = n;
    cam_server.type = t;
    return pic::fits("webcam", o);
  };
  ram_free = 100 * 1024;
  CHECK(fits(200, 30000, "image/jpeg"), "a JPEG the size of the download buffer it has refused");
  CHECK(!fits(200, 40000, "image/jpeg") && pic::S.waiting.find("too big") == 0, "a bigger JPEG: '%s'",
        pic::S.waiting.c_str());
  ram_free = 200 * 1024;
  CHECK(fits(200, 150000, "image/png"), "a PNG bigger than the RAM refused, it's read 4 KB at a time");
  CHECK(!fits(200, 150000, "image/jpeg"), "a JPEG bigger than the RAM taken");
  CHECK(!fits(200, 0, "image/jpeg") && pic::S.waiting.find("doesn't say how big") != std::string::npos,
        "a JPEG without a size: '%s'", pic::S.waiting.c_str());
  CHECK(fits(200, 0, "image/png"), "a PNG without a size refused");
  CHECK(!fits(0, 0, "") && pic::S.waiting.find("couldn't reach") == 0, "no answer: '%s'", pic::S.waiting.c_str());
  CHECK(!fits(404, 100, "text/html") && pic::S.waiting.find("404") != std::string::npos, "a 404: '%s'",
        pic::S.waiting.c_str());
  o.format = "PNG";
  CHECK(fits(200, 150000, "image/jpeg"), "format PNG taken as a JPEG");
  o.format = "AUTO";
  // a download that doesn't start doesn't grow the buffer
  webcam.fail = true;
  cam_server.size = 40000;
  pic::download("webcam", o);
  webcam.fail = false;
  CHECK(o.kept.buffer == 30000, "a download that didn't start grew the buffer to %u", (unsigned) o.kept.buffer);
  pic::download("webcam", o);
  int updates = webcam.updates, asked = cam_server.heads;
  cam_server.size = 50000;
  pic::download("webcam", o);  // while that one still runs
  webcam.decoding = false;
  CHECK(o.kept.buffer == 40000 && webcam.updates == updates && cam_server.heads == asked,
        "buffer %u after two downloads, %d started, the server asked %d times", (unsigned) o.kept.buffer,
        webcam.updates - updates + 1, cam_server.heads - asked + 1);
  // the first PNG needs room for its decoder too
  ram_free = 120 * 1024;
  CHECK(!fits(200, 5000, "image/png") && fits(200, 30000, "image/jpeg"), "a first PNG's decoder not counted");
  ram_free = 200 * 1024;
  cam_server.type = "image/png";
  pic::download("webcam", o);
  webcam.decoding = false;
  ram_free = 120 * 1024;
  CHECK(o.kept.decoder == 'P' && fits(200, 5000, "image/png"), "a PNG refused with its decoder there");
  ram_free = 200 * 1024;
  cam_server.code = 200;
  cam_server.size = 30000;
  cam_server.type = "image/jpeg";
}

// Another picture picked while it's being made: its pixels are freed
static void online_picked() {
  step("another picture picked while an online one is made");
  draw_flat(webcam);
  pic::show("webcam");
  online_pick_after = 1000;
  CHECK(picture("dusk, picked while webcam was being made", 6000) >= 0 && !webcam.w && !online_pick_after,
        "webcam's pixels kept: %dx%d", webcam.w, webcam.h);
}

static void picture_no_ram() {
  step("a picture the ESP doesn't have the RAM to make");
  pic::Source &dusk = pic::sources["dusk"];
  auto no_ram = [] {
    return run_until([] { return panel_picture_status.state == "dusk: the ESP doesn't have the RAM to make it"; },
                     100) >= 0 &&
           pic::S.phase == pic::IDLE;
  };
  ram_free = 30 * 1024;  // not even for counting its colours
  draw_dusk(panel_picture_dusk, 215, false);
  pic::show("dusk");
  CHECK(no_ram(), "too little RAM to count: '%s', phase %d", panel_picture_status.state.c_str(), pic::S.phase);
  // Enough to count its colours, then the ESP has less: for 16 colours, not a JPEG
  ram_free = 40 * 1024;
  auto pixel = dusk.pixel;
  int reads = 0;
  dusk.pixel = [&](int x, int y) {
    if (++reads == 320 * 240)
      ram_free = 28 * 1024;
    return pixel(x, y);
  };
  for (int x = 0; x < 320; x++)  // 16 reds on top, over 16 colours in all, so it tries a JPEG too
    for (int y = 0; y < 8; y++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = (uint32_t) (x / 20 * 17) << 16;
  size_t no_jpeg = log_all.size();
  pic::show("dusk");
  CHECK(picture("dusk without the RAM for a JPEG", 6000) >= 0 && pn_pic_format() == PIC_PALETTE &&
            logged("no RAM for a JPEG", no_jpeg),
        "dusk not shown as 16 colours");
  ram_free = 40 * 1024;
  reads = 0;
  draw_photo(panel_picture_dusk, 320, 240);  // too detailed for 16 colours
  pic::show("dusk");
  CHECK(no_ram(), "a photo without the RAM for a JPEG: '%s', phase %d", panel_picture_status.state.c_str(),
        pic::S.phase);
  dusk.pixel = pixel;
  ram_free = 200 * 1024;
  draw_dusk(panel_picture_dusk, 215, false);
  pic::show("dusk");
  CHECK(picture("dusk with the RAM back", 6000) >= 0, "dusk not shown");
}

static void actions_kept() {
  step("the actions are kept for after a restart, like the selects");
  pic::clear();
  run(3);
  CHECK(panel_picture.saved == "None", "clear: '%s' kept", panel_picture.saved.c_str());
  pic::show("dusk");
  CHECK(picture("dusk", 6000) >= 0 && panel_picture.saved == "dusk", "show: '%s' kept", panel_picture.saved.c_str());
  anim::show("ball");
  CHECK(panel_anim_ball.state, "animation: the switch isn't on");
  panel_clear_animation.execute("");
  CHECK(!panel_anim_ball.state, "animation cleared: the switch is still on");
  run(10);
}

static void number_late() {
  step("a number whose entity answers late");
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

static void esp_restart_newer_setting() {
  step("the ESP restarts while the panel has a newer unit setting, in Norwegian");
  server->hold[0x00D] = 150;  // the ESP's flash copy of the supply min
  pn_set_reg(0x00D, 160);     // changed on the panel while the ESP was down
  pn_set(K_LANGUAGE, 1);
  run(20);
  panel::language = (uint8_t) panel_language;  // what setup() has after a restart
  panel::seen.reset();
  panel::link::send(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK);  // what panel::setup() sends
  int english = 0;                                                         // cycles the panel showed an item in English
  for (int k = 0; k < 100; k++) {
    run(1);
    english += !strcmp(pitem("fireplace").label, "Fireplace");
  }
  CHECK(!english && !strcmp(pitem("fireplace").label, "Peis"), "in English for %d cycles after the hello, then '%s'",
        english, pitem("fireplace").label);
  CHECK(pn_reg(0x00D) == 160, "the ESP's hello put its old supply min %d on the panel", pn_reg(0x00D));
  int c = restart_panel();  // the panel asks, and keeps its copy over the ESP's flash one
  run(15);
  CHECK(c >= 0 && pn_reg(0x00D) == 160, "the panel restarted and took the ESP's old supply min %d", pn_reg(0x00D));
  int supply_max = pn_reg(0x00E) + 10;
  server->setting(0x00E, (uint16_t) supply_max);  // changed on another panel while this one restarts
  c = restart_panel();
  run(15);
  CHECK(c >= 0 && pn_reg(0x00E) == supply_max, "the panel restarted and has supply max %d, the ESP saw %d",
        pn_reg(0x00E), supply_max);
  server->setting(0x0B5, 75);  // a regulator value, which has no link key: only the register goes
  c = restart_panel();
  run(15);
  CHECK(c >= 0 && pn_reg(0x0B5) == 75, "the panel restarted and has heating K %d, the ESP saw 75", pn_reg(0x0B5));
}

static void notes_while_away() {
  step("notes while the panel is away or restarting");
  int icon, level, fresh;
  pn_note(&icon, &level, &fresh);
  link_lost_frames = 1000;  // gone
  run(300);
  CHECK(!panel::link::connected, "the panel's still connected");
  panel::notify("Door open", "door", "warning");
  run(10);
  CHECK(!panel::link::queued(K_NOTIFY), "a note queued for a panel that's away");
  // nothing made for it meanwhile
  anim::clear("");
  pic::show("weather");
  panel_anim_ball.turn_on();
  run(50);
  CHECK(pic::S.phase == pic::START && !met::drawing && anim::W.phase == anim::IDLE &&
            panel_picture_status.state == "weather: waiting for the panel",
        "made for a panel that's away: '%s', '%s'", panel_picture_status.state.c_str(),
        panel_animation_status.state.c_str());
  link_lost_frames = 0;
  restart_panel();
  const char *away = pn_note(&icon, &level, &fresh);
  CHECK(fresh && !strcmp(away, "Door open") && panel::note.empty(), "note while away: '%s', fresh %d", away, fresh);
  CHECK(picture("weather, the panel back", 6000) >= 0 && anim::find("ball") && anim::find("ball")->state == anim::SENT,
        "not made once the panel was back: '%s'", panel_animation_status.state.c_str());
  panel::notify("Washer done", "washer", "info");
  restart_panel();  // before it went out
  const char *restart = pn_note(&icon, &level, &fresh);
  CHECK(fresh && !strcmp(restart, "Washer done"), "note during a restart: '%s', fresh %d", restart, fresh);
}

static void bootloader_away() {
  step("the panel counts as away once it's in its bootloader");
  auto slow = [](int f, int x, int y) {
    delayMicroseconds(1);  // about a frame a slice, so it's made over many loops
    return noise_px(f, x, y);
  };
  anim::clear("");
  run(5);
  anim::add("slow", {64, 64, 6, 100, 16, 16, 1, slow});
  anim::show("slow");
  panel::loop();
  bool making = anim::W.phase == anim::COUNT || anim::W.phase == anim::MAKE;
  server->updating_ = true;  // update.h has the bus
  panel::loop();
  CHECK(making && !panel::link::connected && anim::W.phase == anim::IDLE && anim::find("slow")->state == anim::WANT &&
            anim::find("slow")->data.empty(),
        "still made for a panel in its bootloader: making %d, connected %d, phase %d", making, panel::link::connected,
        anim::W.phase);
  run(50);
  CHECK(anim::W.phase == anim::IDLE, "made again while in its bootloader: phase %d", anim::W.phase);
  server->updating_ = false;
  restart_panel();  // the new firmware
  CHECK(run_until([] { return anim::find("slow")->state == anim::SENT && settled(); }, 3000) >= 0,
        "not made once the panel was back: '%s'", panel_animation_status.state.c_str());
}

static void settings_flash() {
  step("the panel's own settings over a flash");
  panel::set("theme", 2);
  panel::set("screensaver", 3);
  run_until(settled, 300);
  panel::settings::save();  // update.h, before it flashes
  int r = run_until([] { return panel::settings::saved() && settled(); }, 300);
  CHECK(r >= 0 && panel::settings::all && panel::settings::records.size() > 20 && logged("Saved the panel's"),
        "the panel's settings: %d records after %d cycles", (int) panel::settings::records.size(), r);
  panel::settings::flashed();
  size_t sent = log_all.size();
  pn_reboot(1);  // the flash erased them
  CHECK(pn_get(K_THEME) == 0, "theme %d after the flash", pn_get(K_THEME));
  r = run_until([] { return pn_get(K_THEME) == 2 && pn_get(K_SCREENSAVER) == 3 && settled(); }, 300);
  CHECK(r >= 0 && logged("started without settings, sending its", sent),
        "not back after the flash: theme %d, screensaver %d", pn_get(K_THEME), pn_get(K_SCREENSAVER));
  pn_reboot(0);  // the restart it does to take them, saved
  run_until([] { return pn_online() && settled(); }, 300);
  CHECK(!panel::settings::restore && panel::settings::records.empty() && logged("back with its settings", sent),
        "the ESP kept the panel's settings once it had them");
  pn_reboot(1);  // erased without our flash, nothing to send
  run_until([] { return pn_online() && settled(); }, 300);
  CHECK(pn_get(K_THEME) == 0, "theme %d from nowhere", pn_get(K_THEME));
  pn_reboot(0);
  run_until([] { return pn_online() && settled(); }, 300);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: esp_test OUTDIR [PICTURE.ppm]\n");
    return 2;
  }
  outdir = argv[1];
  live_dir = std::string(outdir) + "/live";
  sim_mkdir(outdir);
  sim_mkdir(live_dir.c_str());
  boot();
  hello();
  if (argc > 2)
    return custom_picture(argv[2]);
  items();
  item_decimals();
  item_language();
  ha_sets_settings();
  menu_sets_settings();
  items_from_panel();
  ha_switch();
  text_item();
  ha_entities();
  comfort();
  master_only();
  actions_and_notes();
  cx60_restarts();
  frames_lost();
  link_refused();
  pictures();
  pictures_jpeg();
  animations();
  anim_moving();
  anim_copies();
  anim_turning();
  anim_wandering();
  anim_waved();
  anim_turned_down();
  anim_one_round();
  anim_two();
  anim_half_size();
  anim_not_fitting();
  anim_cleared_while_made();
  anim_no_room();
  anim_repeats();
  anim_esp_restart();
  anim_hello_while_made();
  anim_off_screen();
  anim_shown_again();
  anim_timing();
  anim_without_ram();
  anim_past_pool();
  anim_waves_settling();
  anim_on_its_way();
  met_requests();
  met_reading();
  met_picture();
  met_answers();
  met_forecast_hours();
  met_date_line();
  live_sky();
  live_land();
  live_town();
  live_days();
  live_kept();
  live_costs();
  weather_scene();
  picture_cleared_mid_anim();
  end_lost();
  unknown_names();
  cleared_and_shown();
  online_again();
  online_grew();
  online_too_big();
  online_downloads();
  online_fits();
  online_picked();
  picture_no_ram();
  actions_kept();
  number_late();
  esp_restart_newer_setting();
  notes_while_away();
  bootloader_away();
  settings_flash();
  printf("\n%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
