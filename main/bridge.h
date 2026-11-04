#pragma once
#include "cJSON.h"
#include "core.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
extern bridge_t bridge;
extern SemaphoreHandle_t bridge_mutex;
static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
#define LOCK() xSemaphoreTake(bridge_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(bridge_mutex)
void config_load(config_t *c);
bool config_save(const config_t *c);
cJSON *config_json(const config_t *c, bool secrets);
bool config_from_json(const cJSON *j, config_t *c, char *error, size_t n);
void ble_start(void);
void ble_tick(uint32_t now);
void ble_scan_request(void);
cJSON *ble_seen_json(uint32_t now);
void network_start(void);
void network_tick(uint32_t now);
void web_start(void);
void web_tick(uint32_t now);

const char *network_hostname(void);
