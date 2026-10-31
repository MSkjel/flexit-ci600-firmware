// Draws the ESP's weather picture from packages/code/met/met_scene.h in the ESP repo for a set of weathers
// and times, as the panel would get it. Writes PPMs to OUTDIR, which make met turns into PNGs and a contact
// sheet. Prints each one's colour count and packed size, and flags a scene that wanted more than 16 colours and
// had some folded into others. Over 14 KB packed, the ESP sends it as a JPEG.
//   build/met_render OUTDIR [complete.json from api.met.no]
#include "met_raster.h"
#include "met_case.h"
#include "sim.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

extern "C" unsigned plink_pack_row(const uint8_t *row, unsigned n, uint8_t *out);

using namespace flexit::panel::met;

struct Case {
  std::string name;
  uint32_t t;
  Weather w;
  bool forecast = false;
  Options o = {};
  double lat = OSLO_LAT, lon = OSLO_LON;
};

// A weather by its symbol and cloud cover, the rest as it mostly is
struct Wx {
  const char *symbol;
  float cloud, rain = 0, temp = 8, low = 0, mid = 0, high = 0, fog = 0, wind = 3, thunder = 0;
};
static Weather wx(const Wx &a) {
  Weather w;
  w.ok = true;
  snprintf(w.symbol, sizeof w.symbol, "%s", a.symbol);
  w.cloud = a.cloud;
  w.rain = a.rain;
  w.temp = a.temp;
  w.cloud_low = a.low;
  w.cloud_mid = a.mid;
  w.cloud_high = a.high;
  w.fog = a.fog;
  w.wind = a.wind;
  w.wind_from = 200;
  w.thunder = a.thunder;
  return w;
}

