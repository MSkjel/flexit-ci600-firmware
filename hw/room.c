// see room.h
#include "room.h"
#include "board.h"
#include "model.h"

// Stock's table from panel_temp_sensor_read_raw, in 0.1 C at every 128 ADC counts, before the offset.
static const int16_t TABLE[33] = {
    822, 780, 739, 698, 659, 621, 583, 547, 511, 476, 441, 408, 375, 343,  311,  280,  250,
    220, 191, 163, 135, 107, 80,  54,  28,  3,   -22, -47, -71, -95, -118, -141, -163,
};

static int16_t convert(uint16_t raw) {
  unsigned i = raw / 128, f = raw % 128;
  if (i >= 32)
    return TABLE[32];
  return (int16_t) (TABLE[i] + (TABLE[i + 1] - TABLE[i]) * (int) f / 128);
}

void room_second(void) {
  // smoothed, the thermistor is slow anyway and the ADC a bit noisy
  int16_t t = (int16_t) (convert(board_room_adc()) + M.room_offset);
  M.t_room = M.t_room == T_NONE ? t : (int16_t) ((M.t_room * 7 + t + 4) / 8);
}
