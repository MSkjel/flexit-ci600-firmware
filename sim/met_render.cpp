// Draws the ESP's weather picture from packages/code/panel/met_scene.h in the ESP repo for a set of weathers
// and times, as the panel would get it. Writes PPMs to OUTDIR, which make met turns into PNGs and a contact
// sheet. Prints each one's colour count and packed size. Over 16 colours or 14 KB packed, the ESP sends it as
// a JPEG.
//   build/met_render OUTDIR [complete.json from api.met.no]
#include "met_scene.h"
#include "sim.h"
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

extern "C" unsigned plink_pack_row(const uint8_t *row, unsigned n, uint8_t *out);

using namespace flexit::panel::met;

static const double LAT = 59.9139, LON = 10.7522;  // Oslo

static uint32_t utc(int y, int mo, int d, int h, int mi) {
  return days_from_civil(y, mo, d) * 86400u + h * 3600u + mi * 60u;
}

struct Case {
  const char *name;
  uint32_t t;
  Weather w;
  bool forecast = false;
  Options o = {};
  double lat = LAT, lon = LON;
};

// Oslo's UTC offset in seconds. CEST from 01:00 UTC on the last Sunday in March to the same in October
static int offset_at(uint32_t t) {
  int y = 1970 + (int) (t / 31556952u);  // close enough, summer time starts and ends far from New Year
  auto last_sunday = [y](int month) {    // 01:00 UTC on it. 1970-01-01 was a Thursday
    uint32_t d = days_from_civil(y, month, 31);
    return (d - (d + 4) % 7) * 86400u + 3600;
  };
  return t >= last_sunday(3) && t < last_sunday(10) ? 7200 : 3600;
}

static Weather wx(const char *symbol, float cloud, float rain = 0, float temp = 8, float low = -1, float mid = -1,
                  float high = -1, float fog = 0, float wind = 3, float thunder = 0) {
  Weather w;
  w.ok = true;
  snprintf(w.symbol, sizeof w.symbol, "%s", symbol);
  w.cloud = cloud;
  w.rain = rain;
  w.temp = temp;
  w.cloud_low = low;
  w.cloud_mid = mid;
  w.cloud_high = high;
  w.fog = fog;
  w.wind = wind;
  w.wind_from = 200;
  w.thunder = thunder;
  return w;
}

