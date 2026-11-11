// The cast, see met_live_cast.h: which acts are on together, what each may take of the panel, the turns the features
// take, and what stays through them

// The scene of a case as met.h builds it, the forecast over it or not, in a buffer of its own
static const met::Scene &cast_scene(const Live &c, bool forecast = false, float kp = NAN) {
  static SceneBuf sc;
  met::Options o{};
  o.ground = c.ground;
  o.heater_pct = c.heat;
  o.kp = kp;
  met::build(sc.s, weather_for(c), c.lat, c.lon, c.t, forecast, oslo_offset, o);
  return sc.s;
}

// The acts choose() gave, by name
static std::string named(const std::vector<live::On> &cast) {
  std::string names;
  for (const live::On &o : cast)
    names += (names.empty() ? "" : ", ") + std::string(o.name);
  return names.empty() ? "nothing" : names;
}
static const live::On *in(const std::vector<live::On> &cast, const char *act) {
  for (const live::On &o : cast)
    if (o.act == live::act_of(act))
      return &o;
  return nullptr;
}
// The feature of a line, the first or the second taken, "" without one
static std::string feature_of(const std::vector<live::On> &cast, int line) {
  for (const live::On &o : cast)
    if (live::ACTS[o.act].rank == live::FEATURE && !line--)
      return o.name;
  return "";
}

