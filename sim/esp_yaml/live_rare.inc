// The rare encounters, see met_live_rare.h

// Encounter k over the acts that are on, seconds of it with shots as live/TAG_n.ppm. It comes, takes no more than the
// panel has, and goes again. Where it went.
static Path rare(int k, const char *sprite, const char *tag, int seconds, int shots, bool lamp = false) {
  size_t from = log_all.size();
  std::string made = std::string("Animation w_") + sprite + ":";
  for (auto &t : live::R.timers)  // nothing else comes meanwhile, rare or not
    t.at = millis() + 600000;
  CHECK_MSG(live::encounter(k), "%s: no room for %s", tag, live::RARE[k].name);
  CHECK_MSG(!live::encounter(k), "%s: two at a time", tag);
  Path p;
  int dark = 0, lit = 0;  // its LAMP in a dim light: drawn as it is
  play(seconds, shots, tag, [&] {
    follow(sprite, p);
    dark += out_of_light();
    const overlay::Overlay *a = overlay::find(live::key(sprite));
    Look l;
    if (lamp && a && where(sprite, l) && l.light < 1000)
      lit += draws(*a, a->copies[0].place, live::LAMP, 1).first > 0;
  });
  CHECK_MSG(!dark && (!lamp || lit), "%s: %d cycles in another light than the scene's, its lamp lit in %d", tag, dark,
            lit);
  live_quiet(tag, from);
  CHECK_MSG(logged(made.c_str(), from) && !p.empty(), "%s: %s not shown", tag, sprite);
  CHECK_MSG(live::R.rare.empty() && !overlay::find(std::string("w_") + sprite), "%s: %s still on", tag, sprite);
  return p;
}

