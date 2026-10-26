// ESP-IDF's HTTP client, as much of it as met.h uses. harness.cpp answers from panel_http, the fake internet.
#pragma once
#include <cstdint>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef struct HttpClient *esp_http_client_handle_t;
typedef enum { HTTP_EVENT_ON_HEADER } esp_http_client_event_id_t;
typedef struct {
  esp_http_client_event_id_t event_id;
  void *user_data;
  char *header_key, *header_value;
} esp_http_client_event_t;
typedef struct {
  const char *url, *user_agent;
  int timeout_ms;
  esp_err_t (*crt_bundle_attach)(void *conf);
  esp_err_t (*event_handler)(esp_http_client_event_t *e);
  void *user_data;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t c);
int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
