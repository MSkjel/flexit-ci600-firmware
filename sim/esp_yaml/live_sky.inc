// The sky's acts, see met_live_sky.h
// A scene for the weather at t, its colours placed, in a buffer of its own
static const met::Scene &scene_at(const met::Weather &weather, uint32_t t, met::Options o, double lat = OSLO_LAT,
                                  double lon = OSLO_LON) {
  static SceneBuf sc;
  met::Weather w = weather;
  w.time = t / 3600 * 3600;
  met::build(sc.s, w, lat, lon, t, false, oslo_offset, o);
  met::palette(sc.s, nullptr);
  return sc.s;
}

// The forecast over the weather picture: a still of its own on the panel, in front of every animation, so each pixel
// marks.h draws whole is on the screen whatever moves under it. A redraw with the same forecast leaves it be.
static void forecast_over(const char *what) {
  std::string k = marks::overlay_of("weather");
  auto sent = [&] {
    const overlay::Overlay *a = overlay::find(k);
    return a && a->state == overlay::SENT && a->still && a->copies.size() && a->copies[0].placed;
  };
  CHECK_MSG(run_until([&] { return sent() && settled(); }, 600) >= 0, "%s: no forecast over it", what);
  if (!sent())
    return;
  int behind = 0, animations = 0;
  for (const overlay::Overlay &a : overlay::overlays)
    for (const overlay::Copy &c : a.copies)
      if (a.name != k && c.used) {
        animations++;
        behind += c.depth < overlay::find(k)->copies[0].depth;
      }
  CHECK_MSG(animations && behind == animations, "%s: %d of %d animations behind the forecast", what, behind,
            animations);
  size_t bytes = overlay::find(k)->data.size();
  CHECK_MSG(bytes <= (size_t) live::FORECAST_BYTES, "%s: the forecast takes %u bytes, the acts leave it %d", what,
            (unsigned) bytes, live::FORECAST_BYTES);
  static marks::Sharp band;
  const int rows = (int) std::size(band.px) / met::W;
  const marks::Layer &l = marks::all["weather"].layer;
  int whole = 0, off = 0, looks = 0;
  play(4, 0, "", [&] {
    if (looks++ % 9 || looks < 9)
      return;  // once a second, faded in
    for (int y0 = 0; y0 < met::H; y0 += rows) {
      marks::draw_sharp(l, band, y0);
      for (int y = y0; y < y0 + rows; y++)
        for (int x = 0; x < met::W; x++)
          if (uint32_t p = band.px[(y - y0) * met::W + x]; p >> 24 == 255) {
            whole++;
            off += sim_rgb565(sim_pixel(x, y)) != sim_rgb565(p & 0xFFFFFF);
          }
    }
  });
  CHECK_MSG(whole > 2000 && !off, "%s: %d of the forecast's %d pixels not on the screen", what, off, whole);
  met::redraw();
  run(3);  // so the last "Shown" isn't taken for this one
  bool kept = true;
  int r = run_until(
      [&] {
        kept = kept && sent();
        return panel_picture_status.state.rfind("Shown", 0) == 0 && settled() && panel_pic() == 2;
      },
      3000);
  CHECK_MSG(r >= 0 && kept, "%s: the forecast went with a redraw that didn't change it", what);
  met::with_forecast = false;
  met::redraw();
  CHECK_MSG(run_until([&] { return !overlay::find(k) && settled(); }, 600) >= 0, "%s: the forecast stayed when off",
            what);
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
  // Both layers of rain or snow on the panel, none of it over a middle or low cloud and nearly all of the sky under
  // them falling from just below them: the scene the panel has, and the box and shape of each layer's copy
  auto under = [](const char *what) {
    const met::Scene &sc = panel_scene();
    int layers = 0, over = 0, falls = 0;
    for (int i = 0; i < 2; i++) {
      const overlay::Overlay *a = overlay::find(live::key(live::fall_name(i)));
      if (!a || a->state != overlay::SENT || a->copies.empty() || !a->copies[0].placed)
        continue;
      Look l = look(a->copies[0].place);
      const std::vector<uint8_t> &all = a->copies[0].script.bytes;  // its SOFT, fading in under the clouds, and SHAPE
      const size_t soft = 1 + 4 * 2;
      if (all.size() < soft || all[0] != OVERLAY_DO_SOFT || plink_get16(&all[1 + 2 * 2]) != 10)
        continue;
      std::vector<uint8_t> sh(all.begin() + soft, all.end());
      if (!l.shown || sh.size() < 2 || sh[0] != OVERLAY_DO_SHAPE || sh.size() != 2u + 2 * sh[1])
        continue;
      layers++;
      auto shows = [&](int x, int y) {  // whether its box's shape shows x, y on the screen
        for (int k = 0, at = l.x; k < sh[1]; k += 3) {
          int w = plink_i16(&sh[2 + 2 * k]), top = plink_i16(&sh[4 + 2 * k]), bottom = plink_i16(&sh[6 + 2 * k]);
          if (x >= at && x < at + w)
            return y >= l.y + top && y < l.y + bottom && x < l.x + l.w && y < l.y + l.h;
          at += w;
        }
        return false;
      };
      for (int c = 0; c < sc.nclouds; c++) {
        if (!sc.cloud_layer[c])
          continue;
        const met::Cloud &cl = sc.cloud[c];
        int x0 = met::cloud_x(sc, c);
        for (int y = cl.top; y <= cl.base; y++)
          for (int j = 0; j < cl.n; j++) {
            int h = met::half(y - cl.e[j].y, cl.e[j].rx, cl.e[j].ry), cx = x0 + cl.e[j].x;
            for (int k = 0; k < 2 && h >= 0; k++, cx -= met::FIELD)
              for (int x = std::max(0, cx - h); x <= std::min(met::W - 1, cx + h); x++)
                over += shows(x, y);
          }
      }
      for (int x = 0; x < met::W; x++)
        falls += shows(x, std::min(met::H - 1, sc.prec_top[x] + 8));
    }
    CHECK_MSG(layers == 2 && !over && falls >= 2 * met::W * 9 / 10,
              "%s: %d layers, %d px of them over clouds, %d of %d columns falling from under them", what, layers, over,
              falls, 2 * met::W);
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
  // in the mountains the clouds hang in front of the peaks, and the bolt strikes out of them down to the fjord
  show({"Thunderstorm", "rainandthunder", 0, 80, 90, 3, 8, 18, 0, utc(2026, 7, 10, 15), met::G_MOUNTAINS, 0, 40, 2},
       "mountains");
  under("thunder in the mountains");
  int struck = 0;
  for (int i = 0; i < 6; i++)
    for (int y = live::SKY.bolt_y[i]; y < live::SKY.bolt_y[i + 1]; y++)
      struck += y < live::SKY.top[std::clamp(met::bolt_at(live::SKY.bolt_x, live::SKY.bolt_y, i, y), 0, met::W - 1)];
  CHECK_MSG(struck >= 15, "thunder in the mountains: %d rows of the bolt in front of them", struck);
  live::flash();
  tick(9);
  play(1, 9, "thunderstorm_mountains_flash");
  show({"Light rain", "heavyrain", 0, 60, 100, 5, 12, 8, 0, utc(2026, 10, 20, 12), met::G_COAST, 0, 40, 2}, "heavy");
  under("heavy rain");
  show({"Thunderstorm", "heavyrainandthunder", 0, 90, 100, 6, 10, 20, 0, utc(2026, 7, 10, 15), met::G_CITY, 0, 40, 2},
       "town");
  under("heavy thunder in town");
  // and with the forecast over the busiest of them
  met::with_forecast = true;
  show({"Thunderstorm", "heavyrainandthunder", 0, 90, 100, 6, 10, 20, 0, utc(2026, 7, 10, 15), met::G_CITY, 0, 40, 2},
       "town_forecast");
  forecast_over("heavy thunder in town");
  // and a whole forecast on the coast with the waves, the most it takes
  met::with_forecast = true;
  met::sea = {};
  met::sea.ok = true;
  met::sea.hours = 1;
  met::sea.time[0] = utc(2026, 10, 20, 12);
  met::sea.height[0] = 3.4f;
  show({"Light rain", "heavyrain", 0, 60, 100, 5, 12, 8, 0, utc(2026, 10, 20, 12), met::G_COAST, 0, 10, 0});
  for (int i = met::weather.hours; i < met::HOURS; i++)
    met::weather.hour[met::weather.hours++] = {met::weather.time + 3600u * i, -18.0f + (float) i, met::I_THUNDER};
  met::redraw();
  CHECK_MSG(picture("a whole forecast on the coast", 6000) >= 0, "a whole forecast on the coast: not drawn");
  forecast_over("a whole forecast and the waves on the coast");
  met::sea = {};

  // fog, thick and thin, and on a flag day
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 60, 2}, "thick");
  const overlay::Overlay *bank = overlay::find(live::key("fog0"));
  CHECK_MSG(bank && bank->field && overlay::find(live::key("fog1")) && pn_overlay_used() < 300 && same_pool(),
            "the fog's two banks are fields the panel works out: %u bytes there", pn_overlay_used());
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 10, 0});
  show({"Fog in the valley", "cloudy", 0, 0, 30, 0, 1, 6, 40, utc(2026, 10, 20, 10), met::G_MOUNTAINS, 0, 60, 2},
       "thin");
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 10, 0});
  show({"Fog in the valley + The flag", "fog", 0, 0, 0, 0, 2, 14, 70, utc(2026, 7, 4, 10), met::G_HILLS, 0, 60, 2},
       "flag");

  // the aurora on nights up north, still in the picture and changing with it, under the ground's own night acts: its
  // wide ribbon glows, a field behind the land. The town's antenna blinks over it.
  auto aurora = [](const Live &c, const char *tag) {
    show(c, tag);
    CHECK_MSG(scene_at(met::weather, c.t, met::options, c.lat, c.lon).aurora, "%s with the aurora: no aurora", c.want);
    const overlay::Overlay *l = overlay::find(live::key("lights"));
    CHECK_MSG(l && l->field && l->state == overlay::SENT && l->copies[0].placed && l->copies[0].depth == met::D_SKY &&
                  look(l->copies[0].place).shown && same_pool(),
              "%s with the aurora: its glow %s", c.want, l ? "isn't as it should be" : "isn't there");
  };
  aurora({"The stars + The northern lights", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 15, 21, 10),
          met::G_HILLS, 0, 30, 2, 69.65, 18.96},
         "aurora");
  aurora({"The lighthouse + Light on the water + The stars + The northern lights", "clearsky_night", 0, 0, 0, 0, 3, -12,
          0, utc(2026, 12, 5, 19), met::G_COAST, 0, 30, 2, 69.65, 18.96},
         "aurora");
  aurora({"The stars + The northern lights", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, utc(2026, 2, 3, 20),
          met::G_MOUNTAINS, 0, 30, 2, 67.28, 14.4},
         "aurora_bodo");
  const Live town = {"Evening in town + The antenna's light + The northern lights",
                     "clearsky_night",
                     0,
                     0,
                     0,
                     0,
                     2,
                     -8,
                     0,
                     utc(2026, 1, 15, 21, 10),
                     met::G_CITY,
                     0,
                     30,
                     2,
                     69.65,
                     18.96};
  aurora(town, "aurora");
  const met::Scene &sky = scene_at(met::weather, town.t, met::options, town.lat, town.lon);
  int red = 0, dark = 0;
  play(6, 0, "", [&] {
    uint32_t c = sim_pixel(sky.antenna.x + 1, sky.antenna.y);
    (c >> 16 > 0xD0 && (c >> 8 & 255) < 0x60 ? red : dark)++;
  });
  CHECK_MSG(red && dark, "the town with the aurora: the antenna doesn't blink, %d red and %d dark", red, dark);

  // stars with the moon up, and a shooting star that keeps clear of it; on a flag day in the polar night
  show({"The stars + By the cabin at night", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 2, 21), met::G_HILLS,
        0, 60, 2},
       "moon");
  CHECK_MSG(!overlay::find(live::key("lights")), "no aurora, and its glow is still there");
  show_later();
  CHECK_MSG(visit("The stars", 1), "no clear sky for a shooting star");
  auto *star = overlay::find("w_shooting");
  for (int k = 0; k < 60 && star && !star->copies[0].placed; k++)
    tick(1);
  CHECK_MSG(star && star->copies[0].placed, "no shooting star");
  play(1, 9, "starry_winter_night_shooting");
  visits("The stars", "starry", 3);  // a satellite, shooting stars
  visits("By the cabin at night", "starry_fox", 3, {"fox"});
  show({"The stars + The flag", "clearsky_night", 0, 0, 0, 0, 2, -14, 0, utc(2026, 1, 21, 10), met::G_HILLS, 0, 30, 2,
        78.22, 15.65},
       "svalbard");

  // high clouds, and on a flag day
  show({"High clouds", "fair_day", 60, 0, 0, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 60, 2}, "calm");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"High clouds", "fair_day", 80, 0, 0, 0, 7, 8, 0, utc(2026, 4, 20, 12), met::G_HILLS, 0, 60, 2}, "windy");
  size_t was = log_all.size();
  on(3600, true, "high clouds the next hour");
  play(4, 1, "high_clouds_next_hour");
  CHECK_MSG(logged("Animation w_cloud0:", was), "high clouds the next hour: not made again");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"High clouds + The flag", "fair_day", 40, 0, 0, 0, 7, 16, 0, utc(2026, 6, 7, 11), met::G_HILLS, 0, 60, 2},
       "flag");

  // drifting clouds, and on a flag day; with too many clouds for the panel they stay the picture's
  show({"Drifting clouds", "fair_day", 0, 30, 0, 0, 4, 8, 0, utc(2026, 5, 5, 8), met::G_HILLS, 0, 90, 3}, "mid");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"Drifting clouds", "fair_day", 0, 10, 15, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 60, 2}, "low");
  show({"Fog in the valley", "fog", 0, 0, 0, 0, 1, 6, 80, utc(2026, 10, 20, 10), met::G_HILLS, 0, 10, 0});
  show({"Drifting clouds + The flag", "fair_day", 0, 30, 0, 0, 7, 16, 0, utc(2026, 6, 7, 11), met::G_HILLS, 0, 60, 2},
       "flag");
  weather_of({"Drifting clouds + Crows", "cloudy", 0, 60, 80, 0, 4, 8, 0, utc(2026, 5, 5, 10), met::G_HILLS, 0, 10, 0});
  bool drawn = picture("too many clouds", 6000) >= 0;
  live::Room clouds = live::clouds_cost(panel_scene(), false);
  CHECK_MSG(
      drawn && !(live::whole(panel_scene()) - live::VISIT).has(clouds) && is_on("Crows") && !is_on("Drifting clouds"),
      "an overcast sky's %d clouds of %d bytes: %s on", clouds.slots, clouds.bytes, cast_on().c_str());
}

