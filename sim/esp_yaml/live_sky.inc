// The sky shows, see met_live_sky.h
static void live_sky() {
  step("the weather animations: the sky (packages/code/met/met_live_sky.h)");
  // the picture drawn again s seconds on, the clouds moved. With hour, a new forecast for the next hour.
  auto on = [](int s, bool hour, const char *what) {
    flexit_panel_time.t.timestamp += s;
    if (hour) {
      met::weather.time += 3600;
      met::weather.hour[0].time += 3600;
    }
    met::redraw();
    CHECK(picture(what, 6000) >= 0, "%s: not drawn again", what);
  };
  // No rain or snow over a middle or low cloud: the scene the panel has, and where the tiles are on it
  auto under = [](const char *what) {
    const met::Scene &sc = panel_scene();
    auto *a = anim::find("w_prec");
    CHECK(a && a->state == anim::SENT, "%s: no rain or snow", what);
    if (!a)
      return;
    int tiles = 0, over = 0;
    for (auto &c : a->copies) {
      if (!c.used || !c.placed)
        continue;
      Look l = look(c.place);
      if (!l.shown)
        continue;
      tiles++;
      for (int i = 0; i < sc.nclouds; i++) {
        if (!sc.cloud_layer[i])
          continue;
        const met::Cloud &cl = sc.cloud[i];
        int x0 = met::cloud_x(sc, i);
        for (int y = std::max<int>(cl.top, l.y); y <= cl.base; y++)
          for (int j = 0; j < cl.n; j++) {
            int h = met::half(y - cl.e[j].y, cl.e[j].rx, cl.e[j].ry), cx = x0 + cl.e[j].x;
            for (int k = 0; k < 2 && h >= 0; k++, cx -= met::FIELD)
              over += std::max(0, std::min(cx + h, l.x + a->w - 1) - std::max(cx - h, l.x) + 1);
          }
      }
    }
    CHECK(tiles >= 7 && !over, "%s: %d tiles, %d px of them over clouds", what, tiles, over);
  };

  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots, lat lon
  // rain from under the clouds at several covers, upright and slanting, followed when the clouds move
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 40, 2}, "hills");
  under("light rain");
  on(900, false, "light rain, 15 minutes on");
  play(4, 1, "light_rain_later");
  under("light rain, 15 minutes on");
  on(300, false, "light rain, 20 minutes on");
  play(4, 0, "");
  under("light rain, 20 minutes on");
  show({"Gentle snowfall", "snow", 0, 60, 80, 1.0f, 3, -3, 0, utc(2026, 1, 13, 12), met::G_HILLS, 30, 60, 2}, "snow");
  under("snow");
  show({"Light rain", "rain", 0, 70, 90, 1.5f, 6, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 40, 2}, "slanting");
  under("slanting rain");
  show({"Gentle snowfall", "heavysnow", 0, 90, 100, 3.0f, 5, -5, 0, utc(2026, 1, 13, 12), met::G_MOUNTAINS, 30, 60, 2},
       "heavy");
  under("heavy snow");
  on(900, false, "heavy snow, 15 minutes on");
  play(4, 0, "");
  under("heavy snow, 15 minutes on");
  show({"Light rain", "lightrain", 0, 40, 0, 0.4f, 3, 8, 0, utc(2026, 10, 20, 10), met::G_CITY, 0, 40, 2}, "town");
  under("rain from high bases in town");
  show({"Gentle snowfall", "sleet", 0, 60, 70, 1.2f, 4, 1, 0, utc(2026, 10, 20, 12), met::G_COAST, 30, 60, 2}, "sleet");
  under("sleet");
  // thunder: the scene's bolt flashes out of the cloud and goes. It plays from its start once the screensaver is up.
  show({"Thunderstorm", "rainandthunder", 0, 80, 90, 3, 8, 18, 0, utc(2026, 7, 10, 15), met::G_HILLS, 0, 40, 2},
       "hills");
  under("thunder");
  live::flash();
  tick(9);
  play(1, 9, "thunderstorm_flash");
  CHECK(anim::find("w_bolt"), "thunder: no bolt");
  play(16, 1, "thunderstorm_after");
  CHECK(!anim::find("w_bolt"), "thunder: the bolt stayed");
  show({"Light rain", "heavyrain", 0, 60, 100, 5, 12, 8, 0, utc(2026, 10, 20, 12), met::G_COAST, 0, 40, 2}, "heavy");
  under("heavy rain");
  show({"Thunderstorm", "heavyrainandthunder", 0, 90, 100, 6, 10, 20, 0, utc(2026, 7, 10, 15), met::G_CITY, 0, 40, 2},
       "town");
  under("heavy thunder in town");

  // fog, thick and thin, and on a flag day
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 60, 2}, "thick");
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 10, 0});
  show({"Fog in the valley", "cloudy", 0, 0, 30, 0, 1, 6, 40, utc(2026, 10, 20, 10), met::G_MOUNTAINS, 0, 60, 2},
       "thin");
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 10, 0});
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 2, 14, 70, utc(2026, 7, 4, 10), met::G_HILLS, 0, 60, 2}, "flag");
  CHECK(live::R.flag, "fog on a flag day: no flag");

  // the aurora on several nights up north, made again when its ribbons move with the hour
  show({"Northern lights", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 15, 21, 10), met::G_HILLS, 0, 60, 3,
        69.65, 18.96},
       "tromso");
  size_t was = log_all.size();
  on(3600, true, "the aurora an hour on");
  play(10, 1, "northern_lights_later");
  CHECK(logged("Animation w_aur0:", was), "the aurora an hour on: not made again");
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS, 0, 30, 2},
       "oslo");
  show({"Northern lights", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 1, 21), met::G_HILLS, 0, 60, 2, 69.65,
        18.96},
       "new_year");
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS, 0, 30, 0});
  show({"Northern lights", "clearsky_night", 0, 0, 0, 0, 3, -12, 0, utc(2026, 12, 5, 19), met::G_COAST, 0, 60, 2, 69.65,
        18.96},
       "coast");
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS, 0, 30, 0});
  show({"Northern lights", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, utc(2026, 2, 3, 20), met::G_MOUNTAINS, 0, 60, 2,
        67.28, 14.4},
       "bodo");

  // stars with the moon up, and a shooting star that keeps clear of it; on a flag day in the polar night
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS, 0, 60, 2},
       "moon");
  live::shoot();
  auto *star = anim::find("w_shooting");
  for (int k = 0; k < 60 && star && !star->copies[0].placed; k++)
    tick(1);
  CHECK(star && star->copies[0].placed, "no shooting star");
  play(1, 9, "starry_winter_night_shooting");
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -14, 0, utc(2026, 1, 21, 10), met::G_HILLS, 0, 30, 2,
        78.22, 15.65},
       "svalbard");
  CHECK(live::R.flag, "stars on a flag day: no flag");

  // high clouds, and on a flag day
  show({"High clouds", "fair_day", 60, 0, 0, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 60, 2}, "calm");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"High clouds", "fair_day", 80, 0, 0, 0, 7, 8, 0, utc(2026, 4, 20, 12), met::G_HILLS, 0, 60, 2}, "windy");
  was = log_all.size();
  on(3600, true, "high clouds the next hour");
  play(4, 1, "high_clouds_next_hour");
  CHECK(logged("Animation w_cloud0:", was), "high clouds the next hour: not made again");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"High clouds", "fair_day", 40, 0, 0, 0, 7, 16, 0, utc(2026, 6, 7, 11), met::G_HILLS, 0, 60, 2}, "flag");
  CHECK(live::R.flag, "high clouds on a flag day: no flag");

  // drifting clouds and far gulls, and on a flag day; too many clouds for the pool is another show
  show({"Drifting clouds", "fair_day", 0, 30, 0, 0, 4, 8, 0, utc(2026, 5, 5, 8), met::G_HILLS, 0, 90, 3}, "gulls");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"Drifting clouds", "fair_day", 0, 10, 15, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 60, 2}, "low");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"Drifting clouds", "fair_day", 0, 30, 0, 0, 7, 16, 0, utc(2026, 6, 7, 11), met::G_HILLS, 0, 60, 2}, "flag");
  CHECK(live::R.flag, "drifting clouds on a flag day: no flag");
  show({"Gulls over the cabin", "cloudy", 0, 60, 80, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 10, 0});
}

