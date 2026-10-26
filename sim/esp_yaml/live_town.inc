// The town shows, see met_live_town.h
static void live_town() {
  step("the weather animations: the town (packages/code/met/met_live_town.h)");
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots, lat lon
  // a September afternoon, the sun low over the sea
  show({"Sailboat, glitter and eiders", "clearsky_day", 0, 0, 0, 0, 3, 14, 0, utc(2026, 9, 20, 15), met::G_COAST, 0,
        150, 5},
       "oslo");
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
  // Christmas Day in Tromso, a flag day in the polar night
  show({"Evening in town", "cloudy", 0, 0, 80, 0, 3, -6, 0, utc(2026, 12, 25, 10), met::G_CITY, 0, 30, 1, 69.65, 18.96},
       "tromso", true);
  // 7 June, a flag day, a tram going by
  show({"A day in town", "fair_day", 0, 20, 0, 0, 3, 18, 0, utc(2026, 6, 7, 12), met::G_CITY, 0, 60, 2}, "flag", true);
  {  // and the flag to the right in an east wind, without starting again
    size_t from = log_all.size();
    met::weather.wind_from = 20;
    met::redraw();
    CHECK_MSG(picture("A day in town, an east wind", 6000) >= 0, "the east wind's picture not shown");
    play(4, 1, "a_day_in_town_east");
    CHECK_MSG(!logged("Weather animation:", from), "the east wind started the show again");
    const anim::Anim *flag = anim::find("w_flag");
    Look l = flag ? look(flag->copies[0].place) : Look{};
    int pole = panel_scene().pole_x;
    CHECK_MSG(l.shown && l.x == pole && l.flip == ANIM_FLIP_X,
              "the flag in an east wind: at %d, flip %d, the pole at %d", l.x, l.flip, pole);
  }
  show({"A day in town", "partlycloudy_day", 0, 40, 30, 0, 6, 12, 0, utc(2026, 9, 20, 10), met::G_CITY, 0, 30, 1},
       "autumn");
}
