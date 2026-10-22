// RS-485 on USART1, for the Modbus server and the ESP link.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void rs485_init(void);
bool rs485_busy(void);  // a reply is still being sent
// Waits up to ms for a moment when a page erase can stall the chip without losing anything
bool rs485_wait_quiet(uint32_t ms);