// After the other acts' tests, so their chance events stay as they were: what the acts keep (the rain's frame, the
// flag's light) and the heater's smoke
static void live_kept() {
  step("the weather animations: the rain's frame, the flag's light and the heater (packages/code/met/met_live.h)");
  // A rain still's whole frame for overlay.h is freed once the still is made. Without the RAM for it the still is
  // empty, nothing worse.
  show({"Light rain", "lightrain", 0, 30, 50, 0.3f, 2, 8, 0, utc(2026, 10, 20, 12), met::G_HILLS, 30, 10, 0});
  tick(1);
  CHECK_MSG(!live::R.px.capacity(), "the rain's frame kept: %u bytes", (unsigned) live::R.px.capacity());
  {  // asked of frame_pixel() itself, as overlay.h takes more than the frame and wouldn't get that far
    const live::Frames *tile = live::frames_of(live::fall_name(0));
    size_t need = (size_t) tile->w * tile->h, from = log_all.size();
    ram_block = need - 1;
    uint32_t px = live::frame_pixel(tile, 0, 0, 0);
    ram_block = 100 * 1024;
    CHECK_MSG(!px && live::R.px.empty() && logged("No RAM to draw the frames of a", from),
              "the rain without %u bytes for its frame", (unsigned) need);
    live::free_px();
  }
  // the flag follows the light on the panel, neither it nor another act made again: up at 9 on the King's birthday,
  // brighter by noon
  tz_offset_s = 3600;
  show({"Crows + About the cabin + The flag", "clearsky_day", 0, 0, 0, 0, 3, 5, 0, utc(2026, 2, 21, 8), met::G_HILLS, 0,
        10, 0});
  overlay::Overlay *flag = overlay::find(live::key("flag"));
  int at = flag ? flag->copies[0].place : 0, morning = look(at).light;
  size_t was = log_all.size();
  flexit_panel_time.t.timestamp += 3 * 3600;
  met::redraw();
  CHECK_MSG(picture("the flag at noon", 6000) >= 0, "the flag at noon: not drawn again");
  play(4, 0, "");
  CHECK_MSG(flag && is_on("The flag") && morning > 0 && morning < 1000 && look(at).light == 1000 &&
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
  size_t n = overlay::W.phase == overlay::SEND && !a.half ? a.panel_frame() + a.data.size() : 0;
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

// What the sky's acts plan to take of the pool, met_live_sky.h's FALL_COST and the rest, against the panel's own
// encoder. A change to pdeflate or the records shows up here, not as an act that's too big.
static void live_costs() {
  step("the weather animations' sizes, measured (packages/code/met/met_live_sky.h)");
  run(30);
  CHECK_MSG(overlay::W.phase == overlay::IDLE && !overlay::used_by_others(nullptr), "animations on: %u bytes",
            (unsigned) overlay::used_by_others(nullptr));
  int level = log_level;
  log_level = 0;
  // the stills of rain, snow and sleet, upright and slanting, sparse to dense
  const met::Scene &s = panel_scene();
  const live::FallCost &c = live::FALL_COST;
  Costs falls;
  for (int k = 0; k < 3; k++)
    for (int slant : {0, 2, 4, 12, 24})
      for (float d : {0.0005f, 0.002f, 0.004f, 0.008f, 0.012f}) {
        if (k != 1 && slant > 4)
          continue;  // only snow is blown more
        live::SKY.kind = k;
        live::SKY.slant = slant;
        live::layers(s, k, d, false, live::SKY.layer);
        for (int i = 0; i < 2; i++) {
          const live::Layer &l = live::SKY.layer[i];
          float rows = 0, m = (float) pool_bytes(live::fall_still(i));
          for (int j = 0; j < l.falls; j++)
            rows += l.fall[j].dens * (float) (l.w * l.h) * l.fall[j].size;
          CHECK_MSG(m, "a %dx%d still of rain or snow didn't fit", l.w, l.h);
          falls.took(m, live::fall_bytes(l),
                     {{"FALL_COST.start", (float) c.start, (float) (c.row * l.h) + (float) c.each * rows, 1},
                      {"FALL_COST.row", (float) c.row, (float) c.start + (float) c.each * rows, (float) l.h},
                      {"FALL_COST.each", (float) c.each, (float) (c.start + c.row * l.h), rows}});
        }
      }
  falls.check("FALL_COST");
  // the tiles of snow and sleet whose flakes sway each on its own
  Costs sway;
  const live::SwayCost &w = live::SWAY_COST;
  for (int k = 1; k < 3; k++)
    for (float d : {0.0005f, 0.002f, 0.004f, 0.008f, 0.012f}) {
      live::SKY.kind = k;
      live::layers(s, k, d, true, live::SKY.layer);
      for (int i = 0; i < 2; i++) {
        const live::Layer &l = live::SKY.layer[i];
        if (!l.frames)
          continue;
        float flakes = 0, frame = (float) ((l.w + 1) / 2 * l.h), m = (float) pool_bytes(live::fall_still(i));
        for (int j = 0; j < l.falls; j++)
          flakes += l.fall[j].dens * (float) (l.w * l.h);
        CHECK_MSG(m, "a %dx%d tile of swaying snow didn't fit", l.w, l.h);
        sway.took(m, live::fall_bytes(l),
                  {{"SWAY_COST.start", (float) w.start, frame + (float) w.each * flakes, 1},
                   {"SWAY_COST.each", (float) w.each, frame + (float) w.start, flakes}});
      }
    }
  sway.check("SWAY_COST");
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
    float m = (float) pool_bytes(live::bolt(t.bolt_x, t.bolt_y, n, &x, &y)), rows = (float) b.h;
    bolt.took(m, live::BOLT_ROW * rows + live::BOLT_START,
              {{"BOLT_START", live::BOLT_START, live::BOLT_ROW * rows, 1},
               {"BOLT_ROW", live::BOLT_ROW, live::BOLT_START, rows}});
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
