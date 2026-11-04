#include "bridge.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <stdlib.h>
#if CONFIG_BT_NIMBLE_MAX_CONNECTIONS < 8
#error NimBLE requires eight connections
#endif
#if CONFIG_BT_CTRL_BLE_MAX_ACT < 10
#error Controller requires eight links plus scan/connect resources
#endif
bridge_t bridge;
SemaphoreHandle_t bridge_mutex;
static void worker(void *unused) {
    uint32_t log_at = 0;
    int pin = bridge.config.stop_gpio;
    bool valid = pin >= 0 && GPIO_IS_VALID_GPIO(pin);
    if (valid) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
        ESP_ERROR_CHECK(gpio_config(&io));
    }
    for (;;) {
        uint32_t now = now_ms();
        LOCK();
        if (valid && !gpio_get_level(pin))
            core_stop_all(&bridge);
        ble_tick(now);
        network_tick(now);
        web_tick(now);
        if (now - log_at >= 10000) {
            log_at = now;
            int count = 0;
            for (int i = 0; i < HUBS; i++)
                count += bridge.hubs[i].connected;
            ESP_LOGI("resources", "connected=%d/8 internal_free=%u internal_min=%u largest=%u", count,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        UNLOCK();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    bridge_mutex = xSemaphoreCreateMutex();
    // The configuration grows with device aliases; keep it off the main task stack.
    config_t *initial=malloc(sizeof(*initial));configASSERT(initial);
    config_load(initial);
    core_init(&bridge,initial);
    free(initial);
    ESP_LOGI("boot", "IDF %s host connections=%d controller activities=%d", esp_get_idf_version(),
             CONFIG_BT_NIMBLE_MAX_CONNECTIONS, CONFIG_BT_CTRL_BLE_MAX_ACT);
    ble_start();
    network_start();
    web_start();
    ESP_LOGI("boot","startup stack high-water=%u",(unsigned)uxTaskGetStackHighWaterMark(NULL));
    xTaskCreate(worker, "bridge_worker", 8192, NULL, 5, NULL);
}