static void render(const char *dir, const Case &c) {
  static Scene s;
  static Sprite sprite[SPRITES];
  static uint8_t ov[OV_H][W / 4];
  s.sprite = sprite;
  s.ov = ov;
  Weather w = c.w;
  if (w.ok && !w.time)
    w.time = c.t / 3600 * 3600;
  if (w.ok && !w.hours) {  // no hourly forecast, so make one up around this weather, a bit warmer and colder
    static const char *const NEXT[] = {"", "partlycloudy_day", "cloudy", "lightrain", "fair_night", ""};
    for (int i = 0; i < 16; i++) {
      const char *sym = NEXT[i / 3 % 6][0] ? NEXT[i / 3 % 6] : w.symbol;
      w.hour[w.hours++] = {w.time + (uint32_t) i * 3600, w.temp + 3 * sinf(i / 4.0f), icon_of(sym)};
    }
  }
  build(s, w, c.lat, c.lon, c.t, c.forecast, offset_at(c.t), c.o);
  std::vector<uint32_t> img(W * H);
  for (int y = 0; y < H; y++)
    row(s, y, &img[y * W]);
  std::map<uint32_t, int> pal;
  for (uint32_t p : img)
    pal.emplace(p, (int) pal.size());
  unsigned packed = 0;
  if (pal.size() <= 16)
    for (int y = 0; y < H; y++) {
      uint8_t r[W / 2], out[W];
      for (int x = 0; x < W; x += 2)
        r[x / 2] = (uint8_t) (pal[img[y * W + x]] << 4 | pal[img[y * W + x + 1]]);
      packed += plink_pack_row(r, W / 2, out);
    }
  Pos sun = sun_at(c.lat, c.lon, c.t);
  float ph;
  Pos moon = moon_at(c.lat, c.lon, c.t, &ph);
  printf("%-16s sun %5.1f/%5.1f moon %5.1f/%5.1f phase %.2f  %2zu colours  %s\n", c.name, sun.el, sun.az, moon.el,
         moon.az, ph, pal.size(), pal.size() <= 16 ? (std::to_string(packed) + " bytes").c_str() : "(too many)");
  sim_ppm((std::string(dir) + "/" + c.name + ".ppm").c_str(), W, H, img.data());
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: met_render OUTDIR [MET_ANSWER.json]\n");
    return 2;
  }
  const char *dir = argv[1];
  sim_mkdir(dir);
  std::vector<Case> cases = {
      {"01_clear_noon", utc(2026, 6, 21, 11, 0), wx("clearsky_day", 3)},
      {"02_fair", utc(2026, 10, 19, 11, 30), wx("partlycloudy_day", 45, 0, 8, 30, 15, 10)},
      {"03_sunset", utc(2026, 10, 19, 15, 45), wx("fair_day", 15, 0, 7, 10, 5, 5)},
      {"04_blue_hour", utc(2026, 10, 19, 16, 40), wx("fair_night", 20, 0, 5, 5, 10, 10)},
      {"05_moon_night", utc(2026, 10, 26, 21, 0), wx("clearsky_night", 5, 0, 2)},
      {"06_overcast_night", utc(2026, 10, 19, 21, 0), wx("cloudy", 100, 0, 6, 90, 60, 20)},
      {"07_light_rain", utc(2026, 10, 20, 10, 0), wx("lightrain", 92, 0.4f, 9, 85, 50, 20)},
      {"08_heavy_rain", utc(2026, 10, 20, 13, 0), wx("heavyrain", 100, 6, 10, 100, 80, 40, 0, 9)},
      {"09_snow", utc(2026, 1, 15, 11, 0), wx("snow", 96, 1, -4, 90, 60, 20)},
      {"10_thunder", utc(2026, 7, 10, 14, 0), wx("rainandthunder", 90, 3, 18, 80, 70, 30, 0, 5, 60)},
      {"11_fog", utc(2026, 10, 20, 7, 30), wx("fog", 30, 0, 4, 20, 5, 5, 90)},
      {"12_cirrus_sunrise", utc(2026, 10, 20, 6, 20), wx("fair_day", 50, 0, 3, 0, 5, 60)},
      {"13_no_weather", utc(2026, 10, 19, 11, 30), Weather()},
      {"15_fair_forecast", utc(2026, 10, 19, 11, 30), wx("partlycloudy_day", 45, 0, 8, 30, 15, 10), true},
      {"16_night_forecast", utc(2026, 10, 26, 21, 0), wx("clearsky_night", 5, 0, -2), true},
      {"17_rain_forecast", utc(2026, 10, 20, 10, 0), wx("lightrain", 92, 0.4f, 9, 85, 50, 20), true},
      {"18_snow_forecast", utc(2026, 1, 15, 11, 0), wx("snow", 96, 1, -4, 90, 60, 20), true},
      {"19_sunset_forecast", utc(2026, 10, 19, 15, 45), wx("fair_day", 15, 0, 7, 10, 5, 5), true},
      // seasons, landscapes, the sky, signs of life
      {"21_spring", utc(2026, 4, 20, 10, 0), wx("fair_day", 30, 0, 9, 20, 10, 10)},
      {"22_winter_bare", utc(2026, 12, 5, 12, 0), wx("cloudy", 90, 0, 3, 80, 40, 10)},
      {"23_snowy_spruces", utc(2026, 1, 15, 12, 0), wx("fair_day", 20, 0, -8, 10, 10, 10)},
      {"24_coast", utc(2026, 7, 10, 13, 0), wx("fair_day", 25, 0, 19, 15, 10, 10, 0, 7), false, {G_COAST}},
      {"25_coast_night", utc(2026, 9, 10, 21, 30), wx("clearsky_night", 5, 0, 11, 0, 0, 5, 0, 4), false, {G_COAST}},
      {"26_mountains", utc(2026, 6, 21, 8, 0), wx("partlycloudy_day", 40, 0, 12, 30, 20, 10), false, {G_MOUNTAINS}},
      {"27_mountains_winter_night", utc(2026, 2, 10, 20, 0), wx("clearsky_night", 5, 0, -12), false, {G_MOUNTAINS}},
      {"28_city_night", utc(2026, 11, 3, 19, 0), wx("partlycloudy_night", 40, 0, 4, 30, 20, 10), false, {G_CITY}},
      {"29_city_day", utc(2026, 8, 3, 11, 0), wx("clearsky_day", 5, 0, 24), false, {G_CITY}},
      {"30_stars_orion", utc(2027, 1, 20, 22, 0), wx("clearsky_night", 2, 0, -6), false, {G_HILLS, A_OFF}},
      {"31_aurora", utc(2026, 11, 20, 21, 0), wx("clearsky_night", 5, 0, -7), false, {G_HILLS, A_ALWAYS}, 69.65, 18.96},
      {"32_rainbow", utc(2026, 8, 12, 15, 30), wx("lightrainshowers_day", 55, 0.6f, 16, 50, 20, 10)},
      {"33_smoke_flag",
       utc(2026, 10, 25, 9, 0),
       wx("fair_day", 20, 0, 2, 10, 10, 10, 0, 8),
       false,
       {G_HILLS, A_AUTO, 60}},
      {"34_fireplace", utc(2026, 10, 25, 18, 0), wx("cloudy", 95, 0, 1, 80, 40, 20), false, {G_HILLS, A_AUTO, 0, true}},
      // holidays
      {"40_christmas_eve", utc(2026, 12, 24, 16, 0), wx("snow", 95, 0.8f, -3, 90, 60, 20)},
      {"41_new_year", utc(2026, 12, 31, 23, 30), wx("clearsky_night", 10, 0, -5), false, {G_HILLS, A_OFF}},
      {"42_easter", utc(2026, 4, 5, 11, 0), wx("clearsky_day", 10, 0, 8)},
      {"43_may_17", utc(2026, 5, 17, 11, 0), wx("fair_day", 25, 0, 14, 20, 10, 10, 0, 5)},
      {"44_midsummer", utc(2026, 6, 23, 20, 30), wx("fair_night", 10, 0, 15)},
      {"45_halloween", utc(2026, 10, 31, 19, 0), wx("clearsky_night", 10, 0, 3)},
      {"46_valentine", utc(2026, 2, 14, 15, 0), wx("partlycloudy_day", 50, 0, 1, 40, 20, 10)},
      {"47_olsok", utc(2026, 7, 29, 12, 0), wx("fair_day", 20, 0, 21, 10, 10, 10, 0, 4)},
      {"48_election_day", utc(2029, 9, 10, 13, 0), wx("partlycloudy_day", 50, 0, 13, 40, 20, 10, 0, 6)},
      {"49_kings_day_tromso",
       utc(2026, 2, 21, 11, 0),
       wx("clearsky_day", 5, 0, -9),
       false,
       {G_HILLS, A_AUTO},
       69.65,
       18.96},
  };
  if (argc > 2) {  // a real answer, read as the ESP reads it
    std::ifstream in(argv[2]);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string body = ss.str();
    std::vector<char> buf(Reader::SIZE + 1);
    Reader rd(buf.data());
    for (size_t at = 0, room; at < body.size(); at += room) {
      char *p = rd.space(room);
      room = std::min(room, body.size() - at);
      memcpy(p, body.data() + at, room);
      if (!rd.got(room))
        break;
    }
    const Weather &w = rd.w;
    if (w.ok) {
      printf("parsed: %s %.1f C, clouds %.0f (low %.0f mid %.0f high %.0f) fog %.0f, rain %.1f mm, wind %.1f from "
             "%.0f, thunder %.0f; %d hours\n",
             w.symbol, w.temp, w.cloud, w.cloud_low, w.cloud_mid, w.cloud_high, w.fog, w.rain, w.wind, w.wind_from,
             w.thunder, w.hours);
      cases.push_back({"14_real", w.time + 1800, w});
      cases.push_back({"20_real_forecast", w.time + 1800, w, true});
    } else {
      printf("parse failed\n");
    }
  }
  // the holidays again in the other landscapes
  static std::deque<std::string> names;  // d.name points into these, and a deque doesn't move them as it grows
  std::vector<Case> more;
  for (const Case &c : cases)
    if (c.name[0] == '4')
      for (Ground g : {G_COAST, G_MOUNTAINS, G_CITY}) {
        Case d = c;
        names.push_back(std::string(c.name) + (g == G_COAST ? "_coast" : g == G_MOUNTAINS ? "_mountains" : "_city"));
        d.name = names.back().c_str();
        d.o.ground = g;
        more.push_back(d);
      }
  cases.insert(cases.end(), more.begin(), more.end());
  for (const Case &c : cases)
    render(dir, c);
}
