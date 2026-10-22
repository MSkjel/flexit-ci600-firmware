// Just enough of ESPHome to run the panel packages' lambdas on a PC, see gen.py.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// id() returns a reference, like ESPHome's outside lambdas. gen.py turns id(x)-> in the lambdas into id(x).
template<typename T> T &id(T *p) { return *p; }
template<typename T> T &id(T &v) { return v; }

inline uint32_t random_uint32() { return (uint32_t) rand(); }

extern int log_level;        // 0 errors .. 3 info, 4 debug
extern std::string log_all;  // everything logged, for checks
#define ESP_LOG_(lv, c, tag, ...) \
  do { \
    char b_[256]; \
    snprintf(b_, sizeof b_, __VA_ARGS__); \
    log_all += b_; \
    log_all += '\n'; \
    if (log_level >= lv) \
      fprintf(stderr, "  [%c][%s] %s\n", c, tag, b_); \
  } while (0)
#define ESP_LOGE(tag, ...) ESP_LOG_(0, 'E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOG_(1, 'W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_LOG_(3, 'I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ESP_LOG_(4, 'D', tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) \
  do { \
  } while (0)

uint32_t millis();
template<typename T> struct RAMAllocator {  // picture.h asks how much is free before a download
  size_t get_max_free_block_size() const { return 100 * 1024; }
};
void delayMicroseconds(uint32_t us);
uint32_t micros();

struct StringRef {
  std::string s;
  StringRef(const std::string &v) : s(v) {}
  const char *c_str() const { return s.c_str(); }
  size_t size() const { return s.size(); }
  bool empty() const { return s.empty(); }
  bool operator==(const char *o) const { return s == o; }
  operator std::string() const { return s; }
  std::string str() const { return s; }
};

template<typename T> struct Callbacks {
  std::vector<std::function<void(T)>> f;
  void add(std::function<void(T)> g) { f.push_back(std::move(g)); }
  void call(T v) {
    for (auto &g : f)
      g(v);
  }
};

struct Select {
  std::vector<std::string> options;
  size_t idx = 0;
  bool has = false;
  bool optimistic = true;  // publishes what it's set to, else only what set_action publishes
  std::string state;
  std::function<void(StringRef)> set_action;
  Callbacks<size_t> cb;
  Select(std::vector<std::string> o, const char *initial) : options(std::move(o)) {  // initial null: no state
    if (!initial)
      return;
    auto i = index_of(std::string(initial));
    idx = i ? *i : 0;
    has = true;
    state = options[idx];
  }
  std::optional<size_t> index_of(const char *o, size_t n) const {
    for (size_t i = 0; i < options.size(); i++)
      if (options[i] == std::string(o, n))
        return i;
    return {};
  }
  std::optional<size_t> index_of(const std::string &o) const { return index_of(o.c_str(), o.size()); }
  bool has_option(const std::string &o) const { return index_of(o).has_value(); }
  std::optional<size_t> active_index() const { return has ? std::optional<size_t>(idx) : std::nullopt; }
  StringRef current_option() const { return StringRef(has ? state : std::string()); }
  void publish_state(size_t i) {
    if (i >= options.size())
      return;
    idx = i;
    has = true;
    state = options[i];
    cb.call(i);
  }
  void publish_state(const std::string &o) {
    auto i = index_of(o);
    if (i)
      publish_state(*i);
  }
  void publish_state(const char *o) { publish_state(std::string(o)); }
  template<typename F> void add_on_state_callback(F &&f) { cb.add(std::forward<F>(f)); }
  struct Call {
    Select *s;
    size_t i = 0;
    Call &set_index(size_t v) {
      i = v;
      return *this;
    }
    void perform() { s->control(i); }
  };
  Call make_call() { return Call{this}; }
  void control(size_t i) {  // like a template select
    if (set_action)
      set_action(StringRef(options[i]));
    if (optimistic)
      publish_state(i);
  }
};

struct NumberTraits {
  float min, max, step;
  float get_min_value() const { return min; }
  float get_max_value() const { return max; }
  float get_step() const { return step; }
};

struct Number {
  NumberTraits traits;
  std::string unit;
  float state;
  bool optimistic = true;
  std::function<void(float)> set_action;
  Callbacks<float> cb;
  Number(float lo, float hi, float st, float initial) : traits{lo, hi, st}, state(initial) {}
  bool has_state() const { return !std::isnan(state); }
  StringRef get_unit_of_measurement_ref() const { return StringRef(unit); }
  void publish_state(float v) {
    state = v;
    cb.call(v);
  }
  template<typename F> void add_on_state_callback(F &&f) { cb.add(std::forward<F>(f)); }
  struct Call {
    Number *n;
    float v = 0;
    Call &set_value(float x) {
      v = x;
      return *this;
    }
    void perform() { n->control(v); }
  };
  Call make_call() { return Call{this}; }
  void control(float v) {
    v = std::max(traits.min, std::min(traits.max, v));
    if (set_action)
      set_action(v);
    if (optimistic)
      publish_state(v);
  }
};

struct Switch {
  bool state = false;
  bool refuse = false;  // won't turn on, like fireplace with the unit stopped
  std::function<void()> on_action, off_action;
  Callbacks<bool> cb;
  void publish_state(bool v) {
    bool changed = v != state;
    state = v;
    if (changed)
      cb.call(v);
  }
  template<typename F> void add_on_state_callback(F &&f) { cb.add(std::forward<F>(f)); }
  void turn_on() {
    if (on_action)
      on_action();
    if (!refuse)
      publish_state(true);
  }
  void turn_off() {
    if (off_action)
      off_action();
    publish_state(false);
  }
};

