// FreeRTOS, as much of it as met.h uses
#pragma once
#include <cstdint>
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
#define pdPASS 1
