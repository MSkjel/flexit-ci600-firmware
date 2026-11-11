// The acts by the sea and in town, see met_live_town.h
static void live_town() {
  step("the weather animations: the town (packages/code/met/met_live_town.h)");
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots, lat lon
  // a September afternoon, the sun low over the sea
  show({"The sailboat + Light on the water + Glitter on the water + Eiders + Off the coast", "clearsky_day", 0, 0, 0, 0,
        3, 14, 0, utc(2026, 9, 20, 15), met::G_COAST, 0, 150, 5},
       "oslo");
  {  // the light on the water has its rows bound, narrow and faint at the horizon, next to its wave
    const overlay::Overlay *sea = overlay::find("w_shimmer");
    int bound = 0;
    for (auto &t : sea ? sea->copies[0].told : std::vector<overlay::Copy::Told>())
      bound += t.key == K_OVERLAY_BIND;
    CHECK_MSG(bound == 3 && same_pool(), "the shimmer's wave and its rows: %d bound", bound);
  }
  visits("Off the coast", "sail");  // a sea eagle, a sjark, a porpoise
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
  show({"The sailboat + Light on the water + Eiders + Off the coast", "fair_day", 10, 0, 0, 0, 5, 20, 0,
        utc(2026, 7, 15, 11), met::G_COAST, 0, 60, 2, 64.38, 25.4},
       "north");
  CHECK_MSG(panel_scene().shine_x < 0, "July at noon up north: the sun's reflection at %d", panel_scene().shine_x);
  // a clear November night, the coastal express going by
  show({"The lighthouse + Light on the water + The stars", "clearsky_night", 0, 0, 0, 0, 3, 4, 0, utc(2026, 11, 10, 20),
        met::G_COAST, 0, 90, 3},
       "oslo", true);
  {  // the beam turns round its lamp, to one side, through you and to the other, the lamp flaring as it faces you and
     // its glare across pulled in between
    int left = 0, right = 0, off = 0, flare = 0, dim = 1000, glare = 0, pulled = 1000;
    play(10, 0, "", [&] {  // a round and more
      Look b, g;
      if (!where("beam", b) || !where("lamp", g))
        return;
      left += b.size_x > 0;
      right += b.size_x < 0;
      off += (b.size_x > 0 ? b.x1 : b.x2) != met::LH_X;
      flare = std::max(flare, g.opa);
      dim = std::min(dim, g.opa);
      glare = std::max(glare, g.size_x);
      pulled = std::min(pulled, g.size_x);
    });
    CHECK_MSG(left > 20 && right > 20 && !off && flare > 900 && dim < 500 && glare > 900 && pulled < 300,
              "the beam: %d cycles left, %d right, %d off its lamp, the lamp's glow %d to %d, its glare %d to %d", left,
              right, off, dim, flare, pulled, glare);
  }
  // November dusk, a tram going by
  show({"Evening in town + The antenna's light", "partlycloudy_night", 0, 30, 0, 0, 3, 4, 0, utc(2026, 11, 10, 17),
        met::G_CITY, 0, 90, 3},
       "oslo", true);
  visits("Evening in town", "street_evening");  // a tram, the night bus, a plane
  // Christmas Day in Tromso, a flag day in the polar night
  show({"Evening in town + The antenna's light", "cloudy", 0, 0, 80, 0, 3, -6, 0, utc(2026, 12, 25, 10), met::G_CITY, 0,
        30, 1, 69.65, 18.96},
       "tromso", true);
  visitor_gone();
  CHECK_MSG(visit("Evening in town", 2), "snow lying in Tromso: no plough");
  play(42, 3, "street_plough");
  // 7 June, a flag day, a tram going by
  show({"A day in town + The flag", "fair_day", 0, 20, 0, 0, 3, 18, 0, utc(2026, 6, 7, 12), met::G_CITY, 0, 60, 2},
       "flag", true);
  visits("A day in town", "street_day");  // one of the gulls down, a tram, the bus, a cyclist
  {  // one of the two gulls comes down on a flat roof, small as far off as the town is, or on the park's pole
    std::vector<live::Perch> where = live::perches(panel_scene(), false);
    int roofs = 0;
    for (const live::Perch &p : where)
      roofs += p.size == live::FAR_BIRD && p.depth == met::D_MID_ON;
    Down d = landing("town_land", "gullvisit", "gull", 0, [] { return visit("A day in town", 0); });
    CHECK_MSG(roofs && d.still >= 18 && stood_on(d, where) && d.most == 2 && d.back,
              "a gull of the town's two came down: %d flat roofs to stand on, still for %d cycles at %d to %d, row %d, "
              "%d per mille, %d birds in view at the most, both flying after %d",
              roofs, d.still, d.at.x1, d.at.x2, d.at.y2, d.at.size, d.most, d.back);
  }
  {  // and the flag to the right in an east wind, without starting again
    size_t from = log_all.size();
    met::weather.wind_from = 20;
    met::redraw();
    CHECK_MSG(picture("A day in town, an east wind", 6000) >= 0, "the east wind's picture not shown");
    play(4, 1, "a_day_in_town_east");
    CHECK_MSG(!logged("Weather animation:", from), "the east wind started an act again");
    const overlay::Overlay *flag = overlay::find("w_flag");
    Look l = flag ? look(flag->copies[0].place) : Look{};
    int pole = panel_scene().pole_x;
    CHECK_MSG(l.shown && l.x == pole + 1 && l.flip == OVERLAY_VIEW_FLIP_X,  // its left next to the pole
              "the flag in an east wind: at %d, flip %d, the pole at %d", l.x, l.flip, pole);
  }
  show({"A day in town", "partlycloudy_day", 0, 40, 30, 0, 6, 12, 0, utc(2026, 9, 20, 10), met::G_CITY, 0, 30, 1},
       "autumn");
}