struct Button {
  std::function<void()> on_press;
  int presses = 0;
  void press() {
    presses++;
    if (on_press)
      on_press();
  }
};

struct Sensor {
  float state = NAN;
  bool has = false;
  int8_t decimals = -1;  // accuracy_decimals, -1 = not set
  std::string unit;
  Callbacks<float> cb;
  bool has_state() const { return has; }
  int8_t get_accuracy_decimals() const { return decimals; }
  StringRef get_unit_of_measurement_ref() const { return StringRef(unit); }
  void publish_state(float v) {
    state = v;
    has = true;
    cb.call(v);
  }
  template<typename F> void add_on_state_callback(F &&f) { cb.add(std::forward<F>(f)); }
};

struct BinarySensor {
  bool state = false, has = false;
  Callbacks<bool> cb;
  bool has_state() const { return has; }
  void publish_state(bool v) {
    state = v;
    has = true;
    cb.call(v);
  }
  template<typename F> void add_on_state_callback(F &&f) { cb.add(std::forward<F>(f)); }
};

struct TextSensor {
  std::string state;
  bool has = false;
  Callbacks<std::string> cb;
  bool has_state() const { return has; }
  void publish_state(const std::string &v) {
    state = v;
    has = true;
    cb.call(v);
  }
  template<typename F> void add_on_state_callback(F &&f) { cb.add(std::forward<F>(f)); }
};

struct Event {
  std::string last;
  int count = 0;
  void trigger(const std::string &t) {
    last = t;
    count++;
  }
};

inline int32_t tz_offset_s;  // local time minus UTC, what ESPHome gets from the time zone
struct ESPTime {
  int year, month, day_of_month, hour, minute, second;
  time_t timestamp = 1792359015;  // 2026-10-18 21:30:15 UTC
  bool is_valid() const { return year >= 2019; }
  static int32_t timezone_offset() { return tz_offset_s; }
};

struct Time {
  ESPTime t{2026, 10, 18, 21, 30, 15};
  ESPTime now() { return t; }
};

struct Color {
  uint8_t r, g, b, w = 255;  // w: alpha
};

struct Animation {  // an animated GIF's frames
  int w = 0, h = 0, frame = 0;
  std::vector<std::vector<uint32_t>> frames;  // 0xAARRGGBB
  int get_width() const { return w; }
  int get_height() const { return h; }
  uint32_t get_animation_frame_count() const { return (uint32_t) frames.size(); }
  void set_frame(int f) { frame = f; }
  Color get_pixel(int x, int y) const {
    uint32_t c = frames[frame][(size_t) y * w + x];
    return Color{(uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c, (uint8_t) (c >> 24)};
  }
};

struct Image {
  int w = 0, h = 0;
  std::vector<uint32_t> px;  // 0xRRGGBB
  int get_width() const { return w; }
  int get_height() const { return h; }
  Color get_pixel(int x, int y) const {
    uint32_t c = px[(size_t) y * w + x];
    return Color{(uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c};
  }
};

namespace flexit_modbus_server {
enum HoldingRegisterIndex : uint16_t { REG_CMD_TEMPERATURE_SETPOINT = 0x0C };
}

// The ESP's Modbus server, with the registers, coils and hooks the panel's link uses.
struct Server {
  uint16_t hold[0x200] = {};
  bool coil[0x200] = {};
  std::vector<std::pair<uint16_t, uint16_t>> cmds;  // every send_cmd() to a CU60 register
  uint16_t read_holding_register(flexit_modbus_server::HoldingRegisterIndex r) { return hold[r]; }
  void send_cmd(flexit_modbus_server::HoldingRegisterIndex r, uint16_t v) {
    hold[r] = v;
    coil[r] = true;
    cmds.push_back({(uint16_t) r, v});
  }

  // For the panel's link. The harness plays the bus and feeds the panel's answers back.
  std::vector<std::function<void()>> status_cbs, read_cbs;
  std::vector<std::function<void(const uint8_t *, size_t)>> frame_cbs;
  uint32_t age_us = UINT32_MAX, min_age_us = UINT32_MAX;
  std::vector<uint8_t> sent;  // what send_frame put on the bus, CRC included
  void add_on_status_callback(std::function<void()> &&f) { status_cbs.push_back(std::move(f)); }
  void add_on_frame_callback(std::function<void(const uint8_t *, size_t)> &&f) { frame_cbs.push_back(std::move(f)); }
  void add_on_read_callback(std::function<void()> &&f) { read_cbs.push_back(std::move(f)); }
  size_t (*frame_length)(const uint8_t *, size_t) = nullptr;  // how the splitter sizes the link's frames
  void set_frame_length(uint8_t, size_t (*f)(const uint8_t *, size_t)) { frame_length = f; }
  uint32_t frame_age_us() { return 300; }
  uint32_t quiet_min_us() { return 1000; }
  uint32_t status_age_us() { return age_us; }
  bool synced_ = true;  // a CU60 status broadcast has been heard since boot
  bool synced() const { return synced_; }
  uint32_t status_min_age_us() { return min_age_us; }
  bool updating() const { return false; }    // true while a panel update from update.h owns the bus, never here
  void send_frame(uint8_t *f, size_t len) {  // appends the Modbus CRC like the library's sendResponse()
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
      c ^= f[i];
      for (int k = 0; k < 8; k++)
        c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
    }
    f[len] = c & 0xFF;
    f[len + 1] = c >> 8;
    sent.assign(f, f + len + 2);
  }
};
extern Server *server;
