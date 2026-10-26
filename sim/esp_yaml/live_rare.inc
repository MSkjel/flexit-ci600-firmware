// The rare encounters, see met_live_rare.h

// The show's own timers go ten minutes on, so an encounter has the time
static void show_later() {
  for (auto &t : live::R.timers)
    if (!t.rare)
      t.at = millis() + 600000;
}

// Encounter k over the show that's on, seconds of it with shots as live/TAG_n.ppm. It comes, takes no more than the
// panel has, and goes again. Where it went.
static Path rare(int k, const char *sprite, const char *tag, int seconds, int shots) {
  size_t from = log_all.size();
  std::string made = std::string("Animation w_") + sprite + ":";
  show_later();
  CHECK_MSG(live::encounter(k), "%s: no room for %s", tag, live::RARE[k].name);
  CHECK_MSG(!live::encounter(k), "%s: two at a time", tag);
  Path p;
  play(seconds, shots, tag, [&] { follow(sprite, p); });
  live_quiet(tag, from);
  CHECK_MSG(logged(made.c_str(), from) && !p.empty(), "%s: %s not shown", tag, sprite);
  CHECK_MSG(live::R.rare.empty() && !anim::find(std::string("w_") + sprite), "%s: %s still on", tag, sprite);
  return p;
}

// The places the panel's animations take
static int places_used() {
  int n = 0;
  for (auto &a : anim::anims)
    for (auto &c : a.copies)
      n += c.used;
  return n;
}

