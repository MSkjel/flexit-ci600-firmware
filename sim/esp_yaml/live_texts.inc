// The shows beside the panel's texts over the picture, where most have them: the clock and the date at the bottom
// left, the temperatures at the bottom right. What a show parks on the land must show beside them, in the gap
// between the clock and the temperatures or under the temperatures, see GAP_L in met_scene.h. The texts are on for
// these, and each is shot as live/texts_TAG.ppm.

// How much of copy i of sprite, as the panel has it, is under a text: 0 to 1, or -1 while it isn't shown
static float under_texts(const char *sprite, int i) {
  const anim::Anim *a = anim::find(std::string("w_") + sprite);
  if (!a || a->state != anim::SENT || i >= (int) a->copies.size() || !a->copies[i].used)
    return -1;
  Look l = look(a->copies[i].place);
  if (!l.shown || l.opa <= 0)
    return -1;
  int w = a->w * a->scale, h = a->h * a->scale, box[12 * 4], nb = pn_texts(box, 12), under = 0;
  for (int b = 0; b < nb; b++) {
    const int *t = box + b * 4;
    int ix = std::min(l.x + w - 1, t[2]) - std::max(l.x, t[0]) + 1, iy = std::min(l.y + h - 1, t[3]) - std::max(l.y, t[1]) + 1;
    if (ix > 0 && iy > 0)
      under += ix * iy;
  }
  return (float) under / (float) (w * h);
}

// c's show with the texts on, its chance events due at once, until copy i of sprite is placed, within wait s. Then
// 3 s of it, under a text by a tenth at most all the while, with a shot as live/texts_TAG.ppm.
static void beside_texts(const Live &c, const char *sprite, const char *tag, int wait = 10, int i = 0) {
  size_t from = log_all.size();
  std::string name = std::string("texts_") + tag;
  weather_of(c);
  bool drawn = picture(name.c_str(), 6000) >= 0;
  CHECK_MSG(drawn && !strcmp(show_on(), c.want), "%s: the weather shows %s", name.c_str(), show_on());
  chances_now();
  pn_saver(1);
  int k = 0;
  for (; k < wait * 9 && under_texts(sprite, i) < 0; k++)
    tick(1);
  CHECK_MSG(k < wait * 9, "%s: %s not placed in %d s", name.c_str(), sprite, wait);
  float most = 0;
  for (int t = 0; t < 27 && k < wait * 9; t++) {
    tick(1);
    most = std::max(most, under_texts(sprite, i));
    if (t == 9)
      sim_shot(live_dir.c_str(), name.c_str());
  }
  CHECK_MSG(most <= 0.1f, "%s: %s under the texts by %.0f%%", name.c_str(), sprite, (double) most * 100);
  pn_saver(0);
  live_quiet(name, from);
}

static void live_texts() {
  step("the shows beside the clock and the temperatures");
  pn_saver_texts(SAVER_PIC_ALL, 0);  // the clock, the date and the temperatures over the picture, at the bottom
  // the owl on the cabin's ridge, or a flat roof in town
  Live owl = {"Halloween", "clearsky_night", 0, 0, 0, 0, 2, 4, 0, utc(2026, 10, 31, 22), met::G_HILLS, 0, 10, 0};
  beside_texts(owl, "owlperch", "owl_hills", 120);
  owl.t = utc(2026, 10, 31, 21);
  owl.ground = met::G_CITY;
  beside_texts(owl, "owlperch", "owl_town", 120);
  // the chicks around the eggs in the gap, the hare sitting left of the cabin
  Live easter = {"Easter", "fair_day", 20, 0, 0, 0, 3, 9, 0, utc(2026, 4, 5, 12), met::G_HILLS, 0, 10, 0};
  beside_texts(easter, "chick", "chick_hills");
  beside_texts(easter, "chick", "chick_hills_3", 10, 2);
  beside_texts(easter, "haresit", "hare_hills", 40);
  easter.ground = met::G_CITY;
  beside_texts(easter, "chick", "chick_town");
  beside_texts(easter, "haresit", "hare_town", 40);
  // the bonfire in front of the cabin, or in the gap in town
  Live fire = {"Sankthans", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 6, 23, 20, 30), met::G_HILLS, 0, 10, 0};
  beside_texts(fire, "pyre", "bonfire_hills");
  fire.ground = met::G_CITY;
  beside_texts(fire, "pyre", "bonfire_town");
  // the lights on the spruce by home, and the park's flag on Christmas Day
  Live xmas = {"Christmas Eve", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, utc(2025, 12, 24, 17), met::G_HILLS, 2, 10, 0};
  beside_texts(xmas, "treelights", "xmas_hills");
  beside_texts(xmas, "treestar", "xmas_hills_star");
  Live xmas_town = {"Christmas Eve in town", "cloudy", 0, 0, 80, 0, 4, -10, 0, utc(2025, 12, 25, 10, 30), met::G_CITY,
                    0,                       10,       0, 78.22, 15.65};
  beside_texts(xmas_town, "flag", "flag_town");
  // the moose grazing in front of the cabin, the reindeer along the bottom
  Live moose = {"A moose at dusk", "clearsky_day", 0, 0, 0, 0, 3, 8, 0, utc(2026, 10, 5, 16, 45), met::G_HILLS, 0, 10, 0};
  beside_texts(moose, "moosegraze", "moose_hills", 150);
  Live deer = {"Reindeer and a golden eagle", "clearsky_day", 0, 0, 0, 0, 3, 18, 0, utc(2026, 6, 10, 11),
               met::G_MOUNTAINS,              0,              10, 0};
  for (int i = 0; i < 3; i++)
    beside_texts(deer, "reindeer", (std::string("reindeer_") + std::to_string(i)).c_str(), 10, i);
  // snow off the eaves
  Live ski = {"A skier, and snow off the spruces", "clearsky_day", 0, 0, 0, 0, 3, -5, 0, utc(2026, 2, 7, 11),
              met::G_HILLS,                        0,              10, 0};
  beside_texts(ski, "snowdrop", "snow_hills", 70);
  // the butterflies over the meadow in the gap
  Live summer = {"Summer butterflies", "clearsky_day", 0, 0, 0, 0, 3, 20, 0, utc(2026, 7, 29, 11), met::G_HILLS, 0, 10, 0};
  beside_texts(summer, "butterfly", "butterflies_hills");
  beside_texts(summer, "brimstone", "brimstone_hills");
  // the eiders and the boat on the water in the gap
  Live sail = {"Sailboat, glitter and eiders", "clearsky_day", 0, 0, 0, 0, 3, 14, 0, utc(2026, 9, 20, 15), met::G_COAST, 0, 10, 0};
  beside_texts(sail, "eiders", "eiders_oslo");
  Live north = {"Sailboat, glitter and eiders", "fair_day", 10, 0, 0, 0, 5, 20, 0, utc(2026, 7, 15, 11), met::G_COAST,
                0,                              10,         0,  64.38, 25.4};
  beside_texts(north, "eiders", "eiders_north");
  beside_texts(north, "boat", "boat_north");
  pn_saver_texts(SAVER_PIC, 0);
}
