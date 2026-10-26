// The days shows, see met_live_days.h
static void live_days() {
  step("the weather animations: the days (packages/code/met/met_live_days.h)");
  // show, symbol, clouds high mid low, rain, wind, temperature, fog, UTC, ground, heater, seconds, shots, lat lon
  // Christmas Eve, a frosty evening: the lights, the star, the candles, and the sleigh
  show({"Christmas Eve", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, utc(2025, 12, 24, 17), met::G_HILLS, 2, 60, 4},
       "hills", true);
  show({"Christmas Eve", "clearsky_night", 0, 0, 0, 0, 2, -6, 0, utc(2025, 12, 24, 17), met::G_COAST, 2, 30, 1},
       "coast", true);
  show(
      {"Christmas Eve", "partlycloudy_night", 0, 30, 0, 0, 3, -2, 0, utc(2025, 12, 24, 21), met::G_MOUNTAINS, 0, 30, 1},
      "mountains", true);
  // Christmas Day is a flag day. On Svalbard it's dark while the flag is up. Christmas Eve has no room for it, so
  // the picture keeps its own, in town there's room
  show({"Christmas Eve", "cloudy", 0, 0, 80, 0, 4, -10, 0, utc(2025, 12, 25, 10, 30), met::G_MOUNTAINS, 0, 50, 1, 78.22,
        15.65},
       "svalbard", true);
  CHECK(!live::R.flag, "Christmas Eve on Svalbard: the flag animated");
  show({"Christmas Eve in town", "cloudy", 0, 0, 80, 0, 4, -10, 0, utc(2025, 12, 25, 10, 30), met::G_CITY, 0, 50, 1,
        78.22, 15.65},
       "svalbard", true);
  CHECK(live::R.flag, "Christmas Eve in town on Svalbard: no flag, though there's room");
  show({"Christmas Eve in town", "clearsky_night", 0, 0, 0, 0, 2, -4, 0, utc(2025, 12, 24, 17), met::G_CITY, 0, 60, 4},
       "city", true);
  // New Year's Eve: now and then in the evening, a rocket every few seconds at midnight
  show({"New Year's Eve", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2025, 12, 31, 19), met::G_COAST, 0, 60, 2},
       "coast");
  show({"New Year's Eve", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2026, 1, 1, 0, 5), met::G_CITY, 0, 60, 4},
       "city");
  show({"New Year's Eve", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2025, 12, 31, 23, 50), met::G_HILLS, 0, 120, 16},
       "hills");
  // the bursts under the forecast
  met::with_forecast = true;
  show(
      {"New Year's Eve", "clearsky_night", 0, 0, 0, 0, 2, -8, 0, utc(2025, 12, 31, 23, 55), met::G_MOUNTAINS, 0, 60, 6},
      "forecast");
  met::with_forecast = false;
  // Easter: Good Friday, and Easter Sunday with the flag
  show({"Easter", "fair_day", 20, 0, 0, 0, 3, 9, 0, utc(2026, 4, 3, 11), met::G_COAST, 0, 40, 2}, "coast", true);
  show({"Easter", "fair_day", 20, 0, 0, 0, 3, 9, 0, utc(2026, 4, 3, 11), met::G_MOUNTAINS, 0, 40, 2}, "mountains",
       true);
  show({"Easter", "fair_day", 20, 0, 0, 0, 3, 9, 0, utc(2026, 4, 3, 11), met::G_CITY, 0, 40, 2}, "city", true);
  show({"Easter", "fair_day", 20, 0, 0, 0, 3, 9, 0, utc(2026, 4, 5, 12), met::G_HILLS, 0, 60, 6}, "hills", true);
  CHECK(live::R.flag, "Easter Sunday without the flag");
  // 17 May, with the flag: the parade and a balloon
  show({"17 May", "fair_day", 20, 0, 0, 0, 3, 14, 0, utc(2026, 5, 17, 12), met::G_COAST, 0, 40, 2}, "coast", true);
  show({"17 May", "fair_day", 20, 0, 0, 0, 3, 14, 0, utc(2026, 5, 17, 12), met::G_CITY, 0, 40, 2}, "city", true);
  show({"17 May", "fair_day", 20, 0, 0, 0, 3, 14, 0, utc(2026, 5, 17, 12), met::G_MOUNTAINS, 0, 40, 2}, "mountains",
       true);
  show({"17 May", "clearsky_day", 0, 0, 0, 0, 3, 16, 0, utc(2026, 5, 17, 12), met::G_HILLS, 0, 70, 6}, "hills", true);
  CHECK(live::R.flag, "17 May without the flag");
  // Sankthans, the evening of 23 June
  show({"Sankthans", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 6, 23, 20, 30), met::G_COAST, 0, 30, 1}, "coast");
  show({"Sankthans", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 6, 23, 20, 30), met::G_CITY, 0, 30, 1}, "city");
  show({"Sankthans", "fair_day", 20, 0, 0, 0, 2, 16, 0, utc(2026, 6, 23, 20, 30), met::G_MOUNTAINS, 0, 30, 1},
       "mountains");
  show({"Sankthans", "clearsky_day", 0, 0, 0, 0, 2, 16, 0, utc(2026, 6, 23, 20, 30), met::G_HILLS, 0, 60, 4}, "hills");
  // Halloween: the pumpkin, and an owl on a spruce
  show({"Halloween", "partlycloudy_night", 0, 40, 0, 0, 3, 4, 0, utc(2026, 10, 31, 20), met::G_COAST, 0, 90, 2},
       "coast", true);
  show({"Halloween", "partlycloudy_night", 0, 40, 0, 0, 3, 4, 0, utc(2026, 10, 31, 20), met::G_CITY, 0, 90, 2}, "city",
       true);
  show({"Halloween", "partlycloudy_night", 0, 40, 0, 0, 3, 4, 0, utc(2026, 10, 31, 20), met::G_MOUNTAINS, 0, 90, 2},
       "mountains", true);
  show({"Halloween", "clearsky_night", 0, 0, 0, 0, 2, 4, 0, utc(2026, 10, 31, 20), met::G_HILLS, 0, 100, 8}, "hills",
       true);
  // Valentine's Day, hearts from the chimney by day and by night
  show({"Valentine's Day", "cloudy", 0, 0, 80, 0, 3, -2, 0, utc(2026, 2, 14, 19), met::G_MOUNTAINS, 0, 30, 1},
       "mountains");
  show({"Valentine's Day", "fair_day", 20, 0, 0, 0, 3, -2, 0, utc(2026, 2, 14, 12), met::G_COAST, 40, 30, 1}, "coast");
  show({"Valentine's Day", "fair_day", 20, 0, 0, 0, 3, -2, 0, utc(2026, 2, 14, 12), met::G_HILLS, 0, 60, 4}, "hills");
  met::animate(false);  // the last of the shows
  run(20);
}
