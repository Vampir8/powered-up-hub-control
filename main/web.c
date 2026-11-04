#include "bridge.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const uint8_t index_start[];
static uint32_t restart_at;
static bool reset_requested;
void web_tick(uint32_t now) {
    if (restart_at && (int32_t)(now - restart_at) >= 0) {
        if (reset_requested) {
            nvs_handle_t h;
            if (nvs_open("bridge", NVS_READWRITE, &h) == ESP_OK) {
                nvs_erase_all(h);
                nvs_commit(h);
                nvs_close(h);
            }
        }
        esp_restart();
    }
}
static bool mutation(httpd_req_t *r) {
    char h[32];
    if (httpd_req_get_hdr_value_str(r, "X-Bridge", h, sizeof(h)) != ESP_OK || strcmp(h, "1")) {
        httpd_resp_send_err(r, HTTPD_403_FORBIDDEN, "X-Bridge header required");
        return false;
    }
    return true;
}
static esp_err_t json_response(httpd_req_t *r, cJSON *j) {
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!s)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(r, s);
    free(s);
    return err;
}
static cJSON *body(httpd_req_t *r) {
    if (r->content_len < 2 || r->content_len > 16384)
        return NULL;
    char *s = malloc(r->content_len + 1);
    if (!s)
        return NULL;
    int at = 0;
    while (at < r->content_len) {
        int n = httpd_req_recv(r, s + at, r->content_len - at);
        if (n <= 0) {
            free(s);
            return NULL;
        }
        at += n;
    }
    s[at] = 0;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(s, at + 1, &end, true);
    free(s);
    return j;
}
static esp_err_t index_handler(httpd_req_t *r) {
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Content-Security-Policy",
                       "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' "
                       "'unsafe-inline'; object-src 'none'; frame-ancestors 'none'");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(r, (const char *)index_start);
}
static esp_err_t state_handler(httpd_req_t *r) {
    LOCK();
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "wifi", bridge.wifi);
    cJSON_AddBoolToObject(j, "mqtt", bridge.mqtt);
    cJSON_AddBoolToObject(j, "subscribed", bridge.subscribed);
    cJSON_AddBoolToObject(j, "syncing", bridge.syncing);
    cJSON_AddBoolToObject(j, "sync_done", bridge.sync_done);
    cJSON_AddBoolToObject(j, "fallback_ap", bridge.fallback_ap);
    cJSON_AddBoolToObject(j, "maintenance", bridge.maintenance);
    cJSON_AddStringToObject(j, "ip", bridge.ip);
    cJSON_AddNumberToObject(j, "uptime_ms", now_ms());
    cJSON_AddStringToObject(j, "hostname", network_hostname());
    cJSON_AddNumberToObject(j, "free_internal",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(j, "min_internal",
                            heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(j, "largest_internal",
                            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(j, "host_limit", CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
    cJSON_AddNumberToObject(j, "controller_activities", CONFIG_BT_CTRL_BLE_MAX_ACT);
    cJSON_AddStringToObject(j, "idf", esp_get_idf_version());
    cJSON_AddNumberToObject(j, "errors", bridge.errors);
    cJSON_AddNumberToObject(j, "accepted", bridge.accepted);
    cJSON_AddNumberToObject(j, "ignored", bridge.ignored);
    cJSON_AddNumberToObject(j, "last_train", bridge.last_train);
    cJSON_AddStringToObject(j, "last_message", bridge.last_message);
    cJSON_AddStringToObject(j, "last_error", bridge.last_error);
    cJSON *ts = cJSON_AddArrayToObject(j, "trains");
    for (int t = 0; t < TRAINS; t++) {
        train_state_t *s = &bridge.trains[t];
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "label", bridge.config.trains[t].label);
        cJSON_AddBoolToObject(x, "driving", s->driving);
        cJSON_AddBoolToObject(x, "fresh", s->fresh);
        cJSON_AddNumberToObject(x, "ui_power", s->power);
        cJSON_AddNumberToObject(x, "direction", s->direction);
        cJSON_AddBoolToObject(x, "light", s->light);
        cJSON_AddBoolToObject(x, "active", s->active);
        cJSON_AddNumberToObject(x, "ui_battery_index", s->battery);
        cJSON_AddItemToArray(ts, x);
    }
    int count = 0, queued = 0;
    cJSON *hs = cJSON_AddArrayToObject(j, "hubs");
    for (int i = 0; i < HUBS; i++) {
        hub_state_t *h = &bridge.hubs[i];
        count += h->connected;
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "slot", i + 1);
        cJSON_AddBoolToObject(x, "connected", h->connected);
        cJSON_AddBoolToObject(x, "ready", h->ready);
        cJSON_AddBoolToObject(x, "enabled", h->enabled);
        cJSON_AddNumberToObject(x, "model", h->model);
        cJSON_AddStringToObject(x, "firmware", h->firmware);
        cJSON_AddNumberToObject(x, "battery_percent", h->battery);
        cJSON_AddNumberToObject(x, "rssi", h->rssi);
        cJSON_AddNumberToObject(x, "reason", h->reason);
        cJSON_AddNumberToObject(x, "led_sent_rgb", h->led_sent_rgb);
        cJSON_AddNumberToObject(x, "led_reported_rgb", h->led_reported_rgb);
        cJSON_AddBoolToObject(x, "led_mode_ready", h->led_mode_ready);
        cJSON_AddStringToObject(x, "led_error", h->led_error);
        cJSON_AddStringToObject(x, "error", h->error);
        cJSON_AddStringToObject(x, "last_tx", h->last_tx);
        cJSON *ps = cJSON_AddArrayToObject(x, "ports");
        for (int p = 0; p < PORTS; p++) {
            port_state_t *v = &h->ports[p];
            if (!v->attached)
                continue;
            hub_config_t *hc = &bridge.config.hubs[i];
            bool output = (v->id == hc->motor[0] || v->id == hc->motor[1] || v->id == hc->lamp);
            queued += v->stop_pending || (output && (!v->sent_valid || v->target != v->applied));
            cJSON *y = cJSON_CreateObject();
            cJSON_AddNumberToObject(y, "id", v->id);
            cJSON_AddNumberToObject(y, "device", v->device);
            cJSON_AddBoolToObject(y, "detected", v->detected);
            cJSON_AddNumberToObject(y, "desired", v->target);
            cJSON_AddNumberToObject(y, "sent", v->sent);
            cJSON_AddBoolToObject(y, "sent_valid", v->sent_valid);
            cJSON_AddNumberToObject(y, "feedback", v->feedback);
            cJSON_AddBoolToObject(y, "stop_pending", v->stop_pending);
            cJSON_AddItemToArray(ps, y);
        }
        cJSON_AddItemToArray(hs, x);
    }
    cJSON_AddNumberToObject(j, "connected", count);
    cJSON_AddNumberToObject(j, "queue", queued);
    cJSON_AddItemToObject(j, "seen", ble_seen_json(now_ms()));
    UNLOCK();
    return json_response(r, j);
}
static esp_err_t config_get(httpd_req_t *r) {
    char q[32], v[8];
    bool secrets = httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
                   httpd_query_key_value(q, "secrets", v, sizeof(v)) == ESP_OK && !strcmp(v, "1");
    LOCK();
    cJSON *j = config_json(&bridge.config, secrets);
    UNLOCK();
    return json_response(r, j);
}
static esp_err_t config_post(httpd_req_t *r) {
    if (!mutation(r))
        return ESP_OK;
    cJSON *j = body(r);
    config_t *c = malloc(sizeof(config_t));
    if (!c) {
        cJSON_Delete(j);
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    LOCK();
    *c = bridge.config;
    UNLOCK();
    char error[128];
    if (!j || !config_from_json(j, c, error, sizeof(error))) {
        cJSON_Delete(j);
        free(c);
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, j ? error : "Invalid JSON");
    }
    cJSON_Delete(j);
    LOCK();
    core_stop_all(&bridge);
    bridge.maintenance = true;
    UNLOCK();
    bool ok = config_save(c);
    free(c);
    if (!ok)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
    LOCK();
    restart_at = now_ms() + 2500;
    UNLOCK();
    return httpd_resp_sendstr(r, "Saved; STOP and restart in 2.5 s");
}
static esp_err_t action_handler(httpd_req_t *r) {
    if (!mutation(r))
        return ESP_OK;
    cJSON *j = body(r);
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(j, "action");
    if (!cJSON_IsString(a)) {
        cJSON_Delete(j);
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Missing action");
    }
    bool ok = true;
    LOCK();
    const char *action = a->valuestring;
    if (!strcmp(action, "stop"))
        core_stop_all(&bridge);
    else if (!strcmp(action, "stop_train")) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(j, "train");
        ok = cJSON_IsNumber(t) && t->valuedouble == t->valueint &&
             t->valueint >= 1 && t->valueint <= TRAINS;
        if (ok)
            core_stop_train(&bridge, t->valueint - 1);
    }
    else if (!strcmp(action, "scan"))
        ble_scan_request();
    else if (!strcmp(action, "led")) {
        const cJSON *sl = cJSON_GetObjectItemCaseSensitive(j, "slot"),
                    *color = cJSON_GetObjectItemCaseSensitive(j, "rgb");
        int i = cJSON_IsNumber(sl) ? sl->valueint - 1 : -1;
        ok = i >= 0 && i < HUBS && cJSON_IsNumber(sl) && sl->valuedouble == sl->valueint &&
             cJSON_IsNumber(color) && color->valuedouble == color->valueint &&
             color->valueint >= 0 && color->valueint <= 0xffffff && !bridge.maintenance;
        if (ok) {
            int previous = bridge.config.hubs[i].led_rgb;
            bridge.config.hubs[i].led_rgb = color->valueint;
            ok = config_save(&bridge.config);
            if (!ok)
                bridge.config.hubs[i].led_rgb = previous;
            else {
                bridge.hubs[i].led_setup_attempts = 0;
                bridge.hubs[i].led_probe_attempts = 0;
                bridge.hubs[i].led_error[0] = 0;
                bridge.hubs[i].led_sent_rgb = -2;
            }
        }
    }
    else if (!strcmp(action, "restart") || !strcmp(action, "reset")) {
        core_stop_all(&bridge);
        bridge.maintenance = true;
        reset_requested = !strcmp(action, "reset");
        restart_at = now_ms() + 2500;
    } else if (!strcmp(action, "connect") || !strcmp(action, "disconnect") || !strcmp(action, "test")) {
        const cJSON *sl = cJSON_GetObjectItemCaseSensitive(j, "slot");
        int i = cJSON_IsNumber(sl) ? sl->valueint - 1 : -1;
        if (i < 0 || i >= HUBS)
            ok = false;
        else {
            hub_state_t *h = &bridge.hubs[i];
            int t = bridge.config.hubs[i].train;
            if (!strcmp(action, "connect")) {
                h->enabled = true;
                h->retry_at = 0;
            } else if (!strcmp(action, "disconnect")) {
                core_stop_train(&bridge, t);
                h->enabled = false;
            } else {
                const cJSON *pn = cJSON_GetObjectItemCaseSensitive(j, "port"),
                            *sg = cJSON_GetObjectItemCaseSensitive(j, "power"),
                            *confirm = cJSON_GetObjectItemCaseSensitive(j, "confirm_test");
                int port = cJSON_IsNumber(pn) ? pn->valueint : -1,
                    power = cJSON_IsNumber(sg) ? sg->valueint : 0;
                port_state_t *p = core_port(h, port, h->ready);
                hub_config_t *hc = &bridge.config.hubs[i];
                ok = !bridge.maintenance && !bridge.fallback_ap && bridge.wifi && h->ready &&
                     t >= 0 && cJSON_IsTrue(confirm) && p &&
                     (port == hc->motor[0] || port == hc->motor[1]) && (power == 10 || power == -10);
                if (ok) {
                    core_stop_train(&bridge, t);
                    p->target = power;
                    h->test_until = now_ms() + 600;
                }
            }
        }
    } else
        ok = false;
    UNLOCK();
    cJSON_Delete(j);
    if (!ok)
        return httpd_resp_send_err(
            r, HTTPD_400_BAD_REQUEST,
            "Action unavailable; check MQTT, synchronization, hubs and ports");
    return httpd_resp_sendstr(r, "OK");
}
static esp_err_t ota_handler(httpd_req_t *r) {
    if (!mutation(r))
        return ESP_OK;
    LOCK();
    bool stopped = true;
    for (int i = 0; i < HUBS; i++)
        for (int p = 0; p < PORTS; p++) {
            port_state_t *v = &bridge.hubs[i].ports[p];
            if (v->target || v->applied || v->stop_pending)
                stopped = false;
        }
    core_stop_all(&bridge);
    if (stopped)
        bridge.maintenance = true;
    UNLOCK();
    if (!stopped)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Use STOP ALL first; wait for STOP delivery");
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || r->content_len <= 0 || r->content_len > part->size)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Rozmiar obrazu OTA");
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(part, r->content_len, &handle);
    if (err != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin");
    char buf[1024];
    int remain = r->content_len;
    while (remain > 0) {
        int n = httpd_req_recv(r, buf, remain > sizeof(buf) ? sizeof(buf) : remain);
        if (n <= 0 || esp_ota_write(handle, buf, n) != ESP_OK) {
            esp_ota_abort(handle);
            return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write/receive");
        }
        remain -= n;
    }
    if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK)
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid ESP32-S3 firmware image");
    LOCK();
    restart_at = now_ms() + 2500;
    UNLOCK();
    return httpd_resp_sendstr(r, "OTA accepted; restarting");
}
void web_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 12288;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    httpd_uri_t routes[] = {{.uri = "/", .method = HTTP_GET, .handler = index_handler},
                            {.uri = "/api/state", .method = HTTP_GET, .handler = state_handler},
                            {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
                            {.uri = "/api/config", .method = HTTP_POST, .handler = config_post},
                            {.uri = "/api/action", .method = HTTP_POST, .handler = action_handler},
                            {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_handler}};
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
}
