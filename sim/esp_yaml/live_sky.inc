// The sky shows, see met_live_sky.h
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
    CHECK_MSG(picture(what, 6000) >= 0, "%s: not drawn again", what);
  };
  // No rain or snow over a middle or low cloud: the scene the panel has, and where the tiles are on it
  auto under = [](const char *what) {
    const met::Scene &sc = panel_scene();
    auto *a = overlay::find("w_prec");
    CHECK_MSG(a && a->state == overlay::SENT, "%s: no rain or snow", what);
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
    CHECK_MSG(tiles >= 7 && !over, "%s: %d tiles, %d px of them over clouds", what, tiles, over);
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
  met::weather.wind_from = 20;
  on(60, false, "slanting rain, blowing to the right");
  play(4, 1, "slanting_right");
  under("slanting rain, blowing to the right");
  // snow blown a px every 8 rows for each m/s, and made again when the wind turns
  show({"Gentle snowfall", "snow", 0, 60, 80, 1.0f, 12, -3, 0, utc(2026, 1, 13, 12), met::G_HILLS, 30, 30, 2}, "windy");
  under("windy snow");
  int blown = live::SKY.slant;
  met::weather.wind_from = 20;
  on(60, false, "windy snow, the wind turned");
  play(4, 1, "windy_snow_turned");
  under("windy snow, the wind turned");
  CHECK_MSG(blown == -12 && live::SKY.slant == 12, "snow in 12 m/s from the south-west, then the north-east: %d, %d px",
            blown, live::SKY.slant);
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
  bool lit = false;
  play(1, 9, "thunderstorm_flash", [&] {
    Look l;
    lit |= where("bolt", l);
  });
  CHECK_MSG(lit, "thunder: no bolt");
  play(16, 1, "thunderstorm_after");
  CHECK_MSG(!overlay::find("w_bolt"), "thunder: the bolt stayed");
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
  CHECK_MSG(live::R.flag, "fog on a flag day: no flag");

  // the aurora on nights up north, still in the picture and changing with it, under the ground's own night show; the
  // town's antenna blinking over it
  auto aurora = [](const Live &c, const char *tag) {
    show(c, tag);
    CHECK_MSG(scene_at(met::weather, c.t, met::options, c.lat, c.lon).aurora, "%s with the aurora: no aurora", c.want);
  };
  aurora({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 15, 21, 10), met::G_HILLS, 0, 30,
          2, 69.65, 18.96},
         "aurora");
  aurora({"Lighthouse and the coastal express", "clearsky_night", 0, 0, 0, 0, 3, -12, 0, utc(2026, 12, 5, 19),
          met::G_COAST, 0, 30, 2, 69.65, 18.96},
         "aurora");
  aurora({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, utc(2026, 2, 3, 20), met::G_MOUNTAINS, 0, 30, 2,
          67.28, 14.4},
         "aurora_bodo");
  const Live town = {"Evening in town", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 15, 21, 10), met::G_CITY,
                     0, 30, 2, 69.65, 18.96};
  aurora(town, "aurora");
  const met::Scene &sky = scene_at(met::weather, town.t, met::options, town.lat, town.lon);
  int red = 0, dark = 0;
  play(6, 0, "", [&] {
    uint32_t c = sim_pixel(sky.antenna.x + 1, sky.antenna.y);
    (c >> 16 > 0xD0 && (c >> 8 & 255) < 0x60 ? red : dark)++;
  });
  CHECK_MSG(red && dark, "the town with the aurora: the antenna doesn't blink, %d red and %d dark", red, dark);

  // stars with the moon up, and a shooting star that keeps clear of it; on a flag day in the polar night
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS, 0, 60, 2},
       "moon");
  live::shoot();
  auto *star = overlay::find("w_shooting");
  for (int k = 0; k < 60 && star && !star->copies[0].placed; k++)
    tick(1);
  CHECK_MSG(star && star->copies[0].placed, "no shooting star");
  play(1, 9, "starry_winter_night_shooting");
  visits("starry", 3);  // a satellite, a fox
  show({"Starry winter night", "clearsky_night", 0, 0, 0, 0, 2, -14, 0, utc(2026, 1, 21, 10), met::G_HILLS, 0, 30, 2,
        78.22, 15.65},
       "svalbard");
  CHECK_MSG(live::R.flag, "stars on a flag day: no flag");

  // high clouds, and on a flag day
  show({"High clouds", "fair_day", 60, 0, 0, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 60, 2}, "calm");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"High clouds", "fair_day", 80, 0, 0, 0, 7, 8, 0, utc(2026, 4, 20, 12), met::G_HILLS, 0, 60, 2}, "windy");
  size_t was = log_all.size();
  on(3600, true, "high clouds the next hour");
  play(4, 1, "high_clouds_next_hour");
  CHECK_MSG(logged("Animation w_cloud0:", was), "high clouds the next hour: not made again");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"High clouds", "fair_day", 40, 0, 0, 0, 7, 16, 0, utc(2026, 6, 7, 11), met::G_HILLS, 0, 60, 2}, "flag");
  CHECK_MSG(live::R.flag, "high clouds on a flag day: no flag");

  // drifting clouds, and on a flag day; too many clouds for the pool is another show
  show({"Drifting clouds", "fair_day", 0, 30, 0, 0, 4, 8, 0, utc(2026, 5, 5, 8), met::G_HILLS, 0, 90, 3}, "mid");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"Drifting clouds", "fair_day", 0, 10, 15, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 60, 2}, "low");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"Drifting clouds", "fair_day", 0, 30, 0, 0, 7, 16, 0, utc(2026, 6, 7, 11), met::G_HILLS, 0, 60, 2}, "flag");
  CHECK_MSG(live::R.flag, "drifting clouds on a flag day: no flag");
  show({"Gulls over the cabin", "cloudy", 0, 60, 80, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 10, 0});
}

