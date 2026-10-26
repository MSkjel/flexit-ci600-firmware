// Every UI text in English, X(id, text) gives S_<id>. _(S_X) is the text in the panel's language, tr(s) for ids
// picked at run time. Other languages go in lang_*.c. make check catches missing texts, placeholder mismatches,
// a % not spaced as lang_pct_space() says, and glyphs the fonts lack.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define STRINGS(X) \
  /* themes and the status bar */ \
  X(MIDNIGHT, "Midnight") \
  X(DAYLIGHT, "Daylight") \
  X(GRAPHITE, "Graphite") \
  X(AM, "am") \
  X(PM, "pm") \
  /* home views, fan speed, screensaver */ \
  X(STOP, "Stop") \
  X(MIN, "Min") \
  X(NORMAL, "Normal") \
  X(MAX, "Max") \
  X(STOP_CAPS, "STOP") \
  X(MIN_CAPS, "MIN") \
  X(NORMAL_CAPS, "NORMAL") \
  X(MAX_CAPS, "MAX") \
  X(UNTIL, "until %s") \
  X(MODE_AT, "%s at %s") \
  X(OUTDOOR, "Outdoor") \
  X(SUPPLY, "Supply") \
  X(EXTRACT, "Extract") \
  X(ROOM, "Room") \
  X(ROTOR, "Rotor") \
  X(FILTER, "Filter") \
  X(HEATER, "Heater") \
  X(RECOVERY, "Recovery") \
  X(DAYS_SHORT, "%d d") \
  X(DUE, "Due") \
  X(OFF_LOWER, "off") \
  X(ON, "On") \
  X(OFF, "Off") \
  X(INDOOR_CAPS, "INDOOR") \
  X(OUTDOOR_CAPS, "OUTDOOR") \
  X(SUPPLY_AIR_CAPS, "SUPPLY AIR") \
  X(EXTRACT_AIR_CAPS, "EXTRACT AIR") \
  X(TARGET, "Target %s") \
  X(NO_CONTACT_WITH_UNIT, "No contact with the unit") \
  X(HEATING_PCT, "Heating %d%%") \
  X(COOLING_PCT, "Cooling %d%%") \
  X(HEATER_OFF, "Heater off") \
  X(BOOST_CAPS, "BOOST") \
  X(MIN_LEFT, "%d min left") \
  X(FANS_IN_OUT, "in %d · out %d") \
  X(SUPPLY_CAPS, "SUPPLY") \
  X(EXTRACT_CAPS, "EXTRACT") \
  X(EXHAUST_CAPS, "EXHAUST") \
  X(HEAT_RECOVERY_PCT, "Heat recovery %d%%") \
  X(HEAT_RECOVERY, "Heat recovery") \
  X(AXIS_24H, "-24h") \
  X(AXIS_18H, "-18h") \
  X(AXIS_12H, "-12h") \
  X(AXIS_6H, "-6h") \
  X(AXIS_NOW, "now") \
  X(ESP_NOT_CONNECTED, "ESP not connected") \
  X(COMFORT_NEEDS_ESP, "Comfort features need an ESP at address 1.") \
  X(SMART_NEEDS_ESP, "Smart home needs an ESP at address 1.") \
  X(DASHBOARD_VIEW_HELP, "The main screen. UP and DOWN change the fan speed, and going past MAX starts a boost. BACK " \
                         "shows the other views. Set the tiles under Display > Home screen > Dashboard tiles.") \
  X(AIRFLOW_VIEW_HELP, "How air moves through the unit. Outdoor air is filtered, warmed by the rotor and the heater, " \
                       "and blown in as supply air. The extract air heats the rotor on its way out.") \
  X(TRENDS_VIEW_HELP, "Outdoor, supply and indoor temperatures over the last 24 hours, measured every 15 minutes.") \
  X(SENSORS_VIEW_HELP, "Readings from the ESP's sensors over the last 24 hours, measured every 15 minutes. If there " \
                       "are more than two, BACK shows the next ones.") \
  X(COMFORT_VIEW_HELP, "Readings and automations from the ESP. An automation is highlighted while it's on.") \
  X(SMART_VIEW_HELP, "Values and switches from the ESP, such as Home Assistant entities. If there are more than six, " \
                     "BACK shows the next ones.") \
  X(BOOST, "Boost") \
  X(FAN_SPEED, "Fan speed") \
  X(FAN_SPEED_HUD_HELP, "UP and DOWN change the fan speed. Going past MAX starts a boost: full speed for the boost " \
                        "time, then back to the previous speed.") \
  X(BOOST_MIN_LEFT, "Boost, %d min left") \
  X(FANS_SUPPLY_EXTRACT, "Supply %d%%, extract %d%%") \
  X(FANS_AT, "Fans at %d%%") \
  X(MONDAY, "Monday") \
  X(TUESDAY, "Tuesday") \
  X(WEDNESDAY, "Wednesday") \
  X(THURSDAY, "Thursday") \
  X(FRIDAY, "Friday") \
  X(SATURDAY, "Saturday") \
  X(SUNDAY, "Sunday") \
  X(JANUARY, "January") \
  X(FEBRUARY, "February") \
  X(MARCH, "March") \
  X(APRIL, "April") \
  X(MAY, "May") \
  X(JUNE, "June") \
  X(JULY, "July") \
  X(AUGUST, "August") \
  X(SEPTEMBER, "September") \
  X(OCTOBER, "October") \
  X(NOVEMBER, "November") \
  X(DECEMBER, "December") \
  X(DATE, "%s %d %s") /* weekday, day, month */ \
  X(IN, "In") \
  X(OUT, "Out") \
  X(INDOOR, "Indoor") \
  /* menus */ \
  X(MENU, "Menu") \
  /* Ventilation */ \
  X(VENTILATION, "Ventilation") \
  X(VENTILATION_HELP, "Fan speed and boost.") \
  X(BOOST_MIN, "Boost, %d min") \
  X(FAN_SPEED_HELP, "The fan speed now. A boost, the week program or the ESP can change it.") \
  X(BOOST_NOW, "Boost now") \
  X(BOOST_NOW_HELP, "Runs at MAX for the boost time, then goes back to the previous speed.") \
  X(BOOST_TIME, "Boost time") \
  X(UNIT_MIN, "min") \
  X(BOOST_TIME_HELP, "How long a boost lasts.") \
  X(BOOST_NEEDS_ADDR_1, "A boost needs a panel at address 1") \
  /* Temperature */ \
  X(TEMPERATURE, "Temperature") \
  X(TEMPERATURE_HELP, "The setpoint, heating and cooling, and what the sensors measure.") \
  X(SETPOINT, "Setpoint") \
  X(SETPOINT_HELP, "The temperature the unit regulates to.") \
  X(HEATER_HELP, "Allow the heating element to run.") \
  X(HEATER_KEPT_OFF, "The unit didn't turn the heater on") \
  X(COOLING, "Cooling") \
  X(COOLING_HELP, "Allow the cooler to run.") \
  X(SENSORS, "Sensors") \
  X(OUTDOOR_HELP, "Outdoor air, before the filter.") \
  X(SUPPLY_HELP, "Air blown into the rooms, after the heater.") \
  X(EXTRACT_HELP, "Air taken out of the rooms.") \
  X(RETURN_WATER, "Return water") \
  X(RETURN_WATER_HELP, "Water coming back from the heating coil.") \
  X(PANEL_SENSOR, "Panel sensor") \
  X(PANEL_SENSOR_HELP, "The temperature sensor inside this panel.") \
  /* Comfort and Smart home */ \
  X(COMFORT_MENU_HELP, "Automations from the ESP and their settings.") \
  X(NOTHING_YET, "Nothing yet") \
  X(ESP_OFFLINE, "ESP offline") \
  X(N_ITEM, "%d item") \
  X(N_ITEMS, "%d items") \
  X(ITEM_HELP, "Asking the ESP...") \
  X(NOTHING_HERE_YET, "Nothing here yet") \
  X(SMART_MENU_EMPTY, "Values and switches set up on the ESP show up here.") \
  X(SENT, "Sent") \
  /* Week program, and a period's settings */ \
  X(WEEK_PROGRAM, "Week program") \
  X(WEEK_PROGRAM_HELP, "Run the unit on the week program set up below.") \
  X(SCHEDULE_HELP, "The week program: fan speed and temperature for each day.") \
  X(PROGRAM_ON, "Program on") \
  X(WEEK_OVERVIEW, "Week overview") \
  X(WEEK_OVERVIEW_HELP, "All seven days at a glance.") \
  X(DAYS, "Days") \
  X(MONDAY_HELP, "Periods and fan speeds for Monday.") \
  X(TUESDAY_HELP, "Periods and fan speeds for Tuesday.") \
  X(WEDNESDAY_HELP, "Periods and fan speeds for Wednesday.") \
  X(THURSDAY_HELP, "Periods and fan speeds for Thursday.") \
  X(FRIDAY_HELP, "Periods and fan speeds for Friday.") \
  X(SATURDAY_HELP, "Periods and fan speeds for Saturday.") \
  X(SUNDAY_HELP, "Periods and fan speeds for Sunday.") \
  X(DAY_PERIOD, "%s, period %d") \
  X(START, "Start") \
  X(PERIOD_START_HELP, "When the period starts.") \
  X(END, "End") \
  X(PERIOD_END_HELP, "When the period ends. Outside the periods, the unit runs at the speed you've set.") \
  X(PERIOD_FAN_SPEED_HELP, "Fan speed during the period.") \
  X(PERIOD_SETPOINT_HELP, "Temperature during the period.") \
  X(REMOVE_PERIOD, "Remove period") \
  X(REMOVE_PERIOD_HELP, "Deletes this period.") \
  X(PERIOD_REMOVED, "Period removed") \
  /* Filter */ \
  X(FILTER_HELP, "When to change the filters, and resetting the timer after a change.") \
  X(CHANGE_NOW, "Change now") \
  X(DAYS_LEFT, "%d days left") \
  X(STATUS, "Status") \
  X(STATUS_HELP, "Time until the filters should be changed.") \
  X(FILTER_HOURS, "%u h") \
  X(RUNNING_SINCE_CHANGE, "Hours since change") \
  X(RUNNING_SINCE_CHANGE_HELP, "Fan hours since the filter timer was last reset.") \
  X(TIMES_CHANGED, "Times changed") \
  X(TIMES_CHANGED_HELP, "How many times the filter timer has been reset.") \
  X(CHANGE_EVERY, "Change every") \
  X(UNIT_DAYS, "days") \
  X(CHANGE_EVERY_HELP, "How often the filter reminder comes up.") \
  X(FILTERS_CHANGED, "Filters changed") \
  X(FILTERS_CHANGED_HELP, "Restarts the filter timer. Do this after fitting new filters.") \
  /* Alarms */ \
  X(ALARMS, "Alarms") \
  X(ALARMS_HELP, "Current and past alarms, and which ones pop up.") \
  X(N_ACTIVE, "%d active") \
  X(ALL_CLEAR, "All clear") \
  X(ACTIVE_ALARMS, "Active alarms") \
  X(ACTIVE_ALARMS_HELP, "Alarms that are on right now.") \
  X(POP_UPS, "Pop-ups") \
  X(POP_UPS_HELP, "Which new alarms appear on the screen and wake it up.") \
  X(ALL_ALARMS, "All alarms") \
  X(NOT_THE_FILTER, "All but the filter") \
  X(HISTORY, "History") \
  X(HISTORY_HELP, "The last alarms with date and time.") \
  X(RESET_ALARMS, "Reset alarms") \
  X(RESET_ALARMS_HELP, "Clears alarms whose cause is gone.") \
  X(CLEAR_HISTORY, "Clear history") \
  X(CLEAR_HISTORY_HELP, "Deletes the alarm history.") \
  /* Run hours */ \
  X(STATISTICS, "Run hours") \
  X(STATISTICS_HELP, "How many hours the unit has run at each speed.") \
  X(HOURS_TOTAL, "%s total") \
  X(HOURS_THOUSANDS, "%lu %03lu h") \
  X(HOURS, "%lu h") \
  X(TOTAL, "Total") \
  X(TOTAL_HOURS_HELP, "Hours the unit has been powered.") \
  X(STOPPED, "Stopped") \
  X(STOPPED_HOURS_HELP, "Hours at STOP.") \
  X(MIN_HOURS_HELP, "Hours at MIN.") \
  X(NORMAL_HOURS_HELP, "Hours at NORMAL.") \
  X(MAX_HOURS_HELP, "Hours at MAX and boost.") \
  X(ROTOR_HOURS_HELP, "Hours the heat recovery rotor has turned.") \
  X(HEATER_HOURS_HELP, "Hours the heater has been on.") \
  /* Display */ \
  X(DISPLAY, "Display") \
  X(DISPLAY_HELP, "Theme, brightness, night mode, the home screen, the lights and the language.") \
  X(THEME, "Theme") \
  X(THEME_HELP, "The colour theme for all screens.") \
  X(BRIGHTNESS, "Brightness") \
  X(BRIGHTNESS_HELP, "Screen brightness when in use.") \
  X(LANGUAGE, "Language") \
  X(LANGUAGE_HELP, "The panel's language.") \
  X(N_24_HOUR_CLOCK, "24-hour clock") \
  X(N_24_HOUR_CLOCK_HELP, "14:30 instead of 2:30 pm.") \
  X(TEMPERATURES, "Temperatures") \
  X(TEMPERATURES_HELP, "Show temperatures as 21.5° or 22°. Setpoints always show the decimal.") \
  X(WHOLE_DEGREES, "Whole degrees") \
  X(ONE_DECIMAL, "One decimal") \
  X(KEY_HINTS, "Key hints") \
  X(KEY_HINTS_HELP, "Show what the keys do in the line at the bottom.") \
  X(FAST_DISPLAY, "Fast display") \
  X(FAST_DISPLAY_HELP, "Sends pictures to the screen twice as fast, above its rated speed. Turn it off if anything " \
                       "looks wrong. Only with Processor speed up to 80 MHz, under Advanced.") \
  X(ONLY_UP_TO_80_MHZ, "Only up to 80 MHz, see Advanced") \
  X(PROCESSOR_SPEED, "Processor speed") \
  X(PROCESSOR_SPEED_HELP, \
    "Over 72 MHz the processor runs past its rating, so screens draw faster. The panel restarts to switch. If it " \
    "crashes, or you don't confirm within 20 s, it goes back to 72 MHz by itself. Over 80, Fast display is off.") \
  X(N_72_MHZ_STOCK, "72 MHz (stock)") \
  X(N_80_MHZ, "80 MHz") \
  X(N_88_MHZ, "88 MHz") \
  X(N_96_MHZ, "96 MHz") \
  X(N_128_MHZ, "128 MHz") \
  /* Display > Night mode */ \
  X(NIGHT_MODE, "Night mode") \
  X(NIGHT_MODE_HELP, "Use a different brightness and theme at night.") \
  X(FROM, "From") \
  X(NIGHT_FROM_HELP, "When night mode starts.") \
  X(TO, "To") \
  X(NIGHT_TO_HELP, "When night mode ends.") \
  X(NIGHT_BRIGHTNESS_HELP, "Screen brightness when in use at night.") \
  X(NIGHT_IDLE_BRIGHTNESS_HELP, "Screen brightness at night when not in use. Off turns the screen dark.") \
  X(THEME_AT_NIGHT, "Theme at night") \
  X(THEME_AT_NIGHT_HELP, "The colour theme for all screens at night.") \
  X(SAME_AS_BY_DAY, "Same as daytime") \
  /* Display > Home screen */ \
  X(HOME_SCREEN, "Home screen") \
  X(HOME_SCREEN_HELP, "Views, dashboard tiles and the start view.") \
  X(START_VIEW, "Start view") \
  X(START_VIEW_HELP, "The view the panel starts on.") \
  X(DASHBOARD, "Dashboard") \
  X(AIRFLOW, "Airflow") \
  X(TRENDS, "Trends") \
  X(ESP_SENSORS, "Sensors") \
  X(COMFORT, "Comfort") \
  X(SMART_HOME, "Smart home") \
  X(VIEWS, "Views") \
  X(VIEWS_HELP, "Which views BACK switches between. The dashboard is always included.") \
  X(AIRFLOW_HELP, "Shows how air moves through the unit, with temperatures.") \
  X(TRENDS_HELP, "The temperatures over the last 24 hours.") \
  X(SENSORS_HELP, "Readings from the ESP's sensors over the last 24 hours.") \
  X(COMFORT_HELP, "Readings and automations from the ESP.") \
  X(SMART_HOME_HELP, "Values and switches from the ESP.") \
  X(BIG_TEMPERATURE, "Big temperature") \
  X(BIG_TEMPERATURE_HELP, "The big temperature on the dashboard.") \
  X(SUPPLY_AIR, "Supply air") \
  X(EXTRACT_AIR, "Extract air") \
  X(DASHBOARD_TILES, "Dashboard tiles") \
  X(DASHBOARD_TILES_HELP, "What the four tiles on the dashboard show.") \
  X(TILE_HELP, "What this tile on the dashboard shows.") \
  /* Display > Lights */ \
  X(LIGHTS, "Lights") \
  X(LIGHTS_HELP, "The lights on the front of the panel.") \
  X(OK_LIGHT, "OK light") \
  X(OK_LIGHT_HELP, "The light that shows everything is OK.") \
  X(WHEN_ALL_IS_WELL, "When all is well") \
  X(NOT_AT_NIGHT, "Not at night") \
  X(ALARM_LIGHT, "Alarm light") \
  X(ALARM_LIGHT_HELP, "On while an alarm is active.") \
  X(FILTER_LIGHT, "Filter light") \
  X(FILTER_LIGHT_HELP, "On when it's time to change the filters.") \
  /* Screensaver */ \
  X(SCREENSAVER, "Screensaver") \
  X(SCREENSAVER_MENU_HELP, "When the panel goes idle, how bright it is then, and what it shows.") \
  X(IDLE_AFTER, "Idle after") \
  X(IDLE_AFTER_HELP, "How long after the last key press the panel goes idle.") \
  X(IDLE_15S, "15 s") \
  X(IDLE_30S, "30 s") \
  X(IDLE_1MIN, "1 min") \
  X(IDLE_2MIN, "2 min") \
  X(IDLE_5MIN, "5 min") \
  X(IDLE_10MIN, "10 min") \
  X(IDLE_30MIN, "30 min") \
  X(NEVER, "Never") \
  X(IDLE_BRIGHTNESS, "Idle brightness") \
  X(IDLE_BRIGHTNESS_HELP, "Screen brightness when the panel isn't in use. Off turns the screen dark.") \
  X(SHOW, "Show") \
  X(SCREENSAVER_HELP, "What the screen shows when the panel is idle.") \
  X(CLOCK, "Clock") \
  X(CLOCK_TEMPS, "Clock, temps") \
  X(PICTURE, "Picture") \
  X(PICTURE_CLOCK, "Picture, clock") \
  X(PICTURE_ALL, "Picture, all") \
  X(TEXT_ON_PICTURE, "Text on picture") \
  X(TEXT_ON_PICTURE_HELP, \
    "How the clock goes on the picture. A shaded band is the easiest to read, Shadow and Just text leave the band " \
    "out, and Small only puts the time and temperatures in the corners.") \
  X(SHADED_BAND, "Shaded band") \
  X(SHADOW, "Shadow") \
  X(JUST_TEXT, "Just text") \
  X(SMALL, "Small") \
  X(TEXT_POSITION, "Text position") \
  X(TEXT_POSITION_HELP, "Where the clock goes on the picture.") \
  X(BOTTOM, "Bottom") \
  X(TOP, "Top") \
  X(PICTURE_HELP, \
    "The screensaver picture sent by the ESP. It's lost when the panel restarts, and the ESP sends it again.") \
  X(RECEIVING_PCT, "Receiving %d%%") \
  X(PICTURE_SIZE, "%d x %d") \
  X(NONE_YET, "None yet") \
  X(BACK_TO_START_VIEW, "Back to start view") \
  X(BACK_TO_START_VIEW_HELP, "Go back to the start view when the panel goes idle.") \
  /* Date and time */ \
  X(DATE_AND_TIME, "Date and time") \
  X(DATE_AND_TIME_HELP, "The clock used by the unit and the week program.") \
  X(HOUR, "Hour") \
  X(HOUR_HELP, "Set the hour.") \
  X(MINUTE, "Minute") \
  X(MINUTE_HELP, "Set the minute.") \
  X(DAY, "Day") \
  X(DAY_HELP, "Set the day of the month.") \
  X(MONTH, "Month") \
  X(MONTH_HELP, "Set the month.") \
  X(YEAR, "Year") \
  X(YEAR_HELP, "Set the year.") \
  X(SUMMER_TIME, "Summer time") \
  X(SUMMER_TIME_HELP, "Change the clock automatically on the last Sunday of March and October.") \
  X(TIME_ON_OTHER_PANEL, "Set the time on the other panel") \
  /* System */ \
  X(SYSTEM, "System") \
  X(SYSTEM_HELP, "Versions, equipment, the bus and the keys.") \
  X(NO_CONTACT, "No contact") \
  X(ADDRESS_CX60, "Address %d, CX60 %d.%02d") \
  X(ADDRESS, "Address %d") \
  X(ABOUT, "About") \
  X(ABOUT_HELP, "Versions and what is installed.") \
  X(EQUIPMENT, "Equipment") \
  X(EQUIPMENT_HELP, "What is connected. Settings and views for missing equipment are hidden.") \
  X(EQUIP_ESP, "ESP on the bus") \
  X(EQUIP_ESP_HELP, "An ESP at address 1 runs the comfort automations and shares data with this panel. Without one, " \
                    "set the panel address to 1.") \
  X(EQUIP_EC_FANS, "EC fans") \
  X(EQUIP_EC_FANS_HELP, "Variable-speed fans, so each fan speed can be set as a percentage. Set by a DIP switch in " \
                        "the unit.") \
  X(EQUIP_WATER_HEATER, "Water heater") \
  X(EQUIP_WATER_HEATER_HELP, \
    "A water heating coil instead of an electric heater. Adds the return water sensor and frost protection. Set " \
    "by a DIP switch in the unit.") \
  X(EQUIP_COOLING_HELP, "A cooler is connected to the unit.") \
  X(BUS, "Bus") \
  X(BUS_HELP, "Modbus traffic between this panel and the CX60.") \
  X(DIAGNOSTICS, "Diagnostics") \
  X(DIAGNOSTICS_HELP, "Uptime, restarts, memory and other numbers for finding faults.") \
  X(PANEL_ADDRESS, "Panel address") \
  X(PANEL_ADDRESS_HELP, \
    "Use 3 if an ESP is at address 1, otherwise 1. Only the lowest address can change the setpoint and NORMAL fan " \
    "speeds. Restart the unit after changing this, since the CX60 only looks for panels when it starts.") \
  X(KEY_LOCK, "Key lock") \
  X(KEY_LOCK_HELP, "Hold ? for 3 s to lock or unlock the keys.") \
  X(LOCK_WHEN_IDLE, "Lock when idle") \
  X(LOCK_WHEN_IDLE_HELP, "Locks the keys when the panel goes idle.") \
  X(RESTART_PANEL, "Restart panel") \
  X(RESTART_PANEL_HELP, "Restarts this panel. Settings are kept.") \
  X(RESET_PANEL_SETTINGS, "Reset panel settings") \
  X(RESET_PANEL_SETTINGS_HELP, \
    "Restores this panel's settings to the defaults and restarts it. The address and equipment settings are kept.") \
  /* the value editor and action box */ \
  X(HOLD_OK_WARNING, "Hold OK for 2 seconds. This can't be undone.") \
  X(PRESS_OK, "Press OK to continue.") \
  X(KEEP_HOLDING_OK, "Keep holding OK") \
  X(DONE, "Done") \
  X(SAVED, "Saved") \
  X(UNIT_PCT, "%") \
  X(UNIT_C, "\xC2\xB0" \
            "C") \
  /* the Advanced menu */ \
  X(ADVANCED, "Advanced") \
  X(ADVANCED_HELP, \
    "Settings the installer normally sets once: fans, regulation, sensors, inputs, de-icing and tests.") \
  X(ADVANCED_MENU, "Advanced menu") \
  X(ADVANCED_MENU_HELP, "Shows Advanced in the menu, with the unit's installer settings. Wrong values there can make " \
                        "the unit run badly.") \
  X(INSTALLER_SETTINGS, "Installer settings") \
  X(FACTORY_RESET_UNIT, "Factory reset unit") \
  X(FACTORY_RESET_UNIT_HELP, "Restores all settings in the unit (CX60) to the factory defaults.") \
  /* Advanced > Fans */ \
  X(ADV_FANS_HELP, "Fan output at each speed, and the speed a boost runs at.") \
  X(MIN_SUPPLY, "Min, supply") \
  X(MIN_SUPPLY_HELP, "Supply fan output at MIN.") \
  X(MIN_EXTRACT, "Min, extract") \
  X(MIN_EXTRACT_HELP, "Extract fan output at MIN.") \
  X(NORMAL_SUPPLY, "Normal, supply") \
  X(NORMAL_SUPPLY_HELP, "Supply fan output at NORMAL.") \
  X(NORMAL_EXTRACT, "Normal, extract") \
  X(NORMAL_EXTRACT_HELP, "Extract fan output at NORMAL.") \
  X(MAX_SUPPLY, "Max, supply") \
  X(MAX_SUPPLY_HELP, "Supply fan output at MAX and during boost.") \
  X(MAX_EXTRACT, "Max, extract") \
  X(MAX_EXTRACT_HELP, "Extract fan output at MAX and during boost.") \
  X(BOOST_SPEED, "Boost speed") \
  X(BOOST_SPEED_HELP, "The speed a boost runs at.") \
  /* Advanced > Regulation */ \
  X(REGULATION, "Regulation") \
  X(REGULATION_HELP, "What the unit regulates on, the supply air limits, the neutral zones and the regulator tuning.") \
  X(REGULATE_ON, "Regulate on") \
  X(REGULATE_ON_HELP, "Which air temperature the setpoint applies to.") \
  X(MIN_SUPPLY_AIR, "Min supply air") \
  X(MIN_SUPPLY_AIR_HELP, "Supply air never goes below this, also when regulating on extract air.") \
  X(MAX_SUPPLY_AIR, "Max supply air") \
  X(MAX_SUPPLY_AIR_HELP, "Supply air never goes above this.") \
  X(NEUTRAL_ZONES, "Neutral zones") \
  X(COOLING_TO_ROTOR, "Cooling to rotor") \
  X(COOLING_TO_ROTOR_HELP, "A gap between where the cooling stops and the rotor starts, so they don't fight.") \
  X(ROTOR_TO_HEATING, "Rotor to heating") \
  X(ROTOR_TO_HEATING_HELP, "A gap between the rotor at full speed and the heater starting.") \
  X(REGULATOR_TUNING, "Regulator tuning") \
  X(REGULATOR_TUNING_HELP, \
    "The K and I values of the unit's regulators. Leave them alone unless you know what they do.") \
  X(HEATING, "Heating") \
  X(SUPPLY_AIR_K, "Supply air, K") \
  X(SUPPLY_AIR_I, "Supply air, I") \
  X(EXTRACT_AIR_K, "Extract air, K") \
  X(EXTRACT_AIR_I, "Extract air, I") \
  X(SUPPLY_K_HELP, "Gain when regulating on supply air. Higher reacts harder.") \
  X(SUPPLY_I_HELP, "Integral time when regulating on supply air. Lower catches up faster, but can overshoot.") \
  X(EXTRACT_K_HELP, "Gain when regulating on extract air. Higher reacts harder.") \
  X(EXTRACT_I_HELP, "Integral time when regulating on extract air. Lower catches up faster, but can overshoot.") \
  /* Advanced > Cooling */ \
  X(ADV_COOLING_HELP, "When the cooler is allowed to run.") \
  X(MIN_OUTDOOR_TEMP, "Min outdoor temp") \
  X(MIN_OUTDOOR_TEMP_HELP, "No cooling when it's colder than this outside.") \
  X(MIN_FAN_SPEED, "Min fan speed") \
  X(MIN_FAN_SPEED_HELP, "Cooling only runs at this fan speed or higher.") \
  X(RESTART_DELAY, "Restart delay") \
  X(RESTART_DELAY_HELP, "How long the cooler waits before it starts again.") \
  X(UNIT_S, "s") \
  /* Advanced > Heat recovery */ \
  X(HEAT_RECOVERY_HELP, "The rotor: cooling recovery, what a rotor alarm does, and de-icing.") \
  X(COOLING_RECOVERY, "Cooling recovery") \
  X(COOLING_RECOVERY_HELP, "When it's warmer outside than inside, the rotor keeps the coolness in.") \
  X(DIFFERENCE, "Difference") \
  X(DIFFERENCE_HELP, "How much warmer it has to be outside than inside for cooling recovery.") \
  X(ROTOR_ALARM, "Rotor alarm") \
  X(ROTOR_ALARM_HELP, "What the unit does if the rotor stops turning.") \
  X(STOP_THE_UNIT, "Stop the unit") \
  X(KEEP_RUNNING, "Keep running") \
  X(DE_ICING, "De-icing") \
  X(DE_ICING_HELP, "Keeps the rotor from icing up when it's very cold outside.") \
  X(STARTS_BELOW, "Starts below") \
  X(STARTS_BELOW_HELP, "De-icing starts when it's colder than this outside.") \
  X(ROTOR_SPEED, "Rotor speed") \
  X(ROTOR_SPEED_HELP, "How fast the rotor turns while de-icing. 180 is full speed.") \
  X(ROTOR_PERIOD, "Rotor period") \
  X(ROTOR_PERIOD_HELP, "How long each de-icing of the rotor lasts.") \
  X(ROTOR_FREQUENCY, "Rotor frequency") \
  X(ROTOR_FREQUENCY_HELP, "How often the rotor gets de-iced.") \
  X(LESS_AIR_BELOW, "Less air below") \
  X(LESS_AIR_BELOW_HELP, "Colder than this outside, the supply fan drops to MIN and the extract fan to NORMAL.") \
  X(REDUCED_FAN_SPEED, "Reduced fan speed") \
  X(REDUCED_FAN_SPEED_HELP, "Fan output while there's less air.") \
  X(FAN_PERIOD, "Fan period") \
  X(FAN_PERIOD_HELP, "How long each period with less air lasts.") \
  X(FAN_FREQUENCY, "Fan frequency") \
  X(FAN_FREQUENCY_HELP, "How often the periods with less air come.") \
  X(RAMP_TIME, "Ramp time") \
  X(RAMP_TIME_HELP, "How fast the fans go down to less air and back up again.") \
  /* Advanced > Sensors */ \
  X(ADV_SENSORS_HELP, "Sensor offsets, which sensors are fitted, and the panel's own sensor.") \
  X(SUPPLY_AIR_OFFSET, "Supply air offset") \
  X(EXTRACT_AIR_OFFSET, "Extract air offset") \
  X(OUTDOOR_OFFSET, "Outdoor offset") \
  X(RETURN_WATER_OFFSET, "Return water offset") \
  X(SENSOR_OFFSET_HELP, "Corrects what this sensor in the unit reads.") \
  X(EXTRACT_AIR_SENSOR, "Extract air sensor") \
  X(EXTRACT_AIR_SENSOR_HELP, "The extract air sensor is fitted. Regulating on extract air needs it.") \
  X(OUTDOOR_SENSOR, "Outdoor sensor") \
  X(OUTDOOR_SENSOR_HELP, "The outdoor sensor is fitted. Cooling and de-icing need it.") \
  X(THIS_PANEL, "This panel") \
  X(PANEL_SENSOR_OFFSET, "Panel sensor offset") \
  X(PANEL_SENSOR_OFFSET_HELP, "Corrects the panel sensor. Compare with a thermometer next to the panel after an hour " \
                              "without use, since the panel warms up a little.") \
  X(INDOOR_TEMPERATURE, "Indoor temperature") \
  X(INDOOR_TEMPERATURE_HELP, \
    "Where the indoor temperature comes from: the extract air (the whole home) or this panel (this room).") \
  /* Advanced > Inputs */ \
  X(INPUTS, "Inputs") \
  X(INPUTS_HELP, "What the unit does when something is connected to its inputs: fire alarm, filter guard, home/away " \
                 "and fireplace.") \
  X(FIRE_SMOKE, "Fire/smoke") \
  X(FIRE_SMOKE_HELP, "What the fans do when the fire or smoke alarm input trips.") \
  X(STOP_BOTH_FANS, "Stop both fans") \
  X(BOTH_AT_MAX, "Both at max") \
  X(EXTRACT_AT_MAX, "Extract at max") \
  X(SUPPLY_AT_MAX, "Supply at max") \
  X(FILTER_GUARD_HELP, "A pressure switch on the filters is connected. It raises an alarm when they're clogged.") \
  X(EXTERNAL_SETPOINT, "External setpoint") \
  X(EXTERNAL_SETPOINT_HELP, "Takes the setpoint from a knob wired to the unit, 10 to 30 degrees.") \
  X(HOME_AWAY, "Home/away") \
  X(AWAY_SPEED_HELP, "Fan speed while the home/away input is on.") \
  X(AWAY_SETPOINT_HELP, "Setpoint while the home/away input is on.") \
  X(HOLD_TIME, "Hold time") \
  X(HOLD_TIME_HELP, "How long the unit stays in home/away after the input goes off.") \
  X(FIREPLACE_INPUT, "Fireplace input") \
  X(SUPPLY_SPEED, "Supply speed") \
  X(SUPPLY_SPEED_HELP, "Supply fan speed while the fireplace input is on.") \
  X(EXTRACT_SPEED, "Extract speed") \
  X(EXTRACT_SPEED_HELP, "Extract fan speed while the fireplace input is on.") \
  /* Advanced > Start and stop */ \
  X(START_AND_STOP, "Start and stop") \
  X(START_AND_STOP_HELP, "The pauses between the steps when the unit starts, and how long the fans run after the " \
                         "heater.") \
  X(START_DELAY_1, "Start delay 1") \
  X(START_DELAY_2, "Start delay 2") \
  X(START_DELAY_3, "Start delay 3") \
  X(START_DELAY_4, "Start delay 4") \
  X(START_DELAY_HELP, "How long the unit waits before this step when it starts.") \
  X(FANS_AFTER_HEATER, "Fans after heater") \
  X(FANS_AFTER_HEATER_HELP, "How long the fans keep going after the electric heater stops, to cool it down.") \
  /* Advanced > Test */ \
  X(TEST, "Test") \
  X(TEST_HELP, "Runs the fans, heater, rotor or cooler by hand to check them. A test lasts a minute.") \
  X(TEST_FANS_HELP, "Runs both fans at this for a minute.") \
  X(TEST_HEATER_HELP, "Turns the heater on for a minute.") \
  X(TEST_ROTOR_HELP, "Turns the rotor at full speed for a minute.") \
  X(TEST_COOLING_HELP, "Runs the cooler at this output for a minute.") \
  /* Advanced > Unit */ \
  X(UNIT_INFO_HELP, "The rotor's DIP switches in the unit, and the panels it found on the bus.") \
  X(DS2_ROTOR_GUARD, "DS2 rotor guard") \
  X(DS2_HELP, "DIP switch 2 in the unit, the rotor guard.") \
  X(DS3_ROTOR_MOTOR, "DS3 rotor motor") \
  X(DS3_HELP, "DIP switch 3 in the unit, the kind of rotor motor.") \
  X(PANELS, "Panels") \
  X(PANEL_1, "Panel 1") \
  X(PANEL_2, "Panel 2") \
  X(PANEL_3, "Panel 3") \
  X(PANELS_HELP, "The software version of the panel at this address, as the unit saw it when it started.") \
  X(NO_PANEL, "None") \
  /* alarms, week program, info pages, help */ \
  X(ALARM, "Alarm") \
  X(OVERHEAT_THERMOSTAT, "Overheat thermostat") \
  X(FIRE_OR_SMOKE, "Fire or smoke") \
  X(FILTER_GUARD, "Filter guard") \
  X(SUPPLY_SENSOR_FAULT, "Supply sensor fault") \
  X(EXTRACT_SENSOR_FAULT, "Extract sensor fault") \
  X(OUTDOOR_SENSOR_FAULT, "Outdoor sensor fault") \
  X(RETURN_WATER_SENSOR_FAULT, "Return water sensor fault") \
  X(WATER_COIL_FROST, "Water coil frost") \
  X(ROTOR_STOPPED, "Rotor stopped") \
  X(FILTER_CHANGE_DUE, "Filter change due") \
  X(ADVICE_OVERHEAT, \
    "The heater got too hot and was switched off. Check that air can flow, then reset the thermostat on the unit.") \
  X(ADVICE_FIRE, "The fire alarm input is active. The unit runs as configured for fire and smoke.") \
  X(ADVICE_FILTER_GUARD, "The filter pressure switch reports clogged filters. Change them.") \
  X(ADVICE_SUPPLY_SENSOR, "The supply air sensor is open or shorted. Heating is limited until it is fixed.") \
  X(ADVICE_EXTRACT_SENSOR, "The extract air sensor is open or shorted.") \
  X(ADVICE_OUTDOOR_SENSOR, "The outdoor air sensor is open or shorted. Frost protection may not work.") \
  X(ADVICE_RETURN_WATER_SENSOR, "The return water sensor is open or shorted.") \
  X(ADVICE_WATER_FROST, "The water coil was close to freezing. The supply fan stopped to protect it.") \
  X(ADVICE_ROTOR, "The rotor guard detects no rotation. Check the belt and the rotor motor.") \
  X(ADVICE_FILTER_DUE, "Time to change the filters. Reset the timer under Filter afterwards.") \
  X(MON, "Mon") \
  X(TUE, "Tue") \
  X(WED, "Wed") \
  X(THU, "Thu") \
  X(FRI, "Fri") \
  X(SAT, "Sat") \
  X(SUN, "Sun") \
  X(SERVICE_CAPS, "SERVICE") \
  X(ALARM_CAPS, "ALARM") \
  X(ALARM_SINCE, "Since %02d.%02d %s") \
  X(N_MORE, "+%d more") \
  X(ALARM_POPUP_HELP, "A new alarm. A bell stays in the status bar while any alarm is active.") \
  X(ALARM_HISTORY, "Alarm history") \
  X(NO_ALARMS_LOGGED, "No alarms logged") \
  X(NO_ACTIVE_ALARMS, "No active alarms") \
  X(ALARM_WHEN, "%s %02d.%02d at %s") \
  X(ACTIVE_SINCE, "Active since") \
  X(LOGGED, "Logged") \
  X(SINCE, "Since") \
  X(ALARM_HISTORY_HELP, "The last alarms the CX60 logged, newest first.") \
  X(ALARM_LIST_HELP, "Active alarms. Fix the cause, then use Reset alarms.") \
  X(PROGRAM_OFF, "Program off") \
  X(WEEK_HELP, "Fan speed through the week. Outside the coloured periods, the unit runs at the speed you've set. The " \
               "line shows the time now. Edit the days under Week program.") \
  X(ADD_PERIOD, "+  Add period") \
  X(DAY_SCREEN_HELP, "Up to four periods a day, each with its own fan speed and temperature. Outside them, the unit " \
                     "runs at the speed you've set.") \
  X(PERIOD_ADDED, "Period added") \
  X(PANEL_FIRMWARE, "Panel firmware") \
  X(CX60_SOFTWARE, "CX60 software") \
  X(NOT_SEEN_YET, "not seen yet") \
  X(ESP, "ESP") \
  X(ESP_ADDRESS, "%s, address 1") \
  X(ONLINE, "online") \
  X(OFFLINE, "offline") \
  X(FANS, "Fans") \
  X(EC_VARIABLE, "EC, variable") \
  X(AC_STEPPED, "AC, stepped") \
  X(WATER_COIL, "Water coil") \
  X(ELECTRIC, "Electric") \
  X(INSTALLED, "Installed") \
  X(NOT_INSTALLED, "None") \
  X(PROCESSOR, "Processor") \
  X(MHZ_BUSY, "%u MHz, %d%% busy") \
  X(UNIT, "Unit") \
  X(NO_CONTACT_LOWER, "no contact") \
  X(TALKING, "talking") \
  X(POLLS_TO_THIS_PANEL, "Polls to this panel") \
  X(FRAMES_OK, "Frames OK") \
  X(ERRORS, "Errors") \
  X(ESP_LINK, "ESP link") \
  X(LINK_VERSION, "v%d, %s") \
  X(CONNECTED, "connected") \
  X(WAITING, "waiting") \
  X(REFUSED, "refused") \
  X(LINK_OTHER_VERSION, "v%d, the ESP v%d") \
  X(LINK_FRAMES, "Link frames") \
  X(LATE_BUSY_LOST, "Late, busy, lost") \
  X(UNIT_PAUSE, "Pause after status") \
  X(PAUSE_MS, "%s ms") \
  X(PAUSE_TOO_SHORT, "%s ms, too short") \
  X(PAUSE_NO_GAPS, "%s ms, no gaps") \
  X(BUS_PAGE_HELP, \
    "Traffic on the RS-485 bus. The CX60 is the master and polls this panel. The ESP link uses the pause after the " \
    "unit's status message. If that pause is under 13 ms, the panel stops the link for a while.") \
  X(UPTIME, "Uptime") \
  X(UPTIME_DAYS, "%lu d %02u:%02u") \
  X(LAST_START, "Last start") \
  X(START_POWER, "Power on") \
  X(START_PIN, "Reset pin") \
  X(START_WATCHDOG, "Watchdog") \
  X(START_SOFTWARE, "Restart") \
  X(START_CRASH, "Crash at %08lX") \
  X(RESTARTS, "Restarts") \
  X(MEMORY, "Memory") \
  X(MEMORY_USE, "%s of %u KB, peak %s") \
  X(FREE_STACK, "Free stack") \
  X(FULL_REDRAW, "Full redraw") \
  X(REDRAW_SENDING, "%lu ms, %lu sending") \
  X(SETTINGS_SAVES, "Settings saves") \
  X(DIAG_PAGE_HELP, \
    "For finding faults. Restarts counts since the power came on and should stay low. Processor is its clock and the " \
    "share of the time it's awake. Memory is the screen's, now and at most.") \
  X(ABOUT_PAGE_HELP, "Software versions of this panel and the unit, and what's installed.") \
  X(NOTICE_FIRMWARE, "Replacement firmware %s") \
  X(NOTICE_REVERSED, "Reverse engineered by") \
  X(NOTICE_WRITTEN, "Written by") \
  X(NOTICE_TOOLS, "Taken apart with") \
  X(NOTICE_BUILT_ON, "Built on") \
  X(NOTICE_JPEG, "TJpgDec by ChaN") \
  X(NOTICE_FONT, "Text in Noto Sans") \
  X(NOTICE_ICONS, "Icons from Material Design Icons by Pictogrammers") \
  X(NOTICE_ESPHOME, "Talks to Home Assistant through ESPHome") \
  X(NOTICE_FLEXIT, "Not made or approved by Flexit") \
  X(NOTICE_HARMED, "No ventilation units were harmed") \
  X(NOTICE_CX60, "One CX60 was a bit confused") \
  X(NOTICE_SINCE, "Fresh air since 2026") \
  X(NOTICE_OK, "OK: Flappy Fan") \
  X(KEYTEST, "Flappy Fan") \
  X(KEYTEST_START, "Up to flap") \
  X(KEYTEST_SCORE, "Score %d") \
  X(KEYTEST_BEST, "Best %d") \
  X(KEYTEST_AGAIN, "OK: again") \
  X(KEYTEST_HELP, "UP or OK flaps the fan. Get it through the gaps in the ducts, each one is a point. The ducts " \
                  "speed up with every point.") \
  X(HELP, "Help") \
  X(NO_HELP, "Nothing to explain here.") \
  X(HELP_FOOTER, "BACK goes up one level, hold it to go home. Hold ? for 3 s to lock the keys.") \
  /* ESP views and messages */ \
  X(PRESS, "Press") \
  X(WARNING_CAPS, "WARNING") \
  X(MESSAGE_CAPS, "MESSAGE") \
  X(NOTE_FROM_HA, "from Home Assistant, %s") \
  X(NOTE_HELP, "A message the ESP sent, for example from a Home Assistant automation.") \
  /* keys */ \
  X(KEYS_LOCKED_HOW, "Keys locked: hold ? for 3 s") \
  X(KEYS_LOCKED, "Keys locked") \
  X(KEYS_UNLOCKED, "Keys unlocked") \
  /* start-up */ \
  X(UNKNOWN_DISPLAY_CONTROLLER, "Unknown display controller") \
  X(CRASHED_AT_MHZ, "Crashed at %d MHz, back to 72") \
  /* Processor speed or Fast display on trial */ \
  X(KEEP_MHZ, "Keep %d MHz?") \
  X(KEEP_FAST_DISPLAY, "Keep Fast display?") \
  X(KEEP_QUESTION, "Does the screen look right? OK keeps it, BACK undoes it.") \
  X(BACK_TO_72_IN, "Back to 72 MHz in %d s") \
  X(FAST_OFF_IN, "Fast display goes off in %d s") \
  /* key hints and tiles */ \
  X(MOVE, "Move") \
  X(OPEN, "Open") \
  X(BACK, "Back") \
  X(FAN, "Fan") \
  X(VIEW, "View") \
  X(CHANGE, "Change") \
  X(ADJUST, "Adjust") \
  X(SAVE, "Save") \
  X(CANCEL, "Cancel") \
  X(CHOOSE, "Choose") \
  X(SWITCH, "Switch") \
  X(RUN, "Run") \
  X(EDIT, "Edit") \
  X(LATER, "Later") \
  X(FILTER_MENU, "Filter menu") \
  X(ALARM_LIST, "Alarm list") \
  X(CLOSE, "Close") \
  X(CONFIRM, "Confirm") \
  X(KEEP, "Keep") \
  X(HOLD_TO_CONFIRM, "Hold to confirm") \
  X(TILE_1, "Tile 1") \
  X(TILE_2, "Tile 2") \
  X(TILE_3, "Tile 3") \
  X(TILE_4, "Tile 4")

#define X(id, en) S_##id,
enum { S_NONE, STRINGS(X) S_COUNT };
#undef X
typedef uint16_t str_t;  // S_*

const char *tr(str_t s);                   // text in the panel's language
const char *lang_name(int lang);           // a language's name, in that language
const char *lang_text(int lang, str_t s);  // NULL if the language doesn't have it
bool lang_pct_space(int lang);             // "50 %", not "50%" as in English
#ifdef CI600_FMTCHECK
#define _(s) EN_##s  // make check: the English literal, so the compiler checks every format string
#else
#define _(s) tr(s)
#endif