// After the other shows, so their chance events stay as they were: what the shows keep (the rain's frame, the
// flag's light) and the heater's smoke
static void live_kept() {
  step("the weather animations: the rain's frame, the flag's light and the heater (packages/code/met/met_live.h)");
  // The rain tile's whole frame for animation.h is freed once the tile is made. Without the RAM for it the tile's
  // frames are empty, nothing worse.
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 10, 0});
  tick(1);
  CHECK(!live::R.px.capacity(), "the rain's frame kept: %u bytes", (unsigned) (live::R.px.capacity() * 4));
  {
    live::Frames tile = *live::frames_of("prec");
    size_t need = (size_t) tile.w * tile.h * 4, from = log_all.size();
    live::unload("prec");
    live::add("prec", tile);
    ram_block = need - 1;  // what animation.h takes still fits
    live::prec_place(panel_scene(), false);
    int r = run_until(
        [] {
          auto *a = anim::find("w_prec");
          return a && a->state != anim::WANT;
        },
        3000);
    CHECK(r >= 0 && anim::find("w_prec")->state == anim::SENT && logged("No RAM to draw the frames of a", from),
          "the rain without %u bytes for its frame: %s", (unsigned) need, panel_animation_status.state.c_str());
    ram_block = 100 * 1024;
  }
  // the flag follows the light without the show starting again: up at 9 on the King's birthday, brighter by noon
  tz_offset_s = 3600;
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 3, 5, 0, utc(2026, 2, 21, 8), met::G_HILLS, 0, 10, 0});
  int morning = live::R.flag_light;
  size_t was = log_all.size();
  flexit_panel_time.t.timestamp += 3 * 3600;
  met::redraw();
  CHECK(picture("the flag at noon", 6000) >= 0, "the flag at noon: not drawn again");
  play(4, 0, "");
  CHECK(live::R.flag && morning < 10 && live::R.flag_light == 10 && logged("Animation w_flag:", was) &&
            !logged("Weather animation:", was),
        "the flag at 9 and at noon: lit %d and %d tenths, made again %d", morning, live::R.flag_light,
        logged("Animation w_flag:", was));
  tz_offset_s = 0;
  // met.yaml's redraw takes the heater's % for the chimney's smoke
  uint16_t &heat = server->hold[flexit_modbus_server::REG_PERCENTAGE_HEATING], before = heat;
  heat = 40;
  yaml_every(5 * 60000);
  CHECK(met::options.heater_pct == 40 && !met::fetching && picture("the weather with the heater on", 3000) >= 0,
        "the heater at 40 %%: %d in the picture", met::options.heater_pct);
  heat = before;
  yaml_every(5 * 60000);
  CHECK(picture("the weather, the heater as it was", 3000) >= 0, "weather picture not shown again");
  met::animate(false);
}

