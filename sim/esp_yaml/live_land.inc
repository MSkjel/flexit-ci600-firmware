// The acts on the land, see met_live_land.h

// Watches the copies of sprite fly over the picture, a look each second: how often they went across it, how often of
// those facing the other way, and how often they turned round
// Whether a sprite's copies face the way they go: mirrored for the way across, or with turned, turned to it
struct Facing {
  const char *sprite;
  bool turned = false;
  int moves = 0, wrong = 0, turns = 0, cycle = 0;
  std::vector<Look> was;
  explicit Facing(const char *sprite, bool turned = false) : sprite(sprite), turned(turned) {}
  void operator()() {
    const overlay::Overlay *a = overlay::find(std::string("w_") + sprite);
    if (!a || cycle++ % 9)
      return;
    was.resize(a->copies.size());
    for (size_t i = 0; i < a->copies.size(); i++) {
      Look l = look(a->copies[i].place);
      auto in = [&](const Look &k) { return k.shown && k.x >= 0 && k.x + a->w <= met::W; };
      int dx = l.x - was[i].x, dy = l.y - was[i].y;
      if (in(l) && in(was[i]) && abs(dx) >= 2 && (turned || abs(dx) >= abs(dy))) {
        moves++;
        // turned, it's within a right angle of where it goes: it turns at 60 degrees a second
        wrong += turned ? cos(l.deg * M_PI / 180) * dx + sin(l.deg * M_PI / 180) * dy < 0
                        : (dx < 0) != (bool) (l.flip & OVERLAY_VIEW_FLIP_X);
      }
      turns +=
          was[i].shown && (turned ? abs(l.deg - was[i].deg) % 360 >= 20 : (l.flip ^ was[i].flip) & OVERLAY_VIEW_FLIP_X);
      was[i] = l;
    }
  }
};

// A bird that came down, see landing(): as it stood, the cycles it stood there, the most of it and the two flying
// in view at once, the smallest and biggest it was seen, per mille, and whether it's gone again with both of those
// there
struct Down {
  Look at{};
  int still = 0, most = 0, small = 1000, big = 0;
  bool back = false;
};

// bring() brings a bird of sprite lander down, on its own or as one of the two of sprite flock that fly back and
// forth, copies from first. Watched each cycle of the ms it takes, with a shot of it down as live/TAG_down.ppm.
static Down landing(const char *tag, const char *lander, const char *flock, int first,
                    const std::function<uint32_t()> &bring) {
  Down d;
  show_later();
  visitor_gone();
  size_t from = log_all.size();
  uint32_t ms = bring();
  CHECK_MSG(ms, "%s: no bird came down", tag);
  auto in_view = [](const Look &l) { return l.shown && l.opa > 0 && l.x2 >= 0 && l.x1 < met::W; };
  auto flying = [&](bool seen) {  // those of the two that are shown, or in view
    const overlay::Overlay *a = flock ? overlay::find(std::string("w_") + flock) : nullptr;
    int n = 0;
    for (int i = first; a && i < first + 2 && i < (int) a->copies.size(); i++) {
      Look l = a->copies[i].used ? look(a->copies[i].place) : Look{};
      n += seen ? in_view(l) : l.shown && l.opa == 1000;
    }
    return n;
  };
  Look was{};
  int run = 0;
  play((int) (ms / 1000) + 3, flock ? 0 : 8, tag, [&] {  // one by itself is watched all its way
    Look l{};
    bool here = where(lander, l) && in_view(l);
    d.most = std::max(d.most, flying(true) + here);
    if (here) {
      d.small = std::min(d.small, l.size);
      d.big = std::max(d.big, l.size);
      if (l.size <= 800 && was.size > 800)
        sim_shot(live_dir.c_str(), (std::string(tag) + "_coming").c_str());
    }
    run = here && l.x == was.x && l.y == was.y && l.size == was.size ? run + 1 : 0;
    if (run > d.still)
      d.still = run, d.at = l;
    if (run == 18)
      sim_shot(live_dir.c_str(), (std::string(tag) + "_down").c_str());
    was = l;
  });
  const overlay::Overlay *a = overlay::find(std::string("w_") + lander), *two = nullptr;
  d.back = (!a || a->copies.empty() || !a->copies[0].used) && (!flock || flying(false) == 2);
  // and the two fly on, the one that was away too
  int at[2] = {-1000, -1000}, moved = 0;
  if (flock && (two = overlay::find(std::string("w_") + flock)))
    play(10, 0, "", [&] {
      for (int i = 0; i < 2; i++) {
        int x = look(two->copies[first + i].place).x;
        moved |= (at[i] != -1000 && x != at[i]) << i;
        at[i] = x;
      }
    });
  d.back = d.back && (!flock || moved == 3);
  live_quiet(tag, from);
  return d;
}

