// Stand-ins for what the panel packages use from the rest of a config. The time, the UART, the comfort
// packages' entities, the ones tests/panel.yaml puts on the panel as items, and the pictures.
#pragma once
static Time flexit_panel_time;

struct Uart {
  void set_rx_full_threshold(size_t) {}
};  // core.yaml's UART
static Uart modbus_uart_obj, *modbus_uart = &modbus_uart_obj;

static Switch fireplace_mode_switch;
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

// screen_camera.yaml's, which asks like Home Assistant does: one image, or a stream of them until it stops
struct PanelCamera {
  bool single = false, stream = false;
  std::vector<std::vector<uint8_t>> images;  // what it got, in one piece
  bool wanted() const { return single || stream; }
  void publish(std::vector<std::shared_ptr<const std::vector<uint8_t>>> &&parts) {
    std::vector<uint8_t> j;
    for (auto &p : parts)
      j.insert(j.end(), p->begin(), p->end());
    images.push_back(std::move(j));
    single = false;
  }
};
static PanelCamera panel_screen;

static Image panel_picture_dusk;
static OnlineImage panel_picture_radar, panel_picture_front_door;
static HttpRequest panel_http;
static Animation panel_animation_ball, panel_animation_noise;
