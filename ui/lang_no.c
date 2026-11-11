// Norwegian (bokmål): X(ID, "text") for each S_ID of lang.h, in any order. One that's missing shows in English.
#include "lang.h"

#define TEXTS(X) \
  /* themes and the status bar */ \
  X(MIDNIGHT, "Midnight") \
  X(DAYLIGHT, "Daylight") \
  X(GRAPHITE, "Graphite") \
  X(AM, "am") \
  X(PM, "pm") \
  /* home views, fan speed, screensaver */ \
  X(STOP, "Stopp") \
  X(MIN, "Min") \
  X(NORMAL, "Normal") \
  X(MAX, "Maks") \
  X(STOP_CAPS, "STOPP") \
  X(MIN_CAPS, "MIN") \
  X(NORMAL_CAPS, "NORMAL") \
  X(MAX_CAPS, "MAKS") \
  X(UNTIL, "til %s") \
  X(MODE_AT, "%s kl. %s") \
  X(OUTDOOR, "Ute") \
  X(SUPPLY, "Tilluft") \
  X(EXTRACT, "Avtrekk") \
  X(ROOM, "Rom") \
  X(ROTOR, "Rotor") \
  X(FILTER, "Filter") \
  X(HEATER, "Varme") \
  X(RECOVERY, "Gjenvinning") \
  X(DAYS_SHORT, "%d d") \
  X(DUE, "Bytt") \
  X(OFF_LOWER, "av") \
  X(ON, "På") \
  X(OFF, "Av") \
  X(INDOOR_CAPS, "INNE") \
  X(OUTDOOR_CAPS, "UTE") \
  X(SUPPLY_AIR_CAPS, "TILLUFT") \
  X(EXTRACT_AIR_CAPS, "AVTREKK") \
  X(TARGET, "Ønsket %s") \
  X(NO_CONTACT_WITH_UNIT, "Ingen kontakt med aggregatet") \
  X(HEATING_PCT, "Varme %d %%") \
  X(COOLING_PCT, "Kjøling %d %%") \
  X(HEATER_OFF, "Varme av") \
  X(BOOST_CAPS, "FORSERING") \
  X(MIN_LEFT, "%d min igjen") \
  X(FANS_IN_OUT, "inn %d · ut %d") \
  X(SUPPLY_CAPS, "TILLUFT") \
  X(EXTRACT_CAPS, "AVTREKK") \
  X(EXHAUST_CAPS, "AVKAST") \
  X(HEAT_RECOVERY_PCT, "Varmegjenvinning %d %%") \
  X(HEAT_RECOVERY, "Varmegjenvinning") \
  X(AXIS_24H, "-24h") \
  X(AXIS_18H, "-18h") \
  X(AXIS_12H, "-12h") \
  X(AXIS_6H, "-6h") \
  X(AXIS_NOW, "nå") \
  X(ESP_NOT_CONNECTED, "ESP ikke tilkoblet") \
  X(COMFORT_NEEDS_ESP, "Komfortfunksjonene krever en ESP på adresse 1.") \
  X(SMART_NEEDS_ESP, "Smarthjem krever en ESP på adresse 1.") \
  X(DASHBOARD_VIEW_HELP, \
    "Hovedskjermen. OPP og NED endrer viftehastigheten, og forbi MAKS starter en forsering. TILBAKE viser de andre " \
    "visningene. Velg feltene under Skjerm > Startskjerm > Felt på oversikten.") \
  X(AIRFLOW_VIEW_HELP, "Slik går luften gjennom aggregatet. Uteluften filtreres, varmes av rotoren og " \
                       "varmeelementet og blåses inn som tilluft. Avtrekksluften varmer rotoren på vei ut.") \
  X(TRENDS_VIEW_HELP, "Ute-, tillufts- og innetemperatur de siste 24 timene, målt hvert 15. minutt.") \
  X(SENSORS_VIEW_HELP, "Målinger fra ESP-ens sensorer de siste 24 timene, hvert 15. minutt. Er det flere enn to, " \
                       "viser TILBAKE de neste.") \
  X(COMFORT_VIEW_HELP, "Målinger og automatiseringer fra ESP-en. En automatisering er uthevet mens den er på.") \
  X(SMART_VIEW_HELP, "Verdier og brytere fra ESP-en, for eksempel fra Home Assistant. Er det flere enn seks, " \
                     "viser TILBAKE de neste.") \
  X(BOOST, "Forsering") \
  X(FAN_SPEED, "Viftehastighet") \
  X(FAN_SPEED_HUD_HELP, "OPP og NED endrer viftehastigheten. Forbi MAKS starter en forsering: full hastighet i " \
                        "forseringstiden, så tilbake til forrige hastighet.") \
  X(BOOST_MIN_LEFT, "Forsering, %d min igjen") \
  X(FANS_SUPPLY_EXTRACT, "Tilluft %d %%, avtrekk %d %%") \
  X(FANS_AT, "Vifter på %d %%") \
  X(MONDAY, "Mandag") \
  X(TUESDAY, "Tirsdag") \
  X(WEDNESDAY, "Onsdag") \
  X(THURSDAY, "Torsdag") \
  X(FRIDAY, "Fredag") \
  X(SATURDAY, "Lørdag") \
  X(SUNDAY, "Søndag") \
  X(JANUARY, "januar") \
  X(FEBRUARY, "februar") \
  X(MARCH, "mars") \
  X(APRIL, "april") \
  X(MAY, "mai") \
  X(JUNE, "juni") \
  X(JULY, "juli") \
  X(AUGUST, "august") \
  X(SEPTEMBER, "september") \
  X(OCTOBER, "oktober") \
  X(NOVEMBER, "november") \
  X(DECEMBER, "desember") \
  X(DATE, "%s %d. %s") \
  X(IN, "Inne") \
  X(OUT, "Ute") \
  X(INDOOR, "Inne") \
  /* menus */ \
  X(MENU, "Meny") \
  /* Ventilation */ \
  X(VENTILATION, "Ventilasjon") \
  X(VENTILATION_HELP, "Viftehastighet og forsering.") \
  X(BOOST_MIN, "Forsering, %d min") \
  X(FAN_SPEED_HELP, "Viftehastigheten nå. En forsering, ukeprogrammet eller ESP-en kan endre den.") \
  X(BOOST_NOW, "Forsering nå") \
  X(BOOST_NOW_HELP, "Går på MAKS i forseringstiden, og så tilbake til forrige hastighet.") \
  X(BOOST_TIME, "Forseringstid") \
  X(UNIT_MIN, "min") \
  X(BOOST_TIME_HELP, "Hvor lenge en forsering varer.") \
  X(BOOST_NEEDS_ADDR_1, "Forsering trenger et panel på adresse 1") \
  /* Temperature */ \
  X(TEMPERATURE, "Temperatur") \
  X(TEMPERATURE_HELP, "Ønsket temperatur, varme og kjøling, og hva følerne måler.") \
  X(SETPOINT, "Ønsket temperatur") \
  X(SETPOINT_HELP, "Temperaturen aggregatet regulerer mot.") \
  X(HEATER_HELP, "La varmeelementet gå.") \
  X(HEATER_KEPT_OFF, "Aggregatet slo ikke på varmen") \
  X(COOLING, "Kjøling") \
  X(COOLING_HELP, "La kjøleren gå.") \
  X(SENSORS, "Følere") \
  X(OUTDOOR_HELP, "Uteluft, før filteret.") \
  X(SUPPLY_HELP, "Luft som blåses inn i rommene, etter varmeelementet.") \
  X(EXTRACT_HELP, "Luft som tas ut av rommene.") \
  X(RETURN_WATER, "Returvann") \
  X(RETURN_WATER_HELP, "Vann som kommer tilbake fra varmebatteriet.") \
  X(PANEL_SENSOR, "Panelføler") \
  X(PANEL_SENSOR_HELP, "Temperaturføleren inne i dette panelet.") \
  /* Comfort and Smart home */ \
  X(COMFORT_MENU_HELP, "Automatiseringer fra ESP-en og innstillingene deres.") \
  X(NOTHING_YET, "Ingenting ennå") \
  X(ESP_OFFLINE, "ESP frakoblet") \
  X(N_ITEM, "%d element") \
  X(N_ITEMS, "%d elementer") \
  X(ITEM_HELP, "Spør ESP-en ...") \
  X(NOTHING_HERE_YET, "Ingenting her ennå") \
  X(SMART_MENU_EMPTY, "Verdier og brytere satt opp på ESP-en vises her.") \
  X(SENT, "Sendt") \
  /* Week program, and a period's settings */ \
  X(WEEK_PROGRAM, "Ukeprogram") \
  X(WEEK_PROGRAM_HELP, "Kjør aggregatet etter ukeprogrammet satt opp under.") \
  X(SCHEDULE_HELP, "Ukeprogrammet: viftehastighet og temperatur for hver dag.") \
  X(PROGRAM_ON, "Program på") \
  X(WEEK_OVERVIEW, "Ukeoversikt") \
  X(WEEK_OVERVIEW_HELP, "Alle sju dagene på ett brett.") \
  X(DAYS, "Dager") \
  X(MONDAY_HELP, "Perioder og viftehastigheter for mandag.") \
  X(TUESDAY_HELP, "Perioder og viftehastigheter for tirsdag.") \
  X(WEDNESDAY_HELP, "Perioder og viftehastigheter for onsdag.") \
  X(THURSDAY_HELP, "Perioder og viftehastigheter for torsdag.") \
  X(FRIDAY_HELP, "Perioder og viftehastigheter for fredag.") \
  X(SATURDAY_HELP, "Perioder og viftehastigheter for lørdag.") \
  X(SUNDAY_HELP, "Perioder og viftehastigheter for søndag.") \
  X(DAY_PERIOD, "%s, periode %d") \
  X(START, "Start") \
  X(PERIOD_START_HELP, "Når perioden starter.") \
  X(END, "Slutt") \
  X(PERIOD_END_HELP, "Når perioden slutter. Utenfor periodene går aggregatet på hastigheten du har valgt.") \
  X(PERIOD_FAN_SPEED_HELP, "Viftehastighet i perioden.") \
  X(PERIOD_SETPOINT_HELP, "Temperatur i perioden.") \
  X(REMOVE_PERIOD, "Fjern periode") \
  X(REMOVE_PERIOD_HELP, "Sletter denne perioden.") \
  X(PERIOD_REMOVED, "Periode fjernet") \
  /* Filter */ \
  X(FILTER_HELP, "Når filtrene skal byttes, og nullstilling av telleren etter bytte.") \
  X(CHANGE_NOW, "Bytt nå") \
  X(DAY_LEFT, "%d dag igjen") \
  X(DAYS_LEFT, "%d dager igjen") \
  X(STATUS, "Status") \
  X(STATUS_HELP, "Tid til filtrene bør byttes.") \
  X(FILTER_HOURS, "%u t") \
  X(RUNNING_SINCE_CHANGE, "Timer siden bytte") \
  X(RUNNING_SINCE_CHANGE_HELP, "Viftetimer siden filtertelleren sist ble nullstilt.") \
  X(TIMES_CHANGED, "Antall bytter") \
  X(TIMES_CHANGED_HELP, "Hvor mange ganger filtertelleren er nullstilt.") \
  X(CHANGE_EVERY, "Bytt hver") \
  X(UNIT_DAYS, "dager") \
  X(CHANGE_EVERY_HELP, "Hvor ofte filterpåminnelsen kommer.") \
  X(FILTERS_CHANGED, "Filtre byttet") \
  X(FILTERS_CHANGED_HELP, "Starter filtertelleren på nytt. Gjør dette etter at du har satt inn nye filtre.") \
  /* Alarms */ \
  X(ALARMS, "Alarmer") \
  X(ALARMS_HELP, "Alarmer nå og tidligere, og hvilke som dukker opp.") \
  X(ONE_ACTIVE, "%d aktiv") \
  X(N_ACTIVE, "%d aktive") \
  X(ALL_CLEAR, "Ingen alarmer") \
  X(ACTIVE_ALARMS, "Aktive alarmer") \
  X(ACTIVE_ALARMS_HELP, "Alarmer som er på akkurat nå.") \
  X(POP_UPS, "Popup-varsler") \
  X(POP_UPS_HELP, "Hvilke nye alarmer som vises på skjermen og vekker den.") \
  X(ALL_ALARMS, "Alle alarmer") \
  X(NOT_THE_FILTER, "Alle unntatt filter") \
  X(HISTORY, "Historikk") \
  X(HISTORY_HELP, "De siste alarmene med dato og tid.") \
  X(RESET_ALARMS, "Nullstill alarmer") \
  X(RESET_ALARMS_HELP, "Fjerner alarmer der årsaken er borte.") \
  X(CLEAR_HISTORY, "Slett historikk") \
  X(CLEAR_HISTORY_HELP, "Sletter alarmhistorikken.") \
  /* Run hours */ \
  X(STATISTICS, "Driftstimer") \
  X(STATISTICS_HELP, "Hvor mange timer aggregatet har gått på hvert trinn.") \
  X(HOURS_TOTAL, "%s totalt") \
  X(HOURS_THOUSANDS, "%lu %03lu t") \
  X(HOURS, "%lu t") \
  X(TOTAL, "Totalt") \
  X(TOTAL_HOURS_HELP, "Timer aggregatet har hatt strøm.") \
  X(STOPPED, "Stoppet") \
  X(STOPPED_HOURS_HELP, "Timer på STOPP.") \
  X(MIN_HOURS_HELP, "Timer på MIN.") \
  X(NORMAL_HOURS_HELP, "Timer på NORMAL.") \
  X(MAX_HOURS_HELP, "Timer på MAKS og forsering.") \
  X(ROTOR_HOURS_HELP, "Timer gjenvinningsrotoren har gått.") \
  X(HEATER_HOURS_HELP, "Timer varmeelementet har vært på.") \
  /* Display */ \
  X(DISPLAY, "Skjerm") \
  X(DISPLAY_HELP, "Tema, lysstyrke, nattmodus, startskjermen, lysene og språket.") \
  X(THEME, "Tema") \
  X(THEME_HELP, "Fargetemaet for alle skjermbilder.") \
  X(BRIGHTNESS, "Lysstyrke") \
  X(BRIGHTNESS_HELP, "Skjermens lysstyrke i bruk.") \
  X(LANGUAGE, "Språk") \
  X(LANGUAGE_HELP, "Språket på panelet.") \
  X(N_24_HOUR_CLOCK, "24-timers klokke") \
  X(N_24_HOUR_CLOCK_HELP, "14:30 i stedet for 2:30 pm.") \
  X(TEMPERATURES, "Temperaturer") \
  X(TEMPERATURES_HELP, "Vis temperaturer som 21.5° eller 22°. Ønsket temperatur vises alltid med desimal.") \
  X(WHOLE_DEGREES, "Hele grader") \
  X(ONE_DECIMAL, "En desimal") \
  X(KEY_HINTS, "Tastehjelp") \
  X(KEY_HINTS_HELP, "Vis hva tastene gjør på linjen nederst.") \
  X(PROCESSOR_SPEED, "Prosessorfart") \
  X(PROCESSOR_SPEED_HELP, "Over 72 MHz går prosessoren over oppgitt hastighet, og skjermen tegnes fortere. " \
                          "Panelet starter på nytt for å bytte. Krasjer det, eller bekrefter du ikke innen 20 s, " \
                          "går det tilbake til 72 MHz. Over 80 er skjermen tregere.") \
  X(N_72_MHZ_STOCK, "72 MHz (standard)") \
  X(N_80_MHZ, "80 MHz") \
  X(N_88_MHZ, "88 MHz") \
  X(N_96_MHZ, "96 MHz") \
  X(N_128_MHZ, "128 MHz") \
  /* Display > Night mode */ \
  X(NIGHT_MODE, "Nattmodus") \
  X(NIGHT_MODE_HELP, "Bruk en annen lysstyrke og et annet tema om natten.") \
  X(FROM, "Fra") \
  X(NIGHT_FROM_HELP, "Når nattmodus starter.") \
  X(TO, "Til") \
  X(NIGHT_TO_HELP, "Når nattmodus slutter.") \
  X(NIGHT_BRIGHTNESS_HELP, "Skjermens lysstyrke i bruk om natten.") \
  X(NIGHT_IDLE_BRIGHTNESS_HELP, "Skjermens lysstyrke om natten når den ikke er i bruk. Av gjør skjermen mørk.") \
  X(THEME_AT_NIGHT, "Tema om natten") \
  X(THEME_AT_NIGHT_HELP, "Fargetemaet for alle skjermbilder om natten.") \
  X(SAME_AS_BY_DAY, "Som om dagen") \
  /* Display > Home screen */ \
  X(HOME_SCREEN, "Startskjerm") \
  X(HOME_SCREEN_HELP, "Visninger, felt på oversikten og startvisningen.") \
  X(START_VIEW, "Startvisning") \
  X(START_VIEW_HELP, "Visningen panelet starter på.") \
  X(DASHBOARD, "Oversikt") \
  X(AIRFLOW, "Luftstrøm") \
  X(TRENDS, "Trender") \
  X(ESP_SENSORS, "Sensorer") \
  X(COMFORT, "Komfort") \
  X(SMART_HOME, "Smarthjem") \
  X(VIEWS, "Visninger") \
  X(VIEWS_HELP, "Hvilke visninger TILBAKE bytter mellom. Oversikten er alltid med.") \
  X(AIRFLOW_HELP, "Viser hvordan luften går gjennom aggregatet, med temperaturer.") \
  X(TRENDS_HELP, "Temperaturene de siste 24 timene.") \
  X(SENSORS_HELP, "Målinger fra ESP-ens sensorer de siste 24 timene.") \
  X(COMFORT_HELP, "Målinger og automatiseringer fra ESP-en.") \
  X(SMART_HOME_HELP, "Verdier og brytere fra ESP-en.") \
  X(BIG_TEMPERATURE, "Stor temperatur") \
  X(BIG_TEMPERATURE_HELP, "Den store temperaturen på oversikten.") \
  X(SUPPLY_AIR, "Tilluft") \
  X(EXTRACT_AIR, "Avtrekksluft") \
  X(DASHBOARD_TILES, "Felt på oversikten") \
  X(DASHBOARD_TILES_HELP, "Hva de fire feltene på oversikten viser.") \
  X(TILE_HELP, "Hva dette feltet på oversikten viser.") \
  /* Display > Lights */ \
  X(LIGHTS, "Lamper") \
  X(LIGHTS_HELP, "Lampene foran på panelet.") \
  X(OK_LIGHT, "OK-lampe") \
  X(OK_LIGHT_HELP, "Lampen som viser at alt er i orden.") \
  X(WHEN_ALL_IS_WELL, "Når alt er i orden") \
  X(NOT_AT_NIGHT, "Ikke om natten") \
  X(ALARM_LIGHT, "Alarmlampe") \
  X(ALARM_LIGHT_HELP, "Lyser mens en alarm er aktiv.") \
  X(FILTER_LIGHT, "Filterlampe") \
  X(FILTER_LIGHT_HELP, "Lyser når det er tid for å bytte filtre.") \
  /* Screensaver */ \
  X(SCREENSAVER, "Skjermsparer") \
  X(SCREENSAVER_MENU_HELP, "Når panelet går i hvile, hvor lyst det er da, og hva det viser.") \
  X(IDLE_AFTER, "Hvile etter") \
  X(IDLE_AFTER_HELP, "Hvor lenge etter siste tastetrykk panelet går i hvile.") \
  X(IDLE_15S, "15 s") \
  X(IDLE_30S, "30 s") \
  X(IDLE_1MIN, "1 min") \
  X(IDLE_2MIN, "2 min") \
  X(IDLE_5MIN, "5 min") \
  X(IDLE_10MIN, "10 min") \
  X(IDLE_30MIN, "30 min") \
  X(NEVER, "Aldri") \
  X(IDLE_BRIGHTNESS, "Lysstyrke i hvile") \
  X(IDLE_BRIGHTNESS_HELP, "Skjermens lysstyrke når panelet ikke er i bruk. Av gjør skjermen mørk.") \
  X(SHOW, "Vis") \
  X(SCREENSAVER_HELP, "Hva skjermen viser når panelet er i hvile.") \
  X(CLOCK, "Klokke") \
  X(CLOCK_TEMPS, "Klokke, temp.") \
  X(PICTURE, "Bilde") \
  X(PICTURE_CLOCK, "Bilde, klokke") \
  X(PICTURE_ALL, "Bilde, alt") \
  X(TEXT_ON_PICTURE, "Tekst på bildet") \
  X(TEXT_ON_PICTURE_HELP, "Hvordan klokken står på bildet. Et skyggelagt felt er lettest å lese, Skygge og Bare " \
                          "tekst dropper feltet, og Liten setter bare klokken og temperaturene i hjørnene.") \
  X(SHADED_BAND, "Skyggelagt felt") \
  X(SHADOW, "Skygge") \
  X(JUST_TEXT, "Bare tekst") \
  X(SMALL, "Liten") \
  X(TEXT_POSITION, "Tekstplassering") \
  X(TEXT_POSITION_HELP, "Hvor klokken står på bildet.") \
  X(BOTTOM, "Nederst") \
  X(TOP, "Øverst") \
  X(PICTURE_HELP, \
    "Skjermsparerbildet fra ESP-en. Det forsvinner når panelet starter på nytt, og ESP-en sender det igjen.") \
  X(RECEIVING_PCT, "Mottar %d %%") \
  X(PICTURE_SIZE, "%d x %d") \
  X(NONE_YET, "Ingen ennå") \
  X(BACK_TO_START_VIEW, "Tilbake til startvisning") \
  X(BACK_TO_START_VIEW_HELP, "Gå tilbake til startvisningen når panelet går i hvile.") \
  /* Date and time */ \
  X(DATE_AND_TIME, "Dato og tid") \
  X(DATE_AND_TIME_HELP, "Klokken aggregatet og ukeprogrammet bruker.") \
  X(HOUR, "Time") \
  X(HOUR_HELP, "Still inn timen.") \
  X(MINUTE, "Minutt") \
  X(MINUTE_HELP, "Still inn minuttet.") \
  X(DAY, "Dag") \
  X(DAY_HELP, "Still inn dagen i måneden.") \
  X(MONTH, "Måned") \
  X(MONTH_HELP, "Still inn måneden.") \
  X(YEAR, "År") \
  X(YEAR_HELP, "Still inn året.") \
  X(SUMMER_TIME, "Sommertid") \
  X(SUMMER_TIME_HELP, "Still klokken automatisk siste søndag i mars og i oktober.") \
  X(TIME_ON_OTHER_PANEL, "Still klokken på det andre panelet") \
  /* System */ \
  X(SYSTEM, "System") \
  X(SYSTEM_HELP, "Versjoner, utstyr, bussen og tastene.") \
  X(NO_CONTACT, "Ingen kontakt") \
  X(ADDRESS, "Adresse %d") \
  X(ABOUT, "Om") \
  X(ABOUT_HELP, "Versjoner og hva som er installert.") \
  X(EQUIPMENT, "Utstyr") \
  X(EQUIPMENT_HELP, "Hva som er koblet til. Innstillinger og visninger for utstyr som mangler, skjules.") \
  X(EQUIP_ESP, "ESP på bussen") \
  X(EQUIP_ESP_HELP, "En ESP på adresse 1 kjører komfortautomatiseringene og deler data med dette panelet. Uten " \
                    "en, sett paneladressen til 1.") \
  X(EQUIP_EC_FANS, "EC-vifter") \
  X(EQUIP_EC_FANS_HELP, \
    "Vifter med trinnløs hastighet, så hvert trinn kan settes i prosent. Settes med en DIP-bryter i aggregatet.") \
  X(EQUIP_WATER_HEATER, "Vannbatteri") \
  X(EQUIP_WATER_HEATER_HELP, "Vannbatteri i stedet for elektrisk varme. Gir returvannsføler og frostsikring. " \
                             "Settes med en DIP-bryter i aggregatet.") \
  X(EQUIP_COOLING_HELP, "Aggregatet har kjøling koblet til.") \
  X(BUS, "Buss") \
  X(BUS_HELP, "Modbus-trafikk mellom dette panelet og CX60.") \
  X(DIAGNOSTICS, "Diagnostikk") \
  X(DIAGNOSTICS_HELP, "Oppetid, omstarter, minne og andre tall for feilsøking.") \
  X(PANEL_ADDRESS, "Paneladresse") \
  X(PANEL_ADDRESS_HELP, \
    "Bruk 3 med en ESP på adresse 1, ellers 1. Bare laveste adresse kan endre ønsket temperatur og " \
    "NORMAL-hastighetene. Start aggregatet på nytt etterpå, for CX60 leter bare etter paneler ved oppstart.") \
  X(KEY_LOCK, "Tastelås") \
  X(KEY_LOCK_HELP, "Hold ? i 3 s for å låse eller låse opp tastene.") \
  X(LOCK_WHEN_IDLE, "Lås i hvile") \
  X(LOCK_WHEN_IDLE_HELP, "Låser tastene når panelet går i hvile.") \
  X(RESTART_PANEL, "Start panelet på nytt") \
  X(RESTART_PANEL_HELP, "Starter dette panelet på nytt. Innstillingene beholdes.") \
  X(RESET_PANEL_SETTINGS, "Tilbakestill panelinnstillinger") \
  X(RESET_PANEL_SETTINGS_HELP, \
    "Setter panelets innstillinger tilbake til standard og starter det på nytt. Adresse og utstyr beholdes.") \
  /* the value editor and action box */ \
  X(HOLD_OK_WARNING, "Hold OK i 2 sekunder. Dette kan ikke angres.") \
  X(PRESS_OK, "Trykk OK for å fortsette.") \
  X(KEEP_HOLDING_OK, "Hold OK inne") \
  X(DONE, "Ferdig") \
  X(SAVED, "Lagret") \
  X(UNIT_PCT, "%") \
  X(UNIT_C, "°C") \
  /* the Advanced menu */ \
  X(ADVANCED, "Avansert") \
  X(ADVANCED_HELP, "Innstillinger installatøren vanligvis setter én gang: vifter, regulering, følere, innganger, " \
                   "avfrosting og test.") \
  X(ADVANCED_MENU, "Avansert meny") \
  X(ADVANCED_MENU_HELP, "Viser Avansert i menyen, med installatørinnstillingene til aggregatet. Feil verdier der " \
                        "kan få aggregatet til å gå dårlig.") \
  X(INSTALLER_SETTINGS, "For installatøren") \
  X(FACTORY_RESET_UNIT, "Fabrikkinnstill aggregatet") \
  X(FACTORY_RESET_UNIT_HELP, "Setter alle innstillinger i aggregatet (CX60) tilbake til fabrikkinnstillingene.") \
  /* Advanced > Fans */ \
  X(ADV_FANS_HELP, "Viftepådrag på hver hastighet, og hastigheten en forsering går på.") \
  X(MIN_SUPPLY, "Min, tilluft") \
  X(MIN_SUPPLY_HELP, "Tilluftsviftens pådrag på MIN.") \
  X(MIN_EXTRACT, "Min, avtrekk") \
  X(MIN_EXTRACT_HELP, "Avtrekksviftens pådrag på MIN.") \
  X(NORMAL_SUPPLY, "Normal, tilluft") \
  X(NORMAL_SUPPLY_HELP, "Tilluftsviftens pådrag på NORMAL.") \
  X(NORMAL_EXTRACT, "Normal, avtrekk") \
  X(NORMAL_EXTRACT_HELP, "Avtrekksviftens pådrag på NORMAL.") \
  X(MAX_SUPPLY, "Maks, tilluft") \
  X(MAX_SUPPLY_HELP, "Tilluftsviftens pådrag på MAKS og under forsering.") \
  X(MAX_EXTRACT, "Maks, avtrekk") \
  X(MAX_EXTRACT_HELP, "Avtrekksviftens pådrag på MAKS og under forsering.") \
  X(BOOST_SPEED, "Forseringshastighet") \
  X(BOOST_SPEED_HELP, "Hastigheten en forsering går på.") \
  /* Advanced > Regulation */ \
  X(REGULATION, "Regulering") \
  X(REGULATION_HELP, "Hva aggregatet regulerer på, grensene for tilluften, nøytralsonene og regulatorinnstillingene.") \
  X(REGULATE_ON, "Reguler på") \
  X(REGULATE_ON_HELP, "Hvilken lufttemperatur ønsket temperatur gjelder for.") \
  X(MIN_SUPPLY_AIR, "Min tilluft") \
  X(MIN_SUPPLY_AIR_HELP, "Tilluften går aldri under dette, også ved regulering på avtrekksluft.") \
  X(MAX_SUPPLY_AIR, "Maks tilluft") \
  X(MAX_SUPPLY_AIR_HELP, "Tilluften går aldri over dette.") \
  X(NEUTRAL_ZONES, "Nøytralsoner") \
  X(COOLING_TO_ROTOR, "Kjøling til rotor") \
  X(COOLING_TO_ROTOR_HELP, "Et mellomrom mellom der kjølingen slutter og rotoren starter, så de ikke jobber mot " \
                           "hverandre.") \
  X(ROTOR_TO_HEATING, "Rotor til varme") \
  X(ROTOR_TO_HEATING_HELP, "Et mellomrom mellom full rotor og at varmen starter.") \
  X(REGULATOR_TUNING, "Regulatorinnstilling") \
  X(REGULATOR_TUNING_HELP, "K- og I-verdiene til regulatorene i aggregatet. La dem være hvis du ikke vet hva de " \
                           "gjør.") \
  X(HEATING, "Varme") \
  X(SUPPLY_AIR_K, "Tilluft, K") \
  X(SUPPLY_AIR_I, "Tilluft, I") \
  X(EXTRACT_AIR_K, "Avtrekk, K") \
  X(EXTRACT_AIR_I, "Avtrekk, I") \
  X(SUPPLY_K_HELP, "Forsterkning ved regulering på tilluft. Høyere reagerer kraftigere.") \
  X(SUPPLY_I_HELP, "Integraltid ved regulering på tilluft. Lavere tar igjen fortere, men kan svinge over.") \
  X(EXTRACT_K_HELP, "Forsterkning ved regulering på avtrekk. Høyere reagerer kraftigere.") \
  X(EXTRACT_I_HELP, "Integraltid ved regulering på avtrekk. Lavere tar igjen fortere, men kan svinge over.") \
  /* Advanced > Cooling */ \
  X(ADV_COOLING_HELP, "Når kjølingen får gå.") \
  X(MIN_OUTDOOR_TEMP, "Min utetemperatur") \
  X(MIN_OUTDOOR_TEMP_HELP, "Ingen kjøling når det er kaldere enn dette ute.") \
  X(MIN_FAN_SPEED, "Min viftehastighet") \
  X(MIN_FAN_SPEED_HELP, "Kjølingen går bare på denne viftehastigheten eller høyere.") \
  X(RESTART_DELAY, "Tilslagsforsinkelse") \
  X(RESTART_DELAY_HELP, "Hvor lenge kjølingen venter før den starter igjen.") \
  X(UNIT_S, "s") \
  /* Advanced > Heat recovery */ \
  X(HEAT_RECOVERY_HELP, "Rotoren: kjølegjenvinning, hva en rotoralarm gjør, og avfrosting.") \
  X(COOLING_RECOVERY, "Kjølegjenvinning") \
  X(COOLING_RECOVERY_HELP, "Når det er varmere ute enn inne, holder rotoren på kjøligheten inne.") \
  X(DIFFERENCE, "Differanse") \
  X(DIFFERENCE_HELP, "Hvor mye varmere det må være ute enn inne for kjølegjenvinning.") \
  X(ROTOR_ALARM, "Rotoralarm") \
  X(ROTOR_ALARM_HELP, "Hva aggregatet gjør hvis rotoren slutter å gå.") \
  X(STOP_THE_UNIT, "Stopp aggregatet") \
  X(KEEP_RUNNING, "Fortsett å gå") \
  X(DE_ICING, "Avfrosting") \
  X(DE_ICING_HELP, "Hindrer at rotoren fryser til når det er veldig kaldt ute.") \
  X(STARTS_BELOW, "Starter under") \
  X(STARTS_BELOW_HELP, "Avfrostingen starter når det er kaldere enn dette ute.") \
  X(ROTOR_SPEED, "Rotorhastighet") \
  X(ROTOR_SPEED_HELP, "Hvor fort rotoren går under avfrosting. 180 er full fart.") \
  X(ROTOR_PERIOD, "Rotorperiode") \
  X(ROTOR_PERIOD_HELP, "Hvor lenge hver avfrosting av rotoren varer.") \
  X(ROTOR_FREQUENCY, "Rotorfrekvens") \
  X(ROTOR_FREQUENCY_HELP, "Hvor ofte rotoren avfrostes.") \
  X(LESS_AIR_BELOW, "Mindre luft under") \
  X(LESS_AIR_BELOW_HELP, "Når det er kaldere enn dette ute, går tilluftsviften ned på MIN og avtrekksviften på " \
                         "NORMAL.") \
  X(REDUCED_FAN_SPEED, "Redusert vifte") \
  X(REDUCED_FAN_SPEED_HELP, "Viftepådrag mens det er mindre luft.") \
  X(FAN_PERIOD, "Vifteperiode") \
  X(FAN_PERIOD_HELP, "Hvor lenge hver periode med mindre luft varer.") \
  X(FAN_FREQUENCY, "Viftefrekvens") \
  X(FAN_FREQUENCY_HELP, "Hvor ofte periodene med mindre luft kommer.") \
  X(RAMP_TIME, "Rampetid") \
  X(RAMP_TIME_HELP, "Hvor fort viftene går ned til mindre luft og opp igjen.") \
  /* Advanced > Sensors */ \
  X(ADV_SENSORS_HELP, "Justering av følerne, hvilke følere som er montert, og panelets egen føler.") \
  X(SUPPLY_AIR_OFFSET, "Justering tilluft") \
  X(EXTRACT_AIR_OFFSET, "Justering avtrekk") \
  X(OUTDOOR_OFFSET, "Justering ute") \
  X(RETURN_WATER_OFFSET, "Justering returvann") \
  X(SENSOR_OFFSET_HELP, "Justerer det denne føleren i aggregatet måler.") \
  X(EXTRACT_AIR_SENSOR, "Avtrekksføler") \
  X(EXTRACT_AIR_SENSOR_HELP, "Avtrekksføleren er montert. Regulering på avtrekk trenger den.") \
  X(OUTDOOR_SENSOR, "Uteføler") \
  X(OUTDOOR_SENSOR_HELP, "Uteføleren er montert. Kjøling og avfrosting trenger den.") \
  X(THIS_PANEL, "Dette panelet") \
  X(PANEL_SENSOR_OFFSET, "Justering panelføler") \
  X(PANEL_SENSOR_OFFSET_HELP, "Korrigerer panelføleren. Sammenlign med et termometer ved siden av panelet etter " \
                              "en time uten bruk, siden panelet varmer seg litt.") \
  X(INDOOR_TEMPERATURE, "Innetemperatur") \
  X(INDOOR_TEMPERATURE_HELP, \
    "Hvor innetemperaturen hentes fra: avtrekksluften (hele boligen) eller dette panelet (dette rommet).") \
  /* Advanced > Inputs */ \
  X(INPUTS, "Innganger") \
  X(INPUTS_HELP, "Hva aggregatet gjør når noe er koblet til inngangene: brannalarm, filtervakt, hjemme/borte og " \
                 "peis.") \
  X(FIRE_SMOKE, "Brann/røyk") \
  X(FIRE_SMOKE_HELP, "Hva viftene gjør når inngangen for brann- eller røykalarm løser ut.") \
  X(STOP_BOTH_FANS, "Stopp begge viftene") \
  X(BOTH_AT_MAX, "Begge på maks") \
  X(EXTRACT_AT_MAX, "Avtrekk på maks") \
  X(SUPPLY_AT_MAX, "Tilluft på maks") \
  X(FILTER_GUARD_HELP, "En trykkvakt på filtrene er koblet til. Den gir alarm når de er tette.") \
  X(EXTERNAL_SETPOINT, "Ekstern temp.styring") \
  X(EXTERNAL_SETPOINT_HELP, "Henter ønsket temperatur fra en bryter koblet til aggregatet, 10 til 30 grader.") \
  X(HOME_AWAY, "Hjemme/borte") \
  X(AWAY_SPEED_HELP, "Viftehastighet mens hjemme/borte-inngangen er på.") \
  X(AWAY_SETPOINT_HELP, "Ønsket temperatur mens hjemme/borte-inngangen er på.") \
  X(HOLD_TIME, "Holdetid") \
  X(HOLD_TIME_HELP, "Hvor lenge aggregatet blir i hjemme/borte etter at inngangen går av.") \
  X(FIREPLACE_INPUT, "Peisinngang") \
  X(SUPPLY_SPEED, "Tilluftshastighet") \
  X(SUPPLY_SPEED_HELP, "Tilluftsviftens hastighet mens peisinngangen er på.") \
  X(EXTRACT_SPEED, "Avtrekkshastighet") \
  X(EXTRACT_SPEED_HELP, "Avtrekksviftens hastighet mens peisinngangen er på.") \
  /* Advanced > Start and stop */ \
  X(START_AND_STOP, "Start og stopp") \
  X(START_AND_STOP_HELP, "Pausene mellom trinnene når aggregatet starter, og hvor lenge viftene går etter varmen.") \
  X(START_DELAY_1, "Startforsinkelse 1") \
  X(START_DELAY_2, "Startforsinkelse 2") \
  X(START_DELAY_3, "Startforsinkelse 3") \
  X(START_DELAY_4, "Startforsinkelse 4") \
  X(START_DELAY_HELP, "Hvor lenge aggregatet venter før dette trinnet når det starter.") \
  X(FANS_AFTER_HEATER, "Vifter etter varme") \
  X(FANS_AFTER_HEATER_HELP, "Hvor lenge viftene går etter at elektrovarmen stopper, for å kjøle den ned.") \
  /* Advanced > Test */ \
  X(TEST, "Test") \
  X(TEST_HELP, "Kjører vifter, varme, rotor eller kjøling manuelt for å sjekke dem. En test varer et minutt.") \
  X(TEST_FANS_HELP, "Kjører begge viftene på dette i et minutt.") \
  X(TEST_HEATER_HELP, "Slår på varmen i et minutt.") \
  X(TEST_ROTOR_HELP, "Kjører rotoren på full fart i et minutt.") \
  X(TEST_COOLING_HELP, "Kjører kjølingen på dette pådraget i et minutt.") \
  /* Advanced > Unit */ \
  X(UNIT_INFO_HELP, "DIP-bryterne for rotoren i aggregatet, og panelene det fant på bussen.") \
  X(DS2_ROTOR_GUARD, "DS2 rotorvakt") \
  X(DS2_HELP, "DIP-bryter 2 i aggregatet, rotorvakten.") \
  X(DS3_ROTOR_MOTOR, "DS3 rotormotor") \
  X(DS3_HELP, "DIP-bryter 3 i aggregatet, hva slags rotormotor.") \
  X(PANELS, "Paneler") \
  X(PANEL_1, "Panel 1") \
  X(PANEL_2, "Panel 2") \
  X(PANEL_3, "Panel 3") \
  X(PANELS_HELP, "Programvareversjonen til panelet på denne adressen, slik aggregatet så det da det startet.") \
  X(NO_PANEL, "Ingen") \
  /* alarms, week program, info pages, help */ \
  X(ALARM, "Alarm") \
  X(OVERHEAT_THERMOSTAT, "Overopphetingstermostat") \
  X(FIRE_OR_SMOKE, "Brann eller røyk") \
  X(FILTER_GUARD, "Filtervakt") \
  X(SUPPLY_SENSOR_FAULT, "Feil på tilluftsføler") \
  X(EXTRACT_SENSOR_FAULT, "Feil på avtrekksføler") \
  X(OUTDOOR_SENSOR_FAULT, "Feil på uteføler") \
  X(RETURN_WATER_SENSOR_FAULT, "Feil på returvannsføler") \
  X(WATER_COIL_FROST, "Frost i vannbatteri") \
  X(ROTOR_STOPPED, "Rotor stoppet") \
  X(FILTER_CHANGE_DUE, "Filterbytte") \
  X(ADVICE_OVERHEAT, "Varmeelementet ble for varmt og ble slått av. Sjekk at luften kan strømme, og tilbakestill " \
                     "så termostaten på aggregatet.") \
  X(ADVICE_FIRE, "Brannalarminngangen er aktiv. Aggregatet går slik det er innstilt for brann og røyk.") \
  X(ADVICE_FILTER_GUARD, "Filtervakten melder at filtrene er tette. Bytt dem.") \
  X(ADVICE_SUPPLY_SENSOR, "Tilluftsføleren er brutt eller kortsluttet. Varmen er begrenset til den er rettet.") \
  X(ADVICE_EXTRACT_SENSOR, "Avtrekksføleren er brutt eller kortsluttet.") \
  X(ADVICE_OUTDOOR_SENSOR, "Uteføleren er brutt eller kortsluttet. Frostsikringen virker kanskje ikke.") \
  X(ADVICE_RETURN_WATER_SENSOR, "Returvannsføleren er brutt eller kortsluttet.") \
  X(ADVICE_WATER_FROST, "Vannbatteriet holdt på å fryse. Tilluftsviften stoppet for å beskytte det.") \
  X(ADVICE_ROTOR, "Rotorvakten merker ingen rotasjon. Sjekk reimen og rotormotoren.") \
  X(ADVICE_FILTER_DUE, "Det er tid for å bytte filtre. Velg Filter > Filtre byttet etterpå.") \
  X(MON, "man") \
  X(TUE, "tir") \
  X(WED, "ons") \
  X(THU, "tor") \
  X(FRI, "fre") \
  X(SAT, "lør") \
  X(SUN, "søn") \
  X(SERVICE_CAPS, "SERVICE") \
  X(ALARM_CAPS, "ALARM") \
  X(ALARM_SINCE, "Siden %02d.%02d %s") \
  X(N_MORE, "+%d til") \
  X(ALARM_POPUP_HELP, "En ny alarm. En bjelle står i statuslinjen så lenge en alarm er aktiv.") \
  X(ALARM_HISTORY, "Alarmhistorikk") \
  X(NO_ALARMS_LOGGED, "Ingen alarmer logget") \
  X(NO_ACTIVE_ALARMS, "Ingen aktive alarmer") \
  X(ALARM_WHEN, "%s %02d.%02d kl. %s") \
  X(ACTIVE_SINCE, "Aktiv siden") \
  X(LOGGED, "Logget") \
  X(SINCE, "Siden") \
  X(ALARM_HISTORY_HELP, "De siste alarmene CX60 har logget, nyeste først.") \
  X(ALARM_LIST_HELP, "Aktive alarmer. Rett årsaken, og bruk så Nullstill alarmer.") \
  X(PROGRAM_OFF, "Program av") \
  X(WEEK_HELP, "Viftehastighet gjennom uken. Utenfor de fargede periodene går aggregatet på hastigheten du har " \
               "valgt. Streken viser tiden nå. Rediger dagene under Ukeprogram.") \
  X(ADD_PERIOD, "+  Legg til periode") \
  X(DAY_SCREEN_HELP, "Opptil fire perioder om dagen, hver med egen viftehastighet og temperatur. Utenfor dem går " \
                     "aggregatet på hastigheten du har valgt.") \
  X(PERIOD_ADDED, "Periode lagt til") \
  X(PANEL_FIRMWARE, "Panelets fastvare") \
  X(CX60_SOFTWARE, "CX60-programvare") \
  X(NOT_SEEN_YET, "ikke sett ennå") \
  X(ESP, "ESP") \
  X(ESP_ADDRESS, "%s, adresse 1") \
  X(ONLINE, "tilkoblet") \
  X(OFFLINE, "frakoblet") \
  X(FANS, "Vifter") \
  X(EC_VARIABLE, "EC, trinnløse") \
  X(AC_STEPPED, "AC, trinnvise") \
  X(WATER_COIL, "Vannbatteri") \
  X(ELECTRIC, "Elektrisk") \
  X(INSTALLED, "Installert") \
  X(NOT_INSTALLED, "Ingen") \
  X(PROCESSOR, "Prosessor") \
  X(MHZ_BUSY, "%u MHz, %d %% opptatt") \
  X(UNIT, "Aggregat") \
  X(NO_CONTACT_LOWER, "ingen kontakt") \
  X(TALKING, "svarer") \
  X(POLLS_TO_THIS_PANEL, "Spørringer til dette panelet") \
  X(FRAMES_OK, "Rammer OK") \
  X(ERRORS, "Feil") \
  X(ESP_LINK, "ESP-link") \
  X(LINK_VERSION, "v%d, %s") \
  X(CONNECTED, "tilkoblet") \
  X(WAITING, "venter") \
  X(REFUSED, "avvist") \
  X(LINK_OTHER_VERSION, "v%d, ESP-en v%d") \
  X(LINK_FRAMES, "Link-rammer") \
  X(LATE_BUSY_LOST, "Sene, opptatt, tapt") \
  X(UNIT_PAUSE, "Pause etter status") \
  X(PAUSE_MS, "%s ms") \
  X(PAUSE_TOO_SHORT, "%s ms, for kort") \
  X(PAUSE_NO_GAPS, "%s ms, ingen luker") \
  X(BUS_PAGE_HELP, "Trafikk på RS-485-bussen. CX60 er master og spør dette panelet. ESP-linken bruker pausen " \
                   "etter aggregatets statusmelding. Er pausen under 13 ms, stopper panelet linken en stund.") \
  X(UPTIME, "Oppetid") \
  X(UPTIME_DAYS, "%lu d %02u:%02u") \
  X(LAST_START, "Siste start") \
  X(START_POWER, "Strøm på") \
  X(START_PIN, "Reset-pinne") \
  X(START_WATCHDOG, "Vakthund") \
  X(START_SOFTWARE, "Omstart") \
  X(START_CRASH, "Krasj %08lX") \
  X(START_ONE_RESTART, "%s, 1 omstart") \
  X(START_RESTARTS, "%s, %lu omstarter") \
  X(MEMORY, "Minne") \
  X(MEMORY_USE, "%s av %u KB, topp %s") \
  X(FREE_STACK, "Ledig stakk") \
  X(FULL_REDRAW, "Full opptegning") \
  X(REDRAW_SENDING, "%lu ms, %lu overføring") \
  X(SETTINGS_SAVES, "Lagringer") \
  X(CHIP, "Brikke") \
  X(CHIP_IDS, "%08lX, oppstart %08lX") \
  X(DIAG_PAGE_HELP, "Omstarter teller siden strømmen kom på, og bør holde seg lavt. Prosessor er klokken og hvor " \
                    "mye den er våken. Minne er skjermens, nå og som mest. Brikke er prosessor-ID og sjekksum av " \
                    "oppstartslasteren.") \
  X(ABOUT_PAGE_HELP, "Programvareversjoner for panelet og aggregatet, og hva som er installert.") \
  X(NOTICE_FIRMWARE, "Erstatningsfastvare %s") \
  X(NOTICE_REVERSED, "Reversert av") \
  X(NOTICE_PANEL, "Panelfastvare av") \
  X(NOTICE_ESP, "ESPHome-integrasjon av") \
  X(NOTICE_TOOLS, "Plukket fra hverandre med") \
  X(NOTICE_PICOPWNER, "stm32f1-picopwner av CTXz") \
  X(NOTICE_BUILT_ON, "Bygget på") \
  X(NOTICE_JPEG, "TJpgDec av ChaN") \
  X(NOTICE_FONT, "Tekst i Noto Sans") \
  X(NOTICE_ICONS, "Ikoner fra Material Design Icons\nav Pictogrammers") \
  X(NOTICE_ESPHOME, "Snakker med Home Assistant via ESPHome") \
  X(NOTICE_FLEXIT, "Ikke laget eller godkjent av Flexit") \
  X(NOTICE_HARMED, "Ingen ventilasjonsaggregater ble skadet") \
  X(NOTICE_CX60, "En CX60 ble litt forvirret") \
  X(NOTICE_SINCE, "Frisk luft siden 2026") \
  X(NOTICE_OK, "OK: Flappy Fan") \
  X(KEYTEST, "Flappy Fan") \
  X(KEYTEST_START, "Opp for å flakse") \
  X(KEYTEST_SCORE, "Poeng %d") \
  X(KEYTEST_BEST, "Rekord %d") \
  X(KEYTEST_AGAIN, "OK: igjen") \
  X(KEYTEST_HELP, "OPP eller OK får viften til å flakse. Få den gjennom åpningene i kanalene, hver gir et poeng. " \
                  "Kanalene går fortere for hvert poeng.") \
  X(HELP, "Hjelp") \
  X(NO_HELP, "Ingenting å forklare her.") \
  X(HELP_FOOTER, "TILBAKE går ett nivå opp, hold den for å gå hjem. Hold ? i 3 s for å låse tastene.") \
  /* ESP views and messages */ \
  X(PRESS, "Trykk") \
  X(WARNING_CAPS, "ADVARSEL") \
  X(MESSAGE_CAPS, "MELDING") \
  X(NOTE_FROM_HA, "fra Home Assistant, %s") \
  X(NOTE_HELP, "En melding ESP-en sendte, for eksempel fra en Home Assistant-automatisering.") \
  /* keys */ \
  X(KEYS_LOCKED_HOW, "Tastene er låst: hold ? i 3 s") \
  X(KEYS_LOCKED, "Tastene er låst") \
  X(KEYS_UNLOCKED, "Tastene er låst opp") \
  /* start-up */ \
  X(UNKNOWN_DISPLAY_CONTROLLER, "Ukjent skjermkontroller") \
  X(CRASHED_AT_MHZ, "Krasjet på %d MHz, tilbake på 72") \
  /* Processor speed on trial */ \
  X(KEEP_MHZ, "Beholde %d MHz?") \
  X(KEEP_QUESTION, "Ser skjermen riktig ut? OK beholder det, TILBAKE angrer.") \
  X(BACK_TO_72_IN, "Tilbake til 72 MHz om %d s") \
  /* key hints and tiles */ \
  X(MOVE, "Flytt") \
  X(OPEN, "Åpne") \
  X(BACK, "Tilbake") \
  X(FAN, "Vifte") \
  X(VIEW, "Visning") \
  X(CHANGE, "Endre") \
  X(ADJUST, "Juster") \
  X(SAVE, "Lagre") \
  X(CANCEL, "Avbryt") \
  X(CHOOSE, "Velg") \
  X(SWITCH, "Bytt") \
  X(RUN, "Kjør") \
  X(EDIT, "Rediger") \
  X(LATER, "Senere") \
  X(FILTER_MENU, "Filtermeny") \
  X(ALARM_LIST, "Alarmliste") \
  X(CLOSE, "Lukk") \
  X(CONFIRM, "Bekreft") \
  X(KEEP, "Behold") \
  X(HOLD_TO_CONFIRM, "Hold for å bekrefte") \
  X(TILE_1, "Felt 1") \
  X(TILE_2, "Felt 2") \
  X(TILE_3, "Felt 3") \
  X(TILE_4, "Felt 4")

LANGUAGE(LANG_NO, TEXTS);
