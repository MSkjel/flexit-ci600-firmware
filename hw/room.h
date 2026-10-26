// Room temperature from the panel's own thermistor on PC0.
#pragma once

void room_second(void);          // 1 Hz, updates M.t_room
void room_offset_changed(void);  // M.room_offset was set
