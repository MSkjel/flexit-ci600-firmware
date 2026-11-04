// The land shows, see met_live_land.h

// Watches the copies of sprite fly over the picture, as the panel here moves them on, once a second: how often they
// went across it, how often of those facing the other way, and how often they turned round
struct Facing {
  const char *sprite;
  int moves = 0, wrong = 0, turns = 0, cycle = 0;
  std::vector<Look> was;
  explicit Facing(const char *sprite) : sprite(sprite) {}
  void operator()() {
    const overlay::Overlay *a = overlay::find(std::string("w_") + sprite);
    if (!a || cycle++ % 9)
      return;
    was.resize(a->copies.size());
    for (size_t i = 0; i < a->copies.size(); i++) {
      Look l = look(a->copies[i].place);
      auto in = [&](const Look &k) { return k.shown && k.x >= 0 && k.x + a->w <= met::W; };
      int dx = l.x - was[i].x, dy = l.y - was[i].y;
      if (in(l) && in(was[i]) && abs(dx) >= 2 && abs(dx) >= abs(dy)) {
        moves++;
        wrong += (dx < 0) != (bool) (l.flip & OVERLAY_FLIP_X);
      }
      turns += was[i].shown && ((l.flip ^ was[i].flip) & OVERLAY_FLIP_X);
      was[i] = l;
    }
  }
};