// What sp takes of the panel's pool as animation.h makes it, its frame and records. 0 if it didn't make it whole.
static size_t pool_bytes(const live::Frames &sp) {
  std::vector<uint32_t> px;
  int drawn = -1;
  anim::Source src{sp.w, sp.h, sp.count(), 100, 0, 0, 1, [&](int f, int x, int y) {
                     if (f != drawn) {
                       px.assign((size_t) sp.w * sp.h, 0);
                       sp.draw(f, live::Canvas{sp.w, sp.h, px.data()});
                       drawn = f;
                     }
                     return px[(size_t) y * sp.w + x];
                   }};
  src.delays = sp.delays;
  anim::Anim a;
  a.name = "cost";
  a.src = &src;
  anim::W.a = &a;
  bool ok = anim::start();
  while (ok && (anim::W.phase == anim::COUNT || anim::W.phase == anim::MAKE)) {
    if (anim::W.phase == anim::COUNT)
      anim::count();
    else
      anim::make();
  }
  size_t n = anim::W.phase == anim::SEND && !a.half ? a.frame_bytes() + a.data.size() : 0;
  anim::stop();
  return n;
}

// A scene for the weather at t, its colours placed, in a buffer of its own
static const met::Scene &scene_at(const met::Weather &weather, uint32_t t, met::Options o, double lat = OSLO_LAT,
                                  double lon = OSLO_LON) {
  static SceneBuf sc(false);
  met::Weather w = weather;
  w.time = t / 3600 * 3600;
  met::build(sc.s, w, lat, lon, t, false, oslo_offset, o);
  met::palette(sc.s, nullptr);
  return sc.s;
}

