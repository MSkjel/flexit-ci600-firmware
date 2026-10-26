// ESP-IDF's root certificates for met.h, which harness.cpp's fake internet doesn't check
#pragma once
#include "esp_http_client.h"  // esp_err_t
esp_err_t esp_crt_bundle_attach(void *conf);