// The perch of the picture's a bird stood on, its feet's middle on it at its size and depth, null if on none
static const live::Perch *stood_on(const Down &d, const std::vector<live::Perch> &perches) {
  for (const live::Perch &p : perches)
    if (abs((d.at.x1 + d.at.x2 + 1) / 2 - p.x) <= 2 && abs(d.at.y2 - p.y) <= 1 && d.at.size == p.size * 10 &&
        d.at.depth == p.depth)
      return &p;
  return nullptr;
}

static void live_land() {
  step("the weather animations: the land (packages/code/met/met_live_land.h)");
  // More of the acts that are on, as live/TAG_n.ppm, with their chance events now if chances
  auto more = [](const char *tag, bool chances, int seconds, int shots, const std::function<void()> &each = {}) {
    size_t from = log_all.size();
    if (chances)
      chances_now();
    play(seconds, shots, tag, each);
    live_quiet(tag, from);
  };
  // the same acts again with the wind from the north-east, blowing things to the right
  auto east = [](const char *want) {
    met::weather.wind_from = 20;
    live::stop();
    met::redraw();
    CHECK_MSG(picture(want, 6000) >= 0 && all_on(want), "%s with the wind the other way: %s", want, cast_on().c_str());
    CHECK_MSG(panel_scene().at.wdir > 0, "%s: the wind from the north-east doesn't blow things to the right", want);
  };
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots
  show({"Crows + About the cabin", "clearsky_day", 0, 0, 0, 0, 7, 18, 0, utc(2026, 6, 10, 11), met::G_HILLS, 0, 30, 2});
  more("crows_visit", true, 52, 6);
  visits("About the cabin", "roof", 2, {"squirrel"});  // a crow, a squirrel and a magpie
  // the crows fly back and forth facing the way they go
  Facing crows{"crow"};
  play(120, 0, "crows_facing", std::ref(crows));
  CHECK_MSG(crows.moves >= 100 && !crows.wrong && crows.turns >= 2,
            "the crows: %d of %d moves facing the other way, %d turns", crows.wrong, crows.moves, crows.turns);
  // one of the two comes down on a perch of the picture's, the other flying on: never a third, and both are back
  // after. None comes to the cabin from elsewhere meanwhile.
  {
    std::vector<live::Perch> where = live::perches(panel_scene(), true);
    std::set<int> used;
    for (int k = 0; k < 4; k++) {
      Down d = landing("crows_land", "crow", "crow", 1, [] { return visit("Crows", 0); });
      const live::Perch *on = stood_on(d, where);
      CHECK_MSG(d.still >= 18 && on && d.most == 2 && d.back,
                "a crow of the two came down: still for %d cycles at %d to %d, row %d, %d per mille, %d deep, on a "
                "perch %d, %d birds in view at the most, both flying after %d",
                d.still, d.at.x1, d.at.x2, d.at.y2, d.at.size, d.at.depth, on != nullptr, d.most, d.back);
      if (on)
        used.insert((int) (on - where.data()));
    }
    CHECK_MSG(where.size() >= 3 && used.size() >= 2 && !visit("About the cabin", 0),
              "the hills have %zu perches, the crows came down on %zu of them", where.size(), used.size());
  }
  show({"Gulls + About the cabin", "clearsky_day", 0, 0, 0, 0, 11, 18, 0, utc(2026, 7, 4, 11), met::G_COAST, 0, 20, 0});
  more("gulls_coast", false, 10, 1);
  // one of the two gulls comes down too, where a gull would: the ridge, the pole or the lighthouse, which is far
  // off, so it's smaller there and behind what's nearer
  {
    std::vector<live::Perch> where = live::perches(panel_scene(), false);
    Down d = landing("gulls_land", "gullvisit", "gull", 0, [] { return visit("Gulls", 0); });
    const live::Perch *on = stood_on(d, where);
    CHECK_MSG(live::on(live::act_of("About the cabin"))->visits.size() == 4 && !visit("About the cabin", 3) &&
                  d.still >= 18 && on && d.most == 2 && d.back,
              "a gull of the two came down: still for %d cycles at %d to %d, row %d, %d per mille, on a perch %d, %d "
              "birds in view at the most, both flying after %d",
              d.still, d.at.x1, d.at.x2, d.at.y2, d.at.size, on != nullptr, d.most, d.back);
  }
  // the gulls fly back and forth facing the way they go, and turn round out of sight
  Facing gulls{"gull"};
  play(120, 0, "gulls_facing", std::ref(gulls));
  CHECK_MSG(gulls.moves >= 100 && !gulls.wrong && gulls.turns >= 2,
            "the gulls: %d of %d moves facing the other way, %d turns", gulls.wrong, gulls.moves, gulls.turns);
  east("Gulls + About the cabin");
  {  // against a wind from the north-east one comes down from the right, mirrored, the other out on that side first
    std::vector<live::Perch> where = live::perches(panel_scene(), false);
    Down d = landing("gulls_land_east", "gullvisit", "gull", 0, [] { return visit("Gulls", 0); });
    CHECK_MSG(
        d.still >= 18 && stood_on(d, where) && (d.at.flip & OVERLAY_VIEW_FLIP_X) && d.most == 2 && d.back,
        "a gull of the two came down from the right: still for %d cycles at %d to %d, row %d, flipped %d, %d birds "
        "in view at the most, both flying after %d",
        d.still, d.at.x1, d.at.x2, d.at.y2, d.at.flip, d.most, d.back);
  }
  more("gulls_coast_east", true, 52, 4);
  // with no gulls flying one comes by itself. On the lighthouse, which is far off, it's smaller, as it gets on its
  // way there, and behind what's nearer.
  show({"About the cabin", "clearsky_day", 0, 0, 0, 0, 5, 18, 0, utc(2026, 7, 4, 11), met::G_COAST, 0, 10, 0}, "coast");
  {
    std::vector<live::Perch> where = live::perches(panel_scene(), false);
    Down d = landing("gull_alone", "gullvisit", nullptr, 0, [] { return visit("About the cabin", 3); });
    CHECK_MSG(d.still >= 18 && stood_on(d, where) && d.most == 1 && d.back,
              "a gull by itself: still for %d cycles at %d to %d, row %d, %d per mille, %d in view, gone after %d",
              d.still, d.at.x1, d.at.x2, d.at.y2, d.at.size, d.most, d.back);
    const live::Perch *far = nullptr;
    for (const live::Perch &p : where)
      if (p.size < 100)
        far = &p;
    CHECK_MSG(far && far->x == met::LH_X && far->depth == met::D_MID_ON, "the coast's %zu perches: no lighthouse",
              where.size());
    if (far) {
      bool from_right = panel_scene().at.wdir > 0;
      Down top = landing("gull_lighthouse", "gullvisit", nullptr, 0, [&] {
        live::As as(live::act_of("About the cabin"));
        return live::come([&] { return live::land(live::GULL, *far, from_right); }, live::ON_PANEL);
      });
      CHECK_MSG(top.still >= 18 && stood_on(top, where) == far && top.most == 1 && top.back &&
                    top.small == live::FAR_BIRD * 10 && top.big > 900,
                "a gull on the lighthouse: still for %d cycles at %d to %d, row %d, %d per mille and %d deep, the "
                "lighthouse at %d, row %d; from %d to %d per mille on its way",
                top.still, top.at.x1, top.at.x2, top.at.y2, top.at.size, top.at.depth, far->x, far->y, top.big,
                top.small);
    }
  }
  show({"Smoke from the chimney + The advent star + About the cabin", "clearsky_day", 0, 0, 0, 0, 3, 3, 0,
        utc(2026, 12, 10, 11), met::G_HILLS, 60, 40, 4});
  {  // a crow by itself on a spruce far off on the middle hill: smaller, and behind the near hill's
    const live::Perch *far = nullptr;
    std::vector<live::Perch> where = live::perches(panel_scene(), true);
    for (const live::Perch &p : where)
      if (p.size < 100)
        far = &p;
    CHECK_MSG(far, "the hills' %zu perches: none on the middle hill", where.size());
    if (far) {
      Down d = landing("crow_spruce", "crow", nullptr, 0, [&] {
        live::As as(live::act_of("About the cabin"));
        return live::come([&] { return live::land(live::CROW, *far, live::R.wdir > 0); }, live::ON_PANEL);
      });
      CHECK_MSG(d.still >= 18 && stood_on(d, where) == far && d.small == live::FAR_BIRD * 10 && d.big > 900 &&
                    d.at.depth == met::D_MID_ON && d.back,
                "a crow on a far spruce: still for %d cycles at %d to %d, row %d, %d per mille and %d deep, the spruce "
                "at %d, row %d; from %d to %d per mille on its way",
                d.still, d.at.x1, d.at.x2, d.at.y2, d.at.size, d.at.depth, far->x, far->y, d.big, d.small);
    }
  }
  show({"Smoke from the chimney + The advent star + About the cabin", "clearsky_day", 0, 0, 0, 0, 12, 3, 0,
        utc(2026, 12, 25, 11), met::G_COAST, 90, 30, 0});
  more("smoke_coast_windy", false, 20, 2);
  {  // with no crows flying one comes by itself, and down on a perch too
    std::vector<live::Perch> where = live::perches(panel_scene(), true);
    Down d = landing("crow_alone", "crow", nullptr, 0, [] { return visit("About the cabin", 0); });
    CHECK_MSG(d.still >= 18 && stood_on(d, where) && d.most == 1 && d.back,
              "a crow by itself: still for %d cycles at %d to %d, row %d, %d per mille, %d in view, gone after %d",
              d.still, d.at.x1, d.at.x2, d.at.y2, d.at.size, d.most, d.back);
  }
  show({"Smoke from the chimney", "cloudy", 0, 70, 0, 0, 3, 3, 0, utc(2026, 3, 10, 2), met::G_MOUNTAINS, 60, 30, 0});
  more("smoke_mountains_night", false, 20, 2);
  east("Smoke from the chimney");
  more("smoke_mountains_east", false, 30, 2);
  show({"Firelight in the window + Smoke from the chimney + By the cabin at night", "cloudy", 0, 70, 0, 0, 3, 3, 0,
        utc(2026, 1, 15, 18), met::G_HILLS, 90, 40, 2});
  chances_now("Firelight in the window");  // the cat on the sill
  more("cosy_cat", false, 30, 3);
  visits("By the cabin at night", "cosy", 3, {"fox"});
  show({"Firelight in the window + Smoke from the chimney + By the cabin at night", "cloudy", 0, 70, 0, 0, 6, 3, 0,
        utc(2026, 1, 15, 18), met::G_MOUNTAINS, 90, 30, 0});
  more("cosy_mountains", false, 20, 2);
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 10, 5, 16, 45), met::G_HILLS, 0, 20, 1});
  more("moose_walk", true, 80, 8);
  visits("A moose at dusk", "roe_deer", 2, {"roedeer"});
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 5, 1, 18, 40), met::G_MOUNTAINS, 0, 20, 0});
  more("moose_mountains", true, 80, 4);
  // in May it has no antlers yet, in July it may be a cow with her calf, and in winter the roe deer is grey
  Look cow;
  CHECK_MSG(where("moosecow", cow) && !overlay::find("w_moose"), "a moose in May with its antlers");
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 14, 0, utc(2026, 7, 10, 20, 15), met::G_HILLS, 0, 20, 0});
  more("moose_summer", true, 80, 4);
  CHECK_MSG(overlay::find("w_moose") || where("moosecow", cow), "a summer's dusk without a moose");
  show({"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, -5, 0, utc(2026, 2, 10, 15, 50), met::G_HILLS, 0, 20, 0});
  visits("A moose at dusk", "roe_deer_winter", 2, {"roedeerw"});
  // through Christmas the small birds come to the julenek
  show({"Bullfinches at the julenek", "clearsky_day", 0, 0, 0, 0, 2, -4, 0, utc(2026, 12, 20, 10, 30), met::G_HILLS, 0,
        20, 1});
  visits("Bullfinches at the julenek", "julenek", 3);
  // in town it stands in the park, and they come to it there
  show({"Bullfinches at the julenek", "clearsky_day", 0, 0, 0, 0, 2, -4, 0, utc(2026, 12, 20, 10, 30), met::G_CITY, 0,
        20, 1},
       "town");
  visits("Bullfinches at the julenek", "julenek_town", 2);
  show({"Lambs, a wagtail and geese", "fair_day", 20, 0, 0, 0, 3, 12, 0, utc(2026, 5, 12, 10), met::G_HILLS, 0, 40, 3});
  visits("Lambs, a wagtail and geese", "spring");  // a wagtail, the geese
  show({"Lambs, a wagtail and geese", "fair_day", 20, 0, 0, 0, 3, 12, 0, utc(2026, 5, 12, 10), met::G_COAST, 0, 20, 1},
       "coast");
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, 18, 0, utc(2026, 6, 10, 11), met::G_MOUNTAINS, 0,
        40, 2});
  Facing eagle{"eagle", true};  // circling, it turns the way it goes
  more("reindeer_eagle", true, 60, 4, std::ref(eagle));
  CHECK_MSG(eagle.moves >= 20 && !eagle.wrong && eagle.turns >= 2,
            "the eagle: %d of %d moves facing the other way, %d turns", eagle.wrong, eagle.moves, eagle.turns);
  visits("Reindeer and a golden eagle", "reindeer", 3);  // a hiker, ptarmigan
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, -8, 0, utc(2026, 2, 7, 11), met::G_MOUNTAINS, 0,
        10, 0},
       "snow");
  CHECK_MSG(visit("Reindeer and a golden eagle", 1), "snow in the mountains: no ptarmigan");
  play(30, 3, "reindeer_ptarmiganw");
  show({"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, 18, 0, utc(2026, 6, 7, 15), met::G_MOUNTAINS, 0,
        20, 0});
  more("reindeer_eagle_left", true, 60, 3);
  show({"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 3, -5, 0, utc(2026, 2, 7, 11), met::G_HILLS, 0,
        20, 1});
  more("skier_by", true, 50, 6);
  visits("A skier, and snow off the spruces", "snow", 2, {"harew"});  // a kick-sled, a sledge, a hare
  show({"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 11, -5, 0, utc(2026, 2, 6, 11), met::G_COAST,
        0, 20, 0});
  east("A skier, and snow off the spruces");
  more("skier_coast_east", true, 50, 4);
  show({"Summer butterflies", "clearsky_day", 0, 0, 0, 0, 3, 20, 0, utc(2026, 7, 29, 11), met::G_HILLS, 0, 60, 4});
  show({"Summer butterflies", "clearsky_day", 0, 0, 0, 0, 2, 20, 0, utc(2026, 7, 15, 11), met::G_HILLS, 0, 30, 0});
  more("butterflies_plain", false, 20, 2);
  visits("Summer butterflies", "summer");  // swallows, the mower
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 4, 10, 0, utc(2026, 10, 5, 11), met::G_HILLS, 0, 30, 2});
  more("leaves_fall", true, 60, 6);
  visits("Birch leaves and geese", "autumn", 2, {"squirrel", "mushroomer"});  // a squirrel, a mushroom picker
  show({"Birch leaves and geese", "fair_day", 0, 0, 0, 0, 11, 10, 0, utc(2026, 10, 6, 11), met::G_COAST, 0, 20, 0});
  east("Birch leaves and geese");
  more("leaves_coast_east", true, 40, 3);
  show({"Birch leaves and geese + The flag", "fair_day", 0, 0, 0, 0, 4, 10, 0, utc(2029, 9, 10, 11), met::G_HILLS, 0,
        30, 0, 69.65, 18.96});  // in Tromso, where it's autumn by then
  more("leaves_flag", true, 60, 3);
  // and each visitor with a leaf falling, the last one blowing off, and the geese going over, room for them all and
  // the flag: no other visitor comes while one is here
  for (int k = 0; k < 2; k++) {
    size_t from = log_all.size();
    uint32_t ms = visit("Birch leaves and geese", k);
    chances_now();
    play((int) ms / 1000 + 2, 0, "leaves_flag_crowd");
    live_quiet("leaves_flag_crowd_" + std::to_string(k), from);
  }
}