static void live_land() {
  step("the weather animations: the land (packages/code/met/met_live_land.h)");
  // More of the show that's on, as live/TAG_n.ppm, with its chance events now if chances
  auto more = [](const char *tag, bool chances, int seconds, int shots, const std::function<void()> &each = {}) {
    size_t from = log_all.size();
    if (chances)
      chances_now();
    play(seconds, shots, tag, each);
    live_quiet(tag, from);
  };
  // the same show again with the wind from the north-east, blowing things to the right
  auto east = [](const char *want) {
    met::weather.wind_from = 20;
    live::stop();
    met::redraw();
    CHECK_MSG(picture(want, 6000) >= 0 && !strcmp(show_on(), want), "%s with the wind the other way", want);
    CHECK_MSG(panel_scene().at.wdir > 0, "%s: the wind from the north-east doesn't blow things to the right", want);
  };
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 7, 18, 0, utc(2026, 6, 10, 11), met::G_HILLS, 0, 30, 2});
  more("gulls_visit", true, 52, 6);
  visits("roof", 2, {"squirrel"});  // a gull, a crow and a squirrel
  show({"Gulls over the cabin", "clearsky_day", 0, 0, 0, 0, 11, 18, 0, utc(2026, 7, 4, 11), met::G_COAST, 0, 20, 0});
  more("gulls_coast", false, 10, 1);
  // the gulls fly back and forth facing the way they go, and turn round out of sight
  Facing gulls{"gull"};
  play(120, 0, "gulls_facing", std::ref(gulls));
  CHECK_MSG(gulls.moves >= 100 && !gulls.wrong && gulls.turns >= 2,
            "the gulls: %d of %d moves facing the other way, %d turns", gulls.wrong, gulls.moves, gulls.turns);
  east("Gulls over the cabin");
  more("gulls_coast_east", true, 52, 4);
  show({"Stove smoke and an advent star", "clearsky_day", 0, 0, 0, 0, 3, 3, 0, utc(2026, 12, 10, 11), met::G_HILLS, 60,
        40, 4});
  visits("magpie");
  show({"Stove smoke and an advent star", "clearsky_day", 0, 0, 0, 0, 12, 3, 0, utc(2026, 12, 25, 11), met::G_COAST, 90,
        30, 0});
  more("smoke_coast_windy", false, 20, 2);
  show({"Stove smoke and an advent star", "cloudy", 0, 70, 0, 0, 3, 3, 0, utc(2026, 3, 10, 2), met::G_MOUNTAINS, 60, 30,
        0});
  more("smoke_mountains_night", false, 20, 2);
  east("Stove smoke and an advent star");
  more("smoke_mountains_east", false, 30, 2);
  show({"A cosy evening", "cloudy", 0, 70, 0, 0, 3, 3, 0, utc(2026, 1, 15, 18), met::G_HILLS, 90, 40, 2});
  visits("cosy", 3, {"fox"});  // the cat, a fox
  show({"A cosy evening", "cloudy", 0, 70, 0, 0, 6, 3, 0, utc(2026, 1, 15, 18), met::G_MOUNTAINS, 90, 30, 0});
  more("cosy_mountains", false, 20, 2);
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 10, 5, 16, 45), met::G_HILLS, 0, 20, 1});
  more("moose_walk", true, 80, 8);
  visits("roe_deer", 2, {"roedeerrun"});
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 5, 1, 18, 40), met::G_MOUNTAINS, 0, 20, 0});
  more("moose_mountains", true, 80, 4);
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, 18, 0, utc(2026, 6, 10, 11), met::G_MOUNTAINS, 0,
        40, 2});
  Facing eagle{"eagle"};  // circling, it faces the way it goes
  more("reindeer_eagle", true, 60, 4, std::ref(eagle));
  CHECK_MSG(eagle.moves >= 20 && !eagle.wrong && eagle.turns >= 2,
            "the eagle: %d of %d moves facing the other way, %d turns", eagle.wrong, eagle.moves, eagle.turns);
  visits("reindeer", 3);  // a hiker, ptarmigan
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, -8, 0, utc(2026, 2, 7, 11), met::G_MOUNTAINS, 0,
        10, 0},
       "snow");
  CHECK_MSG(live::R.visit(1), "snow in the mountains: no ptarmigan");
  play(30, 3, "reindeer_ptarmiganw");
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, 18, 0, utc(2026, 6, 7, 15), met::G_MOUNTAINS, 0,
        20, 0});
  more("reindeer_eagle_left", true, 60, 3);
  show({"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 3, -5, 0, utc(2026, 2, 7, 11), met::G_HILLS, 0,
        20, 1});
  more("skier_by", true, 50, 6);
  visits("snow", 2, {"harew"});  // a kick-sled, a sledge, a hare, a magpie
  show({"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 11, -5, 0, utc(2026, 2, 6, 11), met::G_COAST,
        0, 20, 0});
  east("A skier, and snow off the spruces");
  more("skier_coast_east", true, 50, 4);
  show({"Summer butterflies", "clearsky_day", 0, 0, 0, 0, 3, 20, 0, utc(2026, 7, 29, 11), met::G_HILLS, 0, 60, 4});
  show({"Summer butterflies", "clearsky_day", 0, 0, 0, 0, 2, 20, 0, utc(2026, 7, 15, 11), met::G_HILLS, 0, 30, 0});
  more("butterflies_plain", false, 20, 2);
  visits("summer");  // swallows, the mower
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 4, 10, 0, utc(2026, 10, 5, 11), met::G_HILLS, 0, 30, 2});
  more("leaves_fall", true, 60, 6);
  visits("autumn", 2, {"squirrel", "mushroomer"});  // a squirrel, someone picking mushrooms
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 11, 10, 0, utc(2026, 10, 6, 11), met::G_COAST, 0, 20, 0});
  east("Birch leaves and geese");
  more("leaves_coast_east", true, 40, 3);
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 4, 10, 0, utc(2029, 9, 10, 11), met::G_HILLS, 0, 30, 0});
  more("leaves_flag", true, 60, 3);
  // and each visitor with a leaf falling, the last one blowing off, and the geese going over, room for them all and
  // the flag
  CHECK_MSG(live::R.flag, "10 September without the flag");
  for (int k = 0; k < live::R.visits; k++) {
    size_t from = log_all.size();
    auto visit = live::R.visit;
    uint32_t ms = visit(k);
    live::R.visit = [](int) { return 0u; };  // the show's own visitors' timer brings no other meanwhile
    chances_now();
    play((int) ms / 1000 + 2, 0, "leaves_flag_crowd");
    live::R.visit = visit;
    live_quiet("leaves_flag_crowd_" + std::to_string(k), from);
  }
}