// met_live_sky.h's costs against the sprites the panel's encoder made: the least each cost can be and still bound
// every sprite, the others as they are, and the most of the estimate a sprite took
struct Costs {
  std::map<std::string, std::pair<float, float>> need;  // cost, need
  float share = 0;
  // A sprite that took bytes, estimated at said: each cost's value, the estimate without it, and its units
  void took(float bytes, float said, std::initializer_list<std::tuple<const char *, float, float, float>> costs) {
    share = std::max(share, bytes / said);
    for (auto [name, cost, rest, units] : costs)
      if (units > 0) {
        auto it = need.try_emplace(name, cost, -1e9f).first;
        it->second.second = std::max(it->second.second, (bytes - rest) / units);
      }
  }
  // Too small if a sprite took more than they say, far too large if none takes half of it
  void check(const char *what) {
    printf("  %s: the sprites take %.0f%% of it at most\n", what, share * 100);
    for (auto &[name, n] : need) {
      printf("  %s is %g, %.1f would do\n", name.c_str(), n.first, n.second);
      CHECK(n.second <= n.first, "met_live_sky.h's %s is too small: %g, the panel's encoder needs %.1f", name.c_str(),
            n.first, n.second);
    }
    CHECK(share >= 0.5f, "met_live_sky.h's %s is far too large: the sprites take %.0f%% of what it says at most", what,
          share * 100);
  }
};