// After the other shows, so their chance events stay as they were: what the shows keep (the rain's frame, the
// flag's light) and the heater's smoke
static void live_kept() {
  step("the weather animations: the rain's frame, the flag's light and the heater (packages/code/met/met_live.h)");
  // The rain tile's whole frame for overlay.h is freed once the tile is made. Without the RAM for it the tile's
  // frames are empty, nothing worse.
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 10, 0});
  tick(1);
  CHECK_MSG(!live::R.px.capacity(), "the rain's frame kept: %u bytes", (unsigned) live::R.px.capacity());
  {  // asked of frame_pixel() itself, as overlay.h takes more than the frame and wouldn't get that far
    const live::Frames *tile = live::frames_of("prec");
    size_t need = (size_t) tile->w * tile->h, from = log_all.size();
    ram_block = need - 1;
    uint32_t px = live::frame_pixel(tile, 0, 0, 0);
    ram_block = 100 * 1024;
    CHECK_MSG(!px && live::R.px.empty() && logged("No RAM to draw the frames of a", from),
              "the rain without %u bytes for its frame", (unsigned) need);
    live::free_px();
  }
  // the flag follows the light on the panel, neither it nor the show made again: up at 9 on the King's birthday,
  // brighter by noon
  tz_offset_s = 3600;
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 3, 5, 0, utc(2026, 2, 21, 8), met::G_HILLS, 0, 10, 0});
  overlay::Overlay *flag = overlay::find(live::key("flag"));
  int at = flag ? flag->copies[0].place : 0, morning = look(at).light;
  size_t was = log_all.size();
  flexit_panel_time.t.timestamp += 3 * 3600;
  met::redraw();
  CHECK_MSG(picture("the flag at noon", 6000) >= 0, "the flag at noon: not drawn again");
  play(4, 0, "");
  CHECK_MSG(flag && live::R.flag && morning > 0 && morning < 1000 && look(at).light == 1000 &&
                !logged("Animation w_flag:", was) && !logged("Weather animation:", was),
            "the flag at 9 and at noon: lit %d and %d of 1000, made again %d", morning, look(at).light,
            logged("Animation w_flag:", was));
  tz_offset_s = 0;
  // met.yaml's redraw takes the heater's % for the chimney's smoke
  uint16_t &heat = server->hold[flexit_modbus_server::REG_PERCENTAGE_HEATING], before = heat;
  heat = 40;
  yaml_every(5 * 60000);
  CHECK_MSG(
      met::options.heater_pct == 40 && !met::forecasts.asking && picture("the weather with the heater on", 3000) >= 0,
      "the heater at 40 %%: %d in the picture", met::options.heater_pct);
  heat = before;
  yaml_every(5 * 60000);
  CHECK_MSG(picture("the weather, the heater as it was", 3000) >= 0, "weather picture not shown again");
  met::animate(false);
}

