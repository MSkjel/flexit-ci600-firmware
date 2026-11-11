// What the weather picture's tests share, after met_scene.h: Oslo and its clock, a scene with buffers of its own,
// and MET's answer read as the ESP reads it.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static const double OSLO_LAT = 59.9139, OSLO_LON = 10.7522;

// UTC seconds since 1970
inline uint32_t utc(int y, int mo, int d, int h = 0, int mi = 0) { return flexit::panel::unix_of(y, mo, d, h, mi); }

// Oslo's UTC offset in seconds. CEST from 01:00 UTC on the last Sunday in March to the same in October.
inline int oslo_offset(uint32_t t) {
  int y = 1970 + (int) (t / 31556952u);  // close enough, summer time starts and ends far from New Year
  auto last_sunday = [y](int month) {    // 01:00 UTC on it. 1970-01-01 was a Thursday
    uint32_t d = flexit::panel::days_from_civil(y, month, 31);
    return (d - (d + 4) % 7) * 86400u + 3600;
  };
  return t >= last_sunday(3) && t < last_sunday(10) ? 7200 : 3600;
}

// A scene and what it draws into
struct SceneBuf {
  flexit::panel::met::Scene s;
  flexit::panel::met::Stamps stamps;
  flexit::panel::met::Land land;
  SceneBuf() {
    s.stamps = &stamps;
    s.land = &land;
  }
  SceneBuf(const SceneBuf &) = delete;
};

// MET's answer as the ESP reads it, in pieces of at most chunk bytes. *stopped: the reader had what it needs first.
inline flexit::panel::met::Weather read_met(const std::string &body, size_t chunk = SIZE_MAX, bool *stopped = nullptr) {
  flexit::panel::met::Forecast f;
  bool stop = false;
  for (size_t at = 0, n; at < body.size() && !stop; at += n) {
    n = std::min(chunk, body.size() - at);
    stop = !f.feed(body.data() + at, n);
  }
  if (stopped)
    *stopped = stop;
  return f.w;
}