// The places the panel's animations take
static int places_used() {
  int n = 0;
  for (auto &a : overlay::overlays)
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
  CHECK_MSG(places_used() < OVERLAY_PLACES && overlay::used_by_others(nullptr) + live::RARE_BYTES <= OVERLAY_POOL,
            "rain in a gale: %d places and %u bytes taken", places_used(), (unsigned) overlay::used_by_others(nullptr));
  rare(0, "trampoline", "trampoline", 8, 16);
  rare(1, "umbrella", "umbrella", 20, 3);
  rare(2, "shirt", "shirt", 20, 3);
  rare(3, "cow", "cow", 20, 4);
  // in a strong wind short of a gale nothing blows by, though the rain leaves room for it
  show({"Light rain", "rain", 0, 70, 90, 1.5f, 15, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 0, 10, 0});
  show_later();
  CHECK_MSG(live::rare_room(), "rain in a strong wind: no room for a trampoline");
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
  show({"Crows + About the cabin", "cloudy", 0, 20, 60, 0, 18, 6, 0, utc(2026, 3, 3, 11), met::G_HILLS, 0, 10, 0});
  met::weather.wind_from = 20;
  met::redraw();
  CHECK_MSG(picture("the gale from the north-east", 6000) >= 0 && live::SEEN.at.wdir > 0 && live::SEEN.top > met::OV_H,
            "the gale from the north-east: blows %d, from row %d", live::SEEN.at.wdir, live::SEEN.top);
  rare(0, "trampoline", "trampoline_forecast", 20, 4);
  met::with_forecast = false;
  // nothing while a visitor is here: the squirrel on the roof
  show({"Crows + About the cabin", "cloudy", 0, 20, 60, 0, 18, 6, 0, utc(2026, 3, 3, 11), met::G_HILLS, 0, 10, 0});
  show_later();
  CHECK_MSG(visit("About the cabin", 1) && !live::rare_room() && !live::encounter(0),
            "a trampoline while the squirrel visits");
  // a sea serpent off the coast after sunset, and a flying saucer over the cabin on a clear night
  show({"Gulls + About the cabin", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 8, 15, 19, 15), met::G_COAST, 0, 10,
        0});
  CHECK_MSG(live::RARE[4].fits(live::SEEN), "the coast after sunset: no open water for a sea serpent, %d to %d",
            live::SEEN.sea_l, live::SEEN.sea_r);
  rare(4, "serpent", "serpent", 64, 8);
  show({"The stars + By the cabin at night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS,
        0, 10, 0});
  CHECK_MSG(live::RARE[5].fits(live::SEEN), "a clear night: no flying saucer");
  rare(5, "ufo", "saucer", 12, 12);
  CHECK_MSG(!live::RARE[7].fits(live::SEEN), "a dark night: a troll");
  // a wolf up from behind the hill onto the skyline under it, howling at the full moon, facing it, and back down, and
  // a witch slowing across it
  live::Seen e = live::SEEN;
  CHECK_MSG(live::RARE[8].fits(e) && abs(e.den_x + 8 - e.moon_x) <= 24,
            "the night before the full moon: no wolf, phase %.2f, sitting at %d, the moon at %d", e.moon_phase, e.den_x,
            e.moon_x);
  Path p = rare(8, "wolf", "wolf", 16, 4);
  bool on = !p.empty() && p.front().hidden && p.back().hidden;
  int sat = 0;
  for (const Look &l : p) {
    on = on && l.x == e.den_x && l.y >= e.den_y && l.y <= e.den_y + 15 && l.depth == e.den_depth &&
         !(l.flip & OVERLAY_VIEW_FLIP_X) == (e.moon_x >= e.den_x + 8);
    sat += l.y == e.den_y && !l.hidden;
  }
  CHECK_MSG(on && sat * 4 >= (int) p.size() * 3,
            "the wolf at %d,%d flipped %d, %d of %zu cycles up, its place %d,%d, "
            "the moon at %d",
            p.empty() ? -1 : p[0].x, p.empty() ? -1 : p[0].y, p.empty() ? -1 : p[0].flip, sat, p.size(), e.den_x,
            e.den_y, e.moon_x);
  p = rare(9, "witch", "witch", 14, 6);
  int over = 0;
  for (const Look &l : p)
    over += abs(l.x - (e.moon_x - 10)) <= 30;
  CHECK_MSG(abs(row_at(p, e.moon_x - 10) - (e.moon_y - 6)) <= 3 && over >= 27,
            "the witch at row %d where the moon's at %d, %d cycles over it", row_at(p, e.moon_x - 10), e.moon_y, over);
  // the mountains are too steep to sit on
  show({"The stars + By the cabin at night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21),
        met::G_MOUNTAINS, 0, 10, 0});
  CHECK_MSG(live::SEEN.moon_x >= 0 && live::SEEN.den_x < 0, "the mountains: a wolf at %d", live::SEEN.den_x);
  // the wolf only near the full moon and never in town
  show({"The stars + By the cabin at night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 26, 21), met::G_HILLS,
        0, 10, 0},
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
  show({"The stars + By the cabin at night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 23), met::G_HILLS,
        0, 10, 0},
       "moon_forecast");
  CHECK_MSG(panel_scene().moon_y < met::OV_H + met::MOON_R && live::SEEN.moon_x < 0,
            "the moon under the forecast at row %d: at %d", panel_scene().moon_y, live::SEEN.moon_x);
  met::with_forecast = false;
  // the witch in town under a half moon
  show({"Evening in town + The antenna's light", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 26, 19),
        met::G_CITY, 0, 10, 0});
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
  p = rare(7, "troll", "troll", 30, 6, true);
  int off = 0, lo = met::W, hi = 0;
  for (const Look &l : p) {
    off = std::max(off, abs(l.y + 22 - e.ridge.at(l.x + 8)));
    lo = std::min(lo, (int) l.x);
    hi = std::max(hi, (int) l.x);
  }
  CHECK_MSG(off <= 2 && lo >= e.ridge_l && hi + 16 <= e.ridge_r && hi - lo >= 40,
            "the troll %d px off the ridge, from %d to %d, the ridge %d to %d", off, lo, hi, e.ridge_l, e.ridge_r);
  // and a lynx out of the forest from behind the spruce at its edge, short of the cabin, and back into it
  CHECK_MSG(live::RARE[14].fits(e), "dusk: no lynx, the forest's edge at %d", e.edge.x);
  int x0 = live::in_forest(e, "lynx", live::LYNX_UP);
  p = rare(14, "lynx", "lynx", 30, 4);
  lo = met::W, hi = 0;
  bool near = !p.empty() && p.front().hidden && p.back().hidden && p.front().x == x0 && p.back().x <= x0 + 3;
  int seen = 0;
  for (const Look &l : p) {
    lo = std::min(lo, (int) l.x);
    hi = std::max(hi, (int) l.x);
    near = near && l.depth == met::D_NEAR_ON && (!l.hidden || l.x < e.edge.x);
    seen += !l.hidden;
  }
  CHECK_MSG(near && seen * 2 >= (int) p.size() && lo == x0 && hi + 16 < e.cabin_x + met::EAVES_L && hi - lo >= 13,
            "the lynx from %d to %d, behind the spruce at %d, the forest to %d, the cabin at %d", lo, hi, x0, e.edge.x,
            e.cabin_x);
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 10, 5, 17, 10), met::G_MOUNTAINS, 0, 10, 0},
       "mountains");
  CHECK_MSG(!live::RARE[7].fits(live::SEEN), "the mountains at dusk: a troll from %d to %d", live::SEEN.ridge_l,
            live::SEEN.ridge_r);
  // the coast's skyline is the sea's horizon, not the shore under it
  show({"The lighthouse + Light on the water + The stars", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21),
        met::G_COAST, 0, 10, 0});
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
    afloat = afloat && l.y == e.sea_y - 16 && l.x >= e.sea_l && l.x + 32 <= e.sea_r + 1 && l.depth == met::D_SEA;
    lo = std::min(lo, (int) l.x);
    hi = std::max(hi, (int) l.x);
  }
  CHECK_MSG(afloat && hi - lo >= 60, "the longship off the water, from %d to %d at row %d, the water %d to %d at %d",
            lo, hi, p.empty() ? -1 : p[0].y, e.sea_l, e.sea_r, e.sea_y);
  e.at.wind = 8;
  CHECK_MSG(!live::RARE[6].fits(e), "a longship with the sailboat out");
  show({"Gulls + About the cabin", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 8, 15, 19, 15), met::G_COAST, 0, 10,
        0},
       "dusk");
  CHECK_MSG(live::RARE[4].fits(live::SEEN) && !live::RARE[6].fits(live::SEEN), "after sunset: a longship");
  // the huldra at the forest's edge on a summer evening with the sun just down, but not by the sea
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 7, 10, 21), met::G_HILLS, 0, 10, 0},
       "summer_evening");
  e = live::SEEN;
  CHECK_MSG(live::RARE[10].fits(e), "a summer evening: no huldra, the sun at %.1f", e.at.sun_el);
  p = rare(10, "huldra", "huldra", 14, 4);
  CHECK_MSG(!p.empty() && !p.front().hidden && p.back().hidden && p.back().x < e.edge.x,
            "the huldra from %d, hidden %d, to %d, hidden %d, the forest to %d", p.empty() ? -1 : p.front().x,
            !p.empty() && p.front().hidden, p.empty() ? -1 : p.back().x, !p.empty() && p.back().hidden, e.edge.x);
  e.coast = true;
  CHECK_MSG(!live::RARE[10].fits(e), "the huldra by the sea");
  // a nisse on his way to the cabin over the snow on a December night before Christmas Eve
  show({"Firelight in the window + Smoke from the chimney + By the cabin at night", "cloudy", 0, 70, 0, 0, 3, -5, 0,
        utc(2026, 12, 10, 18), met::G_HILLS, 90, 10, 0},
       "advent");
  e = live::SEEN;
  CHECK_MSG(live::RARE[11].fits(e), "a December night: no nisse, snow lying %d", e.at.white);
  p = rare(11, "nisse", "nisse", 16, 4, true);
  CHECK_MSG(!p.empty() && !p.front().hidden && p.back().hidden, "the nisse hidden %d at %d,%d, %d at %d,%d",
            !p.empty() && p.front().hidden, p.empty() ? -1 : p.front().x, p.empty() ? -1 : p.front().y,
            !p.empty() && p.back().hidden, p.empty() ? -1 : p.back().x, p.empty() ? -1 : p.back().y);
  e.at.day = 24;
  CHECK_MSG(!live::RARE[11].fits(e), "a nisse on Christmas Eve, which has its own");
  // draugen on the open water on a windy night
  show({"The lighthouse + Light on the water + The stars", "cloudy", 0, 0, 60, 0, 12, 6, 0, utc(2026, 11, 10, 20),
        met::G_COAST, 0, 10, 0},
       "windy");
  CHECK_MSG(live::RARE[12].fits(live::SEEN), "a windy night on the coast: no draugen, open water %d to %d",
            live::SEEN.sea_l, live::SEEN.sea_r);
  rare(12, "draugen", "draugen", 46, 4);
  // orcas off the coast up north in the winter, but not in the south
  show({"Gulls + About the cabin", "cloudy", 0, 0, 60, 0, 3, 2, 0, utc(2026, 1, 12, 11), met::G_COAST, 0, 10, 0, 69.65,
        18.96},
       "tromso");
  e = live::SEEN;
  CHECK_MSG(live::RARE[13].fits(e), "Tromso in January: no orcas, open water %d to %d, light %.2f", e.sea_l, e.sea_r,
            e.at.light);
  rare(13, "orcas", "orcas", 16, 4);
  e.at.north = false;
  CHECK_MSG(!live::RARE[13].fits(e), "orcas in the south");
  // a sea eagle's catch off the coast on a calm day
  show({"The sailboat + Light on the water + Glitter on the water + Eiders + Off the coast", "clearsky_day", 0, 0, 0, 0,
        3, 14, 0, utc(2026, 9, 20, 15), met::G_COAST, 0, 10, 0},
       "catch");
  CHECK_MSG(live::RARE[15].fits(live::SEEN), "a calm day on the coast: no sea eagle");
  rare(15, "seaeaglecatch", "seaeaglecatch", 14, 6);
  // a ski jumper down onto the snowy hills beside the forest
  show({"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 3, -5, 0, utc(2026, 2, 7, 11), met::G_HILLS, 0,
        10, 0},
       "jump");
  CHECK_MSG(live::RARE[16].fits(live::SEEN), "a snowy day on the hills: no ski jumper, landing at %d",
            live::SEEN.jump_x);
  rare(16, "skijumper", "skijumper", 12, 8);
  // and the next one, who jumps after Wirkola: the jumper twice, the second head over heels down the hill
  e = live::SEEN;
  CHECK_MSG(live::RARE[27].fits(e), "a snowy day on the hills: nobody jumps after Wirkola");
  p = rare(27, "skijumper", "wirkola", 22, 10);
  int turned = 0, starts = 0;
  for (size_t i = 0; i < p.size(); i++) {
    turned += p[i].deg != 0;
    starts += i && p[i].x > p[i - 1].x + 100;
  }
  CHECK_MSG(starts == 1 && turned > 10, "after Wirkola: %d more jumps, %d cycles turned", starts, turned);
  // what people say. Trollkjerringer come down in a downpour on the land, onto the near hill, on their feet, but not in
  // rain that's lighter, nor in town
  show({"", "heavyrain", 0, 70, 90, 6, 5, 12, 0, utc(2026, 9, 8, 12), met::G_HILLS, 0, 10, 0}, "pouring");
  e = live::SEEN;
  CHECK_MSG(live::RARE[17].fits(e), "a downpour: no trollkjerringer, pouring %d", e.at.pouring);
  p = rare(17, "hag", "hag", 17, 8);
  int stood = 0, fell = 0;
  for (const Look &l : p) {
    stood += l.opa && !l.deg && l.y == e.hill.at(l.x + 5) - 11;
    fell += l.opa && l.deg != 0;
  }
  CHECK_MSG(stood >= 20 && fell >= 40, "it rains trollkjerringer: %d cycles on their feet on the hill, %d falling",
            stood, fell);
  town = e;
  town.town = true;
  e.at.pouring = false;
  CHECK_MSG(!live::RARE[17].fits(e) && !live::RARE[17].fits(town), "trollkjerringer in lighter rain or in town");
  show({"", "rain", 0, 70, 90, 4, 5, 12, 0, utc(2026, 9, 8, 12), met::G_HILLS, 0, 10, 0}, "rain_4mm");
  CHECK_MSG(!live::SEEN.at.pouring, "4 mm an hour is a downpour");
  // Tor and his goats across the sky in thunder
  show({"", "heavyrainandthunder", 0, 90, 100, 6, 5, 20, 0, utc(2026, 7, 10, 15), met::G_HILLS, 0, 10, 0}, "thunder");
  CHECK_MSG(live::RARE[18].fits(live::SEEN), "a thunderstorm: no Tor");
  p = rare(18, "tor", "tor", 11, 6);
  lo = met::W, hi = 0;
  for (const Look &l : p) {
    lo = std::min(lo, (int) l.x);
    hi = std::max(hi, (int) l.x);
  }
  CHECK_MSG(lo < 0 && hi > met::W - 36, "Tor from %d to %d", lo, hi);
  // a pig flies on 1 April and no other day
  show({"", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 4, 1, 11), met::G_HILLS, 0, 10, 0}, "april");
  e = live::SEEN;
  CHECK_MSG(live::RARE[19].fits(e), "1 April: no pig flies");
  rare(19, "pig", "pig", 18, 5);
  e.at.day = 2;
  CHECK_MSG(!live::RARE[19].fits(e), "a pig flies on 2 April");
  // a black cat across the near hill on Friday the 13th, its feet on it
  show({"", "clearsky_day", 0, 0, 0, 0, 3, 14, 0, utc(2026, 11, 13, 11), met::G_HILLS, 0, 10, 0}, "friday13");
  e = live::SEEN;
  CHECK_MSG(live::RARE[20].fits(e), "Friday the 13th: no black cat, weekday %d", e.at.weekday);
  p = rare(20, "blackcat", "blackcat", 21, 5);
  off = 0, lo = met::W, hi = 0;
  for (const Look &l : p) {
    if (l.x >= 0 && l.x + 13 < met::W)
      off = std::max(off, abs(l.y + 7 - e.hill.at(l.x + 6)));
    lo = std::min(lo, (int) l.x);
    hi = std::max(hi, (int) l.x);
  }
  CHECK_MSG(off <= 2 && lo < 0 && hi > met::W - 13, "the black cat %d px off the hill, from %d to %d", off, lo, hi);
  e.at.day = 14;
  CHECK_MSG(!live::RARE[20].fits(e), "a black cat on Saturday the 14th");
  // owls in the moss in fog, by the forest's edge
  show({"", "fog", 0, 0, 90, 0, 2, 8, 80, utc(2026, 10, 5, 9), met::G_HILLS, 0, 10, 0}, "foggy");
  e = live::SEEN;
  CHECK_MSG(live::RARE[22].fits(e), "fog: no owls in the moss, the forest's edge at %d", e.edge.x);
  p = rare(22, "owlperch", "owl_moss", 24, 4);
  CHECK_MSG(!p.empty() && p[0].x == e.edge.x + 4 && p[0].depth == met::D_NEAR_ON, "the owl in the moss at %d",
            p.empty() ? -1 : p[0].x);
  e.at.fog = 0;
  CHECK_MSG(!live::RARE[22].fits(e), "owls in the moss without fog");
  // the cow over the moon on a moonlit night: over its top, from the skyline and back to it
  show({"The stars + By the cabin at night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS,
        0, 10, 0},
       "moonlit");
  e = live::SEEN;
  CHECK_MSG(live::RARE[21].fits(e), "a moonlit night: no cow over the moon at %d,%d", e.moon_x, e.moon_y);
  p = rare(21, "cow", "cow_moon", 7, 7);
  int high = met::H;
  for (const Look &l : p)
    if (abs(l.x + 10 - e.moon_x) <= 6)
      high = std::min(high, (int) l.y);
  CHECK_MSG(high + 12 <= e.moon_y - met::MOON_R && high >= e.top, "the cow over the moon at row %d, the moon at %d",
            high, e.moon_y);
  // a meteor shower on the Perseids' night, nine of them, and none the week after
  show({"", "clearsky_night", 0, 0, 0, 0, 2, 12, 0, utc(2026, 8, 12, 23), met::G_HILLS, 0, 10, 0}, "perseids");
  e = live::SEEN;
  CHECK_MSG(live::RARE[24].fits(e), "the Perseids: no meteor shower, %d.%d, dark %d", e.at.day, e.at.month, e.at.dark);
  p = rare(24, "shooting", "meteors", 36, 8);
  int stars = !p.empty();  // each from where it likes, burning down along its 40 px
  for (size_t i = 1; i < p.size(); i++)
    stars += abs(p[i].y - p[i - 1].y) > 16 || abs(p[i].x - p[i - 1].x) > 44;
  CHECK_MSG(stars >= live::METEORS - 2 && stars <= live::METEORS, "the meteor shower: %d falling stars", stars);
  e.at.day = 20;
  CHECK_MSG(!live::RARE[24].fits(e), "a meteor shower on 20 August");
  // the fox along the ridge under the northern lights
  show({"The stars + The northern lights", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 15, 21, 10),
        met::G_HILLS, 0, 10, 0, 69.65, 18.96},
       "fox_fires");
  e = live::SEEN;
  CHECK_MSG(live::RARE[23].fits(e), "the northern lights: no fox, aurora %d, the ridge %d to %d", e.aurora, e.ridge_l,
            e.ridge_r);
  p = rare(23, "foxtrot", "fox_fires", 12, 5);
  off = 0;
  for (const Look &l : p)
    off = std::max(off, abs(l.y + 7 - e.ridge.at(l.x + 8)));
  CHECK_MSG(off <= 2, "the fox %d px off the ridge", off);
  e.aurora = false;
  CHECK_MSG(!live::RARE[23].fits(e), "the fox without the northern lights");
  // one swallow over the snow that still lies in May
  met::snow_cover = {10, utc(2026, 5, 3, 11)};
  show({"", "clearsky_day", 0, 0, 0, 0, 3, 6, 0, utc(2026, 5, 3, 11), met::G_HILLS, 0, 10, 0}, "late_snow");
  e = live::SEEN;
  CHECK_MSG(live::RARE[25].fits(e), "snow in May: no swallow, white %d, fair %d", e.at.white, e.at.fair);
  rare(25, "swallow", "swallow", 9, 4);
  met::snow_cover = {};
  e.at.white = false;
  CHECK_MSG(!live::RARE[25].fits(e), "one swallow with the snow gone");
  // a pot of gold where the rainbow comes down on the land, in front of the far hills
  show({"", "rainshowers_day", 0, 30, 60, 1, 4, 10, 0, utc(2026, 10, 20, 8), met::G_HILLS, 0, 10, 0}, "rainbow");
  e = live::SEEN;
  CHECK_MSG(panel_scene().rainbow && live::RARE[26].fits(e), "a rainbow %d: no pot of gold, at %d",
            panel_scene().rainbow, e.gold_x);
  p = rare(26, "gold", "gold", 25, 3);
  CHECK_MSG(
      !p.empty() && abs(abs(p[0].x + 4 - panel_scene().rb_x) - (met::RB_RX - 6)) <= 12 &&
          p[0].y + 7 == std::min(panel_scene().land->hill[1][p[0].x + 4], panel_scene().land->hill[2][p[0].x + 4]),
      "the pot of gold at %d,%d, the rainbow's middle at %d", p.empty() ? -1 : p[0].x, p.empty() ? -1 : p[0].y,
      panel_scene().rb_x);
  // the rolls: in a gale something blows by before long, on a calm day nothing ever does
  show({"Light rain", "rain", 0, 70, 90, 1.5f, 19, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 0, 10, 0}, "gale_rolls");
  show_later();
  int rolls = 1;
  while (!live::rare_roll() && rolls < 200)
    rolls++;
  CHECK_MSG(!live::R.rare.empty(), "a gale: nothing in %d rolls", rolls);
  printf("  something blew by on roll %d\n", rolls);
  live::rare_off();
  // and nothing on the days with acts of their own, in a gale as well
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
  show({"Crows + About the cabin", "clearsky_day", 0, 0, 0, 0, 7, 18, 0, utc(2026, 6, 10, 11), met::G_HILLS, 0, 10, 0});
  show_later();
  for (rolls = 0; rolls < 2000 && live::R.rare.empty(); rolls++)
    live::rare_roll();
  CHECK_MSG(live::R.rare.empty(), "a calm summer's day: %s", live::R.rare.c_str());
  met::animate(false);
  run(20);
  // what the sprites take of the pool
  int level = log_level;
  log_level = 0;
  float most = 0;
  for (const char *n : {"trampoline", "umbrella", "shirt",  "cow",   "serpent", "ufo",     "longship", "troll",
                        "wolf",       "witch",    "huldra", "nisse", "draugen", "orcas",   "lynx",     "seaeaglecatch",
                        "skijumper",  "hag",      "tor",    "pig",   "gold",    "blackcat"}) {
    live::Frames sp;
    live::make_sprite(n, sp);
    float m = (float) pool_bytes(sp);
    CHECK_MSG(m > 0 && m <= live::RARE_BYTES, "met_live.h's RARE_BYTES is too small: %s takes %.0f", n, m);
    most = std::max(most, m);
  }
  log_level = level;
  printf("  the encounters' sprites take %.0f of the %d bytes at most\n", most, live::RARE_BYTES);
  CHECK_MSG(most >= live::RARE_BYTES * 0.8f, "met_live.h's RARE_BYTES is too large: %.0f would do", most);
}
