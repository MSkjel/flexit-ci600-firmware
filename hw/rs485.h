// RS-485 on USART1, for the Modbus server and the ESP link.
#pragma once
#include <stdbool.h>

void rs485_init(void);
bool rs485_busy(void);   // a reply is still being sent
bool rs485_quiet(void);  // not sending, and the CU60 won't ask us anything for a few ms