static void live_cast() {
  step("the weather animations' cast (packages/code/met/met_live_cast.h)");
  auto clock_was = flexit_panel_time.t.timestamp;  // the tests after this one have the time they had
  met::animate(false);
  run(20);
  live::R.on = true;
  live::R.only = ~0ull;
  live::R.pin = -1;

  // Through a year of all weathers in all four landscapes, with the forecast and without: the acts never take more
  // than the panel has, no two draw the same part of the picture, two features at the most, the same cast each time
  // it's asked for, and every act comes on some time
  struct Sky {
    const char *symbol;
    float high, mid, low, rain, fog;
  };
  static const Sky SKIES[] = {{"clearsky_day", 0, 0, 0, 0, 0},      {"fair_day", 60, 0, 0, 0, 0},
                              {"partlycloudy_day", 0, 30, 0, 0, 0}, {"cloudy", 0, 60, 80, 0, 0},
                              {"rain", 0, 70, 90, 1.5f, 0},         {"heavysnow", 0, 90, 100, 3, 0},
                              {"rainandthunder", 0, 80, 90, 3, 0},  {"fog", 0, 0, 0, 0, 70}};
  std::set<int> came;
  int scenes = 0, over = 0, shared = 0, crowded = 0, unsteady = 0;
  std::string worst;
  for (int day = 0; day < 365; day++)
    for (int hour : {0, 6, 10, 13, 17, 21})
      for (int g = 0; g < 4; g++) {
        const Sky &sky = SKIES[(day * 7 + hour + g * 3) % 8];
        bool snow = !strcmp(sky.symbol, "heavysnow"), north = day % 4 == 0;
        Live c = {"",
                  sky.symbol,
                  sky.high,
                  sky.mid,
                  sky.low,
                  sky.rain,
                  (float) (day % 5 * 5),
                  snow                     ? -5.0f
                  : day > 120 && day < 270 ? 18.0f
                  : day % 2                ? -6.0f
                                           : 2.0f,
                  sky.fog,
                  utc(2026, 1, 1) + (uint32_t) day * 86400 + (uint32_t) hour * 3600 + (uint32_t) (day * 13 % 60) * 60,
                  (met::Ground) g,
                  day % 3 ? 0 : 60,
                  0,
                  0,
                  north ? 69.65 : OSLO_LAT,
                  north ? 18.96 : OSLO_LON};
        for (bool forecast : {false, true}) {
          const met::Scene &s = cast_scene(c, forecast, north ? 8.0f : NAN);
          live::Room spare{}, all{}, whole = live::whole(s);
          std::vector<live::On> cast = live::choose(s, &spare);
          uint16_t parts = 0;
          int features = 0;
          for (const live::On &o : cast) {
            all = all + o.grant;
            shared += (parts & live::ACTS[o.act].live) != 0;
            parts |= live::ACTS[o.act].live;
            features += live::ACTS[o.act].rank == live::FEATURE;
            over += !o.grant.has(live::cost_of(o.act, s));
            came.insert(o.act);
          }
          bool fits = whole.has(all) && (whole - all).has(spare) && spare.has(whole - all);
          if (!fits && worst.empty())
            worst = named(cast);
          over += !fits;
          crowded += features > live::LINES;
          unsteady += named(live::choose(s)) != named(cast) || live::leaves_out(s) != parts;
          scenes++;
        }
      }
  CHECK_MSG(!over && !shared && !crowded && !unsteady,
            "%d casts: %d take more than the panel has, as %s, %d with a part of the picture drawn twice, %d with more "
            "than %d features, %d not the same when asked for again",
            scenes, over, worst.c_str(), shared, crowded, live::LINES, unsteady);
  for (int i = 0; i < live::NACTS; i++)
    CHECK_MSG(came.count(i), "%s never comes on in a year", live::ACTS[i].name);

  // a line's turns are 20 to 40 minutes, and the two lines change at different minutes
  std::set<uint32_t> changes[2];
  int odd = 0;
  for (int line = 0; line < 2; line++)
    for (uint32_t m = 1, from = 0, day = 20000 * 1440; m < 1440; m++)
      if (live::turn_of(day + m, line) != live::turn_of(day + m - 1, line)) {
        odd += from && (m - from < 20 || m - from > 40);
        changes[line].insert(from = m);
      }
  int together = 0;
  for (uint32_t m : changes[0])
    together += (int) changes[1].count(m);
  CHECK_MSG(!odd && changes[0].size() >= 36 && changes[1].size() >= 36 && !together,
            "the features' turns through a day: %zu and %zu of them, %d shorter than 20 or longer than 40 minutes, %d "
            "at the same minute",
            changes[0].size(), changes[1].size(), odd, together);

  // a clear day in May on the coast: the sailboat sails and the light plays on the water through all of it, whatever
  // the features. Of the gulls, the eiders and the lambs two are on at a time, never both that have the picture's
  // birds, each line's changes with its own turn to another than it had, and all three get on.
  Live coast = {"", "clearsky_day", 0, 0, 0, 0, 3, 12, 0, utc(2026, 5, 12, 7), met::G_COAST, 0, 0, 0};
  live::R.only = 0;
  for (const char *act : {"The sailboat", "Light on the water", "Off the coast", "About the cabin", "Gulls", "Eiders",
                          "Lambs, a wagtail and geese"})
    live::R.only |= 1ull << live::act_of(act);
  std::set<std::string> featured;
  std::string was[2], two;
  int moored = 0, off_turn = 0, again = 0, same = 0, alone = 0;
  for (uint32_t m = 0; m < 8 * 60; m++) {
    Live now = coast;
    now.t += m * 60;
    const met::Scene &s = cast_scene(now);
    std::vector<live::On> cast = live::choose(s);
    moored += !in(cast, "The sailboat") || !in(cast, "Light on the water") || !(live::leaves_out(s) & met::L_BOAT);
    std::string on[2] = {feature_of(cast, 0), feature_of(cast, 1)};
    alone += on[0].empty() || on[1].empty();
    if (two.empty() && (on[0] == on[1] || (in(cast, "Gulls") && in(cast, "Lambs, a wagtail and geese"))))
      two = named(cast);
    same += on[0] == on[1] || (in(cast, "Gulls") && in(cast, "Lambs, a wagtail and geese"));
    bool turned[2];
    for (int line = 0; line < 2; line++)
      turned[line] = m && live::turn_of(s.at.clock, line) != live::turn_of(s.at.clock - 1, line);
    off_turn += m && (on[0] != was[0] || on[1] != was[1]) && !turned[0] && !turned[1];
    again += turned[0] && on[0] == was[0];
    for (int line = 0; line < 2; line++)
      featured.insert(was[line] = on[line]);
  }
  CHECK_MSG(
      !moored && !alone && !same && !off_turn && !again && featured.size() == 3,
      "a day on the coast: %d minutes without the sailboat, %d with one feature, %d with two of a kind, as %s, %d "
      "changes out of turn, %d turns with the same again, %zu features in all",
      moored, alone, same, two.c_str(), off_turn, again, featured.size());

  // one asked for is on whenever it fits, one that doesn't fit changes nothing, and only a feature can be asked for
  live::ask_for("Eiders");
  int asked = live::R.pin, missing = 0;
  for (uint32_t m = 0; m < 8 * 60; m += 7) {
    Live now = coast;
    now.t += m * 60;
    missing += feature_of(live::choose(cast_scene(now)), 0) != "Eiders";
  }
  live::ask_for("Evening in town");
  std::string elsewhere = named(live::choose(cast_scene(coast)));
  live::ask_for("The sailboat");
  int resident = live::R.pin;
  live::ask_for("auto");
  CHECK_MSG(asked == live::act_of("Eiders") && !missing && resident < 0 && live::R.pin < 0 &&
                elsewhere == named(live::choose(cast_scene(coast))),
            "the eiders asked for: %d of 69 looks without them. The town's evening asked for on the coast: %s", missing,
            elsewhere.c_str());
  std::set<std::string> options(panel_weather_feature.options.begin(), panel_weather_feature.options.end()),
      features = {"auto"};
  for (const live::Act &a : live::ACTS)
    if (a.rank == live::FEATURE)
      features.insert(a.name);
  CHECK_MSG(options == features, "met.yaml's Panel Weather Feature has %zu options, the features and auto are %zu",
            options.size(), features.size());
  live::R.only = ~0ull;

  // the lighthouse's beam goes round all night on the coast, in rain and snow as well, with the rain as thick as the
  // scene's. By day the sailboat is in when it rains.
  for (const char *symbol : {"clearsky_night", "rain", "heavysnow"}) {
    bool rain = !strcmp(symbol, "rain"), snow = !strcmp(symbol, "heavysnow");
    int dark = 0;
    std::string without;
    for (uint32_t m = 0; m < 6 * 60; m += 5) {
      Live night = {"",
                    symbol,
                    0,
                    rain || snow ? 70.0f : 0.0f,
                    rain || snow ? 90.0f : 0.0f,
                    rain   ? 1.5f
                    : snow ? 3.0f
                           : 0.0f,
                    4,
                    snow ? -4.0f : 6.0f,
                    0,
                    utc(2026, 11, 10, 19) + m * 60,
                    met::G_COAST,
                    40,
                    0,
                    0};
      const met::Scene &s = cast_scene(night, true);
      std::vector<live::On> cast = live::choose(s);
      const live::On *falls = in(cast, rain ? "Light rain" : "Gentle snowfall");
      bool lit =
          in(cast, "The lighthouse") && (!(rain || snow) || (falls && falls->grant.bytes >= live::prec_least(s)));
      if (!lit && without.empty())
        without = named(cast);
      dark += !lit;
    }
    CHECK_MSG(!dark, "a night on the coast, %s: %d looks without the beam, as %s", symbol, dark, without.c_str());
  }
  Live wet_day = {"", "rain", 0, 70, 90, 1.5f, 4, 12, 0, utc(2026, 5, 12, 11), met::G_COAST, 0, 0, 0};
  std::vector<live::On> rainy = live::choose(cast_scene(wet_day));
  CHECK_MSG(in(rainy, "Light rain") && !in(rainy, "The sailboat") && !in(rainy, "Gulls"),
            "a rainy day on the coast: %s", named(rainy).c_str());

  // Valentine's hearts are the chimney's, so its smoke stays out, while Sankthans' bonfire burns beside it
  Live hearts = {"", "fair_day", 20, 0, 0, 0, 3, -2, 0, utc(2026, 2, 14, 12), met::G_HILLS, 40, 0, 0};
  std::vector<live::On> valentine = live::choose(cast_scene(hearts));
  Live fire = {"", "clearsky_day", 0, 0, 0, 0, 2, 12, 0, utc(2026, 6, 23, 20, 30), met::G_HILLS, 30, 0, 0};
  std::vector<live::On> bonfire = live::choose(cast_scene(fire));
  CHECK_MSG(in(valentine, "Valentine's Day") && !in(valentine, "Smoke from the chimney") && in(bonfire, "Sankthans"),
            "Valentine's Day: %s. Sankthans: %s", named(valentine).c_str(), named(bonfire).c_str());

  // What there's more room for gets it: the snow sways each flake on its own when it's alone and is thinned beside
  // the forecast, in a gale it leaves a visitor's bytes; alone the stars sparkle six of them and the chimney smokes
  // all its puffs, and beside others they keep their least
  Live snowy = {"", "heavysnow", 0, 90, 100, 3, 5, -5, 0, utc(2026, 1, 13, 19), met::G_COAST, 90, 0, 0};
  live::R.only = 1ull << live::act_of("Gentle snowfall");
  const met::Scene &white = cast_scene(snowy);
  int alone_bytes = in(live::choose(white), "Gentle snowfall")->grant.bytes,
      full = std::min((int) ceilf(live::prec_bytes(white).full) + 2 * live::FALL_COPY, live::whole(white).bytes);
  live::R.only = ~0ull;
  live::Room left{};
  const met::Scene &crowd = cast_scene(snowy, true);
  std::vector<live::On> night = live::choose(crowd, &left);
  const live::On *falls = in(night, "Gentle snowfall");
  snowy.wind = 19;
  live::Room in_gale{};
  live::choose(cast_scene(snowy, true), &in_gale);
  CHECK_MSG(alone_bytes == full && falls && falls->grant.bytes < alone_bytes &&
                falls->grant.bytes >= live::prec_least(crowd) && in(night, "The lighthouse") &&
                in_gale.bytes >= live::RARE_BYTES,
            "snow alone gets %d of the %d bytes it wants, %d beside %s, which leaves %d, and %d in a gale", alone_bytes,
            full, falls ? falls->grant.bytes : -1, named(night).c_str(), left.bytes, in_gale.bytes);
  Live starry = {"", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 2, 3, 20), met::G_HILLS, 90, 0, 0};
  live::R.only = 1ull << live::act_of("The stars");
  int sparkling = in(live::choose(cast_scene(starry)), "The stars")->grant.places;
  live::R.only = 1ull << live::act_of("Smoke from the chimney");
  int puffing = in(live::choose(cast_scene(starry)), "Smoke from the chimney")->grant.places;
  live::R.only = ~0ull;
  std::vector<live::On> many = live::choose(cast_scene(starry, true));
  const live::On *stars = in(many, "The stars"), *smoke = in(many, "Smoke from the chimney");
  CHECK_MSG(sparkling == 6 && puffing == 5 && stars && smoke && stars->grant.places >= 1 && smoke->grant.places >= 2 &&
                abs(stars->grant.places - smoke->grant.places) <= 2 && stars->grant.places + smoke->grant.places < 11,
            "the stars and the smoke, each alone: %d and %d places. Beside %s: %d and %d", sparkling, puffing,
            named(many).c_str(), stars ? stars->grant.places : -1, smoke ? smoke->grant.places : -1);
  live::R.on = false;

  // And on the panel, every act free to come on, with what comes by chance due at once: no act takes more than it
  // was granted, and the panel refuses nothing
  struct Full {
    Live c;
    const char *tag;
    bool forecast;
  };
  const Full FULL[] = {
      {{"", "clearsky_day", 0, 0, 0, 0, 3, 16, 0, utc(2026, 7, 4, 11), met::G_COAST, 0, 90, 3}, "coast_day", false},
      {{"", "clearsky_night", 0, 0, 0, 0, 3, 2, 0, utc(2026, 11, 10, 20), met::G_COAST, 60, 90, 3},
       "coast_night",
       true},
      {{"", "rain", 0, 70, 90, 1.5f, 6, 6, 0, utc(2026, 11, 10, 20), met::G_COAST, 60, 60, 2}, "coast_rain", true},
      {{"", "heavysnow", 0, 90, 100, 3, 5, -5, 0, utc(2025, 12, 31, 22, 30), met::G_COAST, 90, 90, 3},
       "new_year_snow",
       false},
      {{"", "clearsky_day", 0, 0, 0, 0, 3, -5, 0, utc(2026, 2, 21, 11), met::G_HILLS, 90, 90, 3}, "hills_winter", true},
      {{"", "fair_day", 20, 0, 0, 0, 3, 20, 0, utc(2026, 7, 29, 11), met::G_HILLS, 0, 90, 3}, "hills_summer", false},
      {{"", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, utc(2025, 12, 24, 17), met::G_HILLS, 60, 90, 3},
       "christmas_eve",
       true},
      {{"", "partlycloudy_day", 0, 30, 0, 0, 3, 18, 0, utc(2026, 6, 10, 11), met::G_MOUNTAINS, 0, 90, 3},
       "mountains",
       true},
      {{"", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 15, 21, 10), met::G_CITY, 0, 90, 3, 69.65, 18.96},
       "town_aurora",
       true},
      {{"", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 6, 23, 20, 30), met::G_HILLS, 30, 60, 2},
       "sankthans",
       false},
  };
  for (const Full &f : FULL) {
    size_t from = log_all.size();
    std::string name = std::string("cast_") + f.tag;
    met::with_forecast = f.forecast;
    weather_of(f.c);
    CHECK_MSG(picture(name.c_str(), 6000) >= 0, "%s: not drawn", name.c_str());
    printf("  %s: %s\n", f.tag, cast_on().c_str());
    chances_now();
    play(f.c.seconds, f.c.shots, name);
    chances_now();
    play(30, 0, "");
    int failed = 0;
    for (const overlay::Overlay &a : overlay::overlays)
      failed += !a.name.empty() && a.state == overlay::FAILED;
    live_quiet(name, from);
    bool pool = run_until(same_pool, 20) >= 0;  // a visitor that just left is fading out
    CHECK_MSG(!failed && live::R.cast.size() >= 2 && pool, "%s: %d animations failed, %zu acts on: %s", name.c_str(),
              failed, live::R.cast.size(), cast_on().c_str());
  }
  met::with_forecast = false;

  // The glitter where the sun's reflection has open water: some time of a fine day in October, the sun low in the
  // south
  Live glints = {"The sailboat + Light on the water + Glitter on the water",
                 "clearsky_day",
                 0,
                 0,
                 0,
                 0,
                 3,
                 12,
                 0,
                 utc(2026, 10, 15, 7),
                 met::G_COAST,
                 0,
                 30,
                 2};
  live::R.on = true;
  for (int m = 0; m < 9 * 60 && !in(live::choose(cast_scene(glints)), "Glitter on the water"); m += 10)
    glints.t += 10 * 60;
  live::R.on = false;
  show(glints, "glitter");
  const overlay::Overlay *glitter = overlay::find("w_glitter");
  int glinting = 0;
  for (const overlay::Copy &c : glitter ? glitter->copies : std::vector<overlay::Copy>())
    glinting += c.used && look(c.place).shown;
  CHECK_MSG(glinting == 4, "the glitter: %d of 4 points of light on the water, the sun's reflection at %d", glinting,
            panel_scene().shine_x);

  // A feature's turn ends and another's starts: the sailboat sails on as it was, not made or started again, and so
  // does the light on the water
  Live day = {"", "clearsky_day", 0, 0, 0, 0, 3, 12, 0, utc(2026, 5, 12, 9), met::G_COAST, 0, 0, 0};
  weather_of(day);
  CHECK_MSG(picture("a day on the coast", 6000) >= 0, "a day on the coast: not drawn");
  // to two minutes before a turn's end that brings another feature on: the lines may just swap theirs
  for (int tries = 0; tries < 12; tries++) {
    uint32_t clock = panel_scene().at.clock, ends = 1;
    while (live::turn_of(clock + ends, 0) == live::turn_of(clock, 0) &&
           live::turn_of(clock + ends, 1) == live::turn_of(clock, 1))
      ends++;
    flexit_panel_time.t.timestamp += (ends - 2) * 60;
    std::set<int> on_before, on_after;
    for (const live::On &o : live::choose(panel_scene()))
      on_before.insert(o.act);
    flexit_panel_time.t.timestamp += 4 * 60;
    for (const live::On &o : live::choose(panel_scene()))
      on_after.insert(o.act);
    flexit_panel_time.t.timestamp -= 4 * 60;
    if (on_before != on_after)
      break;
    flexit_panel_time.t.timestamp += 4 * 60;
  }
  met::redraw();
  CHECK_MSG(picture("before a feature's turn ends", 6000) >= 0, "before a feature's turn ends: not drawn");
  play(20, 1, "cast_turn_before");
  const overlay::Overlay *boat = overlay::find("w_boat");
  std::string before = cast_on();
  int place = boat && !boat->copies.empty() ? boat->copies[0].place : -1;
  Look sailing = look(place);
  size_t from = log_all.size();
  flexit_panel_time.t.timestamp += 4 * 60;
  met::redraw();
  CHECK_MSG(picture("after a feature's turn ended", 6000) >= 0, "after a feature's turn ended: not drawn");
  play(20, 1, "cast_turn_after");
  boat = overlay::find("w_boat");
  Look still = look(place);
  CHECK_MSG(before != cast_on() && boat && boat->copies[0].place == place && sailing.shown && still.shown &&
                abs(still.x - sailing.x) <= 60 && !logged("Animation w_boat:", from) &&
                !logged("Animation w_shimmer:", from) && !logged("Weather animation: The sailboat", from) &&
                logged("Weather animation:", from),
            "a feature's turn ended: %s, then %s. The sailboat at %d, then %d, made again %d", before.c_str(),
            cast_on().c_str(), sailing.x, still.x, logged("Animation w_boat:", from));
  live_quiet("a feature's turn", from);

  // What came by on a visitor's room goes when the weather takes that room: a trampoline as heavy snow sets in under
  // the forecast
  Live fair = {"Crows + About the cabin + Gentle snowfall",
               "clearsky_day",
               0,
               0,
               0,
               0,
               3,
               -5,
               0,
               utc(2026, 2, 3, 11),
               met::G_HILLS,
               0,
               0,
               0};
  weather_of(fair);
  bool calm = picture("a calm winter's day", 6000) >= 0 && is_on("Crows");
  show_later();
  bool blew = live::encounter(0);
  play(3, 0, "");
  bool there = overlay::find("w_trampoline");
  Live snowing = fair;
  snowing.symbol = "heavysnow";
  snowing.mid = 90;
  snowing.low = 100;
  snowing.rain = 3;
  met::with_forecast = true;
  met::weather = weather_for(snowing);
  met::redraw();
  CHECK_MSG(
      picture("heavy snow sets in", 6000) >= 0 && calm && blew && there && is_on("Gentle snowfall") &&
          !live::R.spare.has(live::VISIT) && live::R.rare.empty() && !overlay::find("w_trampoline"),
      "heavy snow set in over a trampoline: it came %d and was there %d, then %s on, leaving %d bytes, and it's %s",
      blew, there, cast_on().c_str(), live::R.spare.bytes, overlay::find("w_trampoline") ? "still there" : "gone");
  met::with_forecast = false;

  // The select in Home Assistant asks for a feature, and auto leaves it to the draw again
  weather_of(day);
  CHECK_MSG(picture("the coast again", 6000) >= 0, "the coast again: not drawn");
  auto asked_for = [&](const char *option) {
    for (size_t i = 0; i < panel_weather_feature.options.size(); i++)
      if (panel_weather_feature.options[i] == option)
        panel_weather_feature.control(i);
    return picture(option, 6000) >= 0;
  };
  bool eiders = asked_for("Eiders") && is_on("Eiders") && live::R.pin == live::act_of("Eiders");
  bool gulls = asked_for("Gulls") && is_on("Gulls");
  CHECK_MSG(eiders && gulls && asked_for("auto") && live::R.pin < 0,
            "Panel Weather Feature: the eiders %d, the gulls %d", eiders, gulls);
  met::animate(false);
  flexit_panel_time.t.timestamp = clock_was;
  run(20);
}

// After every test of the weather animations: each act was on in one of them, and none says it takes far more of
// the pool than it did, with what its visitors took beyond a visitor's
static void live_cast_costs() {
  step("what the weather animations' acts took of the panel (packages/code/met/met_live_cast.h)");
  for (int i = 0; i < live::NACTS; i++) {
    const live::Act &a = live::ACTS[i];
    auto took = acts_took.find(i);
    CHECK_MSG(took != acts_took.end(), "%s was on in no test", a.name);
    if (took == acts_took.end())
      continue;
    const live::Room &most = took->second.most, &came = took->second.came, &granted = took->second.granted;
    printf("  %-34s %d animations, %d copies, %4d bytes of %d, %d, %4d, and %d, %d, %4d came by\n", a.name, most.slots,
           most.places, most.bytes, granted.slots, granted.places, granted.bytes, came.slots, came.places, came.bytes);
    int took_bytes = most.bytes + std::max(0, came.bytes - live::VISIT.bytes);
    CHECK_MSG(a.also || a.more || took_bytes * 2 + 200 >= a.cost.bytes,
              "met_live_cast.h says %s takes %d bytes, far more than the %d it took at the most", a.name, a.cost.bytes,
              took_bytes);
  }
}
