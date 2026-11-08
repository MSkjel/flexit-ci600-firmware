// The town shows, see met_live_town.h
static void live_town() {
  step("the weather animations: the town (packages/code/met/met_live_town.h)");
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots, lat lon
  // a September afternoon, the sun low over the sea
  show({"Sailboat, glitter and eiders", "clearsky_day", 0, 0, 0, 0, 3, 14, 0, utc(2026, 9, 20, 15), met::G_COAST, 0,
        150, 5},
       "oslo");
  visits("sail");  // a sea eagle, a sjark, a porpoise
  {  // the sailboat on the water, behind the cabin's pole and all of its pennant, but not where it's on the sea
    const overlay::Overlay *boat = overlay::find("w_boat");
    const met::Scene &s = panel_scene();
    int k = boat && !boat->copies.empty() ? boat->copies[0].place : -1;
    Look l = look(k);
    pn_saver(1);
    bool pole = pn_overlay_covered(k, s.pole_x, s.pole_top + 4), sea = !pn_overlay_covered(k, l.x + 6, l.y + 7);
    int pennant = 0, behind = 0;
    for (int i = 0; i < s.nstamps; i++) {
      const met::Stamp &q = s.stamps->at[i];
      for (int y = 0; y < q.h && q.tag == met::L_FLAG; y++)
        for (int x = 0; x < q.w; x++)
          if (q.rows[y] >> (q.w - 1 - x) & 1) {
            pennant++;
            behind += pn_overlay_covered(k, q.x + x, q.y + y);
          }
    }
    pn_saver(0);
    CHECK_MSG(l.shown && l.depth == met::D_SEA && s.pole_x >= 0 && pole && sea && pennant && behind == pennant,
              "the sailboat at %d,%d, %d deep, behind the pole at %d %d, on the sea %d, behind %d of the pennant's %d",
              l.x, l.y, l.depth, s.pole_x, pole, sea, behind, pennant);
  }
  {  // its script on the panel: to the end of its course, about over the edge on frames there, and back again
    const overlay::Overlay *boat = overlay::find("w_boat");
    int k = boat && !boat->copies.empty() ? boat->copies[0].place : -1;
    Look was = look(k);
    int lo = was.x, hi = was.x, flips = 0, edge_on = 0, edge_on_moving = 0, wrapped = 0;
    for (int t = 0; t < 900000 && flips < 3; t += 50) {
      pn_overlay_step(50);
      if (t % 2000 == 0)
        run(1);
      Look l = look(k);
      lo = std::min(lo, l.x);
      hi = std::max(hi, l.x);
      flips += l.flip != was.flip;
      wrapped += abs(l.x - was.x) > 1;
      if (t % 250 == 0) {
        char f = frame_at("w_boat", 9, k);
        edge_on += f >= '5' && f <= '7';
        edge_on_moving += f >= '5' && f <= '7' && l.x != was.x;
      }
      was = l;
    }
    CHECK_MSG(flips >= 3 && edge_on > 3 && !edge_on_moving && !wrapped && hi - lo > 40,
              "the sailboat from %d to %d, about %d times, edge on %d times, %d of them moving, %d jumps", lo, hi,
              flips, edge_on, edge_on_moving, wrapped);
  }
  // July at noon up north, the sun too high for a reflection
  show({"Sailboat, glitter and eiders", "fair_day", 10, 0, 0, 0, 5, 20, 0, utc(2026, 7, 15, 11), met::G_COAST, 0, 60, 2,
        64.38, 25.4},
       "north");
  // a clear November night, the coastal express going by
  show({"Lighthouse and the coastal express", "clearsky_night", 0, 0, 0, 0, 3, 4, 0, utc(2026, 11, 10, 20),
        met::G_COAST, 0, 90, 3},
       "oslo", true);
  // November dusk, a tram going by
  show({"Evening in town", "partlycloudy_night", 0, 30, 0, 0, 3, 4, 0, utc(2026, 11, 10, 17), met::G_CITY, 0, 90, 3},
       "oslo", true);
  visits("street_evening");  // a tram, the night bus, a plane
  // Christmas Day in Tromso, a flag day in the polar night
  show({"Evening in town", "cloudy", 0, 0, 80, 0, 3, -6, 0, utc(2026, 12, 25, 10), met::G_CITY, 0, 30, 1, 69.65, 18.96},
       "tromso", true);
  CHECK_MSG(live::R.visit(2), "snow lying in Tromso: no plough");
  play(42, 3, "street_plough");
  // 7 June, a flag day, a tram going by
  show({"A day in town", "fair_day", 0, 20, 0, 0, 3, 18, 0, utc(2026, 6, 7, 12), met::G_CITY, 0, 60, 2}, "flag", true);
  visits("street_day");  // a tram, the bus, a cyclist
  {                      // and the flag to the right in an east wind, without starting again
    size_t from = log_all.size();
    met::weather.wind_from = 20;
    met::redraw();
    CHECK_MSG(picture("A day in town, an east wind", 6000) >= 0, "the east wind's picture not shown");
    play(4, 1, "a_day_in_town_east");
    CHECK_MSG(!logged("Weather animation:", from), "the east wind started the show again");
    const overlay::Overlay *flag = overlay::find("w_flag");
    Look l = flag ? look(flag->copies[0].place) : Look{};
    int pole = panel_scene().pole_x;
    CHECK_MSG(l.shown && l.x == pole + 1 && l.flip == OVERLAY_FLIP_X,  // its left next to the pole
              "the flag in an east wind: at %d, flip %d, the pole at %d", l.x, l.flip, pole);
  }
  show({"A day in town", "partlycloudy_day", 0, 40, 30, 0, 6, 12, 0, utc(2026, 9, 20, 10), met::G_CITY, 0, 30, 1},
       "autumn");
}