static void live_rare() {
  step("the weather animations' rare encounters (packages/code/met/met_live_rare.h)");
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots
  // rain in a gale leaves a place and RARE_BYTES of the pool for what blows by: a trampoline, an umbrella, a shirt and
  // a cow
  show({"Light rain", "rain", 0, 70, 90, 1.5f, 19, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 0, 10, 0}, "gale");
  CHECK_MSG(places_used() < ANIM_PLACES && anim::used_by_others(nullptr) + live::RARE_BYTES <= ANIM_POOL,
            "rain in a gale: %d places and %u bytes taken", places_used(), (unsigned) anim::used_by_others(nullptr));
  rare(0, "trampoline", "trampoline", 8, 16);
  rare(1, "umbrella", "umbrella", 20, 3);
  rare(2, "shirt", "shirt", 20, 3);
  rare(3, "cow", "cow", 20, 4);
  // in a strong wind short of a gale the rain takes every place, and nothing blows by
  show({"Light rain", "rain", 0, 70, 90, 1.5f, 15, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 0, 10, 0});
  show_later();
  CHECK_MSG(!live::encounter(0) && live::R.rare.empty(), "rain in a strong wind: room for a trampoline");
  CHECK_MSG(!live::RARE[0].fits(live::SEEN), "a strong wind: a trampoline would blow by");
  {
    met::At a = live::SEEN.at;
    a.gust = 24.9f;
    bool strong = live::gale(a);
    a.gust = 25;
    CHECK_MSG(!strong && live::gale(a), "gusts of 25 m/s: a gale %d, of 24.9: %d", live::gale(a), strong);
  }
  // a dry gale over the gulls and under the forecast, blowing to the right
  met::with_forecast = true;
  show({"Gulls over the cabin", "cloudy", 0, 20, 60, 0, 18, 6, 0, utc(2026, 3, 3, 11), met::G_HILLS, 0, 10, 0});
  met::weather.wind_from = 20;
  met::redraw();
  CHECK_MSG(picture("the gale from the north-east", 6000) >= 0 && live::SEEN.at.wdir > 0 && live::SEEN.top > met::OV_H,
            "the gale from the north-east: blows %d, from row %d", live::SEEN.at.wdir, live::SEEN.top);
  rare(0, "trampoline", "trampoline_forecast", 20, 4);
  met::with_forecast = false;
  // nothing while the show has something due: the gull's visit in 20 s
  show({"Gulls over the cabin", "cloudy", 0, 20, 60, 0, 18, 6, 0, utc(2026, 3, 3, 11), met::G_HILLS, 0, 10, 0});
  show_later();
  live::R.timers.front().at = millis() + 20000;
  CHECK_MSG(!live::encounter(0), "a trampoline while the gull's visit is due");
  // a sea serpent off the coast after sunset, and a flying saucer over the cabin on a clear night
  show(
      {"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 8, 15, 19, 15), met::G_COAST, 0, 10, 0});
  CHECK_MSG(live::RARE[4].fits(live::SEEN), "the coast after sunset: no open water for a sea serpent, %d to %d",
            live::SEEN.sea_l, live::SEEN.sea_r);
  rare(4, "serpent", "serpent", 64, 8);
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS, 0, 10, 0});
  CHECK_MSG(live::RARE[5].fits(live::SEEN), "a clear night: no flying saucer");
  rare(5, "ufo", "saucer", 12, 12);
  CHECK_MSG(!live::RARE[7].fits(live::SEEN), "a dark night: a troll");
  // a wolf howling at the full moon from the skyline under it, facing it, and a witch slowing across it
  live::Seen e = live::SEEN;
  CHECK_MSG(live::RARE[8].fits(e) && abs(e.den_x + 8 - e.moon_x) <= 24,
            "the night before the full moon: no wolf, phase %.2f, sitting at %d, the moon at %d", e.moon_phase, e.den_x,
            e.moon_x);
  Path p = rare(8, "wolf", "wolf", 16, 4);
  bool sat = true;
  for (const Look &l : p)
    sat = sat && l.x == e.den_x && l.y == e.den_y && !(l.flip & ANIM_FLIP_X) == (e.moon_x >= e.den_x + 8);
  CHECK_MSG(sat, "the wolf at %d,%d flipped %d, its place %d,%d, the moon at %d", p.empty() ? -1 : p[0].x,
            p.empty() ? -1 : p[0].y, p.empty() ? -1 : p[0].flip, e.den_x, e.den_y, e.moon_x);
  p = rare(9, "witch", "witch", 14, 6);
  int over = 0;
  for (const Look &l : p)
    over += abs(l.x - (e.moon_x - 10)) <= 30;
  CHECK_MSG(abs(row_at(p, e.moon_x - 10) - (e.moon_y - 6)) <= 3 && over >= 27,
            "the witch at row %d where the moon's at %d, %d cycles over it", row_at(p, e.moon_x - 10), e.moon_y, over);
  // the mountains are too steep to sit on
  show(
      {"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_MOUNTAINS, 0, 10, 0});
  CHECK_MSG(live::SEEN.moon_x >= 0 && live::SEEN.den_x < 0, "the mountains: a wolf at %d", live::SEEN.den_x);
  // the wolf only near the full moon and never in town
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 26, 21), met::G_HILLS, 0, 10, 0},
       "half_moon");
  e = live::SEEN;
  CHECK_MSG(e.den_x >= 0 && live::RARE[9].fits(e) && !live::RARE[8].fits(e), "a half moon over the hills: a wolf");
  e.moon_phase = 0.5f;
  bool wild = live::RARE[8].fits(e);
  e.town = true;
  CHECK_MSG(wild && !live::RARE[8].fits(e), "a full moon: a wolf %d, in town %d", wild, live::RARE[8].fits(e));
  // a moon behind the mountains, the town's buildings or the forecast is none to howl at or fly across
  show({"Halloween", "clearsky_night", 0, 0, 0, 0, 2, 4, 0, utc(2026, 10, 31, 22), met::G_MOUNTAINS, 0, 10, 0},
       "moon_behind");
  CHECK_MSG(live::SEEN.moon_x < 0 && !live::RARE[9].fits(live::SEEN), "the moon behind the mountains: at %d",
            live::SEEN.moon_x);
  show({"Halloween", "clearsky_night", 0, 0, 0, 0, 2, 4, 0, utc(2026, 10, 31, 22), met::G_HILLS, 0, 10, 0}, "moon_up");
  CHECK_MSG(live::SEEN.moon_x >= 0, "the moon over the hills: none");
  show({"Halloween", "clearsky_night", 0, 0, 0, 0, 2, 4, 0, utc(2026, 10, 31, 21), met::G_CITY, 0, 10, 0}, "moon_town");
  CHECK_MSG(panel_scene().moon && live::SEEN.moon_x < 0, "the moon behind the buildings: at %d", live::SEEN.moon_x);
  met::with_forecast = true;
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 23), met::G_HILLS, 0, 10, 0},
       "moon_forecast");
  CHECK_MSG(panel_scene().moon_y < met::OV_H + met::MOON_R && live::SEEN.moon_x < 0,
            "the moon under the forecast at row %d: at %d", panel_scene().moon_y, live::SEEN.moon_x);
  met::with_forecast = false;
  // the witch in town under a half moon
  show({"Evening in town", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 26, 19), met::G_CITY, 0, 10, 0});
  CHECK_MSG(!live::RARE[8].fits(live::SEEN) && live::RARE[9].fits(live::SEEN),
            "a half moon over the town: a wolf %d, a witch %d", live::RARE[8].fits(live::SEEN),
            live::RARE[9].fits(live::SEEN));
  rare(9, "witch", "witch_town", 14, 4);
  // a troll plodding along the ridge at dusk with its feet on it, but not in town, nor in the mountains
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 10, 5, 17, 10), met::G_HILLS, 0, 10, 0});
  e = live::SEEN;
  live::Seen town = e;
  town.town = true;
  CHECK_MSG(live::RARE[7].fits(e) && !live::RARE[7].fits(town), "dusk: a troll %d, in town %d, the ridge %d to %d",
            live::RARE[7].fits(e), live::RARE[7].fits(town), e.ridge_l, e.ridge_r);
  p = rare(7, "troll", "troll", 30, 6);
  int off = 0, lo = met::W, hi = 0;
  for (const Look &l : p) {
    off = std::max(off, abs(l.y + 22 - e.ridge.at(l.x + 8)));
    lo = std::min(lo, (int) l.x);
    hi = std::max(hi, (int) l.x);
  }
  CHECK_MSG(off <= 2 && lo >= e.ridge_l && hi + 16 <= e.ridge_r && hi - lo >= 40,
            "the troll %d px off the ridge, from %d to %d, the ridge %d to %d", off, lo, hi, e.ridge_l, e.ridge_r);
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 10, 5, 17, 10), met::G_MOUNTAINS, 0, 10, 0},
       "mountains");
  CHECK_MSG(!live::RARE[7].fits(live::SEEN), "the mountains at dusk: a troll from %d to %d", live::SEEN.ridge_l,
            live::SEEN.ridge_r);
  // the coast's skyline is the sea's horizon, not the shore under it
  show({"Lighthouse and the coastal express", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_COAST,
        0, 10, 0});
  e = live::SEEN;
  CHECK_MSG(e.ridge_r < 120 && (e.den_x < 0 || e.den_x + 16 < 120), "the coast: the ridge to %d, a wolf at %d",
            e.ridge_r, e.den_x);
  // a Viking longship rowing along the open water on a fresh day, when the sailboat's in, but not after sunset
  show({"Drifting clouds", "fair_day", 0, 20, 20, 0, 12, 14, 0, utc(2026, 6, 2, 9), met::G_COAST, 0, 10, 0}, "fresh");
  e = live::SEEN;
  CHECK_MSG(live::RARE[6].fits(e), "a fresh day on the coast: no longship, open water %d to %d", e.sea_l, e.sea_r);
  p = rare(6, "longship", "longship", 44, 6);
  bool afloat = true;
  lo = met::W, hi = 0;
  for (const Look &l : p) {
    afloat = afloat && l.y == e.sea_y - 16 && l.x >= e.sea_l && l.x + 32 <= e.sea_r + 1;
    lo = std::min(lo, (int) l.x);
    hi = std::max(hi, (int) l.x);
  }
  CHECK_MSG(afloat && hi - lo >= 60, "the longship off the water, from %d to %d at row %d, the water %d to %d at %d",
            lo, hi, p.empty() ? -1 : p[0].y, e.sea_l, e.sea_r, e.sea_y);
  e.at.wind = 8;
  CHECK_MSG(!live::RARE[6].fits(e), "a longship with the sailboat out");
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 8, 15, 19, 15), met::G_COAST, 0, 10, 0},
       "dusk");
  CHECK_MSG(live::RARE[4].fits(live::SEEN) && !live::RARE[6].fits(live::SEEN), "after sunset: a longship");
  // the rolls: in a gale something blows by before long, on a calm day nothing ever does
  show({"Light rain", "rain", 0, 70, 90, 1.5f, 19, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 0, 10, 0}, "gale_rolls");
  show_later();
  int rolls = 1;
  while (!live::rare_roll() && rolls < 200)
    rolls++;
  CHECK_MSG(!live::R.rare.empty(), "a gale: nothing in %d rolls", rolls);
  printf("  something blew by on roll %d\n", rolls);
  live::rare_off();
  // and nothing on the days with a show of their own, in a gale as well
  for (auto [want, at] :
       std::initializer_list<std::pair<const char *, uint32_t>>{{"Light rain", utc(2026, 10, 31, 12)},
                                                                {"Light rain", utc(2025, 12, 25, 12)},
                                                                {"Easter", utc(2026, 4, 3, 12)},
                                                                {"Light rain", utc(2026, 5, 17, 12)},
                                                                {"Sankthans", utc(2026, 6, 23, 19, 30)},
                                                                {"Valentine's Day", utc(2026, 2, 14, 12)}}) {
    show({want, "rain", 0, 70, 90, 1.5f, 19, 8, 0, at, met::G_HILLS, 0, 10, 0}, "day_rolls");
    show_later();
    for (rolls = 0; rolls < 200 && live::R.rare.empty(); rolls++)
      live::rare_roll();
    CHECK_MSG(live::RARE[0].fits(live::SEEN) && live::R.rare.empty(), "a gale on %s: %s",
              live::SEEN.at.halloween ? "Halloween" : want, live::R.rare.c_str());
    live::rare_off();
  }
  show({"New Year's Eve", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2025, 12, 31, 19), met::G_COAST, 0, 10, 0});
  CHECK_MSG(live::special_day(live::SEEN.at), "New Year's Eve isn't a special day");
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 7, 18, 0, utc(2026, 6, 10, 11), met::G_HILLS, 0, 10, 0});
  show_later();
  for (rolls = 0; rolls < 2000 && live::R.rare.empty(); rolls++)
    live::rare_roll();
  CHECK_MSG(live::R.rare.empty(), "a calm summer's day: %s", live::R.rare.c_str());
  met::animate(false);
  run(20);
  // what the sprites take of the pool, in any light the encounters come in, from a gale's 0.3 up
  int level = log_level;
  log_level = 0;
  float most = 0;
  for (const char *n :
       {"trampoline", "umbrella", "shirt", "cow", "serpent", "ufo", "longship", "troll", "wolf", "witch"})
    for (float light = 0.3f; light < 1.01f; light += 0.05f) {
      live::Frames sp;
      live::make_sprite(n, sp);
      float m = (float) pool_bytes(live::dimmed(sp, light));
      CHECK_MSG(m > 0 && m <= live::RARE_BYTES, "met_live.h's RARE_BYTES is too small: %s takes %.0f", n, m);
      most = std::max(most, m);
    }
  log_level = level;
  printf("  the encounters' sprites take %.0f of the %d bytes at most\n", most, live::RARE_BYTES);
  CHECK_MSG(most >= live::RARE_BYTES * 0.8f, "met_live.h's RARE_BYTES is too large: %.0f would do", most);
}
