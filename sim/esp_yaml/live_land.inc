// The land shows, see met_live_land.h
static void live_land() {
  step("the weather animations: the land (packages/code/met/met_live_land.h)");
  // More of the show that's on, as live/TAG_n.ppm, with its chance events now if chances
  auto more = [](const char *tag, bool chances, int seconds, int shots) {
    size_t from = log_all.size();
    if (chances)
      chances_now();
    play(seconds, shots, tag);
    live_quiet(tag, from);
  };
  // the same show again with the wind from the north-east, blowing things to the right
  auto east = [](const char *want) {
    met::weather.wind_from = 20;
    live::stop();
    met::redraw();
    CHECK(picture(want, 6000) >= 0 && !strcmp(show_on(), want), "%s with the wind the other way", want);
    CHECK(panel_scene().at.wdir > 0, "%s: the wind from the north-east doesn't blow things to the right", want);
  };
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 7, 18, 0, utc(2026, 6, 10, 11), met::G_HILLS, 0, 30, 2});
  more("gulls_visit", true, 52, 6);
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 11, 18, 0, utc(2026, 7, 4, 11), met::G_COAST, 0, 20, 0});
  more("gulls_coast", false, 10, 1);
  east("Gulls over the cabin");
  more("gulls_coast_east", true, 52, 4);
  show({"Stove smoke and an advent star", "clearsky_day", 0, 0, 0, 0, 3, 3, 0, utc(2026, 12, 10, 11), met::G_HILLS, 60,
        40, 4});
  show({"Stove smoke and an advent star", "clearsky_day", 0, 0, 0, 0, 12, 3, 0, utc(2026, 12, 25, 11), met::G_COAST, 90,
        30, 0});
  more("smoke_coast_windy", false, 20, 2);
  show({"Stove smoke and an advent star", "cloudy", 0, 70, 0, 0, 3, 3, 0, utc(2026, 3, 10, 2), met::G_MOUNTAINS, 60, 30,
        0});
  more("smoke_mountains_night", false, 20, 2);
  east("Stove smoke and an advent star");
  more("smoke_mountains_east", false, 30, 2);
  show({"A cosy evening", "cloudy", 0, 70, 0, 0, 3, 3, 0, utc(2026, 1, 15, 18), met::G_HILLS, 90, 40, 2});
  show({"A cosy evening", "cloudy", 0, 70, 0, 0, 6, 3, 0, utc(2026, 1, 15, 18), met::G_MOUNTAINS, 90, 30, 0});
  more("cosy_mountains", false, 20, 2);
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 10, 5, 16, 45), met::G_HILLS, 0, 20, 1});
  more("moose_walk", true, 80, 8);
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 5, 1, 18, 40), met::G_MOUNTAINS, 0, 20, 0});
  more("moose_mountains", true, 80, 4);
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, 18, 0, utc(2026, 6, 10, 11), met::G_MOUNTAINS, 0,
        40, 2});
  more("reindeer_eagle", true, 60, 4);
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, 18, 0, utc(2026, 6, 7, 15), met::G_MOUNTAINS, 0,
        20, 0});
  more("reindeer_eagle_left", true, 60, 3);
  show({"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 3, -5, 0, utc(2026, 2, 7, 11), met::G_HILLS, 0,
        20, 1});
  more("skier_by", true, 50, 6);
  show({"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 11, -5, 0, utc(2026, 2, 6, 11), met::G_COAST,
        0, 20, 0});
  east("A skier, and snow off the spruces");
  more("skier_coast_east", true, 50, 4);
  show({"Summer butterflies", "clearsky_day", 0, 0, 0, 0, 3, 20, 0, utc(2026, 7, 29, 11), met::G_HILLS, 0, 60, 4});
  show({"Summer butterflies", "clearsky_day", 0, 0, 0, 0, 2, 20, 0, utc(2026, 7, 15, 11), met::G_HILLS, 0, 30, 0});
  more("butterflies_plain", false, 20, 2);
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 4, 10, 0, utc(2026, 10, 5, 11), met::G_HILLS, 0, 30, 2});
  more("leaves_fall", true, 60, 6);
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 11, 10, 0, utc(2026, 10, 6, 11), met::G_COAST, 0, 20, 0});
  east("Birch leaves and geese");
  more("leaves_coast_east", true, 40, 3);
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 4, 10, 0, utc(2029, 9, 10, 11), met::G_HILLS, 0, 30, 0});
  more("leaves_flag", true, 60, 3);
}
