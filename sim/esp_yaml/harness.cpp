// Runs the ESP's panel packages, turned into C++ by gen.py, against the panel's own link code on a fake CX60.
// micros() and millis() run on simulated time.
//   build/esp_test OUTDIR [PICTURE.ppm]
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include "gen_panel.cpp"
#include "check.h"
#include "cx60.h"
#include "met_case.h"
#include "panel_side.h"
#include "sim.h"

namespace panel = flexit::panel;
namespace pic = flexit::panel::picture;
namespace overlay = flexit::panel::overlay;
namespace met = flexit::panel::met;
namespace fetch = flexit::panel::fetch;
namespace live = flexit::panel::met::live;
namespace space = flexit::panel::space;
namespace marks = flexit::panel::marks;

static void step(const char *what) { printf("%s\n", what); }

static const char *outdir;    // the screenshots
static std::string live_dir;  // the weather shows', not in the goldens: they move by chance

// ---------------------------------------------------------------- stubs
int log_level = 3;
std::string log_all;
static uint32_t now_us;
static uint32_t micros_step;  // each micros() this much later, to see a loop give way after its slice
// millis() goes round at 2^32 ms as the ESP's does, not where now_us does, and 30 s into the run so the code meets it
static uint64_t wrapped_us = (1ull << 32) * 1000 - 30000000;
uint32_t millis() {
  static uint32_t last;
  if (now_us < last)
    wrapped_us += 1ull << 32;
  last = now_us;
  return (uint32_t) ((wrapped_us + now_us) / 1000);
}
uint32_t micros() { return now_us += micros_step; }
static uint32_t waited_us;
void delayMicroseconds(uint32_t us) {
  now_us += us;
  waited_us += us;
}
// FreeRTOS: a new task waits until run_task() runs it, to its end, and fetch.h's answer is taken as the next loop
// would, but for the pictures' questions, see run_asks(). Its stack comes from the fake heap.
static std::vector<std::pair<TaskFunction_t, void *>> tasks;
BaseType_t xTaskCreate(TaskFunction_t f, const char *, uint32_t stack, void *arg, UBaseType_t, TaskHandle_t *) {
  if (stack > ram_block)
    return 0;
  tasks.push_back({f, arg});
  return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 0; }
static int stack_full_at = -1;  // the time it's asked for, from 0, that the stack has nothing left. -1 = never
uint8_t *pxTaskGetStackStart(TaskHandle_t) {
  uint8_t *here = (uint8_t *) __builtin_frame_address(0);
  return stack_full_at >= 0 && !stack_full_at-- ? here : here - (1u << 20);
}
static void run_task() {
  if (tasks.empty())
    return;  // a CHECK before found it missing
  auto t = tasks.front();
  tasks.erase(tasks.begin());
  t.first(t.second);
  fetch::loop();
}
// The pictures' questions to their server, the others wait for run_task()
static void run_asks() {
  while (!tasks.empty() && fetch::running && dynamic_cast<pic::Online *>(fetch::running->by))
    run_task();
}

// ESP-IDF's HTTP client, answering from web
static Web web;
struct HttpClient {
  esp_http_client_config_t config;
  std::string url;  // config's, or where it was moved to
  std::vector<http_request::Header> headers;
  std::shared_ptr<HttpContainer> c;
  std::string left;  // of an answer that wasn't read to its end: still on the connection, ahead of the next one's
};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
  return new HttpClient{*config, config->url, {}, nullptr, {}};
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value) {
  c->headers.push_back({key, value});
  return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int) {
  if (c->c)  // the same server at every URL, so the connection is kept
    c->left += c->c->body.substr(std::min(c->c->at, c->c->body.size()));
  c->c = web.get(c->url, c->headers, {});
  return c->c ? ESP_OK : ESP_FAIL;
}
esp_err_t esp_http_client_flush_response(esp_http_client_handle_t c, int *) {
  c->c->at = c->c->body.size();
  return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) {
  for (const char *h : {"Content-Type", "Last-Modified", "Expires", "Retry-After"}) {  // as servers write them
    std::string k = h, v = c->c->get_response_header(h[0] == 'C'   ? "content-type"
                                                     : h[0] == 'L' ? "last-modified"
                                                     : h[0] == 'E' ? "expires"
                                                                   : "retry-after");
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
  if (!c->left.empty()) {
    size_t n = std::min(c->left.size(), (size_t) len);
    memcpy(buf, c->left.data(), n);
    c->left.erase(0, n);
    return (int) n;
  }
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
static bool loop_between;            // the panel's loop also runs between the two windows, as on the panel
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
  CHECK_MSG(server->frame_length && server->frame_length(f.data(), f.size()) == f.size(), "the component can't size %d",
            n);
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
  if (loop_between)
    pn_poll();
  now_us = end + link_quiet;  // the poll, and the panel's reply
  on_bus(POLL, sizeof POLL, true);
  now_us += 500;
  on_bus(REPLY, sizeof REPLY, true);
  for (int k = 0; k < 8; k++) {  // the gap after it, read every loop as the component does, so frames can follow
    for (auto &f : server->read_cbs)
      f();
    if (server->sent.empty())
      break;
    gap_frames++;
    link_frame();
  }
}

// Whether the panel's clock goes on twice a cycle, so its animations play as they do on the panel, rather than once a
// second, see play()
static bool playing;
static void run(int n) {  // n CX60 cycles of 110 ms
  for (int c = 0; c < n; c++) {
    uint32_t t0 = now_us;
    cx60_cycle();
    for (int k = 0; k < 3; k++) {
      now_us += 20000;
      yaml_loop();
      yaml_interval();
      run_asks();
    }
    pn_poll();
    for (int r = 0; r < 0x200; r++)
      server->coil[r] = false;  // the CX60 has read everything flagged
    for (int k = 0; k < 2 && playing; k++)
      pn_tick(PBUS_CYCLE_US / 2000);
    if (++cycles % 9 == 0)
      pn_second(!playing);
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
  bool hidden;  // a part of the picture in front of its middle, see follow()
};
// What Panel Animation Status lists after how full the panel is, None as it is
static std::string anim_list() {
  const std::string &s = panel_animation_status.state;
  size_t at = s.find(" KB: ");
  return at == std::string::npos ? s : s.substr(at + 5);
}
static Look look(int p) {
  Look l{};
  l.shown = pn_anim(p, &l);
  return l;
}
static bool overlay_ready() {  // the panel shows an animation somewhere
  for (int p = 0; p < OVERLAY_PLACES; p++)
    if (look(p).shown)
      return true;
  return false;
}
static std::string places() {  // what the panel shows where
  std::string s;
  for (int p = 0; p < OVERLAY_PLACES; p++) {
    Look l = look(p);
    if (l.shown)
      s += "[" + std::to_string(p) + " " + std::to_string(l.x) + "," + std::to_string(l.y) + "]";
  }
  return s;
}
static bool same_pool() { return pn_overlay_used() == overlay::used_by_others(nullptr); }

// The animations play for ms, in the panel's motion steps
static void steps(int ms) {
  for (int t = 0; t < ms; t += 50)
    pn_overlay_step(50);
}

static bool overlay_shown() {  // the ESP has one on the panel
  for (auto &a : overlay::overlays)
    if (!a.name.empty() && a.state == overlay::SENT)
      return settled();
  return false;
}

// Runs until the ESP says the animations are want and the link is quiet. The cycles it took, -1 if never.
static int until_anim_status(const char *want, int max = 3000) {
  return run_until([want] { return anim_list() == want && settled(); }, max);
}

// Every animation the ESP wants is on the panel
static bool anims_sent() {
  for (auto &a : overlay::overlays)
    if (!a.name.empty() && a.state == overlay::WANT)
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
  return panel::ycc(d, d);
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
  CHECK_MSG(r < 0 || same_picture(), "%s: the panel draws something else than the ESP's picture", name);
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

// A square in a colour of its own each frame, so the frames can be told apart
static void draw_squares(Animation &a, int size, int frames) {
  a.w = a.h = size;
  a.frames.assign(frames, std::vector<uint32_t>((size_t) size * size, 0));
  for (int f = 0; f < frames; f++)
    for (int y = 2; y < size - 2; y++)
      for (int x = 2; x < size - 2; x++)
        a.frames[f][(size_t) y * size + x] =
            0xFF000000u | (uint32_t) (40 + f * 30) << 16 | (uint32_t) (230 - f * 25) << 8 | (f & 1 ? 0xE0u : 0x20u);
}

// The delays of animation name's records, as the panel plays them
static std::vector<int> overlay_delays(const char *name) {
  std::vector<int> d;
  const overlay::Overlay *a = overlay::find(name);
  for (size_t at = 0; a && at + OVERLAY_RECORD_HEAD <= a->data.size();) {
    const uint8_t *r = a->data.data() + at;
    d.push_back(r[OVERLAY_RECORD_DELAY]);
    at += OVERLAY_RECORD_HEAD + plink_get16(r + OVERLAY_RECORD_LEN);
  }
  return d;
}

// Whether the panel draws frame f of animation `name` at place p as the ESP made it: transparent in the same
// places and palette colours elsewhere. Says where not unless quiet.
static bool same_anim(const char *name, int f, int p = 0, bool quiet = false) {
  overlay::Overlay *a = overlay::find(name);
  if (!a)
    return false;
  auto &A = *a;
  overlay::Overlay *was = overlay::W.a;
  overlay::W.a = a;  // px() goes by it
  struct Back {
    overlay::Overlay *was;
    ~Back() { overlay::W.a = was; }
  } back{was};
  // its colour map, made again as count() and palette() make it, since the ESP frees it
  std::vector<uint32_t> hist(4096);
  bool clear = false;
  for (int g = 0; g < A.src->frames; g++)
    for (int y = 0; y < A.h; y++)
      for (int x = 0; x < A.w; x++) {
        uint32_t c = overlay::px(g, x, y);
        if (c >> 24 >= 128)
          hist[panel::bin(c)]++;
        else
          clear = true;
      }
  uint32_t pal[16] = {};
  std::vector<uint16_t> bins;
  int colours = panel::median_cut(hist, clear ? 15 : 16, pal, bins);
  for (int i = 0; i < colours; i++)
    pal[i] |= 0xFF000000u;
  if (memcmp(pal, A.pal, sizeof pal) || (A.still || A.sheet) != (look(p).still != 0))
    return false;
  std::vector<uint8_t> lut(4096);
  for (int i = 0; i < 4096; i++)
    lut[i] = (uint8_t) panel::nearest(panel::bin_rgb(i), A.pal, colours);
  for (int y = 0; y < A.h * A.scale; y++) {
    uint8_t line[320 * 3];
    if (pn_overlay_line(p, y, A.w, A.scale, line) != A.w * A.scale)
      return false;
    for (int x = 0; x < A.w * A.scale; x++) {
      uint32_t c = overlay::px(f, x / A.scale, y / A.scale);
      uint16_t want = sim_rgb565(A.pal[lut[panel::bin(c)]]), got = line[3 * x] | line[3 * x + 1] << 8;
      bool see = c >> 24 < 128;
      if (see != (line[3 * x + 2] == 0) || (!see && (got != want || line[3 * x + 2] != 255))) {
        if (!quiet)
          printf("  frame %d pixel %d,%d: the panel draws %04x/%d, the ESP made %04x/%d\n", f, x, y, got,
                 line[3 * x + 2], want, !see);
        return false;
      }
    }
  }
  return true;
}

// RGB565 colours a and b no more than near steps apart in red and blue, twice that in green
static bool near565(int a, int b, int near) {
  return abs((a >> 11) - (b >> 11)) <= near && abs((a >> 5 & 63) - (b >> 5 & 63)) <= 2 * near &&
         abs((a & 31) - (b & 31)) <= near;
}

// How many of the pixels place p of animation a draws are near rgb, and how many it draws
static std::pair<int, int> draws(const overlay::Overlay &a, int p, uint32_t rgb, int near = 0) {
  uint8_t line[320 * 3];
  int n = 0, all = 0;
  for (int y = 0; y < a.h && pn_overlay_line(p, y, a.w, 1, line) == a.w; y++)
    for (int x = 0; x < a.w; x++)
      if (line[3 * x + 2]) {
        all++;
        n += near565(line[3 * x] | line[3 * x + 1] << 8, sim_rgb565(rgb), near);
      }
  return {n, all};
}

// Whether each pixel place p of animation a draws is a grey
static bool all_grey(const overlay::Overlay &a, int p) {
  uint8_t line[320 * 3];
  for (int y = 0; y < a.h && pn_overlay_line(p, y, a.w, 1, line) == a.w; y++)
    for (int x = 0; x < a.w; x++) {
      int c = line[3 * x] | line[3 * x + 1] << 8, k = c >> 11;
      if (line[3 * x + 2] && !near565(c, k << 11 | k << 6 | k, 1))
        return false;
    }
  return true;
}

// ---------------------------------------------------------------- the weather picture and its shows
static const std::string &met_status = panel_weather.state;  // what met.h said last, in met.yaml's Panel Weather

// A real answer kept in esp_yaml: met_sample.json is the first 16 KB of api.met.no's, kp_sample.json NOAA's Kp from
// 1 to 11 November 2026, mars_sample.json five photos of NASA's list of Perseverance's, epic_sample.json three of
// EPIC's day, perseverance_sample.json and curiosity_sample.json some of each rover's waypoints, horizons_sample.txt
// JPL Horizons' for each probe, cut short
static std::string sample(const char *file) {
  std::ifstream in(std::string(SAMPLES) + file);
  std::stringstream body;
  body << in.rdbuf();
  return body.str();
}

// The scene of the weather picture the panel has, made again as met.h made it
static const met::Scene &panel_scene() {
  static SceneBuf sc;
  met::build(sc.s, met::weather, met::lat, met::lon, (uint32_t) flexit_panel_time.t.timestamp, false, met::utc_offset,
             met::options);
  return sc.s;
}

// A case of the weather animations: the acts it's of, " + " between their names, which are the only ones that may
// come on, the first naming its shots, or "" for any that fit. MET's symbol, the high, middle and low clouds (%), rain
// (mm), wind (m/s), temperature (C) and fog (%), the time, the landscape and the heater (%), how long it runs (s) and
// how many shots of it to take, and the place
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
    "waves at most", "No room for animation", "copies at most",   "too big",   "no room in the", "No weather animation",
    "no copy",       "doesn't fit",           "the panel didn't", "half size", "'s script"};

// The acts a case names
static std::vector<int> acts_of(const char *want) {
  std::vector<int> acts;
  for (std::string all = want; !all.empty();) {
    size_t end = std::min(all.find(" + "), all.size());
    int i = live::act_of(all.substr(0, end).c_str());
    CHECK_MSG(i >= 0, "no weather act called '%s'", all.substr(0, end).c_str());
    if (i >= 0)
      acts.push_back(i);
    all.erase(0, std::min(all.size(), end + 3));
  }
  return acts;
}
// Its first, which names the case
static std::string first_of(const char *want) { return std::string(want).substr(0, std::string(want).find(" + ")); }

static bool is_on(const char *act) { return live::on(live::act_of(act)); }
// Whether all the acts a case names are on
static bool all_on(const char *want) {
  for (int i : acts_of(want))
    if (!live::on(i))
      return false;
  return true;
}
// The acts on
static std::string cast_on() {
  std::string names;
  for (const live::On &o : live::R.cast)
    names += (names.empty() ? "" : ", ") + std::string(o.name);
  return names.empty() ? "nothing" : names;
}

// The acts' chance events due now, or one act's
static void chances_now(const char *act = nullptr) {
  for (auto &t : live::R.timers)
    if (t.mean && (!act || t.act == live::act_of(act)))
      t.at = millis();
}

// n cycles, each with one run of met.yaml's 50 ms interval, the shows' timers
static void tick(int n) {
  for (int k = 0; k < n; k++) {
    run(1);
    yaml_every(50);
  }
}

// What each act took of the panel at the most over all the cases so far, its visitors apart, and the most it was
// granted
struct Took {
  live::Room most{}, came{}, granted{};
};
static std::map<int, Took> acts_took;
static std::string over_grant;  // the first act that took more than it was granted, "" if none

// What the acts on take of the panel now, each one's animations, their copies and the pool's bytes: no more than it
// was granted, and with what came by, a visitor's more
static void acts_take() {
  bool copy_visits = (int32_t) (millis() - live::R.busy) < 0;  // a visitor that's a copy of what an act has on
  for (const live::On &o : live::R.cast) {
    live::Room own{}, came{};
    for (const live::Made &m : live::R.names) {
      const overlay::Overlay *a = m.act == o.act ? overlay::find(m.key) : nullptr;
      if (!a)
        continue;
      live::Room &r = live::R.comers.count(m.key.substr(2)) ? came : own;
      r.slots++;
      r.bytes += (int) (a->panel_frame() + a->data.size());
      for (const overlay::Copy &c : a->copies) {
        r.places += c.used;
        r.bytes += c.placed ? (int) ((c.own ? a->panel_frame() : 0) + overlay::pool_bytes(c)) : 0;
      }
    }
    Took &t = acts_took[o.act];
    auto most = [](const live::Room &a, const live::Room &b) {
      return live::Room{std::max(a.slots, b.slots), std::max(a.places, b.places), std::max(a.bytes, b.bytes)};
    };
    if (!copy_visits)
      t.most = most(t.most, own);
    t.came = most(t.came, came);
    t.granted = most(t.granted, o.grant);
    bool rains = live::ACTS[o.act].rank == live::WEATHER;  // its bolt comes on its own account
    const live::Room &guest = live::R.guest;               // what a visitor may take
    if (over_grant.empty() && !((copy_visits ? o.grant + guest : o.grant).has(rains ? own + came : own) &&
                                (o.grant + guest).has(own + came))) {
      char said[200];
      snprintf(said, sizeof said,
               "%s takes %d animations, %d copies and %d bytes, and %d, %d and %d that came by, "
               "granted %d, %d and %d",
               o.name, own.slots, own.places, own.bytes, came.slots, came.places, came.bytes, o.grant.slots,
               o.grant.places, o.grant.bytes);
      over_grant = said;
    }
  }
}

// seconds of the acts with the screensaver up, so they play, and shots of them spread over those as
// live/NAME_n.ppm. each after every cycle. None takes more of the panel than it was granted, and a visitor's.
static void play(int seconds, int shots, const std::string &name, const std::function<void()> &each = {}) {
  static std::set<std::string> taken;
  int every = shots ? seconds * 9 / shots : 0;  // 9 cycles a second
  pn_saver(1);
  playing = true;
  for (int k = 0, n = 0; k < seconds * 9; k++) {
    tick(1);
    acts_take();
    if (each)
      each();
    if (every && k % every == every - 1) {
      std::string shot;
      for (char c : name + "_" + std::to_string(n++))
        shot += isalnum((unsigned char) c) ? (char) tolower(c) : '_';
      CHECK_MSG(taken.insert(shot).second, "live/%s.ppm taken twice", shot.c_str());
      sim_shot(live_dir.c_str(), shot.c_str());
    }
  }
  playing = false;
  pn_saver(0);
  CHECK_MSG(over_grant.empty(), "%s: %s", name.c_str(), over_grant.c_str());
  over_grant.clear();
}

// Copy 0 of a weather sprite as the panel has it, or where the ESP placed it, as the panel may have moved it on
// already. False if the panel hasn't it.
enum At { NOW, PLACED };
static bool where(const char *sprite, Look &l, At at = NOW) {
  const overlay::Overlay *a = overlay::find(std::string("w_") + sprite);
  if (!a || a->state != overlay::SENT || a->copies.empty() || !a->copies[0].used)
    return false;
  const overlay::Copy &c = a->copies[0];
  l = look(c.place);
  if (at == PLACED) {
    l.x = c.x;
    l.y = c.y;
  }
  l.hidden = l.shown && pn_overlay_covered(c.place, l.x + a->w / 2, l.y + a->h / 2);
  return l.shown;
}
// Where copy 0 of a weather sprite has been on the panel, from where the ESP placed it, a place each cycle it showed
using Path = std::vector<Look>;
static void follow(const char *sprite, Path &p) {
  Look l;
  if (where(sprite, l, p.empty() ? PLACED : NOW) && l.opa > 0)
    p.push_back(l);
}
// Its row where it crossed column x, between the places either side, -1 if it didn't
static int row_at(const Path &p, int x) {
  for (size_t i = 1; i < p.size(); i++) {
    int x0 = p[i - 1].x, x1 = p[i].x;
    if (x0 != x1 && std::min(x0, x1) <= x && x <= std::max(x0, x1))
      return p[i - 1].y + (p[i].y - p[i - 1].y) * (x - x0) / (x1 - x0);
  }
  return -1;
}

// Nothing a show must never log since log_all was at from
static void live_quiet(const std::string &name, size_t from) {
  for (const char *bad : LIVE_BAD)
    CHECK_MSG(!logged(bad, from), "%s: '%s' in the log", name.c_str(), bad);
}

// MET's weather for a case
static met::Weather weather_for(const Live &c) {
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
  return w;
}

// The weather picture for c, drawn again from its weather, time and landscape
static void weather_of(const Live &c) {
  met::animate(false);
  met::weather = weather_for(c);
  met::lat = c.lat;
  met::lon = c.lon;
  flexit_panel_time.t.timestamp = c.t;
  met::options.ground = c.ground;
  met::options.heater_pct = c.heat;
  live::R.only = *c.want ? 0 : ~0ull;  // the acts it wants and no others
  for (int i : acts_of(c.want))
    live::R.only |= 1ull << i;
  live::R.pin = -1;
  pic::show("weather");
  met::animate(true);
}

// The acts c wants in its weather from their start, c.seconds of them with c.shots shots as live/FIRST[_TAG]_n.ppm,
// and their chance events due at once with chances. They're all on, still at the end, and ask the panel for no more
// than it has.
static void show(const Live &c, const char *tag = "", bool chances = false) {
  size_t from = log_all.size();
  std::string name = first_of(c.want) + (*tag ? "_" : "") + tag;
  weather_of(c);
  bool drawn = picture(name.c_str(), 6000) >= 0;
  CHECK_MSG(drawn && all_on(c.want), "%s: the weather has %s on, not %s", name.c_str(), cast_on().c_str(), c.want);
  if (chances)
    chances_now();
  play(c.seconds, c.shots, name);
  live_quiet(name, from);
  CHECK_MSG(all_on(c.want), "%s: %s on at the end, not %s", name.c_str(), cast_on().c_str(), c.want);
}

// The copies of lit weather sprites the panel shows in another light than the scene's, see in_light(). None while the
// link has something on its way.
static int out_of_light() {
  int n = 0;
  for (auto &[name, least] : live::R.lit)
    if (const overlay::Overlay *a = overlay::find(live::key(name)); a && a->state == overlay::SENT && settled())
      for (const overlay::Copy &c : a->copies) {
        Look l = c.used && c.placed ? look(c.place) : Look{};
        n += l.shown && l.light != live::light_of(least) * 10;
      }
  return n;
}

// The acts' own timers go ten minutes on, so what's tried now has the time
static void show_later() {
  for (auto &t : live::R.timers)
    if (t.act >= 0)
      t.at = millis() + 600000;
}

// The panel's animations
static int slots_used() {
  int n = 0;
  for (auto &a : overlay::overlays)
    n += !a.name.empty();
  return n;
}

// Whoever came by is gone, which the acts' own timers wait for before they bring the next
static void visitor_gone() {
  for (int wait = 0; wait < 9 * 150 && !live::R.comers.empty(); wait++)
    tick(1);
}

// Visitor k of an act that's on, brought now. The ms it takes, 0 if it doesn't fit the picture.
static uint32_t visit(const char *act, int k) {
  const live::On *o = live::on(live::act_of(act));
  CHECK_MSG(o && k < (int) o->visits.size(), "%s: no visitor %d, it has %d", act, k, o ? (int) o->visits.size() : -1);
  live::As as(live::act_of(act));
  return o && k < (int) o->visits.size() ? o->visits[(size_t) k]() : 0;
}

// Each of an act's visitors in turn, the acts' timers put off, as live/TAG_K_n.ppm with shots of each. Each asks the
// panel for no more than it has, and lets go of what it took once it's gone. Those of walkers come from out of sight
// and go out of it, off the screen or behind the picture's parts, rather than fading in and out in the open.
static void visits(const char *act, const char *tag, int shots = 2, std::initializer_list<const char *> walkers = {}) {
  const live::On *host = live::on(live::act_of(act));
  CHECK_MSG(host && !host->visits.empty(), "%s has no visitors", act);
  for (int k = 0; host && k < (int) host->visits.size(); k++) {
    show_later();
    visitor_gone();
    size_t from = log_all.size();
    int slots = slots_used();
    uint32_t ms = visit(act, k);
    if (!ms)
      continue;
    std::string name = std::string(tag) + "_" + std::to_string(k);
    std::map<std::string, Path> paths;  // each walker where it's placed, faded in or not, and where it went
    int dark = 0;
    play((int) (ms + 999) / 1000 + 1, shots, name, [&] {
      dark += out_of_light();
      for (const char *w : walkers) {
        Path &p = paths[w];
        Look l;
        if (where(w, l, p.empty() ? PLACED : NOW))
          p.resize(std::min<size_t>(p.size(), 1)), p.push_back(l);
      }
    });
    live_quiet(name, from);
    CHECK_MSG(!dark, "%s: %d copies in another light than the scene's", name.c_str(), dark);
    CHECK_MSG(slots_used() <= slots, "%s: %d animations after the visit, %d before", name.c_str(), slots_used(), slots);
    for (auto &[w, p] : paths) {
      if (p.empty())
        continue;
      const live::Frames *f = live::frames_of(w);
      auto away = [&](const Look &l) { return l.hidden || l.x + f->w <= 0 || l.x >= met::W; };
      CHECK_MSG(away(p.front()) && away(p.back()), "%s: the %s from %d,%d, hidden %d, to %d,%d, hidden %d",
                name.c_str(), w.c_str(), p.front().x, p.front().y, p.front().hidden, p.back().x, p.back().y,
                p.back().hidden);
    }
  }
}

// ---------------------------------------------------------------- online pictures
static PickingImage webcam;

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
  draw_squares(panel_animation_kite, 16, 8);
  panel_picture.publish_state("dusk");  // restored, shown once on_boot has registered them all
  panel_anim_ball.publish_state(true);  // restored
  yaml_boot();
  yaml_time_sync();  // Home Assistant's time, right after boot
  for (fetch::Asker *a : fetch::askers)
    a->schedule.not_before = UINT32_MAX;  // until their tests
  CHECK_MSG(pic::S.want == "dusk" && overlay::find("ball"),
            "the restored picture and animation aren't shown after boot: '%s', %d", pic::S.want.c_str(),
            overlay::find("ball") != nullptr);
  pic::clear();
  panel_clear_animation.execute("");
  CHECK_MSG(!panel_brightness.has_state() && !panel_idle_brightness.has_state() && !panel_screensaver.has,
            "panel settings known before the panel reported them: brightness %g, idle %g, screensaver '%s'",
            panel_brightness.state, panel_idle_brightness.state, panel_screensaver.state.c_str());
}

static void hello() {
  step("hello, both ways");
  int c = run_until([] { return pn_online() && panel::link::connected && settled(); }, 2000);
  CHECK_MSG(c >= 0, "no link after 2000 cycles");
  int hellos = times_logged("Panel says hello");  // times the ESP sent the panel its state
  CHECK_MSG(hellos == 1, "the ESP sent its state %d times", hellos);
  CHECK_MSG(!link_early, "%d frames sent sooner than %d us after the status", link_early, PBUS_START_MIN_US);
  CHECK_MSG(!link_late, "%d answers ended later than %u us after the status", link_late, (unsigned) link_end());
  CHECK_MSG(gap_frames > 0, "nothing sent in the gaps after polls");
  CHECK_MSG(pn_get(K_SETPOINT) == 200 && pn_reg(0x004) != 20,
            "the ESP sent unit settings it doesn't have: setpoint %d, max supply %d", pn_get(K_SETPOINT),
            pn_reg(0x004));
  printf("  linked and settled after %d cycles\n", c);
  CHECK_MSG(panel_screensaver.state == panel_screensaver.options[pn_get(K_SCREENSAVER)], "screensaver %s vs panel %d",
            panel_screensaver.state.c_str(), pn_get(K_SCREENSAVER));
  CHECK_MSG(panel_brightness.state == pn_get(K_BRIGHTNESS), "brightness %g vs %d", panel_brightness.state,
            pn_get(K_BRIGHTNESS));
  CHECK_MSG(panel_idle_brightness.state == pn_get(K_IDLE_BRIGHTNESS), "idle brightness %g vs %d",
            panel_idle_brightness.state, pn_get(K_IDLE_BRIGHTNESS));
  CHECK_MSG(fabsf(panel_temperature.state * 10 - pn_get(K_ROOM_TEMP)) < 0.5f, "temperature %g vs %d",
            panel_temperature.state, pn_get(K_ROOM_TEMP));
  CHECK_MSG(panel_firmware.state == "0.1.0", "firmware version '%s'", panel_firmware.state.c_str());
  printf("  panel firmware %s, temperature %.1f C, screensaver %s, brightness %.0f %%\n", panel_firmware.state.c_str(),
         panel_temperature.state, panel_screensaver.state.c_str(), panel_brightness.state);
  int y, mo, d, h, mi;
  pn_clock(&y, &mo, &d, &h, &mi);
  CHECK_MSG(y == 2026 && mo == 10 && d == 18 && h == 21 && mi == 30, "clock %d-%d-%d %d:%d", y, mo, d, h, mi);
  // the local hour changes, say summer time starts. The panel must follow within a few cycles
  flexit_panel_time.t.hour = 22;
  tz_offset_s = 3600;
  int cycles_to_clock = run_until(
      [&] {
        pn_clock(&y, &mo, &d, &h, &mi);
        return h == 22;
      },
      20);
  CHECK_MSG(cycles_to_clock >= 0, "the panel's clock didn't follow the hour: %d:%d", h, mi);
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
    CHECK_MSG(pitem(n).type && (pitem(n).flags & ITEMF_COMFORT), "the panel doesn't have comfort item %s", n);
    CHECK_MSG(pitem(n).id == plink_item_id(n), "%s: id %d on the panel, its hash %d", n, pitem(n).id, plink_item_id(n));
  }
  CHECK_MSG(!pitem(N_ITEMS - 1).type, "item %d, which nothing sent, has type %d", N_ITEMS - 1, pitem(N_ITEMS - 1).type);
  CHECK_MSG(pitem("humidity").value == 55 && pitem("co2").value == 912, "humidity %d, co2 %d", pitem("humidity").value,
            pitem("co2").value);
}