// What the sky shows plan to take of the pool, met_live_sky.h's TILE_COST and the rest, against the panel's own
// encoder. A change to pdeflate or the records shows up here, not as a show that's too big.
static void live_costs() {
  step("the weather animations' sizes, measured (packages/code/met/met_live_sky.h)");
  run(30);
  CHECK(anim::W.phase == anim::IDLE && !anim::used_by_others(nullptr), "animations on: %u bytes",
        (unsigned) anim::used_by_others(nullptr));
  int level = log_level;
  log_level = 0;
  // rain, snow and sleet tiles, as wide as the shows make them, short and tall, upright and slanting, as dense as
  // fits the pool
  const met::Scene &s = panel_scene();
  for (int k = 0; k < 3; k++) {
    const live::TileCost &c = live::TILE_COST[k];
    static const char *const PART[] = {"start", "row", "each", "slant"};
    char what[4][32];
    for (int i = 0; i < 4; i++)
      snprintf(what[i], sizeof what[i], "TILE_COST[%d].%s", k, PART[i]);
    Costs costs;
    for (int w : {40, 46})
      for (int slant : {0, 2, 4})
        for (int h : {80, 120, 150})
          for (float d : {0.0005f, 0.002f, 0.004f, 0.008f}) {
            if (k == 1 && slant)
              continue;  // snow falls straight
            live::SKY.tw = w;
            live::SKY.h = h;
            live::SKY.kind = k;
            live::SKY.slant = slant;
            live::SKY.dens = d;
            float per = (float) (c.slant * abs(slant) / 2), frame = (float) (w / 2 * h);
            float most = ((float) ANIM_POOL - frame - (float) (c.start + c.row * h)) / ((float) c.each + per),
                  n = d * met::clamp01(most / (d * (float) (w * h))) * (float) (w * h);  // as prec_tile() thins it
            float m = (float) pool_bytes(live::prec_tile(s, ANIM_POOL));
            CHECK(m, "met_live_sky.h's TILE_COST[%d] is too small: a %dx%d tile with %.0f streaks or flakes didn't fit",
                  k, w, h, n);
            costs.took(m, frame + (float) (c.start + c.row * h) + n * ((float) c.each + per),
                       {{what[0], c.start, frame + (float) (c.row * h) + n * ((float) c.each + per), 1},
                        {what[1], c.row, frame + (float) c.start + n * ((float) c.each + per), (float) h},
                        {what[2], c.each, frame + (float) (c.start + c.row * h) + n * per, n},
                        {what[3], c.slant, frame + (float) (c.start + c.row * h) + n * (float) c.each,
                         n * (float) abs(slant) / 2}});
          }
    snprintf(what[0], sizeof what[0], "TILE_COST[%d]", k);
    costs.check(what[0]);
  }
  // the scene's bolt, a day of thunderstorms
  Costs bolt;
  met::Weather storm;
  storm.ok = true;
  snprintf(storm.symbol, sizeof storm.symbol, "rainandthunder");
  storm.cloud = storm.cloud_low = 90;
  storm.cloud_mid = 70;
  storm.rain = 3;
  storm.temp = 18;
  for (int hour = 0; hour < 24; hour++) {
    const met::Scene &t = scene_at(storm, utc(2026, 7, 10, hour), {});
    int x, y, n = (int) std::size(t.bolt_x);
    live::Box b = live::bolt_box(t.bolt_x, t.bolt_y, n);
    float m = (float) pool_bytes(live::bolt(t.bolt_x, t.bolt_y, n, &x, &y)), frame = (float) ((b.w + 1) / 2 * b.h),
          rows = (float) b.h;
    bolt.took(m, frame + live::BOLT_ROW * rows + live::BOLT_START,
              {{"BOLT_START", live::BOLT_START, frame + live::BOLT_ROW * rows, 1},
               {"BOLT_ROW", live::BOLT_ROW, frame + live::BOLT_START, rows}});
  }
  bolt.check("BOLT_START and BOLT_ROW");
  // the clouds of a few days, high, middle and low
  Costs cirrus, cumulus;
  for (int day = 0; day < 4; day++)
    for (int hour = 0; hour < 24; hour += 3) {
      met::Weather w;
      w.ok = true;
      snprintf(w.symbol, sizeof w.symbol, "fair_day");
      w.cloud = w.cloud_high = 70;
      w.cloud_mid = w.cloud_low = 35;
      w.wind = 3;
      const met::Scene &t = scene_at(w, utc(2026, 5, 4 + day, hour), {});
      for (int i = 0; i < t.nclouds; i++) {
        int x, y;
        live::Frames sp = live::cloud(t, i, &x, &y);
        const live::CloudCost &c = t.cloud_layer[i] ? live::CUMULUS_COST : live::CIRRUS_COST;
        float frame = (float) ((sp.w + 1) / 2 * sp.h), m = (float) pool_bytes(sp);
        if (!t.cloud_layer[i])
          cirrus.took(m, frame * c.fifths / 5 + c.start, {{"CIRRUS_COST.start", c.start, frame * c.fifths / 5, 1}});
        else
          cumulus.took(m, frame * c.fifths / 5 + c.start,
                       {{"CUMULUS_COST.start", c.start, frame * c.fifths / 5, 1},
                        {"CUMULUS_COST.fifths", c.fifths, (float) c.start, frame / 5}});
      }
    }
  cirrus.check("CIRRUS_COST");
  cumulus.check("CUMULUS_COST");
  // the flag in any light, and the far gulls
  Costs flag, gulls;
  for (float light : {0.2f, 0.5f, 0.8f, 1.0f}) {
    live::Frames sp;
    live::make_sprite("flag", sp);
    flag.took((float) pool_bytes(live::dimmed(sp, light)), live::FLAG_BYTES, {{"FLAG_BYTES", live::FLAG_BYTES, 0, 1}});
  }
  flag.check("FLAG_BYTES");
  live::Frames far;
  live::make_sprite("gullfar", far);
  gulls.took((float) pool_bytes(far), live::GULLS_FAR_BYTES, {{"GULLS_FAR_BYTES", live::GULLS_FAR_BYTES, 0, 1}});
  gulls.check("GULLS_FAR_BYTES");
  // the aurora's four patches on clear nights up north, all of them in the pool
  float most = 0;
  for (int night = 0; night < 40; night++) {
    met::Weather w;
    w.ok = true;
    snprintf(w.symbol, sizeof w.symbol, "clearsky_night");
    w.temp = -8;
    met::Options o;
    o.aurora = met::A_ALWAYS;
    const met::Scene &t = scene_at(w, utc(2026, 1, 1, 21) + night * 86400u + night % 5 * 3600u, o, 69.65, 18.96);
    live::Box b[4];
    live::aur_boxes(t, b);
    float bytes = 0;
    for (int k = 0; t.aurora && k < 4; k++)
      bytes += b[k].w ? (float) pool_bytes(live::aur_patch(t, k == 3, b[k])) : 0;
    most = std::max(most, bytes);
  }
  printf("  the aurora's patches, %d and %d wide, take %.0f of the %d bytes at most\n", live::AUR_HIGH_W,
         live::AUR_LOW_W, most, live::SKY_POOL);
  CHECK(most <= live::SKY_POOL, "met_live_sky.h's AUR_HIGH_W and AUR_LOW_W are too wide: %.0f bytes", most);
  CHECK(most >= live::SKY_POOL / 2, "met_live_sky.h's AUR_HIGH_W and AUR_LOW_W are far too narrow: %.0f bytes", most);
  log_level = level;
  anim::status(anim::shown());
}