// What sp takes of the panel's pool as overlay.h makes it, its frame and records. 0 if it didn't make it whole.
static size_t pool_bytes(const live::Frames &sp) {
  std::vector<uint8_t> px;
  live::Colours cols;
  int drawn = -1;
  overlay::Source src{sp.w, sp.h, sp.count(), 100, 0, 0, 1, [&](int f, int x, int y) {
                        if (f != drawn) {
                          px.assign((size_t) sp.w * sp.h, 0);
                          cols = live::Colours();
                          sp.draw(f, live::Canvas{sp.w, sp.h, px.data(), &cols});
                          drawn = f;
                        }
                        uint8_t i = px[(size_t) y * sp.w + x];
                        return i ? 0xFF000000u | cols.rgb[i] : 0u;
                      }};
  src.delays = sp.delays;
  overlay::Overlay a;
  a.name = "cost";
  a.src = &src;
  overlay::W.a = &a;
  bool ok = overlay::start();
  while (ok && (overlay::W.phase == overlay::COUNT || overlay::W.phase == overlay::MAKE)) {
    if (overlay::W.phase == overlay::COUNT && overlay::W.f < src.frames)
      overlay::count();
    else if (overlay::W.phase == overlay::COUNT)
      overlay::palette();
    else
      overlay::make(0, true);
  }
  size_t n = overlay::W.phase == overlay::SEND && !a.half ? a.frame_bytes() + a.data.size() : 0;
  overlay::stop();
  return n;
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
      CHECK_MSG(n.second <= n.first, "met_live_sky.h's %s is too small: %g, the panel's encoder needs %.1f",
                name.c_str(), n.first, n.second);
    }
    CHECK_MSG(share >= 0.5f, "met_live_sky.h's %s is far too large: the sprites take %.0f%% of what it says at most",
              what, share * 100);
  }
};

// What the sky shows plan to take of the pool, met_live_sky.h's TILE_COST and the rest, against the panel's own
// encoder. A change to pdeflate or the records shows up here, not as a show that's too big.
static void live_costs() {
  step("the weather animations' sizes, measured (packages/code/met/met_live_sky.h)");
  run(30);
  CHECK_MSG(overlay::W.phase == overlay::IDLE && !overlay::used_by_others(nullptr), "animations on: %u bytes",
            (unsigned) overlay::used_by_others(nullptr));
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
      for (int slant : {0, 2, 4, 12, 24})
        for (int h : {80, 120, 150})
          for (float d : {0.0005f, 0.002f, 0.004f, 0.008f}) {
            if (k != 1 && slant > 4)
              continue;  // only snow is blown more
            live::SKY.tw = w;
            live::SKY.h = h;
            live::SKY.kind = k;
            live::SKY.slant = slant;
            live::SKY.dens = d;
            float per = (float) (c.slant * abs(slant) / 2), frame = (float) (w / 2 * h);
            float most = ((float) OVERLAY_POOL - frame - (float) (c.start + c.row * h)) / ((float) c.each + per),
                  n = d * met::clamp01(most / (d * (float) (w * h))) * (float) (w * h);  // as prec_tile() thins it
            float m = (float) pool_bytes(live::prec_tile(s, OVERLAY_POOL));
            CHECK_MSG(
                m, "met_live_sky.h's TILE_COST[%d] is too small: a %dx%d tile with %.0f streaks or flakes didn't fit",
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
  // the flag, in whatever light the panel shows it
  Costs flag;
  live::Frames sp;
  live::make_sprite("flag", sp);
  flag.took((float) pool_bytes(sp), live::FLAG_BYTES, {{"FLAG_BYTES", live::FLAG_BYTES, 0, 1}});
  flag.check("FLAG_BYTES");
  log_level = level;
  overlay::status(overlay::shown());
}