static void render(const char *dir, const Case &c) {
  static SceneBuf sc(true);
  Scene &s = sc.s;
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
  build(s, w, c.lat, c.lon, c.t, c.forecast, oslo_offset, c.o);
  palette(s, nullptr);
  std::vector<uint32_t> img(W * H);
  std::set<uint8_t> used;
  unsigned packed = 0;
  for (int y = 0; y < H; y++) {
    uint8_t idx[W], r[W / 2], out[W];
    row(s, y, idx);
    for (int x = 0; x < W; x++) {
      img[y * W + x] = s.pal[idx[x]];
      used.insert(idx[x]);
    }
    for (int x = 0; x < W; x += 2)
      r[x / 2] = (uint8_t) (idx[x] << 4 | idx[x + 1]);
    packed += plink_pack_row(r, W / 2, out);
  }
  Pos sun = sun_at(c.lat, c.lon, c.t);
  float ph;
  Pos moon = moon_at(c.lat, c.lon, c.t, &ph);
  char folded[32] = "";
  if (s.folded)
    snprintf(folded, sizeof folded, ", %d more folded in", s.folded);
  printf("%-16s sun %5.1f/%5.1f moon %5.1f/%5.1f phase %.2f  %2zu colours  %u bytes%s\n", c.name.c_str(), sun.el,
         sun.az, moon.el, moon.az, ph, used.size(), packed, folded);
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
      {"01_clear_noon", utc(2026, 6, 21, 11, 0), wx({.symbol = "clearsky_day", .cloud = 3})},
      {"02_fair", utc(2026, 10, 19, 11, 30),
       wx({.symbol = "partlycloudy_day", .cloud = 45, .low = 30, .mid = 15, .high = 10})},
      {"03_sunset", utc(2026, 10, 19, 15, 45),
       wx({.symbol = "fair_day", .cloud = 15, .temp = 7, .low = 10, .mid = 5, .high = 5})},
      {"04_blue_hour", utc(2026, 10, 19, 16, 40),
       wx({.symbol = "fair_night", .cloud = 20, .temp = 5, .low = 5, .mid = 10, .high = 10})},
      {"05_moon_night", utc(2026, 10, 26, 21, 0), wx({.symbol = "clearsky_night", .cloud = 5, .temp = 2})},
      {"06_overcast_night", utc(2026, 10, 19, 21, 0),
       wx({.symbol = "cloudy", .cloud = 100, .temp = 6, .low = 90, .mid = 60, .high = 20})},
      {"07_light_rain", utc(2026, 10, 20, 10, 0),
       wx({.symbol = "lightrain", .cloud = 92, .rain = 0.4f, .temp = 9, .low = 85, .mid = 50, .high = 20})},
      {"08_heavy_rain", utc(2026, 10, 20, 13, 0),
       wx({.symbol = "heavyrain", .cloud = 100, .rain = 6, .temp = 10, .low = 100, .mid = 80, .high = 40, .wind = 9})},
      {"09_snow", utc(2026, 1, 15, 11, 0),
       wx({.symbol = "snow", .cloud = 96, .rain = 1, .temp = -4, .low = 90, .mid = 60, .high = 20})},
      {"10_thunder", utc(2026, 7, 10, 14, 0),
       wx({.symbol = "rainandthunder",
           .cloud = 90,
           .rain = 3,
           .temp = 18,
           .low = 80,
           .mid = 70,
           .high = 30,
           .wind = 5,
           .thunder = 60})},
      {"11_fog", utc(2026, 10, 20, 7, 30),
       wx({.symbol = "fog", .cloud = 30, .temp = 4, .low = 20, .mid = 5, .high = 5, .fog = 90})},
      {"12_cirrus_sunrise", utc(2026, 10, 20, 6, 20),
       wx({.symbol = "fair_day", .cloud = 50, .temp = 3, .mid = 5, .high = 60})},
      {"13_no_weather", utc(2026, 10, 19, 11, 30), Weather()},
      {"15_fair_forecast", utc(2026, 10, 19, 11, 30),
       wx({.symbol = "partlycloudy_day", .cloud = 45, .low = 30, .mid = 15, .high = 10}), true},
      {"16_night_forecast", utc(2026, 10, 26, 21, 0), wx({.symbol = "clearsky_night", .cloud = 5, .temp = -2}), true},
      {"17_rain_forecast", utc(2026, 10, 20, 10, 0),
       wx({.symbol = "lightrain", .cloud = 92, .rain = 0.4f, .temp = 9, .low = 85, .mid = 50, .high = 20}), true},
      {"18_snow_forecast", utc(2026, 1, 15, 11, 0),
       wx({.symbol = "snow", .cloud = 96, .rain = 1, .temp = -4, .low = 90, .mid = 60, .high = 20}), true},
      {"19_sunset_forecast", utc(2026, 10, 19, 15, 45),
       wx({.symbol = "fair_day", .cloud = 15, .temp = 7, .low = 10, .mid = 5, .high = 5}), true},
      // seasons, landscapes, the sky, signs of life
      {"21_spring", utc(2026, 4, 20, 10, 0),
       wx({.symbol = "fair_day", .cloud = 30, .temp = 9, .low = 20, .mid = 10, .high = 10})},
      {"22_winter_bare", utc(2026, 12, 5, 12, 0),
       wx({.symbol = "cloudy", .cloud = 90, .temp = 3, .low = 80, .mid = 40, .high = 10})},
      {"23_snowy_spruces", utc(2026, 1, 15, 12, 0),
       wx({.symbol = "fair_day", .cloud = 20, .temp = -8, .low = 10, .mid = 10, .high = 10})},
      {"24_coast",
       utc(2026, 7, 10, 13, 0),
       wx({.symbol = "fair_day", .cloud = 25, .temp = 19, .low = 15, .mid = 10, .high = 10, .wind = 7}),
       false,
       {G_COAST}},
      {"25_coast_night",
       utc(2026, 9, 10, 21, 30),
       wx({.symbol = "clearsky_night", .cloud = 5, .temp = 11, .high = 5, .wind = 4}),
       false,
       {G_COAST}},
      {"26_mountains",
       utc(2026, 6, 21, 8, 0),
       wx({.symbol = "partlycloudy_day", .cloud = 40, .temp = 12, .low = 30, .mid = 20, .high = 10}),
       false,
       {G_MOUNTAINS}},
      {"27_mountains_winter_night",
       utc(2026, 2, 10, 20, 0),
       wx({.symbol = "clearsky_night", .cloud = 5, .temp = -12}),
       false,
       {G_MOUNTAINS}},
      {"28_city_night",
       utc(2026, 11, 3, 19, 0),
       wx({.symbol = "partlycloudy_night", .cloud = 40, .temp = 4, .low = 30, .mid = 20, .high = 10}),
       false,
       {G_CITY}},
      {"29_city_day", utc(2026, 8, 3, 11, 0), wx({.symbol = "clearsky_day", .cloud = 5, .temp = 24}), false, {G_CITY}},
      {"30_stars_orion",
       utc(2027, 1, 20, 22, 0),
       wx({.symbol = "clearsky_night", .cloud = 2, .temp = -6}),
       false,
       {G_HILLS, A_OFF}},
      {"31_aurora",
       utc(2026, 11, 20, 21, 0),
       wx({.symbol = "clearsky_night", .cloud = 5, .temp = -7}),
       false,
       {G_HILLS, A_ALWAYS},
       69.65,
       18.96},
      {"32_rainbow", utc(2026, 8, 12, 15, 30),
       wx({.symbol = "lightrainshowers_day", .cloud = 55, .rain = 0.6f, .temp = 16, .low = 50, .mid = 20, .high = 10})},
      {"33_smoke_flag",
       utc(2026, 10, 25, 9, 0),
       wx({.symbol = "fair_day", .cloud = 20, .temp = 2, .low = 10, .mid = 10, .high = 10, .wind = 8}),
       false,
       {G_HILLS, A_AUTO, 60}},
      {"34_fireplace",
       utc(2026, 10, 25, 18, 0),
       wx({.symbol = "cloudy", .cloud = 95, .temp = 1, .low = 80, .mid = 40, .high = 20}),
       false,
       {G_HILLS, A_AUTO, 0, true}},
  };
  // holidays, and further down each again in the other landscapes
  std::vector<Case> holidays = {
      {"40_christmas_eve", utc(2026, 12, 24, 16, 0),
       wx({.symbol = "snow", .cloud = 95, .rain = 0.8f, .temp = -3, .low = 90, .mid = 60, .high = 20})},
      {"41_new_year",
       utc(2026, 12, 31, 23, 30),
       wx({.symbol = "clearsky_night", .cloud = 10, .temp = -5}),
       false,
       {G_HILLS, A_OFF}},
      {"42_easter", utc(2026, 4, 5, 11, 0), wx({.symbol = "clearsky_day", .cloud = 10})},
      {"43_may_17", utc(2026, 5, 17, 11, 0),
       wx({.symbol = "fair_day", .cloud = 25, .temp = 14, .low = 20, .mid = 10, .high = 10, .wind = 5})},
      {"44_midsummer", utc(2026, 6, 23, 20, 30), wx({.symbol = "fair_night", .cloud = 10, .temp = 15})},
      {"45_halloween", utc(2026, 10, 31, 19, 0), wx({.symbol = "clearsky_night", .cloud = 10, .temp = 3})},
      {"45b_halloween_day", utc(2026, 10, 31, 12, 0), wx({.symbol = "fair_day", .cloud = 20, .temp = 7})},
      {"46_valentine", utc(2026, 2, 14, 15, 0),
       wx({.symbol = "partlycloudy_day", .cloud = 50, .temp = 1, .low = 40, .mid = 20, .high = 10})},
      {"47_olsok", utc(2026, 7, 29, 12, 0),
       wx({.symbol = "fair_day", .cloud = 20, .temp = 21, .low = 10, .mid = 10, .high = 10, .wind = 4})},
      {"48_election_day", utc(2029, 9, 10, 13, 0),
       wx({.symbol = "partlycloudy_day", .cloud = 50, .temp = 13, .low = 40, .mid = 20, .high = 10, .wind = 6})},
      {"49_kings_day_tromso",
       utc(2026, 2, 21, 11, 0),
       wx({.symbol = "clearsky_day", .cloud = 5, .temp = -9}),
       false,
       {G_HILLS, A_AUTO},
       69.65,
       18.96},
  };
  cases.insert(cases.end(), holidays.begin(), holidays.end());
  if (argc > 2) {  // a real answer, read as the ESP reads it
    std::ifstream in(argv[2]);
    std::stringstream ss;
    ss << in.rdbuf();
    Weather w = read_met(ss.str());
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
  for (const Case &c : holidays)
    for (Ground g : {G_COAST, G_MOUNTAINS, G_CITY}) {
      Case d = c;
      d.name += std::string("_") + GROUNDS[g];
      d.o.ground = g;
      cases.push_back(d);
    }
  for (const Case &c : cases)
    render(dir, c);
}
