// FreeRTOS's tasks. harness.cpp keeps a new one until the test runs it, to its end.
#pragma once
#include "freertos/FreeRTOS.h"
BaseType_t xTaskCreate(TaskFunction_t f, const char *name, uint32_t stack, void *arg, UBaseType_t priority,
                       TaskHandle_t *task);
void vTaskDelete(TaskHandle_t task);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