// esp_test OUTDIR PICTURE.ppm: only how the panel draws that picture
static int custom_picture(const char *path) {
  if (!load_ppm(panel_picture_dusk, path)) {
    printf("can't read %s\n", path);
    return 1;
  }
  pic::show("dusk");
  CHECK_MSG(picture(path, 6000) >= 0, "picture not shown");
  pn_shot(outdir, "esp_custom");
  printf("  %s/esp_custom.ppm\n", outdir);
  return check_fails ? 1 : 0;
}

static void items() {
  step("items");
  static const char *const NAME[] = {"inside_humidity", "heater_enabled", "heater",
                                     "temperature",     "clear_alarms",   "washer"};
  static const char *const LABEL[] = {"Bathroom", "Heater on", "Heater", "Setpoint", "Clear alarms", "Washer"};
  static const int TYPE[] = {ITEM_SENSOR, ITEM_TOGGLE, ITEM_TOGGLE, ITEM_NUMBER, ITEM_BUTTON, ITEM_TEXT};
  for (int i = 0; i < 6; i++) {
    PanelItem p = pitem(NAME[i]);
    CHECK_MSG(p.type == TYPE[i] && !strcmp(p.label, LABEL[i]), "%s: type %d label '%s'", NAME[i], p.type, p.label);
    printf("  %s: type %d flags 0x%02X icon %d '%s' %d%s (%d..%d step %d, %d decimals)\n", NAME[i], p.type, p.flags,
           p.icon, p.label, p.value, p.unit, p.min, p.max, p.step, p.dec);
  }
  PanelItem p = pitem("temperature");
  CHECK_MSG(p.dec == 1 && p.step == 5 && p.min == 100 && p.max == 300 && p.value == 200 && (p.flags & ITEMF_COMFORT),
            "temperature: %d decimals, step %d, %d..%d, value %d, flags 0x%02X", p.dec, p.step, p.min, p.max, p.value,
            p.flags);
  p = pitem("inside_humidity");
  CHECK_MSG(
      p.value == 55 && (p.flags & (ITEMF_RO | ITEMF_TREND | ITEMF_LEVELS)) == (ITEMF_RO | ITEMF_TREND | ITEMF_LEVELS),
      "sensor value %d flags 0x%02X", p.value, p.flags);
  CHECK_MSG(pitem("heater_enabled").flags & ITEMF_RO, "a binary sensor the panel may change");
  CHECK_MSG(!(pitem("heater").flags & ITEMF_RO), "a switch the panel can't change");
  CHECK_MSG((pitem("washer").flags & ITEMF_RO) && !strcmp(pn_item_text(item("washer")), "Washing"),
            "text item: flags 0x%02X, shows '%s'", pitem("washer").flags, pn_item_text(item("washer")));
  CHECK_MSG(!(pitem("clear_alarms").flags & ITEMF_VIEW) && (pitem("clear_alarms").flags & ITEMF_MENU),
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
  CHECK_MSG(m.dec == 3 && m.value == 1500, "5 decimals asked for: %d on the panel, value %d", m.dec, m.value);
  // 900 ppm is between good and bad, so its levels have to fit the panel's 16 bits
  CHECK_MSG(q.dec == 1 && q.value == 9120 && pn_item_level(item("ppm"), 2) == 8000 &&
                pn_item_level(item("ppm"), 3) == 12000 && (q.flags & ITEMF_LEVELS),
            "ppm with 2 decimals: %d decimals, value %d, levels %d %d", q.dec, q.value, pn_item_level(item("ppm"), 2),
            pn_item_level(item("ppm"), 3));
  // the step goes as 16 bits, the value as 32
  int big = Item("big", ITEM_NUMBER).view(false).menu(false).decimals(2).range(0, 1e8f, 1000).add();
  panel::item_value(big, 3e8f);
  run_until(settled, 200);
  run(3);
  PanelItem b = pitem("big");
  CHECK_MSG(b.dec == 1 && b.step == 10000 && b.value == INT32_MAX,
            "step 1000 with 2 decimals: %d decimals, step %d, 3e8 is %d", b.dec, b.step, b.value);
  panel::item_value(big, -3e8f);
  run(5);
  CHECK_MSG(pitem("big").value == -INT32_MAX, "-3e8 is %d", pitem("big").value);
  size_t at = log_all.size();
  Item("long_label", ITEM_SENSOR).view(false).menu(false).label("Living room temperature|Stue").add();
  run_until(settled, 200);
  run(3);
  CHECK_MSG(!strcmp(pitem("long_label").label, "Living room tempera") &&
                logged("'long_label': the panel shows 19 bytes of its label, 'Living room temperature'", at) &&
                !logged("'Stue'", at),
            "a 23 byte label: '%s' on the panel", pitem("long_label").label);
}

static void item_language() {
  step("their texts in the panel's language");
  int fp = item("fireplace");
  CHECK_MSG(!pn_item_help(fp), "help before the ESP sent it");
  run(10);
  const char *help = pn_item_help(fp);
  CHECK_MSG(help && strstr(help, "fireplace draws"), "fireplace help: %s", help ? help : "(none)");
  pn_set(K_LANGUAGE, 1);
  run(20);
  CHECK_MSG(panel_language == 1, "the panel's language not kept for a restart: %d", panel_language);
  CHECK_MSG(!strcmp(pitem("inside_humidity").label, "Bad") && !strcmp(pitem("fireplace").label, "Peis") &&
                !strcmp(pitem("temperature").label, "Setpoint"),
            "in Norwegian: '%s', '%s', '%s'", pitem("inside_humidity").label, pitem("fireplace").label,
            pitem("temperature").label);
  CHECK_MSG(!strcmp(pn_item_text(item("washer")), "Vasker"), "the text item in Norwegian: '%s'",
            pn_item_text(item("washer")));
  CHECK_MSG(!pn_item_help(fp), "the English help kept");
  run(10);
  help = pn_item_help(fp);
  CHECK_MSG(help && strstr(help, "peisen trekker"), "fireplace help in Norwegian: %s", help ? help : "(none)");
  int hb = item("humidity_boost");
  pn_item_help(hb);  // another item's help doesn't push the fireplace's out
  run(10);
  CHECK_MSG(pn_item_help(hb) && pn_item_help(fp), "the fireplace help wasn't kept after another item's");
  pn_set(K_LANGUAGE, 0);
  run(20);
  CHECK_MSG(!strcmp(pitem("fireplace").label, "Fireplace") && panel_language == 0, "back in English: '%s', kept %d",
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
  CHECK_MSG(pn_get(K_SCREENSAVER) == 1, "screensaver %d", pn_get(K_SCREENSAVER));
  CHECK_MSG(pn_get(K_BRIGHTNESS) == 60, "brightness %d", pn_get(K_BRIGHTNESS));
  CHECK_MSG(pn_get(K_LEDS) == (3 << 2), "leds %d", pn_get(K_LEDS));
  CHECK_MSG(pn_get(K_THEME) == 2, "theme %d", pn_get(K_THEME));
  // the panel doesn't echo what the ESP sets, so the ESP shows it itself
  CHECK_MSG(panel_screensaver.state == panel_screensaver.options[1] && panel_brightness.state == 60,
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
  CHECK_MSG(pn_get(K_SCREENSAVER) == 3 && pn_get(K_IDLE_BRIGHTNESS) == 25 && pn_get(K_NIGHT) == 1 &&
                pn_get(K_KEY_LOCK) == 1 && pn_get(K_LEDS) == (3 << 2),
            "panel_set: screensaver %d, idle brightness %d, night %d, key lock %d, leds %d", pn_get(K_SCREENSAVER),
            pn_get(K_IDLE_BRIGHTNESS), pn_get(K_NIGHT), pn_get(K_KEY_LOCK), pn_get(K_LEDS));
  CHECK_MSG(
      panel_screensaver.idx == 3 && panel_idle_brightness.state == 25 && panel_night.state && panel_key_lock.state,
      "panel_set not in Home Assistant: screensaver '%s', idle brightness %g, night %d, key lock %d",
      panel_screensaver.state.c_str(), panel_idle_brightness.state, panel_night.state, panel_key_lock.state);
  CHECK_MSG(!strcmp(pitem("fireplace").label, "Peis"), "panel_set language: '%s'", pitem("fireplace").label);
  CHECK_MSG(logged("No panel setting called 'leds'", leds_at), "panel_set took leds");
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
  CHECK_MSG(panel_key_lock.state, "key lock not on in Home Assistant");
  CHECK_MSG(panel_screensaver.state == "Picture and clock", "screensaver %s", panel_screensaver.state.c_str());
}

static void items_from_panel() {
  step("items from the panel");
  pn_set(value_key("heater"), 1);
  pn_set(value_key("temperature"), 225);
  pn_set(value_key("clear_alarms"), 1);
  run(30);
  CHECK_MSG(heater.state, "heater not on");
  CHECK_MSG(!strcmp(pn_item_text(item("heater")), "Heating"), "a switch with words shows '%s'",
            pn_item_text(item("heater")));
  CHECK_MSG(fabsf(temperature.state - 22.5f) < 0.01f, "temperature %g", temperature.state);
  CHECK_MSG(clear_alarms.presses == 1, "button pressed %d times", clear_alarms.presses);
  inside_humidity.publish_state(61.4f);
  heater_enabled.publish_state(false);
  run(10);
  CHECK_MSG(pitem("inside_humidity").value == 61, "sensor %d", pitem("inside_humidity").value);
  CHECK_MSG(pitem("heater_enabled").value == 0, "binary sensor %d", pitem("heater_enabled").value);
  CHECK_MSG(!strcmp(pn_item_text(item("heater_enabled")), "Blocked"), "binary sensor shown as '%s', not its off_text",
            pn_item_text(item("heater_enabled")));
  pn_set(value_key("heater_enabled"), 1);  // read only: nothing happens
  run(10);
  CHECK_MSG(!heater_enabled.state, "binary sensor changed from the panel");
}

static void ha_switch() {
  step("a Home Assistant switch");
  int dl = item("door_lock");
  CHECK_MSG(pitem(dl).value == ITEM_UNKNOWN, "a lock Home Assistant hasn't told us about shows '%s'", pn_item_text(dl));
  pn_set(value_key("door_lock"), 1);  // HA isn't there yet
  run(150);
  CHECK_MSG(pitem(dl).value == ITEM_UNKNOWN, "locked before Home Assistant had a state shows '%s'", pn_item_text(dl));
  ha_door_lock_state.publish_state("unlocked");
  run(10);
  CHECK_MSG(pitem(dl).value == 0 && !strcmp(pn_item_text(dl), "Unlocked"), "unlocked shows '%s'", pn_item_text(dl));
  pn_set(value_key("door_lock"), 1);  // locked on the panel, the lock takes its time
  run(5);
  ha_door_lock_state.publish_state("locking");
  run(60);
  CHECK_MSG(pitem(dl).value == 1, "the panel went back to '%s' while the lock was locking", pn_item_text(dl));
  ha_door_lock_state.publish_state("locked");
  run(10);
  CHECK_MSG(pitem(dl).value == 1 && !strcmp(pn_item_text(dl), "Locked"), "locked shows '%s'", pn_item_text(dl));
  pn_set(value_key("door_lock"), 0);  // the lock never follows
  run(150);
  CHECK_MSG(pitem(dl).value == 1, "an unlock the lock never did shows '%s'", pn_item_text(dl));
  ha_door_lock_state.publish_state("unavailable");
  run(10);
  CHECK_MSG(pitem(dl).value == ITEM_UNKNOWN, "an unavailable lock shows '%s'", pn_item_text(dl));
}

static void text_item() {
  step("a text item");
  int wi = item("washer");
  ha_washer.publish_state("Vasker ferdig!\xC3\xA5");  // 16 bytes, the 15th byte is half a character
  run(10);
  CHECK_MSG(!strcmp(pn_item_text(wi), "Vasker ferdig!"), "a long text shows as '%s'", pn_item_text(wi));
  ha_washer.publish_state("Dishwasher done");  // 15 bytes fit
  run(10);
  CHECK_MSG(!strcmp(pn_item_text(wi), "Dishwasher done"), "a 15 byte text shows as '%s'", pn_item_text(wi));
  ha_washer.publish_state("");
  run(10);
  CHECK_MSG(!strcmp(pn_item_text(wi), "\xE2\x80\x93"), "an empty text shows as '%s'", pn_item_text(wi));
  ha_washer.publish_state("Washing");
  run(10);
  pn_set(value_key("washer"), 1);  // read only: the panel keeps the text and the ESP hears nothing
  run(10);
  CHECK_MSG(!strcmp(pn_item_text(wi), "Washing") && pn_get(value_key("washer")) == ITEM_UNKNOWN,
            "a text item set from the panel: '%s', value %d", pn_item_text(wi), pn_get(value_key("washer")));
  pn_restart();  // without restart_panel()'s 5 cycles more, which would move the frames of the animation shots
  run_until([] { return pn_online() && settled(); }, 2000);
  CHECK_MSG(!strcmp(pn_item_text(wi), "Washing"), "the text after a panel restart: '%s'", pn_item_text(wi));
}

static void ha_entities() {
  step("Home Assistant entities (ha_sensor, ha_binary_sensor and ha_switch)");
  ha_hot_water.publish_state(61.4f);
  ha_front_door.publish_state(true);
  ha_door_lock_state.publish_state("locked");
  run(20);
  CHECK_MSG(pitem("hot_water").value == 61 && pitem("front_door").value == 1 && pitem("door_lock").value == 1,
            "hot water %d, front door %d, door lock %d", pitem("hot_water").value, pitem("front_door").value,
            pitem("door_lock").value);
  ha_actions.clear();
  pn_set(value_key("door_lock"), 0);
  run(20);
  CHECK_MSG(ha_actions.size() == 1 && ha_actions[0].action == "lock.unlock" &&
                ha_actions[0].data["entity_id"] == "lock.front_door",
            "unlocking from the panel asked Home Assistant for %s",
            ha_actions.empty() ? "nothing" : ha_actions[0].action.c_str());
  ha_door_lock_state.publish_state("unlocked");
  run(10);
  CHECK_MSG(pitem("door_lock").value == 0, "the lock shows %d once it's unlocked", pitem("door_lock").value);
}

static void comfort() {
  step("comfort");
  pn_set(value_key("fireplace"), 1);
  run(30);
  CHECK_MSG(fireplace_mode_switch.state, "fireplace not on");
  pn_set(value_key("fireplace_minutes"), 45);
  run(10);
  CHECK_MSG(fireplace_duration.state == 45, "fireplace minutes %g", fireplace_duration.state);
  fireplace_mode_switch.publish_state(false);
  fireplace_mode_switch.refuse = true;  // like when the unit is stopped
  run(10);
  pn_set(value_key("fireplace"), 1);
  run(5);
  CHECK_MSG(pn_get(value_key("fireplace")) == 1, "the panel should show its request at first");
  run(30);
  CHECK_MSG(pn_get(value_key("fireplace")) == 0, "the panel still shows fireplace on after the ESP refused");
  pn_set(value_key("humidity_trigger"), 120);
  run(10);
  CHECK_MSG(fabsf(humidity_trigger_delta.state - 12.0f) < 0.01f, "humidity trigger %g", humidity_trigger_delta.state);
  pn_set(value_key("co2_control"), 1);
  run(10);
  CHECK_MSG(co2_control.state == "Stepped", "co2 control %s", co2_control.state.c_str());
  co2_control.control(3);
  run(10);
  pn_set(value_key("co2_control"), 0);
  run(10);
  CHECK_MSG(co2_control.state == "Off", "co2 control %s", co2_control.state.c_str());
  pn_set(value_key("co2_control"), 1);
  run(10);
  CHECK_MSG(co2_control.state == "PI", "co2 control %s, should be back to PI", co2_control.state.c_str());
  co2.publish_state(1250);
  run(10);
  CHECK_MSG(pitem("co2").value == 1250, "co2 %d", pitem("co2").value);
  co2.publish_state(NAN);  // unavailable
  run(10);
  CHECK_MSG(pitem("co2").value == ITEM_UNKNOWN, "co2 %d when unavailable", pitem("co2").value);
  co2.publish_state(1250);
  run(10);
  CHECK_MSG(pitem("co2").value == 1250, "co2 %d once it's back", pitem("co2").value);
}

static void master_only() {
  step("what only the master panel may do");
  pn_set(K_SETPOINT, 215);
  run(10);
  bool sent = false;
  for (auto &m : server->cmds)
    sent |= m.first == 0x00C && m.second == 215;
  CHECK_MSG(sent, "setpoint not written to 0x00C");
}

static void actions_and_notes() {
  step("actions and notifications");
  panel::notify("Filter change due in a week", "filter", "warning");
  panel::loop();  // queues it
  run_until(settled, 300);
  run(3);
  int icon, level, fresh;
  const char *note = pn_note(&icon, &level, &fresh);
  CHECK_MSG(!strcmp(note, "Filter change due in a week") && icon == ICN_FILTER && level == NOTE_WARNING && fresh,
            "note '%s' icon %d level %d", note, icon, level);
  printf("  note: '%s' icon %d level %d\n", note, icon, level);
  pn_action(A_RESET_FILTER);  // Filter > Reset on the panel
  run_until(settled, 100);
  run(5);
  CHECK_MSG(panel_action.last == "reset_filter", "Filter > Reset on the panel is event '%s' in Home Assistant",
            panel_action.last.c_str());
}

static void cx60_restarts() {
  step("the CX60 restarts: no quiet for the link for a while");
  link_windows = false;
  panel_brightness.control(40);  // ESP -> panel
  pn_set(K_NIGHT, 1);            // panel -> ESP
  run(60);
  CHECK_MSG(pn_get(K_BRIGHTNESS) != 40 && !panel_night.state, "something got through without a status broadcast");
  link_windows = true;
  run(20);
  CHECK_MSG(pn_get(K_BRIGHTNESS) == 40, "brightness %d after the CX60 came back", pn_get(K_BRIGHTNESS));
  CHECK_MSG(panel_night.state, "night mode not on after the CX60 came back");
  CHECK_MSG(settled(), "something still waiting after the CX60 came back");
}

static void frames_lost() {
  step("frames and answers lost");
  link_lost_frames = 4;
  link_lost_answers = 4;
  pn_set(K_NIGHT, 0);
  panel_brightness.control(60);
  run(30);
  CHECK_MSG(pn_get(K_BRIGHTNESS) == 60, "brightness %d after lost frames", pn_get(K_BRIGHTNESS));
  CHECK_MSG(!panel_night.state, "night mode still on after lost answers");
  CHECK_MSG(settled(), "something still waiting after lost frames");
}

static void link_refused() {
  step("the panel refuses the link while the CX60 comes back too soon, then lets it back in");
  link_quiet = 10000;  // the CX60 polls 10 ms after its status broadcast
  run(40);
  CHECK_MSG(panel::link::tx.refused, "the ESP didn't take the refusal");
  CHECK_MSG(logged("refuses the link"), "the refusal wasn't logged");
  link_quiet = 14400;
  size_t from = log_all.size();
  int back = run_until([] { return !panel::link::tx.refused && panel::link::online(); }, 4000);
  CHECK_MSG(back >= PBUS_REARM, "back after %d cycles, the panel wants %d clean ones", back, PBUS_REARM);
  CHECK_MSG(back >= 0 && back < PBUS_REARM + 600, "the link didn't come back in time: %d cycles", back);
  CHECK_MSG(logged("lets the link back in", from), "its return wasn't logged");
  CHECK_MSG(!logged("refuses the link", from), "the refusal was logged again");
  printf("  back after %d cycles (%.0f s)\n", back, back * 0.11);
}

static void pictures() {
  step("pictures");
  CHECK_MSG(panel_picture_status.state == "None", "picture status at the start: '%s'",
            panel_picture_status.state.c_str());
  unsigned long frames_before = link_frames;
  pic::Source &dusk = pic::sources["dusk"];
  dusk.encoding = pic::PALETTE;  // 16 colours first, to test their patches
  draw_dusk(panel_picture_dusk, 250, false);
  panel_picture.control(1);  // the select: "dusk"
  CHECK_MSG(picture("whole picture", 6000) >= 0, "picture not shown");
  int pw, ph;
  CHECK_MSG(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "panel picture %dx%d", pw, ph);
  pn_shot(outdir, "esp_picture");

  draw_dusk(panel_picture_dusk, 225, false);  // the moon moved
  pic::show("dusk");
  CHECK_MSG(picture("moon moved", 3000) >= 0, "patch not shown");
  pn_shot(outdir, "esp_picture_moon");

  draw_dusk(panel_picture_dusk, 240, false);
  pic::show("dusk");
  run(5);
  link_lost_answers = 3;  // the ESP misses three of the panel's answers
  CHECK_MSG(picture("moved a bit, 3 answers lost", 3000) >= 0, "not shown after lost answers");

  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  run(5);
  link_lost_frames = 3;  // the panel misses three of the ESP's frames
  CHECK_MSG(picture("moved again, 3 frames lost", 3000) >= 0, "not shown after lost frames");

  // The moon and a box just below it go as one piece, the rows between along
  draw_dusk(panel_picture_dusk, 250, false);
  for (int y = 88; y < 94; y++)
    for (int x = 150; x < 170; x++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = 0x5A3C28;
  size_t one = log_all.size();
  pic::show("dusk");
  CHECK_MSG(picture("the moon and a box below it", 3000) >= 0 && logged("Picture dusk: 1 pieces", one),
            "the moon and a box below it not sent as one piece");

  // Two pieces, the box gone and a box lower down. Back to the picture before once the first is in.
  draw_dusk(panel_picture_dusk, 250, false);
  for (int y = 205; y < 215; y++)
    for (int x = 150; x < 170; x++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = 0x5A3C28;
  pic::show("dusk");
  int half = run_until([&] { return pic::S.phase == pic::SEND && pic::S.jobs.size() == 1; }, 3000);
  CHECK_MSG(half >= 0, "not sent as two pieces");
  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  CHECK_MSG(picture("changed back halfway through", 3000) >= 0, "not shown after a change while sending");

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
  CHECK_MSG(half >= 0 && logged("Picture dusk: 2 pieces", two), "not sent as two pieces");
  size_t rows_only = log_all.size();
  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  CHECK_MSG(picture("changed between pieces", 3000) >= 0, "not shown after a change between pieces");
  CHECK_MSG(logged("Picture dusk: 1 pieces", rows_only), "a change between pieces sent it all");
  // Two pieces once more, and no picture at all while the panel's answer to the first is on its way: the answer
  // isn't for anything any more, and the picture it's picked again after is all of it
  draw_dusk(panel_picture_dusk, 250, false);
  for (int y = 160; y < 200; y++)
    for (int x = 0; x < 320; x++)
      panel_picture_dusk.px[(size_t) y * 320 + x] = pic::S.shown.pal[lcg(seed) >> 16 & 1];
  pic::show("dusk");
  half = run_until([&] { return pic::S.jobs.size() == 2 && pic::S.t.head && pic::S.t.ended; }, 3000);
  CHECK_MSG(half >= 0, "the first of two pieces didn't go");
  pic::picked("None");
  pic::ack((uint32_t) PIC_DONE << 24 | (uint32_t) pic::S.t.id << 16);  // before the loop has seen that it's gone
  run(30);
  CHECK_MSG(pic::S.phase == pic::IDLE && pic::S.shown.rows.empty() && !panel_pic(),
            "cleared with a piece on its way: phase %d, the panel's state %d", pic::S.phase, panel_pic());
  pic::picked("dusk");
  CHECK_MSG(picture("picked again after that", 6000) >= 0, "not shown when it's picked again");
  auto planned = [&] { return run_until([&] { return pic::S.phase == pic::SEND && !pic::S.redo; }, 100) >= 0; };

  // A BEGIN on its way when it changes: the panel has dropped its picture for it, so all of it goes again
  panel_picture_dusk.h = 200;
  panel_picture_dusk.px.resize(320 * 200);
  pic::show("dusk");
  CHECK_MSG(run_until([&] { return pic::S.phase == pic::SEND && pic::S.t.head; }, 100) >= 0 && pic::S.jobs.front().full,
            "a picture of another size not sent whole");
  draw_dusk(panel_picture_dusk, 230, false);
  pic::show("dusk");
  CHECK_MSG(planned() && pic::S.jobs.front().full, "a change during a BEGIN sends rows the panel doesn't have");
  CHECK_MSG(picture("changed during a BEGIN", 3000) >= 0, "not shown after a change during a BEGIN");

  // New colours in rows far apart, so the palette changes. The panel takes it at the piece's END, so they go as one
  // piece and it never draws a row in colours that aren't its own.
  {
    pic::Pic was = pic::S.shown;
    for (int y = 0; y < 240; y++)
      for (int x = 100; x < 200; x++)
        if ((y >= 20 && y < 30) || (y >= 200 && y < 210))
          panel_picture_dusk.px[(size_t) y * 320 + x] = 0xE01020;
    pic::show("dusk");
    CHECK_MSG(planned() && pic::S.jobs.size() == 1 && pic::S.jobs[0].pal && !pic::S.jobs[0].full &&
                  memcmp(was.pal, pic::S.made.pal, sizeof was.pal),
              "new colours in rows far apart: %d pieces", (int) pic::S.jobs.size());
    pic::Pic made = pic::S.made;
    int off = 0;
    for (int c = 0; c < 100 && pic::S.phase == pic::SEND; c++) {
      run(1);
      off = std::max(off, rows_off(was, made));
    }
    CHECK_MSG(!off, "new colours in rows far apart: the panel drew %d rows that are neither picture", off);
    CHECK_MSG(picture("new colours in rows far apart", 3000) >= 0, "not shown");
  }

  // A picture in 16 colours that aren't on the 4-bit grid shows them as they are
  {
    static const uint32_t COLOURS[4] = {0xFFF6D2, 0x123456, 0x7A1B9C, 0x0D0E0F};
    for (int y = 0; y < 240; y++)
      for (int x = 0; x < 320; x++)
        panel_picture_dusk.px[(size_t) y * 320 + x] = COLOURS[(x / 80 + y / 60) % 4];
    pic::show("dusk");
    CHECK_MSG(picture("4 colours off the grid", 3000) >= 0, "not shown");
    int exact = 0;
    for (uint32_t c : COLOURS)
      exact += std::count(pic::S.shown.pal, pic::S.shown.pal + 16, c) > 0;
    CHECK_MSG(exact == 4 && pic::S.shown.err < 1e-6, "%d of the 4 colours exactly, error %g", exact, pic::S.shown.err);
  }

  draw_dusk(panel_picture_dusk, 200, false);
  pic::show("dusk");
  run(5);
  link_windows = false;  // the CX60 restarts mid-picture
  run(60);
  link_windows = true;
  CHECK_MSG(picture("the CX60 restarted mid-picture", 3000) >= 0, "not shown after the CX60 restarted");

  pn_restart();
  CHECK_MSG(picture("panel restarted", 6000) >= 0, "picture not sent again after a panel restart");
  CHECK_MSG(panel_pic() == 2, "no picture after the restart");
  printf("  %lu frames for these pictures\n", link_frames - frames_before);
}

static void pictures_jpeg() {
  step("pictures: 16 colours or JPEG");
  unsigned long frames_before = link_frames;
  pic::Source &dusk = pic::sources["dusk"];
  int pw, ph;
  auto as = [&](const char *name, int format) {
    CHECK_MSG(picture(name, 8000) >= 0, "%s not shown", name);
    CHECK_MSG(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "%s drawn at %dx%d", name, pw, ph);
    CHECK_MSG(pn_pic_format() == format, "%s went as %s", name, pn_pic_format() == PIC_JPEG ? "a JPEG" : "16 colours");
    CHECK_MSG(pic::S.shown.data.size() <= PIC_POOL, "%s: %u bytes sent", name, (unsigned) pic::S.shown.data.size());
    CHECK_MSG(pic::S.shown.data.capacity() == pic::S.shown.data.size(), "%s: %u bytes kept for %u", name,
              (unsigned) pic::S.shown.data.capacity(), (unsigned) pic::S.shown.data.size());
  };
  size_t warned = log_all.size();
  draw_dusk(panel_picture_dusk, 200, true);  // noise doesn't fit in 16 colours, so it's a JPEG anyway
  pic::show("dusk");
  as("noise, 16 colours asked for", PIC_JPEG);
  CHECK_MSG(logged("too detailed for 16 colours", warned), "no warning about the JPEG");
  dusk.encoding = pic::AUTO;
  draw_photo(panel_picture_dusk, 320, 240);
  size_t tries = log_all.size();
  pic::show("dusk");
  as("a photo", PIC_JPEG);
  int best = pic::S.shown.quality;
  CHECK_MSG(best > 30 && best < 95, "the photo's quality %d", best);
  CHECK_MSG(jpeg_size(best) == pic::S.shown.data.size() && jpeg_size(best + 1) > PIC_POOL,
            "quality %d isn't the best that fits: %u bytes, %u at %d", best, (unsigned) jpeg_size(best),
            (unsigned) jpeg_size(best + 1), best + 1);
  int encodes = 0;
  tries = log_all.find("as a JPEG at quality", tries);
  CHECK_MSG(tries != std::string::npos &&
                sscanf(log_all.c_str() + tries, "as a JPEG at quality %*d (%d", &encodes) == 1 && encodes <= 3,
            "the photo took %d JPEGs to find its quality", encodes);
  pn_shot(outdir, "esp_photo");
  draw_photo(panel_picture_dusk, 320, 180);  // 16:9, padded to whole MCUs: 4 rows above, 8 below
  pic::show("dusk");
  CHECK_MSG(
      picture("a 16:9 photo", 8000) >= 0 && pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 192 && pic::S.search.oy == 4,
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
  CHECK_MSG(avg == 35 * 64, "%d of %d pixels of a 640x480 photo the mean of the 4 they cover", avg, 35 * 64);
  draw_photo(panel_picture_dusk, 320, 240);
  dusk.quality = 30;  // no higher than 30
  pic::show("dusk");
  as("a photo, quality 30 at most", PIC_JPEG);
  CHECK_MSG(pic::S.shown.quality == 30, "quality %d", pic::S.shown.quality);
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
  CHECK_MSG(picture("dusk, automatic", 6000) >= 0, "dusk not shown");
  printf("  %lu frames for these pictures\n", link_frames - frames_before);
}

// ---- animations
static void animations() {
  step("animations");
  panel_anim_ball.turn_on();  // the switch
  int ar = run_until(overlay_shown, 2000);
  printf("  ball: %s after %d cycles\n", panel_animation_status.state.c_str(), ar);
  CHECK_MSG(ar >= 0 && same_anim("ball", 0), "the panel's first frame isn't the ESP's");
  for (int f = 1; f <= 8; f++) {
    pn_overlay_step(100);
    CHECK_MSG(same_anim("ball", f % 8), "frame %d isn't the ESP's", f % 8);
  }
  pn_shot(outdir, "esp_animation");
}

// The actions reach the panel, pn_overlay_step(100) is two steps of the panel's motion timer
static void overlay_moving() {
  step("animations moved and drifting");
  Look l = look(0);
  CHECK_MSG(l.x == 250 && l.y == 20, "ball at %d,%d, not where the yaml put it", l.x, l.y);
  panel_move_animation.execute("ball", 0, 100, 40, 0, "none");
  int ar = run_until(
      [&] {
        l = look(0);
        return l.x == 100 && l.y == 40 && settled();
      },
      100);
  CHECK_MSG(ar >= 0, "panel_move_animation didn't move it: at %d,%d", l.x, l.y);
  CHECK_MSG(anim_list() == "ball, moved to 100,40", "status '%s'", panel_animation_status.state.c_str());
  size_t clamped = log_all.size();
  panel_move_animation.execute("ball", 0, 100, 40, 70000, "none");  // ms only has 16 bits
  run(3);
  CHECK_MSG(logged("panel_move_animation: over takes 0 to 65535 ms", clamped), "no warning about the glide time");
  panel_drift_animation.execute("ball", 0, -20, 0);
  ar = until_anim_status("ball, drifting", 100);
  pn_overlay_step(100);
  l = look(0);
  CHECK_MSG(ar >= 0 && l.x == 98 && l.y == 40, "panel_drift_animation: status '%s', at %d,%d after a step",
            panel_animation_status.state.c_str(), l.x, l.y);
  panel_drift_animation.execute("ball", 0, 0, 0);
  ar = until_anim_status("ball", 100);
  pn_overlay_step(100);
  l = look(0);
  CHECK_MSG(ar >= 0 && l.x == 98, "stopping the drift: status '%s', at %d", panel_animation_status.state.c_str(), l.x);
  panel_move_animation.execute("ball", 1, 0, 0, 0, "none");  // no such copy
  run(3);
  CHECK_MSG(logged("panel_move_animation: no copy 1 of animation 'ball' shown"), "no warning about the copy");
}

// More places: a copy in step, and one with a frame of its own two frames ahead
static void overlay_copies() {
  step("animation copies");
  panel_place_animation.execute("ball", 1, 10, 150, false, 0);
  panel_place_animation.execute("ball", 2, 200, 150, true, 5);  // the ball is on frame 2 now, so 3 frames apart
  int ar = until_anim_status("ball ×3", 100);
  CHECK_MSG(ar >= 0, "the copies: status '%s'", panel_animation_status.state.c_str());
  Look a = look(1), b = look(2);  // in front of anything a picture has in front of the weather's animations
  CHECK_MSG(
      a.x == 10 && a.y == 150 && b.x == 200 && b.y == 150 && a.depth == OVERLAY_NEAREST && b.depth == OVERLAY_NEAREST,
      "copies at %d,%d and %d,%d, %d and %d deep", a.x, a.y, b.x, b.y, a.depth, b.depth);
  const overlay::Overlay *ball = overlay::find("ball");
  size_t want = ball->frame_bytes() * 2 + ball->data.size() + OVERLAY_SCRIPT_HEAD + 3;  // copy 2's frame and FRAME 5
  CHECK_MSG(pn_overlay_used() == want && overlay::used_by_others(nullptr) == want,
            "the panel's pool holds %u bytes, the ESP thinks %u", pn_overlay_used(),
            (unsigned) overlay::used_by_others(nullptr));
  // copy 1 shows what copy 0 shows, copy 2 started on frame 5 when it was placed, so it's out of step
  int apart = 0;
  for (int i = 0; i < 8; i++, pn_overlay_step(100))
    for (int f = 0; f < 8; f++)
      if (same_anim("ball", f, 0)) {
        CHECK_MSG(same_anim("ball", f, 1), "copy 1 isn't in step at frame %d", f);
        apart += !same_anim("ball", f, 2);
        break;
      }
  CHECK_MSG(apart, "copy 2 is in step with copy 0");
  panel_drift_animation.execute("ball", 2, 0, 30);
  run(3);
  pn_overlay_step(100);
  a = look(0);
  b = look(2);
  CHECK_MSG(b.x == 200 && b.y == 153 && a.x == 98 && a.y == 40, "only copy 2 drifts: %d,%d and %d,%d", b.x, b.y, a.x,
            a.y);
  pn_shot(outdir, "esp_animation_copies");
}

// Copy 2 turned and flipped as it drifts, a flip keeps a turn going, a spin and a stop, and a panel restart puts it
// back as it was. The panel turns it while it's shown.
static void overlay_turning() {
  step("an animation turned and flipped");
  panel_glide_animation.execute("ball", 2, "turn", 450, 0, "none");  // 90
  panel_tell_animation.execute("ball", 2, "flip x");
  run(3);
  Look l = look(2), l0 = look(0);
  CHECK_MSG(l.shown && l.deg == 90 && l.flip == OVERLAY_VIEW_FLIP_X && l0.shown && !l0.deg && !l0.flip,
            "turned and flipped: %d degrees, flip %d", l.deg, l.flip);
  panel_glide_animation.execute("ball", 2, "turn", 0, 2000, "none");
  run(3);
  steps(1000);
  l = look(2);
  CHECK_MSG(l.shown && l.deg > 30 && l.deg < 60, "halfway through a turn: %d degrees", l.deg);
  panel_tell_animation.execute("ball", 2, "flip y");
  run(3);
  steps(2100);
  l = look(2);
  CHECK_MSG(l.shown && l.deg == 0 && l.flip == OVERLAY_VIEW_FLIP_Y, "a flip halfway through a turn: %d, flip %d", l.deg,
            l.flip);
  panel_tell_animation.execute("ball", 2, "spin -180");
  run(3);
  steps(500);
  l = look(2);
  CHECK_MSG(l.shown && l.deg > 180 && l.deg < 360, "spinning back: %d degrees", l.deg);
  panel_tell_animation.execute("ball", 2, "spin 0");
  run(3);
  int stopped = look(2).deg;
  steps(1000);
  l = look(2);
  CHECK_MSG(l.shown && l.deg == stopped, "the spin didn't stop: %d then %d", stopped, l.deg);
  panel_glide_animation.execute("ball", 2, "turn", 180, 0, "none");
  run(3);
  restart_panel();
  l = look(2);
  CHECK_MSG(l.shown && l.deg == 180 && l.flip == OVERLAY_VIEW_FLIP_Y, "after a panel restart: %d, flip %d", l.deg,
            l.flip);
  CHECK_MSG(l.shown && l.opa == 0, "placed again it fades in: opacity %d", l.opa);
  steps(400);
  l = look(2);
  CHECK_MSG(l.shown && l.opa == 1000, "faded in to %d", l.opa);
}

// Copy 2 half as wide and mirrored round its top right corner, and so after a panel restart
static void overlay_pivoted() {
  step("an animation sized round a pivot");
  panel_glide_animation.execute("ball", 2, "turn", 0, 0, "none");
  panel_tell_animation.execute("ball", 2, "flip none");
  panel_tell_animation.execute("ball", 2, "pivot 100, 0");
  panel_glide_animation.execute("ball", 2, "size_x", -50, 0, "none");
  run(3);
  play(1, 0, "");  // drawn, so its box is
  Look l = look(2);
  bool right = l.shown && l.pivot_x == 1000 && !l.pivot_y && l.size_x == -500 && l.x1 == l.x + l.w &&
               l.x2 == l.x + l.w + (l.w + 1) / 2 - 1 && l.y1 == l.y;
  restart_panel();
  play(1, 0, "");
  Look r = look(2);
  CHECK_MSG(right && r.shown && r.x1 == l.x1 && r.x2 == l.x2 && r.pivot_x == 1000,
            "right of its corner at %d: %d to %d, after a restart %d to %d", l.x + l.w, l.x1, l.x2, r.x1, r.x2);
  panel_glide_animation.execute("ball", 2, "size_x", 100, 0, "none");
  panel_tell_animation.execute("ball", 2, "pivot 50, 50");
  panel_tell_animation.execute("ball", 2, "blend add");
  run(3);
  CHECK_MSG((look(2).view & OVERLAY_VIEW_BLEND) == OVERLAY_BLEND_ADD, "copy 2 not added: view %d", look(2).view);
  panel_tell_animation.execute("ball", 2, "blend mix");
  run(3);
}

// Copy 2 sized, faded and wandering, and back as it was after a restart
static void overlay_wandering() {
  step("an animation sized, faded and wandering");
  panel_glide_animation.execute("ball", 2, "size", 150, 0, "none");
  panel_glide_animation.execute("ball", 2, "opacity", 50, 1000, "none");
  run(3);
  steps(500);
  Look l = look(2);
  CHECK_MSG(l.shown && l.size == 1500 && l.opa > 500 && l.opa < 1000, "size %d, opacity %d halfway", l.size, l.opa);
  steps(600);
  l = look(2);
  CHECK_MSG(l.shown && l.opa == 500, "opacity %d after the fade", l.opa);
  panel_area_animation.execute("ball", 2, 100, 100, 300, 230, "bounce", "bounce");
  panel_wander_animation.execute("ball", 2, 20, 60, 0, 180, 300, 900, 0, 0);
  panel_face_animation.execute("ball", 2, 0, true, 0, 0, false);
  run(3);
  const overlay::Overlay *ball = overlay::find("ball");
  int bw = ball->w * ball->scale, bh = ball->h * ball->scale, zw = (bw * 1500 + 500) / 1000,
      zh = (bh * 1500 + 500) / 1000;
  Look at;                     // where it was last
  auto wanders = [&](int n) {  // inside its area, and some way across it on x or y, wherever the random path goes
    bool inside = true;
    int lo = 999, hi = -999, top = 999, bottom = -999;
    for (int i = 0; i < n; i++) {
      steps(50);
      at = look(2);
      int cx = at.x + (bw - zw) / 2, cy = at.y + (bh - zh) / 2;
      inside &= cx >= 100 && cx + zw <= 300 && cy >= 100 && cy + zh <= 230;
      lo = std::min(lo, cx);
      hi = std::max(hi, cx);
      top = std::min(top, cy);
      bottom = std::max(bottom, cy);
    }
    return inside && (hi - lo > 40 || bottom - top > 40);
  };
  CHECK_MSG(wanders(200) && at.shown && at.deg == 0 && ball->copies[2].doing == ", wandering",
            "wandering in its area: at %d,%d, %d degrees", at.x, at.y, at.deg);
  restart_panel();
  steps(400);
  l = look(2);
  CHECK_MSG(l.shown && l.size == 1500 && l.opa == 500 && wanders(100), "after a panel restart: size %d, opacity %d",
            l.size, l.opa);
}

// Copy 1 waved, mapped and its frames paused, and all of it back after a panel restart
static void overlay_waved() {
  step("an animation waved, mapped and paused");
  int p1 = overlay::find("ball")->copies[1].place;
  panel_tell_animation.execute("ball", 1, "wave x 5, 5, 0, 0");
  panel_tell_animation.execute("ball", 1, "map opacity 50, 50, 100, 100");
  panel_frame_rate_animation.execute("ball", 0);
  run(3);
  steps(100);
  int f0 = -1;
  for (int f = 0; f < 8 && f0 < 0; f++)
    if (same_anim("ball", f, p1))
      f0 = f;
  steps(500);
  Look l = look(p1);
  CHECK_MSG(l.shown && l.x == 15 && l.opa == 500 && f0 >= 0 && same_anim("ball", f0, p1),
            "waved, mapped and paused: at %d, opacity %d, frame %d", l.x, l.opa, f0);
  restart_panel();
  steps(400);
  l = look(p1);
  CHECK_MSG(l.shown && l.x == 15 && l.opa == 500 && same_anim("ball", 0, p1),
            "after a panel restart: at %d, opacity %d", l.x, l.opa);
  steps(500);
  CHECK_MSG(same_anim("ball", 0, p1), "the frames went on after a restart");
}

static void overlay_turned_down() {
  step("animation actions the ESP turns down");
  const overlay::Overlay *ball = overlay::find("ball");
  int p1 = ball->copies[1].place;
  size_t warned = log_all.size();
  panel_tell_animation.execute("ball", 1, "wave colour 0, 1, 0, 0");
  panel_glide_animation.execute("ball", 1, "size", 500, 0, "none");
  panel_tell_animation.execute("ball", 1, "map x 0, 1, 0, 0");
  panel_tell_animation.execute("ball", 2, "pivot 100.5, 0");
  panel_tell_animation.execute("ball", 1, "x += nope");
  panel_tell_animation.execute("ball", 1, "wait 100");
  // the panel binds OVERLAY_BINDS at a time: small ones, a pixel, a degree or 99 %, with copy 1's from before
  for (int i = 0; i < OVERLAY_BINDS; i++) {
    int c = i % OVERLAY_CHANNELS;
    panel_tell_animation.execute("ball", i / OVERLAY_CHANNELS,
                                 std::string(overlay::CHANNELS[c]) + (overlay::no_change(c) ? " *= 99" : " += 1"));
  }
  static_assert(OVERLAY_BINDS > 2 * OVERLAY_CHANNELS && OVERLAY_BINDS < 3 * OVERLAY_CHANNELS, "some of copy 2's");
  CHECK_MSG(overlay::bound() == OVERLAY_BINDS && !logged("binds", warned), "%d bound of %d", overlay::bound(),
            OVERLAY_BINDS);
  panel_tell_animation.execute("ball", 2, "depth += 1");
  run_until([] { return !panel::link::waiting(); }, 3000);
  steps(100);
  for (const char *w :
       {"'colour' isn't one of x, y, pan_x, pan_y, w, h, turn, phase, size, size_x, size_y, opacity, "
        "speed, light, level, pivot_x, pivot_y, depth",
        "size takes 10 to 400", "x and y don't follow where it is", "what's 'nope'?", "it can't be told, it's a script",
        "the panel binds 40 at a time", "pivot takes 0 to 100 percent of its box"})
    CHECK_MSG(logged(w, warned), "no warning '%s'", w);
  Look l = look(ball->copies[2].place);
  CHECK_MSG(l.shown && l.depth == OVERLAY_NEAREST && same_pool(), "one bind too many: depth %d, %u bytes, the ESP %u",
            l.depth, pn_overlay_used(), (unsigned) overlay::used_by_others(nullptr));
  for (int i = 0; i < OVERLAY_BINDS; i++) {  // and free again, overlay_waved()'s too
    panel_tell_animation.execute("ball", i / OVERLAY_CHANNELS,
                                 std::string("free ") + overlay::CHANNELS[i % OVERLAY_CHANNELS]);
  }
  panel_frame_rate_animation.execute("ball", 100);
  run_until([] { return !panel::link::waiting(); }, 3000);
  steps(100);
  l = look(p1);
  CHECK_MSG(l.shown && l.x == 10 && l.size == 1000 && l.opa == 1000 && !overlay::bound() && same_pool(),
            "back to no change: at %d, size %d, opacity %d, %d bound", l.x, l.size, l.opa, overlay::bound());
  l = look(ball->copies[0].place);
  CHECK_MSG(l.shown && l.size == 1000 && l.opa == 1000 && l.deg == 0,
            "copy 0 back to no change: size %d, opacity %d, %d degrees", l.size, l.opa, l.deg);
}

// Copy 3 plays one round from its first frame and goes, and the ESP hears so
static void overlay_one_round() {
  step("an animation copy that plays one round");
  panel_place_animation.execute("ball", 3, 20, 20, true, 0);
  panel_rounds_animation.execute("ball", 3, 1);
  run(3);
  CHECK_MSG(look(3).shown && anim_list() == "ball ×4", "copy 3: '%s'", panel_animation_status.state.c_str());
  steps(3000);
  run(5);
  CHECK_MSG(!look(3).shown && !overlay::find("ball")->copies[3].used && anim_list() == "ball ×3",
            "after its round: '%s'", panel_animation_status.state.c_str());
  panel_glide_animation.execute("ball", 2, "size", 100, 0, "none");
  panel_glide_animation.execute("ball", 2, "opacity", 100, 0, "none");
  panel_move_animation.execute("ball", 2, 200, 150, 0, "none");  // still, for what follows
  run(3);
}

// A second animation alongside, placed by the action. A frame alone is over OVERLAY_POOL at full size, so it's made at
// half size right away.
static void overlay_two() {
  step("two animations");
  overlay::add("big", {200, 120, 4, 100, 16, 16, 1, [](int f, int x, int y) -> uint32_t {  // a corner moves
                         return 0xFF000000u | (uint32_t) ((x / 8 + y / 8 + (x < 16 && y < 16 ? f : 0)) % 3) * 0x405060u;
                       }});
  size_t big_at = log_all.size();
  overlay::place("big", 0, 16, 16, false, 0);
  int ar = until_anim_status("ball ×3, big");
  CHECK_MSG(ar >= 0 && same_anim("big", 0, 3), "two animations: '%s'", panel_animation_status.state.c_str());
  CHECK_MSG(overlay::find("big")->half && !logged("making it at half size", big_at),
            "big: half %d, tried at full size first: %d", overlay::find("big") && overlay::find("big")->half,
            logged("making it at half size", big_at));
  unsigned used = pn_overlay_used();
  CHECK_MSG(used == overlay::used_by_others(nullptr) && used <= OVERLAY_POOL,
            "the panel's pool holds %u bytes, the ESP thinks %u", used, (unsigned) overlay::used_by_others(nullptr));
  // its frame, its script, and what its wander and face take
  size_t copy2 = overlay::find("ball")->frame_bytes() + OVERLAY_SCRIPT_HEAD + 3 + OVERLAY_ROAM;
  panel_remove_animation.execute("ball", 2);
  run(3);
  CHECK_MSG(!look(2).shown && pn_overlay_used() == used - copy2 && anim_list() == "ball ×2, big",
            "removing a copy: '%s', %u bytes", panel_animation_status.state.c_str(), pn_overlay_used());
  panel_clear_animation.execute("big");
  run(3);
  CHECK_MSG(!overlay::find("big") && look(0).shown && !look(3).shown && anim_list() == "ball ×2",
            "clearing one animation: '%s'", panel_animation_status.state.c_str());
  overlay::clear("");
  run(3);
  CHECK_MSG(!overlay_ready() && !pn_overlay_used() && !panel_anim_ball.state,
            "the animations are still there after clearing");
}

static void overlay_half_size() {
  step("an animation at half size");
  panel_anim_noise.turn_on();
  int ar = run_until(overlay_shown, 3000);
  CHECK_MSG(ar >= 0 && overlay::find("noise")->half && overlay::find("noise")->scale == 2 && same_anim("noise", 0),
            "noise not shown at half size: %s", panel_animation_status.state.c_str());
  // its drift_x from the yaml, 10 px/s left: half a pixel a step
  pn_overlay_step(100);
  Look l = look(0);
  CHECK_MSG(anim_list() == "noise, drifting" && l.x == 15 && l.y == 16,
            "noise's drift_x: status '%s', at %d,%d after a step", panel_animation_status.state.c_str(), l.x, l.y);
  CHECK_MSG(overlay::find("noise")->data.capacity() == overlay::find("noise")->data.size(), "%u bytes kept for %u",
            (unsigned) overlay::find("noise")->data.capacity(), (unsigned) overlay::find("noise")->data.size());
  pn_restart();
  ar = run_until([] { return overlay_ready() && overlay_shown(); }, 3000);
  CHECK_MSG(ar >= 0 && same_anim("noise", 0), "the animation not sent again after a panel restart");
  l = look(0);
  pn_overlay_step(100);
  CHECK_MSG(l.x == 16 && l.y == 16 && anim_list() == "noise, drifting", "after the restart: at %d,%d, status '%s'", l.x,
            l.y, panel_animation_status.state.c_str());
  l = look(0);
  CHECK_MSG(l.x == 15, "the drift wasn't sent again after a panel restart: at %d", l.x);
  printf("  noise at half size, and again after a panel restart after %d cycles\n", ar);
  panel_anim_noise.turn_off();
  overlay::show("big");  // no yaml, no switch
  ar = until_anim_status("big");
  CHECK_MSG(ar >= 0 && overlay::find("big")->half && same_anim("big", 0) && !overlay::find("noise"), "big: %s, half %d",
            panel_animation_status.state.c_str(), overlay::find("big") && overlay::find("big")->half);
}

static void overlay_not_fitting() {
  step("animations that don't fit");
  // flush with the right and bottom edges, half size rounds the odd height up, so it moves up
  overlay::add("edge", {62, 45, 8, 100, 258, 195, 1, noise_px});
  overlay::clear("big");
  overlay::show("edge");
  int ar = until_anim_status("edge");
  Look l = look(0);
  CHECK_MSG(ar >= 0 && overlay::find("edge")->half && l.x == 258 && l.y == 194 && same_anim("edge", 0),
            "edge: %s, half %d, at %d,%d", panel_animation_status.state.c_str(), overlay::find("edge")->half, l.x, l.y);
  // the panel draws 2x at most, so scale 2 can't go to half size
  overlay::add("noise2", {80, 80, 4, 100, 16, 16, 2, noise_px});
  overlay::clear("edge");
  overlay::show("noise2");
  ar = run_until([] { return overlay::find("noise2") && overlay::find("noise2")->state == overlay::FAILED; }, 100);
  CHECK_MSG(
      ar >= 0 && anim_list().rfind("noise2: too big at scale 2 (over ", 0) == 0 && overlay::find("noise2")->scale == 2,
      "noise2 at scale 2: %s, scale %d", panel_animation_status.state.c_str(), overlay::find("noise2")->scale);
  overlay::add("scale3", {8, 8, 1, 100, 0, 0, 3, noise_px});
  overlay::clear("noise2");
  overlay::show("scale3");
  run(1);
  CHECK_MSG(anim_list() == "scale3: scale has to be 1 or 2", "scale3: %s", panel_animation_status.state.c_str());
  overlay::add("left", {8, 8, 1, 100, -4, 0, 1, noise_px});
  overlay::clear("scale3");
  overlay::show("left");
  run(1);
  CHECK_MSG(anim_list() == "left: doesn't fit on the screen at -4,0", "left: %s", panel_animation_status.state.c_str());
  size_t slow_at = log_all.size();
  overlay::add("slow", {8, 8, 2, 5000, 0, 0, 1, noise_px});
  CHECK_MSG(overlay::sources["slow"].frame_ms == 2550 && logged("frame_ms 5000 is outside 40..2550", slow_at),
            "frame_ms 5000 taken as %d", overlay::sources["slow"].frame_ms);
}

// Clearing it while it's being made frees its buffers
static void overlay_cleared_while_made() {
  step("an animation cleared while it's made");
  overlay::clear("left");
  overlay::show("edge");
  overlay::W.a = overlay::find("edge");
  overlay::start();
  while (overlay::W.f < overlay::W.a->src->frames)
    overlay::count();
  overlay::palette();
  overlay::make(0, true);
  CHECK_MSG(overlay::W.phase == overlay::MAKE && overlay::W.prev.capacity(), "edge isn't being made");
  overlay::clear("");
  CHECK_MSG(!overlay::W.hist.capacity() && !overlay::W.frame.capacity() && !overlay::W.prev.capacity() &&
                !overlay::W.first.capacity(),
            "clearing kept the buffers");
}

// No room next to the others: ball and three big ones at half size, the third is refused. Then two more refused the
// same way fill the slots, and one past that doesn't get one
static void overlay_no_room() {
  step("no room for another animation");
  overlay::show("ball");
  overlay::place("big", 0, 16, 16, false, 0);
  int ar = until_anim_status("ball, big", 6000);
  CHECK_MSG(ar >= 0, "ball and big: '%s'", panel_animation_status.state.c_str());
  overlay::add("big2", overlay::sources["big"]);
  overlay::add("big3", overlay::sources["big"]);
  overlay::place("big2", 0, 16, 16, false, 0);
  overlay::place("big3", 0, 16, 16, false, 0);
  ar = run_until([] { return overlay::find("big3") && overlay::find("big3")->state != overlay::WANT && settled(); },
                 9000);
  CHECK_MSG(ar >= 0 && overlay::find("big2")->state == overlay::SENT &&
                overlay::find("big3")->state == overlay::FAILED &&
                anim_list().rfind("ball, big, big2; big3: too big even at half size (over ", 0) == 0,
            "four animations: '%s'", panel_animation_status.state.c_str());
  CHECK_MSG(pn_overlay_used() == overlay::used_by_others(nullptr) && pn_overlay_used() <= OVERLAY_POOL,
            "the panel's pool holds %u bytes, the ESP thinks %u", pn_overlay_used(),
            (unsigned) overlay::used_by_others(nullptr));
  for (const char *name : {"big4", "big5", "big6"}) {  // refused like big3, but each takes a slot
    overlay::add(name, overlay::sources["big"]);
    overlay::place(name, 0, 16, 16, false, 0);
    ar = run_until([name] { return overlay::find(name) && overlay::find(name)->state != overlay::WANT && settled(); },
                   9000);
    CHECK_MSG(ar >= 0 && overlay::find(name)->state == overlay::FAILED, "%s: '%s'", name,
              panel_animation_status.state.c_str());
  }
  overlay::add("big7", overlay::sources["big"]);
  overlay::place("big7", 0, 16, 16, false, 0);  // no eighth slot
  run(1);
  CHECK_MSG(!overlay::find("big7") && logged("No room for animation big7: the panel holds 7 at a time"),
            "an eighth animation: '%s'", panel_animation_status.state.c_str());
  overlay::clear("");
  run(3);
}

// A GIF that repeats itself goes once round, and a frame shown twice in a row is an empty record, so the panel's
// frames are the GIF's for a script
static void overlay_repeats() {
  step("animation frames that repeat");
  overlay::add("cycle", {16, 16, 9, 100, 0, 0, 1, [](int f, int x, int y) { return noise_px(f % 3, x, y); }});
  overlay::add("held", {16, 16, 4, 100, 0, 40, 1, [](int f, int x, int y) { return noise_px(f == 2 ? 1 : f, x, y); }});
  size_t made = log_all.size();
  overlay::show("cycle");
  overlay::show("held");
  int ar = until_anim_status("cycle, held");
  CHECK_MSG(ar >= 0 && logged("Animation cycle: 16x16, 3 frames (the rest repeat them)", made) &&
                overlay_delays("cycle") == std::vector<int>({10, 10, 10, 10}) && same_anim("cycle", 0),
            "cycle: '%s', %u records", panel_animation_status.state.c_str(), (unsigned) overlay_delays("cycle").size());
  CHECK_MSG(ar >= 0 && overlay_delays("held") == std::vector<int>({10, 10, 10, 10, 10}) && same_anim("held", 0, 1),
            "held: %u records", (unsigned) overlay_delays("held").size());
  overlay::clear("");
  run(3);
}

// ---- what the panel shows is what the ESP thinks it shows
// The ESP restarts. The panel drops what it had for the old one, the copies Home Assistant placed too, and shows what
// the switches do.
static void overlay_esp_restart() {
  step("animations after the ESP restarts");
  overlay::add("a1", {16, 16, 2, 100, 10, 10, 1, noise_px});
  overlay::add("a2", {16, 16, 2, 100, 100, 10, 1, noise_px});
  overlay::show("a1");
  overlay::show("a2");
  overlay::place("a2", 1, 100, 100, false, 0);
  run_until(anims_sent, 3000);
  overlay::clear("a1");  // so a2 gets another slot and place after the restart
  run(3);
  overlay::stop();
  for (auto &a : overlay::overlays)
    a = overlay::Overlay();
  // what panel::setup() sends, and on_boot shows what the switches have on
  panel::link::send(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK);
  overlay::show("a2");
  int r = run_until(anims_sent, 3000);
  CHECK_MSG(r >= 0 && places() == "[0 100,10]" && same_pool(),
            "after an ESP restart the panel shows %s in %u bytes, the ESP has a2 at 100,10 in %u", places().c_str(),
            pn_overlay_used(), (unsigned) overlay::used_by_others(nullptr));
  overlay::clear("");
}

// A hello while one is made: it's made again from the start, not sent half made. A loop makes a few rows, and a
// frame's record on its own.
static void overlay_hello_while_made() {
  step("a hello while an animation is made");
  static int reads;
  overlay::add("six", {128, 40, 6, 100, 50, 50, 1, [](int f, int x, int y) -> uint32_t {
                         reads++;
                         return 0xFF000000u | (uint32_t) ((x / 8 + y / 4 + 2 * f) % 7) * 0x203040u;
                       }});
  overlay::show("six");
  overlay::Overlay *six = overlay::find("six");
  micros_step = 1000;  // 4 calls in a loop's 5 ms
  int most = 0;
  bool moved = false;  // the records, which have their RAM from the start
  const uint8_t *at = nullptr;
  for (int k = 0; k < 1000 && !(overlay::W.phase == overlay::MAKE && overlay::W.f == 2); k++) {
    reads = 0;
    overlay::loop(micros() + panel::SLICE_US);
    most = std::max(most, reads);
    if (overlay::W.phase == overlay::MAKE) {
      moved |= at && six->data.data() != at;
      at = six->data.data();
    }
  }
  micros_step = 0;
  CHECK_MSG(overlay::W.f == 2 && most == 4 * overlay::ROWS_PX && !moved && six->data.capacity() == overlay::data_max(),
            "%d pixels read in a loop, the records moved %d, %u bytes for them", most, moved,
            (unsigned) six->data.capacity());
  pn_restart();  // its hello comes before the next loop
  int r = run_until([&] { return six->state == overlay::SENT && settled(); }, 3000);
  CHECK_MSG(r >= 0 && overlay_delays("six").size() == 7 && same_anim("six", 0), "made again after a hello: %u records",
            (unsigned) overlay_delays("six").size());
  overlay::clear("");
}

// One frame goes as a still, of which the panel keeps only its pieces, and colours of a source's own keep their
// opacity on the panel
static void overlay_stills() {
  step("a still overlay, and one with soft edges");
  overlay::clear("");
  overlay::add("dot", {60, 40, 1, 500, 20, 30, 1, [](int, int x, int y) -> uint32_t {
                         int dx = x - 30, dy = y - 20;
                         return dx * dx + dy * dy < 150 ? 0xFF3080C0u : 0;
                       }});
  overlay::Source soft{64, 48, 1, 500, 120, 30, 1, [](int, int x, int y) -> uint32_t {
                         int d = (x - 32) * (x - 32) + (y - 24) * (y - 24);
                         uint32_t a = d < 200 ? 255 : d < 500 ? (uint32_t) (500 - d) * 255 / 300 : 0;
                         return a << 24 | 0xF0C040;
                       }};
  soft.colours = {0xFFF0C040, 0xC0F0C040, 0x80F0C040, 0x40F0C040, 0};
  overlay::add("soft", soft);
  overlay::show("dot");
  overlay::show("soft");
  int r = run_until(
      [] {
        return overlay::find("dot")->state == overlay::SENT && overlay::find("soft")->state == overlay::SENT &&
               settled();
      },
      600);
  const overlay::Overlay &dot = *overlay::find("dot"), &sa = *overlay::find("soft");
  CHECK_MSG(r >= 0 && dot.still && sa.still && pn_overlay_used() == dot.data.size() + sa.data.size() &&
                same_anim("dot", 0, dot.copies[0].place),
            "two stills: '%s', %u bytes on the panel", panel_animation_status.state.c_str(),
            (unsigned) pn_overlay_used());
  int p = sa.copies[0].place, off = 0, between = 0;
  for (int y = 0; y < sa.h; y++) {
    uint8_t line[320 * 3];
    if (pn_overlay_line(p, y, sa.w, 1, line) != sa.w) {
      off++;
      break;
    }
    for (int x = 0; x < sa.w; x++) {
      uint32_t c = sa.src->pixel(0, std::min(x, sa.src->w - 1), y), k = sa.pal[overlay::nearest_own(c, sa.pal, 5)];
      int opa = (int) ((k >> 24) * 15 + 127) / 255 * 17, got = line[3 * x] | line[3 * x + 1] << 8;
      off += line[3 * x + 2] != opa || (opa && got != sim_rgb565(k));
      between += opa > 0 && opa < 255;
    }
  }
  CHECK_MSG(!off && between > 50, "soft edges: %d pixels off, %d between clear and opaque", off, between);
  overlay::clear("");
}

// A long frame goes on in empty records. Past the end of the pool they must fit the records' capacity, or the
// records move, a peak the RAM check doesn't count. The animation is refused there anyway. With frames enough that
// their records could pass the pool, or the capacity is what they take.
static void overlay_long_frame() {
  step("an animation's long frame past the end of the pool");
  overlay::add("long", {32, 32, 20, 100, 0, 0, 1, noise_px});
  overlay::show("long");
  overlay::W.a = overlay::find("long");
  overlay::start();
  while (overlay::W.f < overlay::W.a->src->frames)
    overlay::count();
  overlay::palette();
  overlay::Overlay &a = *overlay::W.a;
  uint32_t seed = 5;
  for (auto &b : overlay::W.frame)
    b = (uint8_t) (lcg(seed) >> 24);  // noise, so each record is a frame's size
  const uint8_t *at = a.data.data();
  size_t room = a.data.capacity(), past;
  while ((past = a.data.size()) + a.frame_bytes() <= OVERLAY_POOL)
    overlay::record(nullptr, overlay::W.frame, 100);
  bool ok = overlay::record(&overlay::W.prev, overlay::W.frame, 20000);  // 8 records of 2.55 s
  CHECK_MSG(overlay::W.phase == overlay::MAKE && room == overlay::data_max() && ok && a.data.data() == at &&
                a.data.capacity() == room && a.data.size() == past,
            "a long frame past the pool: the records moved %d, %u bytes of %u", a.data.data() != at,
            (unsigned) a.data.size(), (unsigned) a.data.capacity());
  overlay::clear("");
}

// A copy can start off the screen, to come in from there. One that isn't a copy takes nothing.
static void overlay_off_screen() {
  step("an animation copy off the screen");
  size_t warned = log_all.size();
  overlay::place("ball", 9, 10, 10, false, 0);
  run(5);
  CHECK_MSG(!overlay::find("ball") && !overlay_ready() && logged("Animation ball: copy 9 isn't one of 0 to 8", warned),
            "copy 9: ball %d, the panel shows %s", overlay::find("ball") != nullptr, places().c_str());
  overlay::place("ball", 1, -40, 300, false, 0);
  bool sent =
      run_until([] { return overlay::find("ball") && overlay::find("ball")->state == overlay::SENT && settled(); },
                3000) >= 0;
  Look l = sent ? look(overlay::find("ball")->copies[1].place) : Look{};
  CHECK_MSG(sent && l.shown && l.x == -40 && l.y == 300, "a copy off the screen: the panel shows %s", places().c_str());
  overlay::clear("");
  run(5);
}

// panel_show_animation puts copy 0 back where the yaml has it, drifting, once it was removed or played its rounds
static void overlay_shown_again() {
  step("an animation shown again");
  auto back = [] {  // at 16,16 or just left of it, and going left
    Look was = look(0);
    bool at = was.shown && was.x > 10 && was.x <= 16 && was.y == 16;
    steps(500);
    Look l = look(0);
    return at && l.shown && l.x < was.x && anim_list() == "noise, drifting";
  };
  panel_anim_noise.turn_on();
  run_until(anims_sent, 3000);
  panel_remove_animation.execute("noise", 0);
  run(3);
  CHECK_MSG(!overlay_ready() && anim_list() == "noise", "copy 0 removed: %s, '%s'", places().c_str(),
            panel_animation_status.state.c_str());
  panel_show_animation.execute("noise");
  run(3);
  CHECK_MSG(back(), "shown again: %s, '%s'", places().c_str(), panel_animation_status.state.c_str());
  panel_rounds_animation.execute("noise", 0, 1);
  run(3);
  steps(2000);
  run(5);
  CHECK_MSG(!overlay_ready() && anim_list() == "noise", "after its round: %s, '%s'", places().c_str(),
            panel_animation_status.state.c_str());
  panel_show_animation.execute("noise");
  run(3);
  CHECK_MSG(back(), "shown again after its round: %s, '%s'", places().c_str(), panel_animation_status.state.c_str());
  overlay::clear("");
}

static void overlay_timing() {
  step("animation timing");
  // A GIF that ends on its first frame. The record back to it stays its own, so a round ends there.
  overlay::add("aba", {16, 16, 3, 100, 0, 0, 1, [](int f, int x, int y) { return noise_px(f == 1, x, y); }});
  overlay::show("aba");
  run_until(anims_sent, 3000);  // records, with no copy of its own yet to make it a sheet
  overlay::place("aba", 1, 100, 100, true, 0);
  overlay::rounds("aba", 1, 1);
  run(3);
  std::string seen;  // the frames it shows, 2 is like 0
  for (int t = 0; t < 100 && look(1).shown; t++, pn_overlay_step(10)) {
    char f = same_anim("aba", 0, 1, true) ? '0' : same_anim("aba", 1, 1, true) ? '1' : '?';
    if (seen.empty() || seen.back() != f)
      seen += f;
  }
  CHECK_MSG(overlay_delays("aba") == std::vector<int>({10, 10, 10, 10}) && seen == "010",
            "a round of F0 F1 F0 shows %s, %u records", seen.c_str(), (unsigned) overlay_delays("aba").size());
  overlay::clear("");

  // Each frame its own time, past a record's 2550 ms in OVERLAY_RECORD_MORE ones after it: F0 4 s, F1 0.1 s, F2
  // 3 s. In a loop F0 shows for the record back to it and the MORE one after the first record.
  overlay::Source slow{16, 16, 3, 100, 0, 0, 1, noise_px};
  slow.delays = {4000, 100, 3000};
  overlay::add("slow", slow);
  overlay::show("slow");
  run_until(anims_sent, 3000);
  std::vector<std::pair<int, int>> turns;  // when the panel shows another frame, and which
  int was = 0;
  for (int t = 0; t <= 16000; t += 10, pn_overlay_step(10)) {
    int f = same_anim("slow", 0, 0, true) ? 0 : same_anim("slow", 1, 0, true) ? 1 : 2;
    if (f != was)
      turns.push_back({t, f});
    was = f;
  }
  int want[3] = {4000, 100, 3000}, timed = 0;  // how long each frame shows, every time it's seen whole
  for (size_t i = 0; i + 1 < turns.size(); i++)
    timed += abs(turns[i + 1].first - turns[i].first - want[turns[i].second]) <= 60 ? 1 : -100;
  CHECK_MSG(overlay_delays("slow") == std::vector<int>({255, 145, 10, 255, 45, 255}) && timed >= 4,
            "frames of 4 s, 0.1 s and 3 s: %u records, %u changes, %d timed right",
            (unsigned) overlay_delays("slow").size(), (unsigned) turns.size(), timed);
  overlay::clear("");
}

// What making one takes, pdeflate and all. Short of it, it waits and says so, and is made once the ESP has it. Short
// of it for RAM_LOOKS, it's made at half size if the ESP has the RAM for that.
static void overlay_without_ram() {
  step("animations without the RAM");
  size_t tls = panel::requests * panel::REQUEST_RAM;  // met's request from boot, never run here, holds its share
  auto waits = [](const char *name) {
    overlay::Overlay *a = overlay::find(name);
    return a && a->state == overlay::WANT && a->looks && a->data.empty();
  };
  const char *waiting = "Animation ball: waiting for the RAM to make it, ";
  std::string has = "The ESP has " + std::to_string(45 + tls / 1024) + " KB free";
  // held for a few seconds, by a picture being made or a request: made at full size once it's back
  ram_free = 45 * 1024 + tls;
  size_t warned = log_all.size();
  panel_anim_ball.turn_on();
  run(60);
  CHECK_MSG(waits("ball") && !overlay::find("ball")->half && anim_list() == "ball: waiting for the ESP's RAM" &&
                times_logged(waiting, warned) == 1 && logged(has.c_str(), warned),
            "ball short of RAM for a while: '%s', said %d times", panel_animation_status.state.c_str(),
            times_logged(waiting, warned));
  ram_free = 200 * 1024;
  int r = run_until(anims_sent, 3000);
  CHECK_MSG(r >= 0 && r < 40 && !overlay::find("ball")->half && same_anim("ball", 0) && anim_list() == "ball",
            "ball once the RAM is back: %d cycles, half %d, '%s'", r, overlay::find("ball")->half,
            panel_animation_status.state.c_str());
  // short of it at full size for long: at half size
  overlay::clear("ball");
  ram_free = 45 * 1024 + tls;
  warned = log_all.size();
  overlay::show("ball");
  r = run_until(anims_sent, 3000);
  int looks = (int) (overlay::RAM_LOOKS * overlay::RAM_LOOK_MS / 110);  // in cycles
  CHECK_MSG(r >= looks && r < looks + 60 && overlay::find("ball")->half && same_anim("ball", 0) &&
                logged("Animation ball: the ESP has the RAM to make it at half size only", warned),
            "ball without the RAM for full size: half %d after %d cycles", overlay::find("ball")->half, r);
  // one that can't halve waits on, and a smaller one is made meanwhile
  overlay::add("ball2x", {32, 32, 4, 100, 0, 100, 2, noise_px});
  overlay::add("tiny", {16, 16, 1, 100, 200, 100, 1, noise_px});
  ram_free = 30 * 1024 + tls;
  overlay::show("ball2x");
  overlay::show("tiny");
  run(looks + 100);
  CHECK_MSG(waits("ball2x") && anim_list() == "ball, tiny; ball2x: waiting for the ESP's RAM", "'%s'",
            panel_animation_status.state.c_str());
  ram_free = 200 * 1024;
  r = run_until(anims_sent, 3000);
  CHECK_MSG(r >= 0 && r < 40 && anim_list() == "ball, ball2x, tiny", "not made once the RAM is back: %d cycles, '%s'",
            r, panel_animation_status.state.c_str());
  // a hello while one waits: it's made once the RAM is back
  overlay::clear("ball2x");
  overlay::clear("tiny");
  ram_free = 30 * 1024 + tls;
  overlay::show("ball2x");
  run(3);
  restart_panel();
  CHECK_MSG(waits("ball2x"), "ball2x through a hello: '%s'", panel_animation_status.state.c_str());
  ram_free = 200 * 1024;
  r = run_until(anims_sent, 3000);
  CHECK_MSG(r >= 0 && anim_list() == "ball, ball2x", "not made after a hello: '%s'",
            panel_animation_status.state.c_str());
  overlay::clear("");
  // a small one whose copy plays its frames takes what its few frames do, not the pool's worth of records twice
  overlay::add("boat9", {13, 15, 9, 300, 0, 0, 1, noise_px});
  ram_free = 31 * 1024 + tls;
  overlay::place("boat9", 0, 100, 100, true, 0);
  run(30);
  bool short_of = waits("boat9");
  ram_free = 32 * 1024 + tls;
  r = run_until(anims_sent, 3000);
  ram_free = 200 * 1024;
  CHECK_MSG(short_of && r >= 0 && anim_list() == "boat9", "a 13x15 of 9 frames with 31 KB, then 32: waited %d, '%s'",
            short_of, panel_animation_status.state.c_str());
  overlay::clear("");
  // one that would only fail again isn't made again after a hello, until it's shown again
  static int making;
  panel_animation_status.add_on_state_callback(
      [](const std::string &s) { making += s.find("making noise2") != std::string::npos; });
  overlay::show("noise2");
  run(3);
  restart_panel();
  CHECK_MSG(making == 1 && overlay::find("noise2")->state == overlay::FAILED, "noise2 made %d times, through a hello",
            making);
  overlay::show("noise2");
  run(3);
  CHECK_MSG(making == 2, "noise2 not tried again when shown");
  overlay::clear("");
}

// Panel Animation Status: how full the panel is, what's on its way while it's made and sent, and no longer than Home
// Assistant takes, the rest counted
static void overlay_status() {
  step("the animations' status");
  overlay::clear("");
  run(3);
  static bool sending;  // the callback stays
  panel_animation_status.add_on_state_callback(
      [](const std::string &s) { sending |= s.find("sending ball") != std::string::npos; });
  overlay::show("ball");
  run_until(anims_sent, 3000);
  const std::string &st = panel_animation_status.state;
  CHECK_MSG(sending && st.rfind("1 of 7, 1 of 9 places, ", 0) == 0 && anim_list() == "ball", "status '%s'", st.c_str());
  std::vector<std::string> names;
  for (int i = 0; i < 6; i++) {  // names long enough that they don't all fit
    names.push_back("a_rather_long_animation_name_to_fill_the_status_up_" + std::to_string(i));
    overlay::add(names.back(), overlay::sources["ball"]);
    overlay::show(names.back());
  }
  run_until(anims_sent, 20000);
  CHECK_MSG(st.size() <= 255 && st.find(" more") != std::string::npos && st.rfind("7 of 7, ", 0) == 0,
            "seven animations: %u characters, '%s'", (unsigned) st.size(), st.c_str());
  overlay::clear("");
  run(3);
  for (auto &n : names)
    overlay::sources.erase(n);
}

// Copies with frames of their own when it's made make it a sheet, which keeps no frame for them. Placed once it's
// sent, each takes one, and those past the panel's pool aren't kept. One placed again elsewhere has its own frame
// back first.
static void overlay_past_pool() {
  step("animation copies past the panel's pool");
  overlay::show("ball");
  for (int i = 1; i <= 6; i++)
    overlay::place("ball", i, 40 * i, 150, true, 0);
  run_until(anims_sent, 3000);
  overlay::Overlay *ball = overlay::find("ball");
  CHECK_MSG(
      ball->sheet && ball->copies[6].used && anim_list() == "ball ×7" && same_pool() &&
          pn_overlay_used() == ball->data.size() && look(ball->copies[6].place).shown &&
          [&] {  // one of its frames, as the ESP made it
            for (int f = 0; f < ball->src->frames; f++)
              if (same_anim("ball", f, ball->copies[6].place, true))
                return true;
            return false;
          }(),
      "a sheet with six copies of its own: '%s', %u bytes on the panel", panel_animation_status.state.c_str(),
      pn_overlay_used());
  overlay::clear("");
  run(3);
  overlay::show("ball");  // 1825 bytes, and 1152 for each frame of its own: as many of those as fit, and no more
  run_until(anims_sent, 3000);
  ball = overlay::find("ball");
  int fit = (int) ((OVERLAY_POOL - ball->frame_bytes() - ball->data.size()) / ball->frame_bytes());
  static_assert(OVERLAY_POOL == 8960, "six fit, with places left over");
  for (int i = 1; i <= fit + 1; i++)
    overlay::place("ball", i, 40 * i, 150, true, 0);
  run(3);
  ball = overlay::find("ball");
  int shown = 0;
  for (int p = 0; p < OVERLAY_PLACES; p++)
    shown += look(p).shown;
  std::string all = "ball ×" + std::to_string(fit + 1);
  auto no_more = [&] { return (int) ball->copies.size() < fit + 2 || !ball->copies[fit + 1].used; };
  CHECK_MSG(fit == 6 && no_more() && anim_list() == all && same_pool() && shown == fit + 1,
            "%d copies with frames of their own: '%s', the panel has %u bytes, the ESP thinks %u", fit,
            panel_animation_status.state.c_str(), pn_overlay_used(), (unsigned) overlay::used_by_others(nullptr));
  size_t warned = log_all.size();
  overlay::place("ball", fit + 1, 150, 150, true, 0);
  overlay::place("ball", 2, 100, 50, true, 0);
  run(3);
  Look l = look(ball->copies[2].place);
  CHECK_MSG(no_more() && anim_list() == all && same_pool() && l.shown && l.x == 100 && l.y == 50 &&
                logged("no room in the panel's 8960 bytes for a copy with a frame of its own", warned),
            "placed with no room: '%s', copy 2 at %d,%d", panel_animation_status.state.c_str(), l.x, l.y);
  // all nine places taken: no slot for another
  for (int i = fit + 1; i < OVERLAY_PLACES; i++)
    overlay::place("ball", i, 20 * i, 10, false, 0);
  warned = log_all.size();
  overlay::show("noise");
  CHECK_MSG(!overlay::find("noise") && logged("the panel shows 9 copies at most", warned),
            "a slot taken with no place for it");
  overlay::clear("");
}

// panel_tell_animation: a copy bound while its script runs on, freed again, and bound again after a panel restart
static void overlay_told() {
  step("an animation copy told what binds it");
  overlay::show("ball");
  overlay::place("ball", 1, 10, 150, false, 0);
  panel_script_animation.execute("ball", 1, "on: v = y; wait 100; goto on");
  panel_tell_animation.execute("ball", 1, "x += 30 + sin(ms / 10, 20)");
  panel_tell_animation.execute("ball", 1, "opacity *= 50");
  run_until(anims_sent, 3000);
  int p = overlay::find("ball")->copies[1].place, lo = 999, hi = -999;
  for (int again = 0; again < 2; again++) {
    for (int t = 0; t < 80; t++) {
      steps(50);
      lo = std::min(lo, look(p).x);
      hi = std::max(hi, look(p).x);
    }
    CHECK_MSG(lo >= 20 && lo < 24 && hi > 56 && hi <= 60 && look(p).opa == 500 && same_pool(),
              "bound%s: x %d to %d, opacity %d, %u bytes, the ESP %u", again ? " after a restart" : "", lo, hi,
              look(p).opa, pn_overlay_used(), (unsigned) overlay::used_by_others(nullptr));
    restart_panel();
    steps(400);
  }
  panel_tell_animation.execute("ball", 1, "free x, opacity");
  run(3);
  steps(100);
  CHECK_MSG(look(p).x == 10 && look(p).opa == 1000 && !overlay::bound() && same_pool(),
            "freed: at %d, opacity %d, %d bound", look(p).x, look(p).opa, overlay::bound());
  overlay::clear("");
  run(3);
}

// What docs/panel.md shows is what the compiler takes: each `script: |` and `code: |` of its YAML, with the clips
// its block names, and each `statement:` an action there tells a copy
static void overlay_documented() {
  step("the scripts the docs show");
  std::ifstream f(DOCS "panel.md");
  std::vector<std::string> lines;
  for (std::string l; std::getline(f, l);)
    lines.push_back(l);
  auto indent = [](const std::string &l) { return l.find_first_not_of(' '); };
  auto quoted = [](const std::string &l, const char *key) {  // key: "this"
    size_t at = l.find(key), a = at == std::string::npos ? at : l.find('"', at), b = l.find('"', a + 1);
    return a == std::string::npos || b == std::string::npos ? std::string() : l.substr(a + 1, b - a - 1);
  };
  int scripts = 0, statements = 0;
  size_t fence = 0;  // the line the YAML it's in started at
  for (size_t i = 0; i < lines.size(); i++) {
    const std::string &l = lines[i];
    std::string error, told = quoted(l, "statement: ");
    if (l.rfind("```", 0) == 0)
      fence = i;
    if (!told.empty()) {
      overlay::Program p = overlay::compile(told, error);
      CHECK_MSG(error.empty() && p.bytes.empty(), "docs/panel.md line %u, '%s': %s", (unsigned) i + 1, told.c_str(),
                error.empty() ? "it can't be told, it's a script" : error.c_str());
      statements++;
    }
    size_t key = l.find("script: |") != std::string::npos ? l.find("script: |") : l.find("code: |");
    if (key == std::string::npos || key != indent(l))
      continue;
    std::string text, clips;
    for (size_t k = fence; k < i; k++)
      if (lines[k].find("clips: ") != std::string::npos)
        clips = quoted(lines[k], "clips: ");
    for (size_t k = i + 1; k < lines.size() && (indent(lines[k]) == std::string::npos || indent(lines[k]) > key); k++)
      text += lines[k] + "\n";
    overlay::Clips c = overlay::parse_clips(clips, error);
    if (error.empty())
      overlay::compile(text, error, c);
    CHECK_MSG(error.empty(), "docs/panel.md line %u's script: %s", (unsigned) i + 1, error.c_str());
    scripts++;
  }
  CHECK_MSG(scripts >= 7 && statements >= 9, "%d scripts and %d statements found in docs/panel.md", scripts,
            statements);
}

// field.yaml's glow: 4 bytes on the panel, which works out what it shows, lightest in its middle and none in its
// corners, its opacity bound by its script
static void overlay_field() {
  step("a field from its yaml");
  CHECK_MSG(!logged("Animation glow:"), "glow's yaml warned about");
  std::string none, bad[3];
  std::vector<uint32_t> cols = overlay::ramp("00FFF4D8, #FFFFF4D8", none);
  overlay::ramp("FFF4D8, red", bad[0]);
  overlay::ramp("FFF4D8", bad[1]);
  std::string many;
  for (int k = 0; k < 17; k++)
    many += k ? ", 101010" : "101010";
  overlay::ramp(many, bad[2]);
  CHECK_MSG(none.empty() && cols.size() == 16 && cols[0] == 0x00FFF4D8 && cols[15] == 0xFFFFF4D8 &&
                cols[6] == 0x66FFF4D8 && bad[0] == "'red' isn't a colour like FFF4D8 or 80FFF4D8" &&
                bad[1] == "a field goes along 2 colours or more" && bad[2] == "16 colours at most",
            "a field's colours: '%s', %u of them, '%s', '%s'", none.c_str(), (unsigned) cols.size(), bad[0].c_str(),
            bad[1].c_str());
  pn_saver(1);
  pn_draw(50);
  const int X = 200, Y = 20, FW = 60, FH = 40;
  uint32_t was[2] = {pn_pixel(X + FW / 2, Y + FH / 2), pn_pixel(X + 1, Y + 1)};
  panel_anim_glow.turn_on();
  run_until(anims_sent, 3000);
  const overlay::Overlay *a = overlay::find("glow");
  int p = a ? a->copies[0].place : -1, lo = 1000, hi = 0, lit = 0;
  Look l = look(p);
  CHECK_MSG(a && a->field && a->data.size() == OVERLAY_FIELD_LEN && l.shown && l.x == X && l.y == Y && l.w == FW &&
                l.h == FH && (l.view & OVERLAY_VIEW_BLEND) == OVERLAY_BLEND_ADD && same_pool() &&
                pn_overlay_used() < 64,
            "the glow: at %d,%d, %d x %d, view %d, %u bytes, the ESP %u", l.x, l.y, l.w, l.h, l.view, pn_overlay_used(),
            (unsigned) overlay::used_by_others(nullptr));
  auto bright = [](uint32_t c) { return (int) ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255)); };
  for (int t = 0; t < 8; t++)  // faded in
    pn_draw(50);
  for (int t = 0; t < 80; t++) {
    pn_draw(50);
    lo = std::min(lo, look(p).opa);
    hi = std::max(hi, look(p).opa);
    lit = std::max(lit, bright(pn_pixel(X + FW / 2, Y + FH / 2)) - bright(was[0]));
  }
  CHECK_MSG(lo >= 200 && lo < 260 && hi > 940 && hi <= 1000 && lit > 60 && pn_pixel(X + 1, Y + 1) == was[1],
            "its opacity %d to %d, its middle %d lighter, its corner %06X then %06X", lo, hi, lit, was[1],
            pn_pixel(X + 1, Y + 1));
  pn_saver(0);
  panel_anim_glow.turn_off();
  run(5);
  CHECK_MSG(!overlay::find("glow") && same_pool(), "the glow turned off");
}

// Taking a copy off or clearing another while one is on its way: it goes on, and the panel has what the ESP thinks
static void overlay_on_its_way() {
  step("animations changed while one is on its way");
  overlay::add("mid", {32, 32, 6, 100, 200, 100, 1, noise_px});
  overlay::show("mid");
  run_until([] { return overlay::W.t.head && overlay::W.t.pos > 512; }, 3000);
  CHECK_MSG(overlay::W.t.head && same_pool(), "on its way: the panel has %u bytes, the ESP thinks %u",
            pn_overlay_used(), (unsigned) overlay::used_by_others(nullptr));
  size_t warned = log_all.size();
  overlay::remove("ball", 1);
  int r = run_until(anims_sent, 3000);
  CHECK_MSG(r >= 0 && !logged("sending it again", warned) && same_anim("mid", 0, overlay::find("mid")->copies[0].place),
            "mid sent again after a copy of ball went");
  overlay::clear("mid");
  overlay::show("mid");
  run_until([] { return overlay::W.t.head && overlay::W.t.pos > 512; }, 3000);
  overlay::clear("mid");
  run(5);
  CHECK_MSG(same_pool() && !overlay::find("mid"), "cleared on its way: the panel has %u bytes, the ESP thinks %u",
            pn_overlay_used(), (unsigned) overlay::used_by_others(nullptr));
  overlay::clear("");
  run(3);
}

// ---- animation scripts, packages/code/panel/overlay_script.h
// Which of its frames place p shows of animation name's, '?' for none
static char frame_at(const char *name, int frames, int p) {
  for (int f = 0; f < frames; f++)
    if (same_anim(name, f, p, true))
      return (char) ('0' + f);
  return '?';
}

// The animations play for ms, the link going as it does
static void play_ms(int ms) {
  for (int t = 0; t < ms; t += 50) {
    pn_overlay_step(50);
    if (t % 100 == 0)
      run(1);
  }
}

static bool kite_at(int i, int x, int y) {
  const overlay::Overlay *a = overlay::find("kite");
  Look l = look(a ? a->copies[i].place : -1);
  return l.shown && l.x == x && l.y == y;
}

// kite's script from its yaml: clip up twice, mark 3, clip down once, and it goes
static void overlay_yaml_script() {
  step("an animation's script from its yaml");
  CHECK_MSG(!logged("Animation kite:"), "kite's yaml script warned about");
  std::vector<std::string> marks;
  overlay::marked = [&marks](const std::string &name, int i, int v) {
    marks.push_back(name + "." + std::to_string(i) + " " + std::to_string(v));
  };
  panel_anim_kite.turn_on();
  run_until(anims_sent, 3000);
  int p = overlay::find("kite")->copies[0].place;
  std::string seen, when;  // its frames, and the frame at the mark
  for (int t = 0; t < 3000 && look(p).shown; t += 10) {
    char f = frame_at("kite", 8, p);
    if (seen.empty() || seen.back() != f)
      seen += f;
    if (!marks.empty() && when.empty())
      when = seen;
    pn_overlay_step(10);
    if (t % 100 == 0)
      run(1);
  }
  run(5);
  CHECK_MSG(seen == "012301234567" && marks == std::vector<std::string>{"kite.0 3"} && when.size() >= 9 &&
                when.size() <= 10 && !overlay::find("kite")->copies[0].used && same_pool(),
            "up twice and down once: %s, marked at %s, %u marks, %d left", seen.c_str(), when.c_str(),
            (unsigned) marks.size(), (int) overlay::find("kite")->copies[0].used);
  overlay::show("kite");  // and again from the start
  run_until(anims_sent, 3000);
  steps(200);
  CHECK_MSG(frame_at("kite", 8, overlay::find("kite")->copies[0].place) == '2', "not up again when shown again");
  overlay::marked = nullptr;
  overlay::clear("");
  run(3);
}

// panel_script_animation: copy 1 goes to and fro, copy 2 follows it, past a panel restart
static void overlay_script_action() {
  step("animation scripts from panel_script_animation");
  overlay::place("kite", 1, 40, 60, false, 0);
  overlay::place("kite", 2, 0, 0, false, 0);
  panel_script_animation.execute("kite", 1,
                                 "x0 = x\nback: move x0 + 40, y over 500\nwait 1000\n"
                                 "move x0, y over 500  # and home\nwait 1000\ngoto back");
  panel_script_animation.execute("kite", 2, "on: move x(kite.1), y(kite.1) + 20; wait 100; goto on");
  run_until(anims_sent, 3000);
  for (int again = 0; again < 2; again++) {
    play_ms(800);
    bool there = kite_at(1, 80, 60) && kite_at(2, 80, 80);
    play_ms(1000);
    CHECK_MSG(there && kite_at(1, 40, 60) && kite_at(2, 40, 80) && same_pool(),
              "to and fro and followed%s: %s, %u bytes on the panel, %u on the ESP", again ? " after a restart" : "",
              places().c_str(), pn_overlay_used(), (unsigned) overlay::used_by_others(nullptr));
    pn_restart();
    run_until(anims_sent, 3000);
  }
  // one that can't be is warned about, and the one it runs runs on
  struct {
    int copy;
    const char *script, *why;
  } bad[] = {
      {1, "move x +", "line 1: a number or a name expected"},
      {1, "wait 100\ngoto nowhere", "line 2: no 'nowhere:'"},
      {1, "clip fly", "line 1: what's 'fly'?"},
      {1, "move x(kite.9), 0", "line 1: no copy 9, they're 0 to 8"},
      {1, "wait 100.5", "only whole numbers"},
      {1, "mark 256", "marks are 0 to 255"},
      {2, "clip up", "copy 2 has no frame of its own"},
      {1, "a = 1; b = 2; c = 3; d = 4; e = 5; f = 6; g = 7; k = 8; m = 9", "only 8 of its own"},
      {1, "tint 0x1000000", "a tint is a colour like 0xFF77BB"},
      {1, "glide light 201", "light takes 0 to 200"},
      {1, "glide w -1 over 100", "w takes 0 to 1024"},
      {1, "map x 0, 1, 0, 1", "x and y don't follow where it is"},
      {1, "shape 4, 5", "a shape is runs of width, top and bottom"},
      {1, "shape 4, 5, 1", "its bottom under its top"},
      {1, "tell kite.2 wait 100", "tell sends another copy what moves, shows or binds it"},
      {1, "k = 1\ntell kite.2 x += k", "another copy's is bound by what it has itself, or what's shared"},
      {1, "frame += 1", "'frame' is only bound with :="},
      {1, "v = sin(1, 2, 3)", "sin takes 2"},
      {1, "v = wave(ms, 0, 1)", "wave takes 5 to 9"},
      {1, "v = pick(x, 1, y)", "pick's list is whole numbers"},
      {1, "free nothing", "what's 'nothing'?"},
      {1, "tell kite.2 tell kite.0 flip x", "one tell at a time"},
      {1, "wander -5, 5, 0, 180, 0, 0, 0, 0", "its speeds and times can't be below 0"},
  };
  std::vector<uint8_t> was = overlay::find("kite")->copies[1].script.bytes;
  for (auto &b : bad) {
    size_t warned = log_all.size();
    panel_script_animation.execute("kite", b.copy, b.script);
    CHECK_MSG(logged("panel_script_animation: kite's script, ", warned) && logged(b.why, warned),
              "'%s' on copy %d isn't warned about with '%s': %s", b.script, b.copy, b.why,
              log_all.substr(warned).c_str());
  }
  {  // what a script tells other copies counts for the panel's memory and binds, a copy that isn't shown is nobody,
    // and its own fade for an end is as long as a fade can be
    std::string err;
    overlay::Program p = overlay::compile("wait 100\ntell kite.2 wander 5, 9, 0, 180, 0, 0, 0, 0\n"
                                          "tell kite.2 opacity *= 50 + sin(ms / 9, 50)\ntell kite.2 free opacity\n"
                                          "wander 5, 9, 0, 180, 0, 0, 0, 0\nend",
                                          err);
    const uint8_t *fade = nullptr;
    for (auto &k : p.patches)
      if (k.kind == overlay::Program::FADE)
        fade = &p.bytes[k.at];
    CHECK_MSG(err.empty() && p.roams && p.gives > OVERLAY_ROAM + 8 && p.gives < OVERLAY_ROAM + 40 &&
                  p.gives_binds == 1 && fade && fade[-1] == OVERLAY_V32 && fade + 4 < p.bytes.data() + p.bytes.size(),
              "what a script gives other copies: '%s', %u bytes, %d bound", err.c_str(), (unsigned) p.gives,
              p.gives_binds);
    CHECK_MSG(overlay::place_of("kite", 7) == OVERLAY_NOBODY && overlay::place_of("nothing", 0) == OVERLAY_NOBODY &&
                  overlay::place_of("kite", 1) == overlay::find("kite")->copies[1].place,
              "a copy that isn't shown is at %d", overlay::place_of("kite", 7));
  }
  {  // What a value's operators take, by the steps they make: the same as with brackets round each. And how deep
    // its brackets and how high what it works out may go, which is what an ESP's stack has room for: one that
    // took more of it wrote over the heap.
    auto steps = [](const std::string &value, std::string &err) { return overlay::compile("v = " + value, err).bytes; };
    static const char *const SAME[][2] = {
        {"1 + x * 3 - y / 2", "(1 + (x * 3)) - (y / 2)"},
        {"x - y - 3", "(x - y) - 3"},
        {"x % 4 * y", "(x % 4) * y"},
        {"x * y / 3", "scale(x, y, 3)"},
        {"x < y or not x == 3 and y > 2", "(x < y) or ((not (x == 3)) and (y > 2))"},
        {"not x + 1 < y", "not ((x + 1) < y)"},
        {"not not x", "not (not x)"},
        {"--x", "-(-x)"},
        {"-x * y", "(-x) * y"},
        {"min(x, max(y, 2 * (x + 1))) + 1", "(min(x, (max(y, (2 * (x + 1)))))) + 1"},
    };
    for (auto &pair : SAME) {
      std::string err[2];
      std::vector<uint8_t> plain = steps(pair[0], err[0]), bracketed = steps(pair[1], err[1]);
      CHECK_MSG(err[0].empty() && err[1].empty() && !plain.empty() && plain == bracketed,
                "'%s' isn't '%s': '%s', '%s', %u and %u bytes", pair[0], pair[1], err[0].c_str(), err[1].c_str(),
                (unsigned) plain.size(), (unsigned) bracketed.size());
    }
    std::string err, apart;
    CHECK_MSG(steps("x * (y / 3)", err) != steps("x * y / 3", apart) && err.empty() && apart.empty(),
              "brackets round a division change nothing: '%s', '%s'", err.c_str(), apart.c_str());
    auto inside = [](const char *open, int n) {
      std::string value = "x";
      for (int k = 0; k < n; k++)
        value = open + value + ")";
      return value;
    };
    auto sum_of = [](int n) {
      std::string sum = "x";
      for (int k = 1; k < n; k++)
        sum += " + x";
      return sum;
    };
    using overlay::text::HIGHEST;
    using overlay::text::NESTED;
    struct {
      std::string value;
      const char *why;  // "" if it's made
    } ends[] = {
        {inside("(", NESTED), ""},
        {inside("(", NESTED + 1), "more than 6 brackets inside each other"},
        {inside("abs(", NESTED), ""},
        {inside("abs(", NESTED + 1), "more than 6 brackets inside each other"},
        {inside("clamp(0, 9, ", 3), ""},
        {inside("clamp(0, 9, ", 4), "too much to work out at once"},
        {sum_of(HIGHEST), ""},
        {sum_of(HIGHEST + 1), "too much to work out at once"},
        {std::string(HIGHEST - 1, '-') + "x", ""},
        {std::string(200, '-') + "x", "too much to work out at once"},
        {"x + not y", "what's 'not'?"},
        {"x < not y", "what's 'not'?"},
    };
    for (auto &e : ends) {
      std::string why;
      bool made = !steps(e.value, why).empty() && why.empty();
      CHECK_MSG(*e.why ? !made && why.find(e.why) != std::string::npos : made, "'%s' %s: '%s'", e.value.c_str(),
                *e.why ? "is made, or isn't refused for what it is" : "isn't made", why.c_str());
    }
    // The stack is looked at before a script is read, at each bracket and at each step up a value. Wherever it's
    // found full the script isn't made, and says so.
    int looks = 0;
    for (bool made = false; !made && looks < 100; looks++) {
      stack_full_at = looks;
      made = !steps("min(x, (y + 1) * 3)", err).empty();
      CHECK_MSG(made ? err.empty() && stack_full_at >= 0 : err.find("no room on the stack") != std::string::npos,
                "the stack found full the %d. time it's looked at: '%s'", looks + 1, err.c_str());
    }
    stack_full_at = -1;
    CHECK_MSG(looks > 6 && looks < 100, "the stack is looked at %d times for a call and a bracket", looks - 1);
    overlay::Program rows = overlay::compile("a: b: c: wait 100\ngoto b", err);
    CHECK_MSG(err.empty() && !rows.bytes.empty(), "three labels on one step: '%s'", err.c_str());
  }
  {  // a clip by name shows its w x h of the frames, one by number all of them, and a tint is told before it starts
    std::string err;
    overlay::Program p =
        overlay::compile("tint 0xFF77BB\nclip stay\nclip 1 to 2\ntint none", err, {{"stay", {4, 9, 17, 300}}});
    std::vector<uint8_t> clips = {OVERLAY_DO_CLIP,
                                  OVERLAY_V8,
                                  4,
                                  OVERLAY_V8,
                                  9,
                                  OVERLAY_CLIP_NOW,
                                  17,
                                  0,
                                  44,
                                  1,
                                  OVERLAY_DO_CLIP,
                                  OVERLAY_V8,
                                  1,
                                  OVERLAY_V8,
                                  2,
                                  OVERLAY_CLIP_NOW,
                                  0,
                                  0,
                                  0,
                                  0,
                                  OVERLAY_DO_SEND,
                                  OVERLAY_SELF,
                                  K_OVERLAY_TINT - K_OVERLAY_FRAMES,
                                  OVERLAY_TINT_LEN,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0},
                         tint = {0, 1, 0xFF, 0x77, 0xBB};
    CHECK_MSG(
        err.empty() && p.bytes == clips && p.told.size() == 1 && p.told[0].key == K_OVERLAY_TINT && p.told[0].m == tint,
        "clips and tints: '%s', %u bytes, %u told", err.c_str(), (unsigned) p.bytes.size(), (unsigned) p.told.size());
  }
  {  // views: the box glided, its frame scrolled and the box grown, repeats and flips told as views, a shape as a step,
     // another copy told as it runs, and all of a copy read
    std::string err;
    overlay::Program p = overlay::compile("glide w 90\nscroll 1.5, -2\ngrow 3, 0\nrepeat both mirror\nflip x\n"
                                          "shape 4, 0, 10, 6, 2, 8\ntell kite.2 glide pan_x 7\n"
                                          "v = pan_x + w(kite.2) + frames + frame_w + frame_h + depth + speed",
                                          err);
    const unsigned R = OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_REPEAT_Y | OVERLAY_VIEW_MIRROR;
    std::vector<uint8_t> shape = {OVERLAY_DO_SHAPE, 6, 4, 0, 0, 0, 10, 0, 6, 0, 2, 0, 8, 0};
    using overlay::msg::sets;
    bool told =
        p.told.size() == 5 && p.told[0].key == K_OVERLAY_GLIDE && p.told[1].key == K_OVERLAY_DRIFT &&
        sets(K_OVERLAY_DRIFT, p.told[1].m) == (1u << OVERLAY_PAN_X | 1u << OVERLAY_PAN_Y) &&
        p.told[1].m[OVERLAY_DRIFT_HEAD] == OVERLAY_PAN_X && plink_i16(&p.told[1].m[OVERLAY_DRIFT_HEAD + 1]) == 150 &&
        plink_i16(&p.told[1].m[OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH + 1]) == -200 &&
        sets(K_OVERLAY_DRIFT, p.told[2].m) == (1u << OVERLAY_W | 1u << OVERLAY_H) && p.told[3].key == K_OVERLAY_VIEW &&
        p.told[3].m[OVERLAY_VIEW_MASK] == R && p.told[3].m[OVERLAY_VIEW_FLAGS] == R &&
        p.told[4].key == K_OVERLAY_VIEW && p.told[4].m[OVERLAY_VIEW_FLAGS] == OVERLAY_VIEW_FLIP_X;
    bool sends =
        p.bytes.size() > shape.size() + 3 && std::equal(shape.begin(), shape.end(), p.bytes.begin()) &&
        p.bytes[shape.size()] == OVERLAY_DO_SEND && p.bytes[shape.size() + 2] == K_OVERLAY_GLIDE - K_OVERLAY_FRAMES &&
        std::any_of(p.patches.begin(), p.patches.end(), [&](const overlay::Program::Patch &k) {
          return k.kind == overlay::Program::PLACE && k.at == shape.size() + 1 && k.name == "kite" && k.copy == 2;
        });
    CHECK_MSG(err.empty() && told && sends, "views: '%s', %u told, %u bytes", err.c_str(), (unsigned) p.told.size(),
              (unsigned) p.bytes.size());
  }
  {  // several channels in one glide, each with its value, and a pivot and a depth as glides of theirs
    std::string err;
    overlay::Program p =
        overlay::compile("glide opacity 40 size_x -50 turn 9 over 300 ease out\npivot 99.5, 0\ndepth 7", err);
    const std::vector<uint8_t> &g = p.told.empty() ? std::vector<uint8_t>() : p.told[0].m;
    const std::vector<uint8_t> each = {OVERLAY_OPACITY, 40, 0, OVERLAY_SIZE_X, 0xCE, 0xFF, OVERLAY_TURN, 9, 0};
    bool glide = p.told.size() == 3 && p.told[0].key == K_OVERLAY_GLIDE &&
                 g.size() == OVERLAY_GLIDE_HEAD + each.size() &&
                 std::equal(each.begin(), each.end(), g.begin() + OVERLAY_GLIDE_HEAD) &&
                 plink_get16(&g[OVERLAY_GLIDE_MS]) == 300 && g[OVERLAY_GLIDE_EASE] == OVERLAY_EASE_OUT;
    const std::vector<uint8_t> at = {OVERLAY_PIVOT_X, 0xE3, 0x03, OVERLAY_PIVOT_Y, 0, 0}, deep = {OVERLAY_DEPTH, 7, 0};
    bool pivot = glide && p.told[1].key == K_OVERLAY_GLIDE && p.told[2].key == K_OVERLAY_GLIDE &&
                 std::equal(at.begin(), at.end(), p.told[1].m.begin() + OVERLAY_GLIDE_HEAD) &&
                 std::equal(deep.begin(), deep.end(), p.told[2].m.begin() + OVERLAY_GLIDE_HEAD);
    std::string twice, out, soft_out;
    overlay::compile("glide size 50 size 60", twice);
    overlay::compile("pivot 50, 101", out);
    overlay::compile("soft 0, 0, 1025, 0", soft_out);
    // a blend told as a view, soft edges as a step
    std::string e2;
    overlay::Program q = overlay::compile("blend screen\nsoft 2, 0, 10, 4\nsoft none", e2);
    std::vector<uint8_t> steps = {OVERLAY_DO_SOFT, 2, 0, 0, 0, 10, 0, 4, 0, OVERLAY_DO_SOFT, 0, 0, 0, 0, 0, 0, 0, 0};
    pivot &= e2.empty() && q.told.size() == 1 && q.told[0].key == K_OVERLAY_VIEW &&
             q.told[0].m[OVERLAY_VIEW_MASK] == OVERLAY_VIEW_BLEND &&
             q.told[0].m[OVERLAY_VIEW_FLAGS] == OVERLAY_BLEND_SCREEN && q.bytes == steps &&
             soft_out == "line 1: soft takes 0 to 1024 px for each edge";
    CHECK_MSG(err.empty() && pivot && twice == "line 1: size twice" &&
                  out == "line 1: pivot takes 0 to 100 percent of its box",
              "a glide of several and a pivot: '%s', '%s', '%s'", err.c_str(), twice.c_str(), out.c_str());
  }
  std::string long_one;
  for (int k = 0; k < OVERLAY_SCRIPT_MAX / 4; k++)
    long_one += "move " + std::to_string(k) + ", 0\n";
  size_t warned = log_all.size();
  panel_script_animation.execute("kite", 1, long_one);
  CHECK_MSG(logged("bytes, the panel takes", warned), "one too long for the panel isn't warned about");
  run(5);
  CHECK_MSG(overlay::find("kite")->copies[1].script.bytes == was && same_pool(), "the script copy 1 ran is gone");
  play_ms(300);
  panel_script_animation.execute("kite", 1, "");  // it stops, where its last move takes it
  play_ms(1000);
  std::string at = places();
  play_ms(2000);
  CHECK_MSG(places() == at && same_pool(), "stopped at %s, then %s", at.c_str(), places().c_str());
  // a long one: where its goto and wait until go on, the copy it looks at and its end's tag past its first 256 bytes
  std::string longer = "wait 1\n";
  for (int k = 0; k < 30; k++)
    longer += "move 40, " + std::to_string(60 + k % 2) + "\n";
  longer += "goto far\nend\nfar:\nwait until x(kite.2) > -1000\nmove 120, 60\nwait 1000\nend";
  panel_script_animation.execute("kite", 1, longer);
  size_t bytes = overlay::find("kite")->copies[1].script.bytes.size();
  play_ms(1000);  // its bytes take a few messages
  bool there = kite_at(1, 120, 60);
  play_ms(1500);
  run(5);
  CHECK_MSG(bytes > 300 && there && !overlay::find("kite")->copies[1].used && same_pool(),
            "%u bytes of script: at 120,60 %d, then %s", (unsigned) bytes, there, places().c_str());
  overlay::clear("");
  run(3);
}

// ---- the weather picture, packages/panel/met.yaml and packages/code/met/met.h
static void met_requests() {
  step("MET Norway's forecast, asked for and backed off");
  flexit_panel_time.t.timestamp = 1792409400;  // 2026-10-19 11:30 UTC, half an hour into the first forecast hour
  tz_offset_s = 7200;                          // Oslo in summer time
  met::Forecasts &fc = met::forecasts;
  fetch::Schedule &sc = fc.schedule;
  sc.not_before = 0;
  fetch::tick(true);  // the panel's picture being made
  CHECK_MSG(!fc.asking && tasks.empty(), "asked MET Norway while the panel's picture was made");
  // panel.yaml's interval asks once it's due, its request waits in its task
  yaml_every(1000);
  CHECK_MSG(!strcmp(fc.url, "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=59.9139&lon=10.7522") &&
                fc.timeout_ms == 15000 && fc.modified.empty() && fc.asking && tasks.size() == 1,
            "first request: %s, If-Modified-Since '%s', %d requests", fc.url, fc.modified.c_str(), (int) tasks.size());
  network::up = false;
  CHECK_MSG(!sc.due(), "asks MET Norway while offline");
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
  CHECK_MSG(same && met::ground_of("moon") == met::G_HILLS && logged("No weather option called 'moon'"),
            "met.yaml's options aren't met_scene.h's names");
  web.down = true;  // right after boot the network may not be up: a quick retry, no back-off
  run_task();
  CHECK_MSG(sc.backoff_s == 0 && sc.early_tries == 1 && !sc.due() && met_status == "Couldn't reach MET Norway",
            "backed off before MET Norway ever answered: '%s'", met_status.c_str());
  sc.not_before = 0;
  yaml_every(1000);
  CHECK_MSG(fc.asking && !tasks.empty(), "the early retry didn't ask");
  run_task();
  CHECK_MSG(sc.early_tries == 2, "the retry's failure isn't counted as early");
  for (int i = 0; i < 2; i++) {
    sc.not_before = 0;
    yaml_every(1000);
    run_task();
  }
  CHECK_MSG(sc.backoff_s == 600 && !sc.due(), "kept trying without backing off");
  // the request, in a task of its own: no answer, then the forecast in 512-byte reads, then without the RAM
  auto ask = [] {
    yaml_every(1000);  // panel.yaml's
    if (!tasks.empty())
      run_task();
  };
  sc.not_before = 0;
  ask();
  CHECK_MSG(sc.backoff_s == 1200 && met_status == "Couldn't reach MET Norway", "no answer: %s, back-off %u",
            met_status.c_str(), (unsigned) sc.backoff_s);
  web.down = false;
  web.body = sample("met_sample.json");
  web.last_modified = "Mon, 19 Oct 2026 11:34:38 GMT";
  web.expires = "Mon, 19 Oct 2026 12:00:00 GMT";
  sc.not_before = 0;
  yaml_every(1000);
  yaml_every(1000);  // still asking, it waits for that one
  fetch::loop();     // nothing yet
  CHECK_MSG(tasks.size() == 1 && fc.asking && !met::weather.ok, "%u requests at once", (unsigned) tasks.size());
  run_task();
  CHECK_MSG(!fc.asking && met::weather.ok, "the answer wasn't taken from the request's task");
  CHECK_MSG(web.sent.empty(), "If-Modified-Since sent before any Last-Modified");
  CHECK_MSG(sc.not_before == 1792411200 && !sc.due(), "asks again before Expires: %u", (unsigned) sc.not_before);
  sc.not_before = 0;
  ram_block = fetch::STACK - 1;  // for its task's stack
  yaml_every(1000);
  CHECK_MSG(met_status == "No RAM for the forecast" && sc.not_before == 1792410000 && !fc.asking && tasks.empty(),
            "without the RAM: %s, next at %u", met_status.c_str(), (unsigned) sc.not_before);
  ram_block = 100 * 1024;
  sc.not_before = 0;
  ask();
  CHECK_MSG(web.sent.size() == 1 && web.sent[0].name == "If-Modified-Since" &&
                web.sent[0].value == "Mon, 19 Oct 2026 11:34:38 GMT",
            "If-Modified-Since not sent with the Last-Modified we have");
  CHECK_MSG(met::weather.ok && !strcmp(met::weather.symbol, "partlycloudy_day") &&
                fabsf(met::weather.temp - 6.6f) < 0.01f && fabsf(met::weather.wind - 1.6f) < 0.01f &&
                fabsf(met::weather.gust - 3.3f) < 0.01f,
            "forecast read as %s, %.1f, wind %.1f gusting %.1f", met::weather.symbol, met::weather.temp,
            met::weather.wind, met::weather.gust);
  CHECK_MSG(fc.modified == "Mon, 19 Oct 2026 11:34:38 GMT", "Last-Modified not kept: %s", fc.modified.c_str());
  CHECK_MSG(met::weather.hours == 12 && met::weather.hour[2].time == met::weather.hour[0].time + 7200,
            "forecast hours: %d", met::weather.hours);
  CHECK_MSG(met_status.find("partlycloudy_day") == 0, "status: %s", met_status.c_str());
  web.status = 304;  // not modified, through the task too
  sc.not_before = 0;
  ask();
  CHECK_MSG(met::weather.ok && met_status.find("partlycloudy_day") == 0 && sc.not_before == 1792411200,
            "a 304: %s, next at %u", met_status.c_str(), (unsigned) sc.not_before);
  // a server error keeps the forecast and asks again a minute later, then two, and Retry-After makes it wait longer
  web.status = 503;
  sc.not_before = 0;
  ask();
  uint32_t t0 = (uint32_t) flexit_panel_time.t.timestamp;
  CHECK_MSG(met::weather.ok && met_status == "Kept the last forecast (HTTP 503)" && sc.not_before == t0 + 60,
            "a 503: '%s', next in %d s", met_status.c_str(), (int) (sc.not_before - t0));
  flexit_panel_time.t.timestamp += 60;
  yaml_every(1000);
  CHECK_MSG(fc.asking && !tasks.empty(), "not asked again a minute after a 503");
  web.status = 502;
  web.retry_after = "600";
  run_task();
  yaml_every(1000);
  t0 = (uint32_t) flexit_panel_time.t.timestamp;
  CHECK_MSG(met::weather.ok && sc.backoff_s == 120 && sc.not_before == t0 + 600,
            "a 502 with Retry-After 600: back-off %u, next in %d s", (unsigned) sc.backoff_s,
            (int) (sc.not_before - t0));
  web.retry_after.clear();
  web.status = 200;
  sc.not_before = 0;
  ask();
  CHECK_MSG(met::weather.ok && sc.backoff_s == 0, "the forecast after the errors: '%s'", met_status.c_str());
  flexit_panel_time.t.timestamp -= 60;
  // a 301 is followed, three times at most
  web.location = std::string(fc.url) + "&moved=1";
  web.moves = 1;
  sc.not_before = 0;
  ask();
  CHECK_MSG(web.url == web.location && met::weather.ok && sc.backoff_s == 0, "after a 301: asked for %s, %s",
            web.url.c_str(), met_status.c_str());
  web.moves = 9;
  sc.not_before = 0;
  ask();
  CHECK_MSG(web.moves == 5 && met_status == "Kept the last forecast (HTTP 301)", "moved on and on: %d requests, '%s'",
            9 - web.moves, met_status.c_str());
  web.moves = 0;
  // A cut-off answer counts as none, wherever it's cut: the forecast and its Last-Modified stay, and it's asked
  // again in minutes
  web.last_modified = "Tue, 20 Oct 2026 08:00:00 GMT";
  for (size_t cut : {6000, 300})
    for (int with : {0, -1}) {  // the connection closed, or failed
      web.cut = cut;
      web.cut_with = with;
      sc.backoff_s = 0;
      sc.not_before = 0;
      ask();
      CHECK_MSG(met_status == "Couldn't reach MET Norway" && sc.backoff_s == 600 && met::weather.hours == 12 &&
                    fc.modified == "Mon, 19 Oct 2026 11:34:38 GMT",
                "cut off after %u bytes, read() %d: '%s', back-off %u, %d hours, Last-Modified '%s'", (unsigned) cut,
                with, met_status.c_str(), (unsigned) sc.backoff_s, met::weather.hours, fc.modified.c_str());
    }
  // but the reader can have all it needs before the cut
  std::string b = sample("met_sample.json");
  size_t e = b.find("{\"time\""), f = b.find("{\"time\"", e + 1);
  web.body = b.substr(0, f);
  for (int i = 0; i < 30; i++)
    web.body += b.substr(e, f - e);
  web.cut = web.body.size() - 100;
  sc.not_before = 0;
  ask();
  CHECK_MSG(met::weather.hours == met::HOURS && fc.modified == "Tue, 20 Oct 2026 08:00:00 GMT",
            "cut off once it had %d hours: %d taken, '%s'", met::HOURS, met::weather.hours, met_status.c_str());
  web.cut = SIZE_MAX;
  web.body = b;
  web.last_modified = "Mon, 19 Oct 2026 11:34:38 GMT";
  sc.not_before = 0;
  ask();
  CHECK_MSG(met::weather.hours == 12 && sc.backoff_s == 0, "the forecast not back: %d hours", met::weather.hours);
}

// The reader with the answer cut anywhere, more hours than it needs, and a value too long for it
static void met_reading() {
  step("MET Norway's answer read in pieces");
  std::string b = sample("met_sample.json");
  bool stopped;
  met::Weather w1 = read_met(b, 1, &stopped);
  CHECK_MSG(!stopped && w1.hours == met::weather.hours && w1.temp == met::weather.temp &&
                w1.cloud == met::weather.cloud && !strcmp(w1.symbol, met::weather.symbol) &&
                w1.hour[5].time == met::weather.hour[5].time,
            "a byte at a time: %d hours, %s", w1.hours, w1.symbol);
  size_t e = b.find("{\"time\""), f = b.find("{\"time\"", e + 1);
  std::string many = b.substr(0, f);
  for (int i = 0; i < 30; i++)
    many += b.substr(e, f - e);
  met::Weather w2 = read_met(many, 512, &stopped);
  CHECK_MSG(stopped && w2.ok && w2.hours == met::HOURS, "30 hours: %d read, stopped %d", w2.hours, stopped);
  // a value longer than the reader keeps is cut, and the hour it was in, without a time, skipped
  std::string big = b.substr(0, f) + "{\"time\":\"" + std::string(5000, 'x') + "\"}," + b.substr(e, f - e);
  met::Weather w3 = read_met(big, 512, &stopped);
  CHECK_MSG(!stopped && w3.ok && w3.hours == 2, "a value over 5000 bytes: %d hours, stopped %d", w3.hours, stopped);
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
  CHECK_MSG(!strcmp(w4.symbol, "lightssleetshowersandthunder_polartwilight") && thunder == partly && partly > 6 &&
                n.thunder && n.snow && n.rain,
            "MET's longest symbols: '%s', %d of %d hours with thunder, thunder %d snow %d rain %d", w4.symbol, thunder,
            partly, n.thunder, n.snow, n.rain);
  met::Weather w5 = symbols("lightssleetshowersandthunder_polartwilight_and_then_some", "fog");
  CHECK_MSG(strlen(w5.symbol) == met::SYMBOL - 1 && !strncmp(w5.symbol, "lightssleetshowersandthunder", 28) &&
                w5.hour[0].icon == met::I_THUNDER,
            "a symbol too long: '%s', icon %d", w5.symbol, w5.hour[0].icon);
}

static void met_picture() {
  step("the weather picture");
  pic::show("weather");
  CHECK_MSG(picture("weather", 6000) >= 0, "weather picture not shown");
  int pw, ph;
  CHECK_MSG(pn_pic(&pw, &ph) == 2 && pw == 320 && ph == 240, "weather picture %dx%d", pw, ph);
  CHECK_MSG(!met::drawing && met::land, "the scene kept its RAM once the picture was made, or not the land");
  CHECK_MSG(!memcmp(pic::S.shown.pal, met::pal, sizeof met::pal), "the picture isn't in the scene's own colours");
  run(10);
  PanelItem wx = pitem("weather");
  CHECK_MSG(
      wx.value == 66 && wx.icon == ICN_SUN && !strcmp(wx.label, "Fair") && !(wx.flags & (ITEMF_VIEW | ITEMF_MENU)),
      "the weather now for a tile: %d, icon %d, '%s', flags 0x%02X (want 66, sun, Fair)", wx.value, wx.icon, wx.label,
      wx.flags);
  pn_shot(outdir, "esp_weather");
  size_t changed = log_all.size();
  uint32_t was_pal[16];
  memcpy(was_pal, met::pal, sizeof was_pal);
  flexit_panel_time.t.timestamp += 600;  // ten minutes later, the sun and the clouds have moved
  met::redraw();
  CHECK_MSG(picture("weather, ten minutes on", 3000) >= 0, "weather picture not shown again");
  CHECK_MSG(logged("Picture weather:", changed) && logged("pieces", changed), "not sent as changed rows");
  for (int k = 0; k < 16; k++)  // the colours both have kept their places
    for (int j = 0; j < 16; j++)
      CHECK_MSG(
          met::pal[j] != was_pal[k] || met::pal[k] == was_pal[k] || std::count(was_pal, was_pal + 16, was_pal[k]) > 1,
          "colour %06X moved from %d to %d", (unsigned) was_pal[k], k, j);
  panel_weather_forecast.turn_on();
  CHECK_MSG(picture("weather with the forecast", 3000) >= 0, "weather picture with the forecast not shown");
  std::string fk = marks::overlay_of("weather");
  CHECK_MSG(run_until(
                [&] {
                  const overlay::Overlay *a = overlay::find(fk);
                  return a && a->state == overlay::SENT && settled();
                },
                600) >= 0,
            "the forecast not over the weather");
  play_ms(400);  // faded in
  pn_shot(outdir, "esp_weather_forecast");
  panel_weather_forecast.turn_off();
  ram_block = sizeof(met::Drawing) - 1;  // the scene's biggest piece
  met::redraw();
  run(20);
  CHECK_MSG(!met::drawing && panel_picture_status.state == "weather: no RAM to draw it",
            "without the RAM to draw it: '%s'", panel_picture_status.state.c_str());
  ram_block = 100 * 1024;
  CHECK_MSG(picture("weather, with the RAM back", 3000) >= 0 && !met::drawing, "not drawn with the RAM back");
  // PSRAM takes the scene only when internal RAM is short of it, here by a byte, the land kept. Too short to make
  // the picture then, which takes more
  ram_free = sizeof(met::Drawing) + sizeof(met::Stamps) + 8191;
  psram_free = psram_block = 4096 * 1024;
  bool ps = met::new_drawing() && in_psram.count(met::drawing) && in_psram.count(met::drawing->scene.stamps);
  met::free_drawing();
  ram_free = 200 * 1024;
  bool in = met::new_drawing() && !in_psram.count(met::drawing) && !in_psram.count(met::drawing->scene.stamps);
  met::free_drawing();
  psram_free = psram_block = 0;
  CHECK_MSG(ps && in && in_psram.empty(), "the scene in PSRAM: %d with internal RAM short, %d with it to spare", ps,
            !in);
}

// A server error keeps the forecast and asks again a minute later, then twice as long up to half an hour. Refusals
// back off an hour, then twice as long, and an answer starts over. No answer at all is ten minutes and twice that up
// to an hour, after a refusal still an hour. Retry-After can only make it longer.
static void met_answers() {
  step("MET Norway's answers");
  const met::Weather sample = met::weather;                                    // the forecast as read from the sample
  auto fetched = [](int status, const met::Weather &w, const char *expires) {  // an answer from the request
    auto *r = new met::Ask();
    r->status = status;
    r->f.w = w;
    r->expires = expires;
    met::forecasts.answered(r);
  };
  auto unreachable = [&] { fetched(0, {}, ""); };
  fetch::Schedule &sc = met::forecasts.schedule;
  fetched(304, {}, "Mon, 19 Oct 2026 13:00:00 GMT");  // not modified: nothing changes
  CHECK_MSG(met::weather.ok && met::forecasts.modified == "Mon, 19 Oct 2026 11:34:38 GMT", "304 changed the forecast");
  CHECK_MSG(sc.not_before == 1792414800, "the 304's Expires not kept");
  uint32_t now = (uint32_t) flexit_panel_time.t.timestamp;
  for (uint32_t want : {60, 120, 240, 480, 960, 1800, 1800}) {
    fetched(want == 60 ? 500 : 503, {}, "");
    CHECK_MSG(
        met::weather.ok && sc.not_before == now + want &&
            met_status == (want == 60 ? "Kept the last forecast (HTTP 500)" : "Kept the last forecast (HTTP 503)"),
        "a server error: '%s', next in %d s, not %u", met_status.c_str(), (int) (sc.not_before - now), (unsigned) want);
  }
  fetched(429, {}, "");
  CHECK_MSG(sc.not_before == now + 3600, "a 429 after server errors waited %d s", (int) (sc.not_before - now));
  fetched(429, {}, "");
  CHECK_MSG(sc.not_before == now + 7200 && !sc.due(), "back-off not doubled after a 429");
  CHECK_MSG(fetch::retry_after_s("120", now) == 120 &&
                fetch::retry_after_s("Mon, 19 Oct 2026 13:00:00 GMT", 1792414500) == 300 &&
                fetch::retry_after_s("Mon, 19 Oct 2026 13:00:00 GMT", 1792415000) == 0 &&
                fetch::retry_after_s("", now) == 0,
            "Retry-After read wrong");
  unreachable();
  CHECK_MSG(sc.backoff_s == 3600, "no answer after a 429 waited %u s", (unsigned) sc.backoff_s);
  flexit_panel_time.t.timestamp += 7200;
  CHECK_MSG(sc.due(), "not due after the back-off");
  fetched(304, {}, "");  // without Expires, an hour
  CHECK_MSG(sc.backoff_s == 0 && sc.not_before == now + 7200 + 3600, "an answer didn't reset the back-off");
  for (uint32_t want : {600, 1200, 2400, 3600, 3600}) {
    unreachable();  // a short drop costs minutes, not hours
    CHECK_MSG(sc.backoff_s == want && met_status == "Couldn't reach MET Norway", "no answer backed off %u s, not %u",
              (unsigned) sc.backoff_s, (unsigned) want);
  }
  fetched(403, {}, "");
  CHECK_MSG(sc.backoff_s == 7200, "a refusal after no answers backed off %u s", (unsigned) sc.backoff_s);
  met::weather.ok = false;
  fetched(203, sample, "");  // a deprecated product, its data is still good
  CHECK_MSG(met::weather.ok && sc.backoff_s == 0, "a 203 not taken: %s", met_status.c_str());
  static const struct {
    const char *date;
    uint32_t t;
  } DATES[] = {{"Fri, 31 Dec 1999 23:59:59 GMT", 946684799}, {"soon", 0}, {"Mon, 19 Foo 2026 11:34:38 GMT", 0}};
  for (auto d : DATES)
    CHECK_MSG(fetch::http_date(d.date) == d.t, "HTTP date '%s' read as %u, not %u", d.date,
              (unsigned) fetch::http_date(d.date), (unsigned) d.t);
}

// Through 304s and refusals the tile follows the forecast's hours, and the forecast is dropped once it runs out
static void met_forecast_hours() {
  step("the weather tile through the forecast's hours");
  const met::Weather was = met::weather;
  auto &tile = panel::items[met::now_item];
  time_t then = flexit_panel_time.t.timestamp;
  flexit_panel_time.t.timestamp = met::weather.hour[4].time + 1200;
  met::redraw();
  CHECK_MSG(tile.value_ == lroundf(met::weather.hour[4].temp * 10) && met::weather.ok,
            "4 hours on the tile shows %d, the hour has %.1f", (int) tile.value_, met::weather.hour[4].temp);
  flexit_panel_time.t.timestamp = met::ends(met::weather);
  met::redraw();
  CHECK_MSG(!met::weather.ok && tile.value_ == ITEM_UNKNOWN && met::forecasts.modified.empty() &&
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
  CHECK_MSG(met::draw(), "the scene not drawn");
  static SceneBuf want;
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
  CHECK_MSG(same, "UTC+13 drawn for the day before");
  flexit_panel_time.t = was;
  tz_offset_s = 7200;
  met::redraw();
  CHECK_MSG(picture("weather, back in Oslo", 3000) >= 0 && !met::drawing, "weather picture not shown again");
  CHECK_MSG(!logged("refused a piece"), "the panel refused a piece");
  tz_offset_s = 0;
}

#include "live_sky.inc"
#include "live_land.inc"
#include "live_town.inc"
#include "live_days.inc"
#include "live_rare.inc"
#include "live_texts.inc"
#include "live_cast.inc"

static void weather_scene() {
  step("the weather scene (packages/code/met/met_scene.h)");
  static SceneBuf sb;
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
  // never more than 16 colours, and a flag day on the coast's stamps keep theirs
  draw(wx("fair_day", 25, 14, 5, 200), utc(2026, 5, 17, 11), {met::G_COAST});
  bool in_pal = true, under_16 = true;
  for (int i = 0; i < s.nstamps; i++)
    in_pal = in_pal && s.pal[s.stamps->at[i].ix] == s.stamps->at[i].c;
  for (int y = 0; y < met::H; y++) {
    uint8_t r[met::W];
    met::row(s, y, r);
    for (int x = 0; x < met::W; x++)
      under_16 = under_16 && r[x] < 16;
  }
  CHECK_MSG(s.folded && in_pal && under_16, "a flag day on the coast: %d folded, stamps' colours kept %d", s.folded,
            in_pal);
  // the wind blows things downwind. The picture faces south, so a wind from the east goes to the right.
  for (float from : {90.0f, 270.0f}) {
    met::Weather w = wx("lightrain", 90, 5, 8, from);
    w.rain = 1;
    met::Options o{};
    o.heater_pct = 60;
    draw(w, utc(2026, 5, 17, 10), o);
    int dir = from < 180 ? 1 : -1, flag = 0,
        smoke = s.stamps->at[0].x + s.stamps->at[0].w / 2 - (s.cabin_x + met::CHIMNEY_X);
    for (int i = 0; i < s.nstamps; i++)
      if (s.stamps->at[i].tag == met::L_FLAG)
        flag = s.stamps->at[i].x - s.pole_x;
    CHECK_MSG(s.slant * dir > 0 && flag * dir > 0 && smoke * dir > 0, "wind from %.0f: rain %d, flag %d, smoke %d",
              from, s.slant, flag, smoke);
  }
  // Christmas is from Advent's first Sunday, or 1 December, to 13 January: the julenek stands by the cabin, the
  // spruce has its lights after dark and the eaves theirs from dusk to dawn. A snowman comes once 8 cm of snow lie,
  // and a snow lantern is lit by the door on a snowy evening.
  met::Options deep{};
  deep.snow_cm = 12;
  met::Weather frost = wx("clearsky_day", 0, -5, 2, 200);
  draw(frost, utc(2026, 12, 20, 11), deep);
  bool nek = s.nek.x >= 0 && !s.yule_lights && !s.nlights, man = s.snowman.x >= 0 && s.lantern.x < 0;
  draw(frost, utc(2027, 1, 13, 3), deep);
  bool night = s.nek.x >= 0 && s.yule_lights && s.nlights && s.lantern.x < 0;  // asleep, the lights still on
  draw(frost, utc(2027, 1, 13, 18), deep);
  bool evening = s.yule_lights && s.nlights && s.lantern.x >= 0;
  deep.snow_cm = 3;
  draw(frost, utc(2027, 1, 14, 18), deep);
  CHECK_MSG(nek && man && night && evening && s.nek.x < 0 && !s.yule_lights && !s.nlights && s.snowman.x < 0 &&
                s.lantern.x >= 0,
            "the julenek by day %d, the snowman %d, the lights at night %d, the evening's and the lantern %d, and "
            "after Christmas: nek %d, lights %d, snowman in 3 cm %d",
            nek, man, night, evening, s.nek.x, s.yule_lights, s.snowman.x);
  // in town they stand in the park, all below the road, and the old houses have the lights
  deep.snow_cm = 12;
  deep.ground = met::G_CITY;
  draw(frost, utc(2026, 12, 20, 17), deep);
  int park = std::min({s.nek.y, s.snowman.y, s.lantern.y});
  CHECK_MSG(
      s.nek.x >= 0 && s.snowman.x >= 0 && s.lantern.x >= 0 && park > met::ROAD_NEAR && s.yule_lights,
      "Christmas in town: the julenek at %d, the snowman at %d, the lantern at %d, the highest at row %d, lights %d",
      s.nek.x, s.snowman.x, s.lantern.x, park, s.yule_lights);
  deep.ground = met::G_HILLS;
  // Advent's first Sunday is 29 November in 2026 and 3 December in 2028, when 1 December starts it
  for (auto f : {std::make_tuple(utc(2026, 11, 28, 12), false), std::make_tuple(utc(2026, 11, 29, 12), true),
                 std::make_tuple(utc(2028, 11, 30, 12), false), std::make_tuple(utc(2028, 12, 1, 12), true)}) {
    met::Now n(frost, deep, OSLO_LAT, OSLO_LON, std::get<0>(f), 3600);
    CHECK_MSG(n.yule == std::get<1>(f), "%d.%d.%d: Christmas on %d", n.day, n.month, n.year, n.yule);
  }
  // the pennant stands straight out in a fresh breeze and hangs in a calm
  int tall[2] = {};
  for (int k = 0; k < 2; k++) {
    draw(wx("clearsky_day", 0, 12, k ? 8.0f : 0.0f, 270), utc(2026, 7, 14, 10), {});
    for (int i = 0; i < s.nstamps; i++)
      if (s.stamps->at[i].tag == met::L_FLAG)
        tall[k] = s.stamps->at[i].h;
  }
  CHECK_MSG(tall[0] > 20 && tall[1] == met::PENNANT_H, "the pennant's rows in a calm %d, in a breeze %d", tall[0],
            tall[1]);
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
  CHECK_MSG(trees == tree_xs(), "the coast's trees moved with the weather");
  // the flag goes up at 9 in December even before the sun is, and down at sunset. Up north it's 10 to 15.
  met::Options o{};
  met::Weather clear = wx("clearsky_day", 0, -2, 2, 200);
  for (auto f : {std::make_tuple(OSLO_LAT, OSLO_LON, utc(2026, 12, 25, 8, 5), 3600, true),
                 std::make_tuple(OSLO_LAT, OSLO_LON, utc(2026, 12, 25, 15, 30), 3600, false),
                 std::make_tuple(69.65, 18.96, utc(2027, 1, 21, 9, 30), 3600, true),
                 std::make_tuple(69.65, 18.96, utc(2027, 1, 21, 14, 30), 3600, false)}) {
    met::Now n(clear, o, std::get<0>(f), std::get<1>(f), std::get<2>(f), std::get<3>(f));
    CHECK_MSG(n.flag_up == std::get<4>(f), "the flag at %.2f N at %02d local with the sun at %.1f: up %d",
              std::get<0>(f), n.hour, n.sun.el, n.flag_up);
  }
  // spring comes later up north: the grass is green and the birches in leaf in Oslo in mid May, not yet in Tromso,
  // and nowhere on 1 April
  for (auto f :
       {std::make_tuple(OSLO_LAT, utc(2026, 4, 1, 10), 1, false),
        std::make_tuple(OSLO_LAT, utc(2026, 5, 15, 10), 2, true),
        std::make_tuple(69.65, utc(2026, 5, 15, 10), 2, false), std::make_tuple(69.65, utc(2026, 5, 5, 10), 1, false),
        std::make_tuple(OSLO_LAT, utc(2026, 7, 1, 10), 3, true),
        std::make_tuple(OSLO_LAT, utc(2026, 10, 1, 10), 4, true),
        std::make_tuple(OSLO_LAT, utc(2026, 11, 1, 10), 4, false),
        std::make_tuple(69.65, utc(2026, 11, 1, 10), 1, false)}) {
    met::Now n(wx("fair_day", 20, 9, 2, 200), o, std::get<0>(f), OSLO_LON, std::get<1>(f), 3600);
    CHECK_MSG(n.season == std::get<2>(f) && n.leaves == std::get<3>(f), "%.0f N on %d.%d: season %d, in leaf %d",
              std::get<0>(f), n.day, n.month, n.season, n.leaves);
  }
  // snow lies by what has fallen and melted, not by the temperature now: a cold clear night in October leaves the
  // ground bare, snow whitens it once a centimetre has come, a mild hour leaves it and a mild week takes it
  met::SnowCover cover;
  auto lies = [&](const met::Weather &v, uint32_t t) {
    cover.step(v, t);
    met::Options with{};
    with.snow_cm = cover.cm;
    return met::Now(v, with, OSLO_LAT, OSLO_LON, t, 3600).white;
  };
  met::Weather snowfall = wx("snow", 100, -2, 2, 200), mild = wx("cloudy", 80, 2, 2, 200);
  snowfall.rain = 1;
  uint32_t oct = utc(2026, 10, 20, 2);
  bool bare = !lies(wx("clearsky_night", 0, -4, 1, 200), oct), first = lies(snowfall, oct + 1800),
       white = lies(snowfall, oct + 3 * 3600), stays = lies(mild, oct + 5 * 3600);
  for (int h = 6; h < 60; h += 5)
    lies(wx("cloudy", 80, 5, 2, 200), oct + (uint32_t) h * 3600);
  CHECK_MSG(bare && !first && white && stays && cover.cm == 0,
            "snow on the ground: bare at -4 %d, white at once %d, after 3 h of snow %d, through a mild hour %d, "
            "%.1f cm after two mild days",
            bare, first, white, stays, (double) cover.cm);
  // with no memory it goes by the weather and the month: white at +1 in January, bare at -4 in October
  CHECK_MSG(
      met::snow_likely(wx("cloudy", 80, 1, 2, 200), 1) && !met::snow_likely(wx("clearsky_night", 0, -4, 2, 200), 10),
      "snow by the weather alone");
  // the forecast's hours are labelled in the local time of each, across the end of summer time
  met::Weather w = wx("cloudy", 50, 8, 3, 200);
  w.time = utc(2026, 10, 25);
  for (int i = 0; i < met::HOURS; i++)
    w.hour[w.hours++] = {w.time + i * 3600u, 8, met::I_CLOUD};
  int at[4], n = met::hour_marks(w, w.time + 1800, oslo_offset, at);
  CHECK_MSG(n == 4 && at[0] == 2 && met::local_hour(w.hour[at[0]].time, oslo_offset) == 3 && at[1] == 5,
            "forecast marks after the change: %d of them, the first hour %d", n, at[0]);
  // the lighthouse's lamp is lit at night whatever the redraw, the animations' glow and beam going round it
  int lit = 0;
  for (uint32_t frame : {0, 1}) {
    met::Options c{met::G_COAST};
    c.frame = frame;
    draw(wx("clearsky_night", 0, 8, 2, 200), utc(2026, 9, 10, 21, 30), c);
    for (int i = 0; i < s.nstamps; i++)
      lit += s.stamps->at[i].x == met::LH_X - 1 && s.stamps->at[i].c == met::WARM_LIGHT;
  }
  CHECK_MSG(lit == 2, "the lamp lit in %d of 2 redraws", lit);
  // the moon wraps around the edges like the sun
  draw(wx("clearsky_night", 0, 2, 2, 200), utc(2026, 10, 26, 21), {});
  CHECK_MSG(s.moon, "no moon to wrap");
  s.moon_x = 2;
  s.moon_phase = 0.5f;
  uint8_t r[met::W];
  met::row(s, s.moon_y, r);
  CHECK_MSG(r[met::W - 4] == s.ix[met::C_MOON], "the moon isn't drawn on the other edge");
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
  CHECK_MSG(smallest >= 4, "a cloud lobe %d wide", smallest);
  // the mountains stand in the middle and low clouds: wherever one of those is over the ridge or a peak it's the
  // cloud that shows, so the rain falls out of clouds that are there
  met::Weather pour = wx("rain", 90, 8, 3, 200);
  pour.rain = 2;
  draw(pour, utc(2026, 10, 20, 12), {met::G_MOUNTAINS});
  int shroud = 0, peaks = 0;
  for (int i = 0; i < s.nclouds; i++) {
    const met::Cloud &c = s.cloud[i];
    for (int y = c.top; y <= c.base && !met::high_cloud(s, i); y++) {
      met::row(s, y, r);
      for (int j = 0; j < c.n; j++) {
        int h = met::half(y - c.e[j].y, c.e[j].rx, c.e[j].ry), cx = met::cloud_x(s, i) + c.e[j].x;
        for (int x = std::max(0, cx - h); x <= std::min(met::W - 1, cx + h); x++) {
          if (y < std::min(s.land->ridge[x], s.land->hill[0][x]))
            continue;
          bool cloud = false;
          for (int l = 1; l < 3; l++)
            cloud |= r[x] == s.ix[met::C_LIT + l] || r[x] == s.ix[met::C_SHADE + l];
          (cloud ? shroud : peaks)++;
        }
      }
    }
  }
  CHECK_MSG(shroud > 2000 && !peaks, "rain in the mountains: %d px of cloud over them, %d px of them over a cloud",
            shroud, peaks);
  // the Christmas tree has lights on both sides
  draw(wx("cloudy", 95, -3, 2, 200), utc(2026, 12, 24, 16), {});
  int left = 0, right = 0, tree = -1;
  for (int i = 0; i < s.ntrees; i++)
    if (s.tree[i].layer == 2 && s.tree[i].kind == 0 &&
        (tree < 0 || abs(s.tree[i].x - s.home_x) < abs(s.tree[tree].x - s.home_x)))
      tree = i;
  for (int i = 0; i < s.nstamps; i++)
    if (s.stamps->at[i].w == 2 && s.stamps->at[i].h == 2)
      (s.stamps->at[i].x < s.tree[tree].x ? left : right)++;
  printf("  Christmas lights: %d on the left, %d on the right\n", left, right);
  CHECK_MSG(left >= 5 && right >= 5, "Christmas lights: %d on the left, %d on the right", left, right);
}

static void picture_cleared_mid_anim() {
  step("clearing the picture while an animation is on its way");
  draw_dusk(panel_picture_dusk, 200, false);
  panel_picture.control(1);
  CHECK_MSG(picture("dusk before the animation", 6000) >= 0, "dusk not shown");
  panel_anim_noise.turn_on();  // big enough to take a while
  int r = run_until(
      [] { return overlay::W.t.head && overlay::W.t.pos > 0 && overlay::W.t.pos < overlay::W.a->data.size(); }, 3000);
  CHECK_MSG(r >= 0, "the animation never went out");
  panel_picture.control(0);
  r = run_until(overlay_shown, 3000);
  run(3);
  CHECK_MSG(r >= 0 && overlay_ready() && panel_pic() == 0,
            "after clearing the picture on the animation's way: '%s', animation %d, picture %d",
            panel_animation_status.state.c_str(), overlay_ready(), panel_pic());
  panel_picture.control(1);
  CHECK_MSG(picture("dusk again", 3000) >= 0, "the picture is stuck behind the animation");
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
  CHECK_MSG(picture("its answer lost", 3000) >= 0 && !lose, "the picture not sent again");
  CHECK_MSG(pic::S.t.tries == 0, "%d tries left over for the next transfer", pic::S.t.tries);
  lose = 1;
  panel_anim_noise.turn_off();
  panel_anim_ball.turn_on();
  int r = until_anim_status("ball");
  CHECK_MSG(r >= 0 && !lose && same_anim("ball", 0), "the animation not sent again: %s",
            panel_animation_status.state.c_str());
  printf("  ball: %s after %d cycles\n", panel_animation_status.state.c_str(), r);
  lose = 3;  // gives up after three tries, tries again after a panel restart
  panel_anim_ball.turn_off();
  run(3);
  panel_anim_ball.turn_on();
  r = run_until([] { return overlay::find("ball") && overlay::find("ball")->state == overlay::FAILED; }, 3000);
  CHECK_MSG(r >= 0 && !lose && anim_list() == "ball: the panel didn't answer", "lost answers: %s",
            panel_animation_status.state.c_str());
  pn_restart();
  r = until_anim_status("ball");
  CHECK_MSG(r >= 0 && overlay_ready() && same_anim("ball", 0), "not tried again after a panel restart: %s",
            panel_animation_status.state.c_str());
  h = real;
}

// Refused transfers. A bad CRC goes again, a patch the panel can't take goes again as the whole picture, a JPEG or
// an animation it can't take doesn't.
static void refused() {
  step("transfers the panel refuses");
  static int turn;  // DONE acks to answer with st instead
  static uint8_t st;
  auto &h = panel::handlers[K_PIC_ACK];
  auto real = h;
  h = [real](uint32_t a) {
    if (a >> 24 == PIC_DONE && turn > 0) {
      turn--;
      a = (uint32_t) st << 24 | (a & 0xFFFFFF);
    }
    real(a);
  };
  uint8_t ids = panel::transfer_ids;
  auto went = [&ids] { return (uint8_t) (panel::transfer_ids - ids); };
  std::string bad = std::to_string(PIC_BAD), want;
  st = PIC_BAD_CRC;
  turn = 1;
  draw_photo(panel_picture_dusk, 320, 240);
  pic::show("dusk");
  CHECK_MSG(picture("a JPEG with a bad CRC", 8000) >= 0 && !turn && went() == 2 && pn_pic_format() == PIC_JPEG,
            "a JPEG with a bad CRC: %d transfers, '%s'", went(), panel_picture_status.state.c_str());
  st = PIC_BAD;
  turn = 1;
  draw_photo(panel_picture_dusk, 300, 240);
  ids = panel::transfer_ids;
  pic::show("dusk");
  want = "dusk: the panel didn't take it (" + bad + ")";
  run_until([&] { return panel_picture_status.state == want; }, 8000);
  run(50);
  CHECK_MSG(!turn && went() == 1 && panel_picture_status.state == want && pic::S.phase == pic::IDLE,
            "a JPEG the panel can't draw: %d transfers, '%s'", went(), panel_picture_status.state.c_str());
  draw_dusk(panel_picture_dusk, 200, false);
  pic::show("dusk");
  CHECK_MSG(picture("16 colours after a refused JPEG", 6000) >= 0, "dusk not shown");
  turn = 1;
  draw_dusk(panel_picture_dusk, 215, false);
  ids = panel::transfer_ids;
  pic::show("dusk");
  CHECK_MSG(picture("a patch the panel can't take", 6000) >= 0 && !turn && went() == 2 && pic::S.jobs.empty(),
            "a refused patch: %d transfers", went());
  panel_anim_ball.turn_off();
  run(3);
  turn = 1;
  ids = panel::transfer_ids;
  panel_anim_ball.turn_on();
  want = "ball: the panel didn't take it (" + bad + ")";
  int r = until_anim_status(want.c_str());
  run(50);
  CHECK_MSG(r >= 0 && !turn && went() == 1 && overlay::find("ball")->state == overlay::FAILED,
            "an animation the panel can't take: %d transfers, '%s'", went(), panel_animation_status.state.c_str());
  st = PIC_BAD_CRC;
  turn = 1;
  ids = panel::transfer_ids;
  overlay::show("ball");  // tried again
  r = until_anim_status("ball");
  CHECK_MSG(r >= 0 && !turn && went() == 2 && same_anim("ball", 0), "an animation with a bad CRC: %d transfers, '%s'",
            went(), panel_animation_status.state.c_str());
  h = real;
}

// An animation on its way while the panel is away: no DATA goes until it's back
static void overlay_while_away() {
  step("an animation on its way while the panel is away");
  panel_anim_ball.turn_off();
  panel_anim_noise.turn_on();
  int r = run_until(
      [] { return overlay::W.t.head && overlay::W.t.pos > 0 && overlay::W.t.pos < overlay::W.a->data.size(); }, 3000);
  cx60_cycle();  // the panel takes what's queued, so there's room for more
  uint32_t pos = overlay::W.t.pos;
  unsigned queued = panel::link::waiting();
  server->updating_ = true;  // update.cpp has the bus
  for (int k = 0; k < 20; k++)
    panel::loop();
  CHECK_MSG(r >= 0 && !panel::link::connected && overlay::W.t.pos == pos && panel::link::waiting() == queued,
            "DATA queued while the panel was away: %u bytes on, %u more queued", (unsigned) (overlay::W.t.pos - pos),
            panel::link::waiting() - queued);
  server->updating_ = false;
  r = until_anim_status("noise, drifting");
  CHECK_MSG(r >= 0 && same_anim("noise", 0), "noise not shown once the panel was back: '%s'",
            panel_animation_status.state.c_str());
  panel_anim_noise.turn_off();
  panel_anim_ball.turn_on();
  until_anim_status("ball");
}

static void unknown_names() {
  step("names that aren't there, and none");
  pic::show("nope");
  overlay::show("nope");
  run(10);
  CHECK_MSG(logged("No picture called 'nope'") && logged("No animation called 'nope'"), "no warnings about the names");
  CHECK_MSG(panel_picture.state == "dusk" && panel_picture_status.state == "nope: no picture with that name" &&
                panel_pic() == 2 && panel_anim_ball.state && overlay_ready(),
            "an unknown name changed what's shown: '%s' '%s', %d '%s'", panel_picture.state.c_str(),
            panel_picture_status.state.c_str(), panel_anim_ball.state, panel_animation_status.state.c_str());
  overlay::clear("");  // panel_clear_animation without a name
  pic::show("");
  run(20);
  CHECK_MSG(panel_picture.state == "None" && panel_picture_status.state == "None" && panel_pic() == 0,
            "picture '%s' '%s' after no name", panel_picture.state.c_str(), panel_picture_status.state.c_str());
  CHECK_MSG(!panel_anim_ball.state && panel_animation_status.state == "None" && !overlay_ready(),
            "animation %d '%s' after no name", panel_anim_ball.state, panel_animation_status.state.c_str());
  panel_picture.control(1);
  CHECK_MSG(picture("dusk", 6000) >= 0, "dusk not shown");
  panel_picture.control(0);  // None
  run(20);
  CHECK_MSG(panel_pic() == 0, "picture still there after clearing");
}

static void cleared_and_shown() {
  step("the same picture cleared and shown again before the loop runs");
  panel_picture.control(1);
  CHECK_MSG(picture("dusk", 6000) >= 0, "dusk not shown");
  pic::clear();
  pic::show("dusk");
  CHECK_MSG(picture("dusk after clear and show", 3000) >= 0, "the panel has no picture after clear and show: '%s'",
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
    CHECK_MSG(panel_picture_status.state.rfind("flaky:", 0) == 0 && asked > 0,
              "while the download %s: '%s', asked for it %d times", failing ? "failed" : "ran",
              panel_picture_status.state.c_str(), asked);
    downloading = false;  // the download came
    pic::changed("flaky");
    CHECK_MSG(picture(failing ? "flaky, downloaded again after a failure" : "flaky, downloaded again", 3000) >= 0,
              "not shown after the download came");
  }
}

static void online_grew() {
  step("a shown online picture that grew too big for the ESP");
  static int fetched = 0;
  web.status = 200;
  web.length = 200 * 1024;
  web.type = "image/jpeg";
  int asked = web.asked;
  auto o = std::make_shared<pic::Online>();
  o->url = "http://x/flaky";
  o->every = 1000;
  o->format = "JPEG";
  o->busy = [] { return false; };
  o->fetch = [] {
    fetched++;
    return true;
  };
  pic::online["flaky"] = o;
  run(30);
  asked = web.asked - asked;
  CHECK_MSG(asked >= 3 && asked <= 5 && !fetched && panel_picture_status.state.rfind("flaky: too big", 0) == 0,
            "asked %d times, fetched %d, status '%s'", asked, fetched, panel_picture_status.state.c_str());
  pic::online.erase("flaky");
}

static void online_too_big() {
  step("online pictures: one too big for the ESP, and a Home Assistant camera's token");
  web.length = 120 * 1024;  // a JPEG, kept whole
  web.type = "image/jpeg";
  pic::show("radar");
  run(10);
  CHECK_MSG(!panel_picture_radar.updates && panel_picture_status.state.rfind("radar: too big for the ESP", 0) == 0,
            "a download too big for the ESP: %d tries, '%s'", panel_picture_radar.updates,
            panel_picture_status.state.c_str());
  ram_free = 400 * 1024;
  ram_block = 200 * 1024;
  run(300);  // retried after 30 s
  CHECK_MSG(panel_picture_radar.updates == 1, "%d downloads once it fits", panel_picture_radar.updates);
  draw_dusk(panel_picture_radar, 180, false);
  panel_picture_radar.decoding = false;
  panel_picture_radar.on_download_finished();
  CHECK_MSG(picture("radar", 6000) >= 0, "radar not shown after its download");
  ram_free = 200 * 1024;
  ram_block = 100 * 1024;
  ha_front_door_token.publish_state("t0k3n");
  CHECK_MSG(panel_picture_front_door.url ==
                "http://homeassistant.local:8123/api/camera_proxy/camera.front_door?width=320&token=t0k3n",
            "the camera's URL with its token: %s", panel_picture_front_door.url.c_str());
  panel_picture.control(0);
  run(20);
}

static void online_downloads() {
  step("an online picture's downloads and refreshes");
  web.length = 30000;
  web.asked = 0;
  pic::add_online_image("webcam", webcam, "http://x/webcam", "10min", "auto", "160x120", "auto", "auto");
  pic::Online &o = *pic::online["webcam"];
  CHECK_MSG(o.every == 600000 && o.pixels == 160 * 120 * 2 && o.format == "AUTO", "webcam: every %u, %u bytes, %s",
            (unsigned) o.every, (unsigned) o.pixels, o.format.c_str());
  static const struct {
    const char *every;
    uint32_t ms;
  } EVERY[] = {{"1d", 86400000}, {"1.5h", 5400000}, {"2 min", 120000}, {"500ms", 500},
               {"30s", 30000},   {"never", 0},      {"5", 0},          {"5 parsecs", 0}};
  for (auto e : EVERY)
    CHECK_MSG(pic::interval_ms(e.every) == e.ms, "update_interval '%s' read as %u ms, not %u", e.every,
              (unsigned) pic::interval_ms(e.every), (unsigned) e.ms);
  auto shown = [](int max) {
    run(3);
    return run_until([] { return panel_picture_status.state.rfind("Shown: webcam", 0) == 0 && settled(); }, max);
  };
  pic::show("webcam");
  run(3);
  CHECK_MSG(web.asked == 1 && webcam.updates == 1 && panel_picture_status.state == "webcam: waiting for it",
            "not downloaded when shown: asked %d, %d downloads, '%s'", web.asked, webcam.updates,
            panel_picture_status.state.c_str());
  draw_photo(webcam, 160, 120);  // the download came
  webcam.decoding = false;
  pic::changed("webcam");
  CHECK_MSG(shown(3000) >= 0 && pn_pic_format() == PIC_JPEG, "webcam not shown as a JPEG: '%s'",
            panel_picture_status.state.c_str());
  CHECK_MSG(o.kept.buffer == 30000 && o.kept.decoder == 'J' && !webcam.w,
            "download buffer %u, decoder %c, pixels %dx%d kept", (unsigned) o.kept.buffer, o.kept.decoder, webcam.w,
            webcam.h);

  // the same picture downloaded again: made again, nothing sent
  size_t mark = log_all.size();
  uint8_t ids = panel::transfer_ids;
  o.fetched = millis() - o.every;
  run(1);
  CHECK_MSG(web.asked == 2 && webcam.updates == 2, "not downloaded again after update_interval");
  draw_photo(webcam, 160, 120);
  webcam.decoding = false;
  pic::changed("webcam");
  bool ok = shown(3000) >= 0;
  int made = times_logged("Picture webcam: 320x240 as a JPEG", mark);
  CHECK_MSG(ok && made == 1 && panel::transfer_ids == ids, "the same JPEG: made %d times, %d transfers", made,
            (uint8_t) (panel::transfer_ids - ids));

  // a failed refresh: the panel keeps the picture until the next one
  o.fetched = millis() - o.every;
  run(1);
  webcam.decoding = false;
  pic::failed("webcam");
  run(20);
  CHECK_MSG(web.asked == 3 && webcam.updates == 3 && panel_picture_status.state.rfind("Shown: webcam", 0) == 0 &&
                panel_pic() == 2,
            "after a failed refresh: asked %d, %d downloads, '%s'", web.asked, webcam.updates,
            panel_picture_status.state.c_str());

  // a failed first download: the reason stays, the next try waits
  pic::show("dusk");
  CHECK_MSG(picture("dusk", 6000) >= 0, "dusk not shown");
  o.at = millis();
  pic::show("webcam");
  run(3);
  CHECK_MSG(webcam.updates == 4, "webcam not downloaded");
  webcam.decoding = false;
  pic::failed("webcam");
  run(20);
  CHECK_MSG(webcam.updates == 4 && panel_picture_status.state ==
                                       "webcam: couldn't download it, trying again in 29 s, or when it's picked again",
            "after a failed download: %d downloads, '%s'", webcam.updates, panel_picture_status.state.c_str());
}

// What the server says before a download
static void online_fits() {
  step("an online picture's RAM checks");
  pic::Online &o = *pic::online["webcam"];
  auto fits = [&](int c, size_t n, const char *t) {
    web.status = c;
    web.length = n;
    web.type = t;
    o.answer = {c, n, t};
    return pic::fits("webcam", o);
  };
  auto download = [&] {  // asks the server, and starts the download with its answer
    pic::download("webcam", o);
    run_asks();
    if (o.asking == pic::Online::ANSWERED)
      pic::start_download("webcam", o);
  };
  ram_free = 100 * 1024;
  CHECK_MSG(fits(200, 30000, "image/jpeg"), "a JPEG the size of the download buffer it has refused");
  CHECK_MSG(!fits(200, 40000, "image/jpeg") && pic::S.waiting.find("too big") == 0, "a bigger JPEG: '%s'",
            pic::S.waiting.c_str());
  ram_free = 200 * 1024;
  CHECK_MSG(fits(200, 150000, "image/png"), "a PNG bigger than the RAM refused, it's read 4 KB at a time");
  CHECK_MSG(!fits(200, 150000, "image/jpeg"), "a JPEG bigger than the RAM taken");
  CHECK_MSG(!fits(200, 0, "image/jpeg") && pic::S.waiting.find("doesn't say how big") != std::string::npos,
            "a JPEG without a size: '%s'", pic::S.waiting.c_str());
  CHECK_MSG(fits(200, 0, "image/png"), "a PNG without a size refused");
  CHECK_MSG(!fits(0, 0, "") && pic::S.waiting.find("couldn't reach") == 0, "no answer: '%s'", pic::S.waiting.c_str());
  CHECK_MSG(!fits(404, 100, "text/html") && pic::S.waiting.find("404") != std::string::npos, "a 404: '%s'",
            pic::S.waiting.c_str());
  o.format = "PNG";
  CHECK_MSG(fits(200, 150000, "image/jpeg"), "format PNG taken as a JPEG");
  o.format = "AUTO";
  // a download that doesn't start doesn't grow the buffer
  webcam.fail = true;
  web.length = 40000;
  int asked = web.asked;
  pic::download("webcam", o);  // in a task of its own, the loop goes on meanwhile
  CHECK_MSG(tasks.size() == 1 && web.asked == asked && o.asking == pic::Online::ASKING,
            "the server asked in the loop: %d times, %u tasks", web.asked - asked, (unsigned) tasks.size());
  run_asks();
  pic::start_download("webcam", o);
  webcam.fail = false;
  CHECK_MSG(o.kept.buffer == 30000, "a download that didn't start grew the buffer to %u", (unsigned) o.kept.buffer);
  download();
  int updates = webcam.updates;
  asked = web.asked;
  web.length = 50000;
  download();  // while that one still runs
  webcam.decoding = false;
  CHECK_MSG(o.kept.buffer == 40000 && webcam.updates == updates && web.asked == asked,
            "buffer %u after two downloads, %d started, the server asked %d times", (unsigned) o.kept.buffer,
            webcam.updates - updates + 1, web.asked - asked + 1);
  // the first PNG needs room for its decoder too
  ram_free = 120 * 1024;
  CHECK_MSG(!fits(200, 5000, "image/png") && fits(200, 30000, "image/jpeg"), "a first PNG's decoder not counted");
  ram_free = 200 * 1024;
  web.type = "image/png";
  download();
  webcam.decoding = false;
  ram_free = 120 * 1024;
  CHECK_MSG(o.kept.decoder == 'P' && fits(200, 5000, "image/png"), "a PNG refused with its decoder there");
  ram_free = 200 * 1024;
  // 320x240 is 150 KB decoded, in one piece: only with PSRAM, which takes the buffer and the pixels
  uint32_t pixels = o.pixels;
  o.pixels = 320 * 240 * 2;
  CHECK_MSG(!fits(200, 30000, "image/jpeg"), "320x240 taken without PSRAM");
  psram_free = psram_block = 4096 * 1024;
  CHECK_MSG(fits(200, 30000, "image/jpeg"), "320x240 refused with PSRAM: '%s'", pic::S.waiting.c_str());
  ram_free = 50 * 1024;  // converting it is internal RAM's
  size_t logged = log_all.size();
  CHECK_MSG(!fits(200, 30000, "image/jpeg") && log_all.find("PSRAM 4096 KB free", logged) != std::string::npos,
            "with PSRAM but too little internal RAM: '%s'", log_all.substr(logged).c_str());
  ram_free = 200 * 1024;
  psram_free = psram_block = 0;
  o.pixels = pixels;
  web.status = 200;
  web.length = 30000;
  web.type = "image/jpeg";
}

// Another picture picked while it's being made: its pixels are freed
static void online_picked() {
  step("another picture picked while an online one is made");
  draw_flat(webcam);
  pic::show("webcam");
  online_pick_after = 1000;
  CHECK_MSG(picture("dusk, picked while webcam was being made", 6000) >= 0 && !webcam.w && !online_pick_after,
            "webcam's pixels kept: %dx%d", webcam.w, webcam.h);
}

// ---------------------------------------------------------------- the space pictures

// A picture from the PPMs the Makefile makes, see ESP_PICTURES
static void picture_of(Image &img, const char *file) {
  CHECK_MSG(load_ppm(img, (std::string(PICTURES) + file).c_str()), "no %s%s", PICTURES, file);
}

// Whether picture name's marks have text s
static bool has_mark(const std::string &name, const std::string &s) {
  for (const marks::Op &o : marks::all[name].layer.ops)
    if (o.kind == marks::Op::TEXT && o.text == s)
      return true;
  return false;
}

// Picture name's marks' texts, for a failure's message
static std::string mark_texts(const std::string &name) {
  std::string t;
  for (const marks::Op &o : marks::all[name].layer.ops)
    if (o.kind == marks::Op::TEXT)
      t += (t.empty() ? "" : " | ") + o.text;
  return t;
}

// a is due, the other askers wait
static void ask_only(fetch::Asker &a) {
  for (fetch::Asker *o : fetch::askers)
    o->schedule.not_before = o == &a ? 0 : UINT32_MAX;
}

// NOAA's Kp, asked for while the aurora's on and read from a real answer, and the aurora by it: in Oslo on auto once
// the Kp reaches 3.3, coloured as is usual at the Kp, brighter the further over, and mostly red in a storm that only
// just reaches as far south
static void met_kp() {
  step("NOAA's Kp, asked for and read, and the aurora by it");
  met::KpIndex &k = met::kp_index;
  const std::string &status = panel_weather_kp.state;
  met::lat = OSLO_LAT;
  met::lon = OSLO_LON;
  flexit_panel_time.t.timestamp = utc(2026, 11, 8, 22, 30);  // in the 3 hours from 21, an estimated 4.67
  panel_weather_aurora.control(1);                           // off
  ask_only(k);
  yaml_every(1000);
  CHECK_MSG(!k.asking && tasks.empty(), "asked NOAA for the Kp with the aurora off");
  panel_weather_aurora.control(0);  // auto
  web.status = 200;
  web.last_modified = web.expires = "";
  web.body = sample("kp_sample.json");
  web.type = "application/json";
  yaml_every(1000);
  CHECK_MSG(k.asking && tasks.size() == 1, "the Kp not asked for on auto");
  run_task();
  const met::KpBlocks &kp = met::kp;
  CHECK_MSG(web.url == met::KP_URL && !k.asking && kp.n == met::KpBlocks::MAX && kp.b[0].t == utc(2026, 11, 7, 3) &&
                k.schedule.not_before == fetch::now_s() + 3 * 3600,
            "the Kp not taken: %d blocks from %u, next in %d s", kp.n, (unsigned) kp.b[0].t,
            (int) (k.schedule.not_before - fetch::now_s()));
  CHECK_MSG(fabsf(kp.at(fetch::now_s()) - 4.67f) < 0.01f && fabsf(kp.at(utc(2026, 11, 11, 2, 59)) - 2.33f) < 0.01f &&
                std::isnan(kp.at(utc(2026, 11, 11, 3))) && std::isnan(kp.at(utc(2026, 11, 7, 2, 59))),
            "the Kp read wrong: %.2f now", kp.at(fetch::now_s()));
  CHECK_MSG(status == "Kp 4.7 now, the aurora needs 3.3 here", "Panel Weather Kp says '%s'", status.c_str());
  // an answer without a Kp from now on keeps the last
  web.body = "[]";
  k.schedule.not_before = 0;
  yaml_every(1000);
  run_task();
  CHECK_MSG(kp.n == met::KpBlocks::MAX && status == "Kept the last Kp (HTTP 200)", "an empty answer: '%s'",
            status.c_str());
  k.schedule.not_before = UINT32_MAX;
  // the picture has the Kp for the time it's drawn: in Oslo on auto with 4.67, not with 1.67, always with either
  auto night = [](uint32_t t, met::Aurora a, const char *name) {
    weather_of({"", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, t, met::G_HILLS, 0, 0, 0});
    met::options.aurora = a;
    met::redraw();
    CHECK_MSG(picture(name, 6000) >= 0, "%s: not shown", name);
    return panel_scene().aurora;
  };
  CHECK_MSG(night(utc(2026, 11, 8, 22, 30), met::A_AUTO, "Kp 4.67 in Oslo") && met::options.kp > 4.6f,
            "Kp 4.67 in Oslo: no aurora, Kp %.2f", met::options.kp);
  pn_shot(outdir, "esp_aurora_kp");
  CHECK_MSG(!night(utc(2026, 11, 10, 18, 30), met::A_AUTO, "Kp 1.67 in Oslo"), "Kp 1.67 in Oslo: an aurora");
  CHECK_MSG(night(utc(2026, 11, 10, 18, 30), met::A_ALWAYS, "Kp 1.67 in Oslo, always"), "always: no aurora at Kp 1.67");
  // what it takes, and the colours by the Kp
  struct Place {
    double lat, lon;
    float need;
  } const PLACES[] = {{69.65, 18.96, 0}, {63.43, 10.4, 1.7f}, {OSLO_LAT, OSLO_LON, 3.3f}, {55.68, 12.57, 5.4f}};
  for (const Place &p : PLACES)
    CHECK_MSG(fabsf(met::kp_needed(p.lat, p.lon) - p.need) < 0.1f, "at %.2f, %.2f it takes Kp %.2f, not %.1f", p.lat,
              p.lon, met::kp_needed(p.lat, p.lon), p.need);
  auto at = [](float kpv, double la, double lo) -> const met::Scene & {
    met::Options o = met::options;
    o.aurora = met::A_AUTO;
    o.kp = kpv;
    return scene_at(met::weather, utc(2026, 11, 8, 22, 30), o, la, lo);
  };
  auto rgb = [](uint32_t c, int k) { return (int) (c >> (16 - 8 * k) & 255); };
  auto lum = [&](uint32_t c) { return rgb(c, 0) * 3 + rgb(c, 1) * 6 + rgb(c, 2); };
  CHECK_MSG(!at(3.0f, OSLO_LAT, OSLO_LON).aurora && at(3.67f, OSLO_LAT, OSLO_LON).aurora, "Oslo's Kp 3.3 not kept to");
  uint32_t quiet = at(1, 69.65, 18.96).rgb[met::C_AUR_TOP], storm = at(7, 69.65, 18.96).rgb[met::C_AUR_TOP];
  CHECK_MSG(rgb(quiet, 0) < rgb(quiet, 1) && rgb(storm, 0) > rgb(storm, 1) && rgb(storm, 0) > rgb(storm, 2),
            "the top at Kp 1 %06X and 7 %06X", (unsigned) quiet, (unsigned) storm);
  uint32_t north = at(7.33f, 69.65, 18.96).rgb[met::C_AUR], south = at(7.33f, 52.52, 13.4).rgb[met::C_AUR];
  CHECK_MSG(rgb(north, 1) > rgb(north, 0) && rgb(south, 0) > rgb(south, 1),
            "a storm at Kp 7.33 isn't green in Tromso, %06X, and red in Berlin, %06X", (unsigned) north,
            (unsigned) south);
  uint32_t far_over = at(4.67f, 69.65, 18.96).rgb[met::C_AUR],
           just_over = at(4.67f, OSLO_LAT, OSLO_LON).rgb[met::C_AUR];
  CHECK_MSG(lum(far_over) > lum(just_over) && !at(4.67f, 55.68, 12.57).aurora,
            "Kp 4.67: in Tromso %06X isn't brighter than in Oslo %06X, or Copenhagen has it", (unsigned) far_over,
            (unsigned) just_over);
  met::kp = {};
  met::options.aurora = met::A_AUTO;
}

// A photo of the day: its list asked for and read, its photo downloaded and shown, and its marks over it
static void photo_of_the_day(const char *name, fetch::Asker &list, OnlineImage &img, const char *body,
                             const std::function<void()> &draw) {
  std::string k = marks::overlay_of(name);
  pic::show(name);
  run(5);
  CHECK_MSG(panel_picture_status.state == std::string(name) + ": looking for the newest photo" && !img.updates,
            "%s before its list: '%s', %d downloads", name, panel_picture_status.state.c_str(), img.updates);
  web.body = sample(body);
  web.type = "application/json";
  ask_only(list);
  yaml_every(1000);
  CHECK_MSG(list.asking && tasks.size() == 1, "%s's list not asked for", name);
  run_task();
  CHECK_MSG(!list.asking && list.have && list.schedule.not_before == fetch::now_s() + list.every_s && !img.url.empty(),
            "%s's list not taken: next in %d s, URL '%s'", name, (int) (list.schedule.not_before - fetch::now_s()),
            img.url.c_str());
  web.body.clear();
  web.type = "image/jpeg";
  web.length = 11600;
  CHECK_MSG(run_until([&] { return img.updates == 1; }, 100) >= 0, "%s's photo not downloaded", name);
  draw();
  img.decoding = false;
  img.on_download_finished();
  std::string shown = std::string("Shown: ") + name;  // its pixels are gone once it's made, so not compared
  CHECK_MSG(run_until([&] { return panel_picture_status.state.rfind(shown, 0) == 0 && settled() && panel_pic() == 2; },
                      6000) >= 0,
            "%s not shown: '%s'", name, panel_picture_status.state.c_str());
  int r = run_until([&] { return overlay::find(k) && overlay::find(k)->state == overlay::SENT && settled(); }, 600);
  CHECK_MSG(r >= 0 && overlay::find(k)->still, "%s's marks not over it: '%s'", name,
            panel_animation_status.state.c_str());
  steps(400);  // they fade in
  pn_shot(outdir, (std::string("esp_") + name).c_str());
  pic::show("dusk");
  CHECK_MSG(picture("dusk", 6000) >= 0 && !overlay::find(k), "%s's marks left over dusk", name);
}

static void space_photos() {
  step("Mars today and Earth today: NASA's lists read as they stream, the photos shown with their marks over them");
  time_t then = flexit_panel_time.t.timestamp;  // put back after, for the tests that follow
  int32_t tz = tz_offset_s;
  flexit_panel_time.t.timestamp = 1793606400;  // 2026-11-02 08:00 UTC
  tz_offset_s = 3600;                          // Oslo in winter time
  Web kept = web;
  web = Web();
  const std::string &mars_status = panel_mars_today_status.state, &earth_status = panel_earth_today_status.state;

  // Perseverance's newest sol: its Mastcam-Z colour photo, not the one through the sun filter, the sky camera's, the
  // Hazcam's or the Navcam's of the sol before
  photo_of_the_day("mars_today", space::mars::list, panel_picture_mars_today, "mars_sample.json",
                   [] { picture_of(panel_picture_mars_today, "mars_sample.ppm"); });
  const space::mars::Photo &p = space::mars::photo;
  CHECK_MSG(p.sol == 2024 && p.camera == "Mastcam-Z" && p.on_mars == "13:10" && p.elevation < -59 &&
                p.url.find("/ZL0_2024_0846622537_") != std::string::npos && panel_picture_mars_today.url == p.url,
            "the photo picked: sol %d, %s at %g, %s", p.sol, p.camera.c_str(), p.elevation, p.url.c_str());
  CHECK_MSG(mars_status == "Sol 2024, Mastcam-Z, 30 Oct · 10:21", "Mars today's status: '%s'", mars_status.c_str());
  CHECK_MSG(has_mark("mars_today", "Sol 2024") && has_mark("mars_today", "30 OCT · 13:10 ON MARS") &&
                has_mark("mars_today", "Jezero crater, by Perseverance's Mastcam-Z"),
            "Mars today's marks");
  space::mars::Photo a, b;  // of one sol, the one looking out nearer the horizon, then the later
  a.sol = b.sol = 9;
  a.elevation = -60;
  b.elevation = -8;
  bool level = space::mars::better(b, a);
  a.elevation = -6;
  a.taken = 2;
  b.taken = 1;
  CHECK_MSG(level && space::mars::better(a, b), "a photo of the ground picked over one of the view");

  // a server error keeps the photo and asks again a minute later
  std::string url = p.url;
  web.status = 503;
  ask_only(space::mars::list);
  yaml_every(1000);
  run_task();
  CHECK_MSG(mars_status == "Kept the last photo (HTTP 503)" && p.url == url &&
                space::mars::list.schedule.not_before == fetch::now_s() + 60,
            "after a server error: '%s', next in %d s", mars_status.c_str(),
            (int) (space::mars::list.schedule.not_before - fetch::now_s()));
  web.status = 200;

  // EPIC's newest of the day, late on 1 November
  photo_of_the_day("earth_today", space::earth::list, panel_picture_earth_today, "epic_sample.json",
                   [] { picture_of(panel_picture_earth_today, "epic_sample.ppm"); });
  CHECK_MSG(panel_picture_earth_today.url ==
                "https://epic.gsfc.nasa.gov/archive/natural/2026/11/01/thumbs/epic_1b_20261101224105.jpg",
            "Earth today's photo: %s", panel_picture_earth_today.url.c_str());
  CHECK_MSG(earth_status == "epic_1b_20261101224105, 1 Nov · 23:36", "Earth today's status: '%s'",
            earth_status.c_str());
  CHECK_MSG(has_mark("earth_today", "1.49") && has_mark("earth_today", "the Pacific") &&
                has_mark("earth_today", "6° S 173° W") && has_mark("earth_today", "near Apia") &&
                has_mark("earth_today", "1 NOV · 23:36"),
            "Earth today's marks");
  web = kept;
  flexit_panel_time.t.timestamp = then;
  tz_offset_s = tz;
}

// A rover's waypoints read from body and its picture shown with its track, the marks over it
static void rover_shown(const char *rover, const char *body) {
  space::rover::Map &m = *space::rover::map_of(rover);
  std::string k = marks::overlay_of(rover);
  web.body = sample(body);
  ask_only(m);
  yaml_every(1000);
  CHECK_MSG(m.asking && tasks.size() == 1, "%s's waypoints not asked for", rover);
  run_task();
  CHECK_MSG(m.have && m.track.size() > 20 && m.schedule.not_before == fetch::now_s() + 21600,
            "%s's waypoints not taken: %u points, next in %d s", rover, (unsigned) m.track.size(),
            (int) (m.schedule.not_before - fetch::now_s()));
  pic::show(rover);
  CHECK_MSG(picture(rover, 6000) >= 0, "%s not shown", rover);
  int r = run_until([&] { return overlay::find(k) && overlay::find(k)->state == overlay::SENT && settled(); }, 600);
  CHECK_MSG(r >= 0, "%s's marks not over it: '%s'", rover, panel_animation_status.state.c_str());
  steps(400);
  pn_shot(outdir, (std::string("esp_") + rover).c_str());
}

static void rover_tracks() {
  step("the rovers: their waypoints read as they stream, their tracks on their maps with the marks over them");
  time_t then = flexit_panel_time.t.timestamp;
  int32_t tz = tz_offset_s;
  flexit_panel_time.t.timestamp = 1793606400;  // 2026-11-02 08:00 UTC
  tz_offset_s = 3600;
  Web kept = web;
  web = Web();
  web.type = "application/json";
  // Perseverance's sol 2024 began on Earth when its Mastcam-Z photo at 13:10 on Mars, 09:21:10 UTC, says it did
  const space::rover::Rover &m20 = space::rover::ROVERS[0];
  int32_t off = (int32_t) (space::sol_start(2024, m20.landed) - 1793303349u);
  CHECK_MSG(abs(off) < 120, "sol 2024 began %d s off", off);

  rover_shown("perseverance", "perseverance_sample.json");
  space::rover::Map &m = *space::rover::map_of("perseverance");
  space::rover::Point end = space::rover::point_of(m20, m.last.lon, m.last.lat, 0);
  CHECK_MSG(m.first.sol == 13 && m.last.sol == 2022 && m.track.size() < 79 && m.track.back().x == end.x &&
                m.track.back().y == end.y && panel_perseverance_status.state == "Sol 2022, 45.25 km",
            "Perseverance: sols %d to %d, %u points, '%s'", m.first.sol, m.last.sol, (unsigned) m.track.size(),
            panel_perseverance_status.state.c_str());
  CHECK_MSG(has_mark("perseverance", "45.25") && has_mark("perseverance", "634") &&
                has_mark("perseverance", "DRIVEN SINCE 2021") && has_mark("perseverance", "Landed 2021") &&
                has_mark("perseverance", "NOW") && has_mark("perseverance", "LAST 120 SOLS") &&
                has_mark("perseverance", "2 KM"),
            "Perseverance's marks");
  // driven off the map, west: an arrow at the edge
  space::rover::Map away = m;
  away.track.push_back(space::rover::point_of(m20, 77.0, 18.47, 2030));
  away.last.sol = 2030;
  marks::Layer l = space::rover::layer_of(away);
  const marks::Op *arrow = nullptr;
  for (const marks::Op &o : l.ops)
    if (o.kind == marks::Op::POLY)
      arrow = &o;
  CHECK_MSG(arrow && arrow->box.x0 >= 0 && arrow->box.x0 < 12 && fabsf(arrow->box.y0 - 116) < 8,
            "no arrow at the left edge: %g, %g", arrow ? arrow->box.x0 : -1, arrow ? arrow->box.y0 : -1);

  rover_shown("curiosity", "curiosity_sample.json");
  CHECK_MSG(panel_curiosity_status.state == "Sol 5045, 38.48 km" && has_mark("curiosity", "1013") &&
                has_mark("curiosity", "UP MOUNT SHARP"),
            "Curiosity: '%s'", panel_curiosity_status.state.c_str());
  pic::show("dusk");
  CHECK_MSG(picture("dusk", 6000) >= 0 && !overlay::find("curiosity_marks"), "Curiosity's marks left over dusk");
  web = kept;
  flexit_panel_time.t.timestamp = then;
  tz_offset_s = tz;
}

// The probe Horizons is being asked about
static space::probes::Horizons *asked() {
  return static_cast<space::probes::Horizons *>(space::probes::horizons.asking);
}

// A round of JPL Horizons' answers, each probe's from horizons_sample.txt as its question comes
static void horizons_round() {
  std::string all = sample("horizons_sample.txt");
  while (space::probes::horizons.asking && !tasks.empty()) {
    int id = space::probes::PROBES[asked()->probe].id;
    size_t at = all.find("(" + std::to_string(id) + ")"), from = all.rfind("API VERSION", at),
           to = all.find("API VERSION", at);
    web.body = all.substr(from, to == std::string::npos ? to : to - from);
    run_task();
  }
}

static void probes_chart() {
  step("the probes: JPL Horizons asked where each is, one after the other, the planets worked out, on a chart");
  time_t then = flexit_panel_time.t.timestamp;
  int32_t tz = tz_offset_s;
  flexit_panel_time.t.timestamp = 1793880000;  // 2026-11-05 12:00 UTC, the sample's day
  tz_offset_s = 3600;
  Web kept = web;
  web = Web();
  web.type = "text/plain";
  pic::show("probes");
  run(5);
  CHECK_MSG(panel_picture_status.state == "probes: asking JPL Horizons where they are", "before the answers: '%s'",
            panel_picture_status.state.c_str());
  ask_only(space::probes::horizons);
  yaml_every(1000);
  CHECK_MSG(asked() && asked()->url.find("COMMAND=%27-31%27") != std::string::npos &&
                asked()->url.find("START_TIME=%272026-11-05%27&STOP_TIME=%272026-11-06%27") != std::string::npos,
            "the first question: %s", asked() ? asked()->url.c_str() : "none");
  horizons_round();
  int known = 0;
  for (auto &t : space::probes::tracks)
    known += t.ok;
  CHECK_MSG(known == space::probes::N && panel_probes_status.state == "11 probes, 5 Nov · 13:00" &&
                space::probes::horizons.schedule.not_before == fetch::now_s() + 86400,
            "after a round: %d probes, '%s'", known, panel_probes_status.state.c_str());
  // the light-day: Voyager 1's signal takes a day on 19 November 2026, as JPL has it
  CHECK_MSG(has_mark("probes", "Voyager 1") && has_mark("probes", "172") && has_mark("probes", "144") &&
                has_mark("probes", "66") && has_mark("probes", "VOYAGER 1'S SIGNAL") &&
                has_mark("probes", "A light-day on 19 Nov"),
            "the farthest: %s", mark_texts("probes").c_str());
  CHECK_MSG(has_mark("probes", "Mars") && has_mark("probes", "Psyche") && has_mark("probes", "Lucy") &&
                has_mark("probes", "Neptune") && space::probes::k > 80,
            "the chart's marks, at %g px an AU", space::probes::k);
  CHECK_MSG(picture("probes", 6000) >= 0, "probes not shown");
  int r = run_until(
      [] {
        return overlay::find("probes_marks") && overlay::find("probes_marks")->state == overlay::SENT && settled();
      },
      600);
  CHECK_MSG(r >= 0, "the probes' marks not over it: '%s'", panel_animation_status.state.c_str());
  steps(400);
  pn_shot(outdir, "esp_probes");
  // a server error part way: what came stays, the round is asked again
  ask_only(space::probes::horizons);
  yaml_every(1000);
  run_task();
  web.status = 500;
  run_task();
  CHECK_MSG(!asked() && panel_probes_status.state == "Kept the last positions (HTTP 500)" &&
                space::probes::tracks[1].ok && space::probes::horizons.schedule.not_before == fetch::now_s() + 60,
            "after a server error: '%s'", panel_probes_status.state.c_str());
  pic::show("dusk");
  CHECK_MSG(picture("dusk", 6000) >= 0, "dusk not shown");
  web = kept;
  flexit_panel_time.t.timestamp = then;
  tz_offset_s = tz;
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
  CHECK_MSG(no_ram(), "too little RAM to count: '%s', phase %d", panel_picture_status.state.c_str(), pic::S.phase);
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
  CHECK_MSG(picture("dusk without the RAM for a JPEG", 6000) >= 0 && pn_pic_format() == PIC_PALETTE &&
                logged("no RAM for a JPEG", no_jpeg),
            "dusk not shown as 16 colours");
  ram_free = 40 * 1024;
  reads = 0;
  draw_photo(panel_picture_dusk, 320, 240);  // too detailed for 16 colours
  pic::show("dusk");
  CHECK_MSG(no_ram(), "a photo without the RAM for a JPEG: '%s', phase %d", panel_picture_status.state.c_str(),
            pic::S.phase);
  dusk.pixel = pixel;
  ram_free = 200 * 1024;
  draw_dusk(panel_picture_dusk, 215, false);
  pic::show("dusk");
  CHECK_MSG(picture("dusk with the RAM back", 6000) >= 0, "dusk not shown");
}

static void actions_kept() {
  step("the actions are kept for after a restart, like the selects");
  pic::clear();
  run(3);
  CHECK_MSG(panel_picture.saved == "None", "clear: '%s' kept", panel_picture.saved.c_str());
  pic::show("dusk");
  CHECK_MSG(picture("dusk", 6000) >= 0 && panel_picture.saved == "dusk", "show: '%s' kept",
            panel_picture.saved.c_str());
  overlay::show("ball");
  CHECK_MSG(panel_anim_ball.state, "animation: the switch isn't on");
  panel_clear_animation.execute("");
  CHECK_MSG(!panel_anim_ball.state, "animation cleared: the switch is still on");
  run(10);
}

static void number_late() {
  step("a number whose entity answers late");
  static float set_to = 0;
  temperature.optimistic = false;  // like a number whose set_action goes to the unit and reads back later
  temperature.set_action = [](float v) { set_to = v; };
  pn_set(value_key("temperature"), 230);
  run(20);  // 2.2 s, nothing from the entity yet
  CHECK_MSG(set_to == 23.0f && pitem("temperature").value == 230,
            "the panel shows %d after setting 23.0, entity got %g", pitem("temperature").value, set_to);
  temperature.publish_state(23.0f);
  run(40);
  CHECK_MSG(pitem("temperature").value == 230, "the panel shows %d once the entity answered",
            pitem("temperature").value);
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
  CHECK_MSG(!english && !strcmp(pitem("fireplace").label, "Peis"),
            "in English for %d cycles after the hello, then '%s'", english, pitem("fireplace").label);
  CHECK_MSG(pn_reg(0x00D) == 160, "the ESP's hello put its old supply min %d on the panel", pn_reg(0x00D));
  int c = restart_panel();  // the panel asks, and keeps its copy over the ESP's flash one
  run(15);
  CHECK_MSG(c >= 0 && pn_reg(0x00D) == 160, "the panel restarted and took the ESP's old supply min %d", pn_reg(0x00D));
  int supply_max = pn_reg(0x00E) + 10;
  server->setting(0x00E, (uint16_t) supply_max);  // changed on another panel while this one restarts
  c = restart_panel();
  run(15);
  CHECK_MSG(c >= 0 && pn_reg(0x00E) == supply_max, "the panel restarted and has supply max %d, the ESP saw %d",
            pn_reg(0x00E), supply_max);
  server->setting(0x0B5, 75);  // a regulator value, which has no link key: only the register goes
  c = restart_panel();
  run(15);
  CHECK_MSG(c >= 0 && pn_reg(0x0B5) == 75, "the panel restarted and has heating K %d, the ESP saw 75", pn_reg(0x0B5));
}

static void notes_while_away() {
  step("notes while the panel is away or restarting");
  int icon, level, fresh;
  pn_note(&icon, &level, &fresh);
  link_lost_frames = 1000;  // gone
  run(300);
  CHECK_MSG(!panel::link::connected, "the panel's still connected");
  panel::notify("Door open", "door", "warning");
  run(10);
  CHECK_MSG(!panel::link::queued(K_NOTIFY), "a note queued for a panel that's away");
  // nothing made for it meanwhile
  overlay::clear("");
  pic::show("weather");
  panel_anim_ball.turn_on();
  run(50);
  CHECK_MSG(pic::S.phase == pic::START && !met::drawing && overlay::W.phase == overlay::IDLE &&
                panel_picture_status.state == "weather: waiting for the panel",
            "made for a panel that's away: '%s', '%s'", panel_picture_status.state.c_str(),
            panel_animation_status.state.c_str());
  link_lost_frames = 0;
  restart_panel();
  const char *away = pn_note(&icon, &level, &fresh);
  CHECK_MSG(fresh && !strcmp(away, "Door open") && panel::note.empty(), "note while away: '%s', fresh %d", away, fresh);
  CHECK_MSG(picture("weather, the panel back", 6000) >= 0 && overlay::find("ball") &&
                overlay::find("ball")->state == overlay::SENT,
            "not made once the panel was back: '%s'", panel_animation_status.state.c_str());
  panel::notify("Washer done", "washer", "info");
  restart_panel();  // before it went out
  const char *restart = pn_note(&icon, &level, &fresh);
  CHECK_MSG(fresh && !strcmp(restart, "Washer done"), "note during a restart: '%s', fresh %d", restart, fresh);
}

static void bootloader_away() {
  step("the panel counts as away once it's in its bootloader");
  auto slow = [](int f, int x, int y) {
    delayMicroseconds(1);  // about a frame a slice, so it's made over many loops
    return noise_px(f, x, y);
  };
  overlay::clear("");
  run(5);
  overlay::add("slow", {64, 64, 6, 100, 16, 16, 1, slow});
  overlay::show("slow");
  panel::loop();
  bool making = overlay::W.phase == overlay::COUNT || overlay::W.phase == overlay::MAKE;
  server->updating_ = true;  // update.h has the bus
  panel::loop();
  CHECK_MSG(making && !panel::link::connected && overlay::W.phase == overlay::IDLE &&
                overlay::find("slow")->state == overlay::WANT && overlay::find("slow")->data.empty(),
            "still made for a panel in its bootloader: making %d, connected %d, phase %d", making,
            panel::link::connected, overlay::W.phase);
  run(50);
  CHECK_MSG(overlay::W.phase == overlay::IDLE, "made again while in its bootloader: phase %d", overlay::W.phase);
  server->updating_ = false;
  restart_panel();  // the new firmware
  CHECK_MSG(run_until([] { return overlay::find("slow")->state == overlay::SENT && settled(); }, 3000) >= 0,
            "not made once the panel was back: '%s'", panel_animation_status.state.c_str());
}

static void settings_flash() {
  step("the panel's own settings over a flash");
  panel::set("theme", 2);
  panel::set("screensaver", 3);
  run_until(settled, 300);
  panel::settings::save();  // update.h, before it flashes
  int r = run_until([] { return panel::settings::saved() && settled(); }, 300);
  CHECK_MSG(r >= 0 && panel::settings::all && panel::settings::records.size() > 20 && logged("Saved the panel's"),
            "the panel's settings: %d records after %d cycles", (int) panel::settings::records.size(), r);
  panel::settings::flashed();
  size_t sent = log_all.size();
  pn_reboot(1);  // the flash erased them
  CHECK_MSG(pn_get(K_THEME) == 0, "theme %d after the flash", pn_get(K_THEME));
  r = run_until([] { return pn_get(K_THEME) == 2 && pn_get(K_SCREENSAVER) == 3 && settled(); }, 300);
  CHECK_MSG(r >= 0 && logged("started without settings, sending its", sent),
            "not back after the flash: theme %d, screensaver %d", pn_get(K_THEME), pn_get(K_SCREENSAVER));
  pn_reboot(0);  // the restart it does to take them, saved
  run_until([] { return pn_online() && settled(); }, 300);
  CHECK_MSG(!panel::settings::restore && panel::settings::records.empty() && logged("back with its settings", sent),
            "the ESP kept the panel's settings once it had them");
  pn_reboot(1);  // erased without our flash, nothing to send
  run_until([] { return pn_online() && settled(); }, 300);
  CHECK_MSG(pn_get(K_THEME) == 0, "theme %d from nowhere", pn_get(K_THEME));
  pn_reboot(0);
  run_until([] { return pn_online() && settled(); }, 300);
}

// A missing icon is warned about once, not at every hello, and its id goes anyway
static void item_icon_typo() {
  step("an item's icon that isn't there");
  size_t at = log_all.size();
  panel::Item("typo", ITEM_SENSOR).view(false).menu(false).icon("nope").add();
  run(3);
  restart_panel();
  CHECK_MSG(pitem("typo").icon == ICN_CIRCLE && times_logged("No icon called 'nope'", at) == 1 &&
                panel::icon("Fire") == ICN_FIRE,
            "an icon called 'nope': %d on the panel, warned %d times", pitem("typo").icon,
            times_logged("No icon called 'nope'", at));
}

// ---------------------------------------------------------------- the panel's screen as a camera
// How far off what the panel shows the camera's last image is, see sim_jpeg_off()
static double screen_off() {
  if (panel_screen.images.empty())
    return -1;
  auto &j = panel_screen.images.back();
  return sim_jpeg_off(j.data(), j.size());
}

// Cycles with the panel drawing as it does between them, until done() or max. The cycles it took, -1 if it didn't.
static int watch(const std::function<bool()> &done, int max) {
  for (int c = 0; c < max; c++) {
    if (done())
      return c;
    run(1);
    pn_draw(PBUS_CYCLE_US / 1000);
  }
  return -1;
}

// n cycles with the panel drawing between them
static void draw_cycles(int n) {
  watch([] { return false; }, n);
}

static bool every_tile() {
  auto &t = panel::shot::S.tiles;
  return !panel::shot::S.asked && !t.empty() && std::all_of(t.begin(), t.end(), [](auto &b) { return b != nullptr; });
}

// An image for one look, within max cycles. The cycles it took, -1 if none came.
static int one_image(int max) {
  size_t had = panel_screen.images.size();
  panel_screen.single = true;
  return watch([had] { return panel_screen.images.size() > had; }, max);
}

static void screen_camera() {
  step("the panel's screen as a camera");
  namespace shot = panel::shot;
  auto &cam = panel_screen;
  const int partial = (int) (shot::PARTIAL_MS * 1000 / PBUS_CYCLE_US) + 5;
  CHECK_MSG(shot::S.quality == 90, "screen_camera.yaml didn't attach the camera, quality %d", shot::S.quality);
  pn_saver(0);
  watch([] { return settled() && !panel::link::gaps.off; }, 2000);  // the gaps after polls back since earlier tests
  // The panel's loop between the windows too, so it fills both with tiles
  loop_between = true;
  // The first look gets what came within PARTIAL_MS, black where nothing has. A stream fills in the rest.
  int c = one_image(partial);
  CHECK_MSG(c >= 0, "no image within %d cycles of the first look", partial);
  CHECK_MSG(!cam.images.empty() && cam.images.back().size() > 1000, "the first image has %u bytes",
            cam.images.empty() ? 0u : (unsigned) cam.images.back().size());
  cam.stream = true;
  c = watch(every_tile, 1000);
  size_t bytes = 0;
  for (auto &t : shot::S.tiles)
    bytes += t ? t->size() : 0;
  printf("  every tile after %d cycles = %.1f s more, %u bytes of tiles\n", c, c * (PBUS_CYCLE_US / 1e6),
         (unsigned) bytes);
  CHECK_MSG(c >= 0, "the tiles didn't all come in 1000 cycles");
  draw_cycles(15);  // the stream's next image, done after the last tile
  double off = screen_off();
  CHECK_MSG(off >= 0 && off < 20, "the camera's image isn't the screen, a tile off by %.1f", off);
  sim_ppm((std::string(outdir) + "/esp_screen_camera.ppm").c_str(), PIC_MAX_W, PIC_MAX_H, sim_jpeg_rgb());
  cam.stream = false;
  // Nothing changed: the next look takes a few cycles
  draw_cycles(10);
  c = one_image(30);
  CHECK_MSG(c >= 0 && c < 15 && screen_off() < 20, "an image of the unchanged screen took %d cycles", c);
  printf("  then of the same screen after %d cycles\n", c);
  // and isn't the last again, which Home Assistant's stream wouldn't pass on, so Chrome, a frame behind, would never
  // show the last
  auto &im = cam.images;
  CHECK_MSG(im.size() >= 2 && im[im.size() - 1] != im[im.size() - 2], "two images of the same screen are the same");
  // The screen changes, the next image shows it. Without the animations, a moving one would be off.
  panel_clear_animation.execute("");
  watch(settled, 500);
  pn_saver(1);
  c = one_image(partial + 300);
  printf("  then of the screensaver after %d cycles\n", c);
  CHECK_MSG(c >= 0, "no image of the screensaver");
  cam.stream = true;  // until the shot is done
  watch([] { return !panel::shot::S.asked; }, 600);
  draw_cycles(15);
  cam.stream = false;
  off = screen_off();
  CHECK_MSG(off >= 0 && off < 20, "the screensaver's image is off by %.1f", off);
  pn_saver(0);
  // The panel restarts: it doesn't know what we have, so the next shot asks for every tile
  restart_panel();
  CHECK_MSG(shot::S.all && !shot::S.asked, "after the panel's restart the next shot isn't of every tile");
  // Nobody looks for KEEP_MS: the tiles are let go, the next look starts over
  draw_cycles((int) (shot::KEEP_MS * 1000 / PBUS_CYCLE_US) + 20);
  CHECK_MSG(shot::S.tiles.empty() && shot::S.tiles.capacity() == 0, "the tiles are kept after a minute without a look");
  // Without the RAM for the tiles it doesn't ask
  size_t free = ram_free;
  ram_free = 16 * 1024;
  cam.single = true;
  draw_cycles(30);
  CHECK_MSG(!shot::S.asked && shot::S.tiles.empty(), "a screenshot without the RAM for it");
  ram_free = free;
  c = watch([] { return !panel_screen.single; }, partial);
  CHECK_MSG(c >= 0, "no image once the RAM was back");
  cam.single = cam.stream = false;
  watch([] { return !panel::shot::S.asked; }, 600);
  // The RAM goes while the tiles of a new picture come: the rest aren't kept, and with it back the next shot is of
  // every tile
  pn_saver(1);
  cam.single = true;
  auto kept = [] {
    auto &t = panel::shot::S.tiles;
    return std::count_if(t.begin(), t.end(), [](auto &b) { return b != nullptr; });
  };
  c = watch([] { return panel::shot::S.asked && panel::shot::S.head_n; }, 300);
  long had = kept();
  ram_free = 4 * 1024;
  draw_cycles(60);
  CHECK_MSG(c >= 0 && shot::S.lost && shot::S.all && kept() <= had + 1, "tiles kept without the RAM: %ld, then %ld",
            had, (long) kept());
  ram_free = free;
  c = watch([] { return !panel_screen.single; }, 600);
  CHECK_MSG(c >= 0, "no image once the RAM was back, with tiles on their way");
  cam.single = cam.stream = false;
  watch([] { return !panel::shot::S.asked; }, 600);
  pn_saver(0);
  loop_between = false;
}

// ---------------------------------------------------------------- the screensaver's lines
static void saver_lines() {
  step("the screensaver's lines (panel_saver_text)");
  // line i as the panel has it, any size if it's empty
  auto has = [](int i, const std::string &text, int size) {
    int s;
    return pn_saver_line(i, &s) == text && (text.empty() || s == size);
  };
  auto sent = [] {  // the ESP's loop queues them, the link takes them over
    run(3);
    run_until(settled, 100);
  };
  panel_saver_text.execute({"22.4 \u00b0C", "Humidity 45 %", "CO\u2082 812 ppm"}, {28, 15});
  sent();
  CHECK_MSG(
      has(0, "22.4 \u00b0C", 28) && has(1, "Humidity 45 %", 16) && has(2, "CO\u2082 812 ppm", 16) && has(3, "", 0),
      "the panel doesn't have the lines, or their sizes");
  panel_saver_text.execute({"22.5 \u00b0C", "Humidity 45 %", "CO\u2082 812 ppm"}, {20});
  sent();
  CHECK_MSG(has(0, "22.5 \u00b0C", 20) && has(1, "Humidity 45 %", 16), "a changed line didn't reach the panel");
  // The panel forgets them when it restarts, the ESP sends them again
  CHECK_MSG(restart_panel() >= 0, "the link didn't come back");
  CHECK_MSG(has(0, "22.5 \u00b0C", 20) && has(2, "CO\u2082 812 ppm", 16), "the lines didn't come back after a restart");
  // More than SAVER_LINES, and one too long, cut where a character starts
  std::string a63(63, 'a');
  size_t from = log_all.size();
  panel_saver_text.execute({"1", "2", "3", a63 + "\u00b0", "5"}, {});
  sent();
  CHECK_MSG(logged("lines at most", from), "no warning about a fifth line");
  CHECK_MSG(has(0, "1", 16) && has(3, a63, 16), "the long line isn't cut before its last character");
  // None brings the temperatures back
  panel_saver_text.execute({}, {});
  sent();
  for (int i = 0; i < SAVER_LINES; i++)
    CHECK_MSG(has(i, "", 0), "line %d is still on the panel", i);
}

// panel_tint_animation and a glide of the light: the ball tinted grey in a dim light, warnings for what can't be,
// and back in its own colours
static void overlay_coloured() {
  step("an animation tinted and lit");
  overlay::show("ball");
  CHECK_MSG(run_until(anims_sent, 3000) >= 0, "the ball not shown: '%s'", panel_animation_status.state.c_str());
  const overlay::Overlay &ball = *overlay::find("ball");
  int p = ball.copies[0].place;
  auto own = [p] {
    for (int f = 0; f < 8; f++)
      if (same_anim("ball", f, p, true))
        return true;
    return false;
  };
  CHECK_MSG(own() && !all_grey(ball, p), "the ball: not in its own colours");
  panel_tint_animation.execute("ball", 0, "#808080");
  panel_glide_animation.execute("ball", 0, "light", 50, 0, "none");
  run(3);
  CHECK_MSG(!own() && look(p).light == 500, "the ball tinted at 50 %% light: in its own colours %d, lit %d", own(),
            look(p).light);
  panel_glide_animation.execute("ball", 0, "light", 100, 0, "none");
  run(3);
  CHECK_MSG(all_grey(ball, p), "the ball tinted grey: not all grey");
  size_t bad = log_all.size();
  panel_tint_animation.execute("ball", 0, "red");
  panel_glide_animation.execute("ball", 0, "light", OVERLAY_LIGHT_MAX + 1, 0, "none");
  run(3);
  CHECK_MSG(logged("panel_tint_animation: 'red' isn't a colour like FF77BB", bad) &&
                logged("panel_glide_animation: light takes 0 to 200", bad) && look(p).light == 1000,
            "no warnings about the tint and the light");
  panel_tint_animation.execute("ball", 0, "none");
  run(3);
  CHECK_MSG(own(), "the ball isn't in its own colours again");
  overlay::clear("");
  run(3);
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
  overlay_moving();
  overlay_copies();
  overlay_turning();
  overlay_pivoted();
  overlay_wandering();
  overlay_waved();
  overlay_turned_down();
  overlay_one_round();
  overlay_two();
  overlay_half_size();
  overlay_not_fitting();
  overlay_cleared_while_made();
  overlay_no_room();
  overlay_repeats();
  overlay_esp_restart();
  overlay_hello_while_made();
  overlay_long_frame();
  overlay_stills();
  overlay_off_screen();
  overlay_shown_again();
  overlay_timing();
  overlay_yaml_script();
  overlay_script_action();
  overlay_without_ram();
  overlay_status();
  overlay_past_pool();
  overlay_told();
  overlay_documented();
  overlay_field();
  overlay_on_its_way();
  met_requests();
  met_reading();
  met_picture();
  met_answers();
  met_forecast_hours();
  met_date_line();
  met_kp();
  live_sky();
  live_land();
  live_town();
  live_days();
  live_rare();
  live_texts();
  live_kept();
  live_cast();
  live_cast_costs();
  live_costs();
  weather_scene();
  picture_cleared_mid_anim();
  end_lost();
  refused();
  overlay_while_away();
  unknown_names();
  cleared_and_shown();
  online_again();
  online_grew();
  online_too_big();
  online_downloads();
  online_fits();
  online_picked();
  space_photos();
  rover_tracks();
  probes_chart();
  picture_no_ram();
  actions_kept();
  number_late();
  esp_restart_newer_setting();
  notes_while_away();
  bootloader_away();
  settings_flash();
  item_icon_typo();
  screen_camera();
  saver_lines();
  overlay_coloured();
  printf("\n%d checks, %d failed\n", check_count, check_fails);
  return check_fails ? 1 : 0;
}
