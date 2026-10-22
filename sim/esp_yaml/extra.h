// Stand-ins for what the panel packages use from the rest of a config. The time, the UART, the comfort
// packages' entities, the ones tests/panel.yaml puts on the panel as items, and the pictures.
#pragma once
static Time flexit_panel_time;

struct Uart {
  void set_rx_full_threshold(size_t) {}
};  // core.yaml's UART
static Uart modbus_uart_obj, *modbus_uart = &modbus_uart_obj;

static Switch fireplace_mode_switch;
static int fireplace_duration_minutes = 60;
static Number fireplace_duration{1, 240, 1, 60};
static Switch humidity_boost_enable;
static Number humidity_trigger_delta{2, 30, 0.5, 8};
static Sensor inside_humidity;
static Select co2_control{{"Off", "Stepped", "Proportional", "PI"}, "Off"};
static Number co2_target{400, 1500, 50, 800};
static Sensor co2;
static Switch summer_cooling_enable;

static BinarySensor heater_enabled;
static Switch heater;
static Number temperature{10, 30, 0.5, 20};
static Button clear_alarms;

static Image panel_picture_dusk;
static Animation panel_animation_ball, panel_animation_noise;
