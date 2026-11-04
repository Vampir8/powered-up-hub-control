#include "bridge.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "mdns.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static esp_mqtt_client_handle_t mqtt;
static int subscription = -1;
static uint32_t disconnected_at, next_wifi, telemetry_at;
static bool ap_on, start_mqtt;
static char status_topic[160], uri[180];
static char node_id[40], node_info_topic[80], node_status_topic[80];
static char node_name[64],hostname[64];
static bool node_announce;
static uint32_t node_retry_at;
const char *network_hostname(void){return hostname;}
static int announce_node(const char *client_id,const char *ip) {
    cJSON *info=cJSON_CreateObject();if(!info)return -1;
    cJSON_AddStringToObject(info,"client_id",client_id);
    cJSON_AddStringToObject(info,"name",node_name);
    cJSON_AddStringToObject(info,"type","esp32s3");
    cJSON_AddStringToObject(info,"model","ESP32-S3 / LEGO Powered Up");
    cJSON_AddStringToObject(info,"firmware","powered-up-hub-1.1.1");
    cJSON_AddStringToObject(info,"ip",ip);
    char url[48];snprintf(url,sizeof(url),"http://%s/",ip);cJSON_AddStringToObject(info,"web_url",url);
    char *payload=cJSON_PrintUnformatted(info);cJSON_Delete(info);if(!payload)return -1;
    int a=esp_mqtt_client_enqueue(mqtt,node_info_topic,payload,0,1,1,true);free(payload);
    int b=esp_mqtt_client_enqueue(mqtt,node_status_topic,"{\"online\":true}",0,1,1,true);
    return a<0||b<0?-1:0;
}
static char rx[129];
static int rx_used, rx_total;
static bool rx_valid, rx_retained;
static void publication_task(void *arg);
static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_mqtt_event_handle_t e = data;
    bool subscribe_now = false;
    LOCK();
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        bridge.mqtt = true;
        node_announce=true;node_retry_at=now_ms();
        bridge.subscribed = false;
        core_sync(&bridge, now_ms());
        subscribe_now = true;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (e->msg_id == subscription) {
            bridge.subscribed = e->data_len == 1 && e->data && (uint8_t)e->data[0] < 0x80;
            if (bridge.subscribed)
                core_sync(&bridge, now_ms());
            else {
                core_stop_all(&bridge);
                bridge.errors++;
                strcpy(bridge.last_error, "Broker rejected the subscription");
            }
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
    case MQTT_EVENT_ERROR:
        bridge.mqtt = bridge.subscribed = bridge.sync_done = false;
        bridge.syncing = false;
        core_stop_all(&bridge);
        rx_valid = false;
        break;
    case MQTT_EVENT_DATA:
        if (e->current_data_offset == 0) {
            rx_used = 0;
            rx_total = e->total_data_len;
            rx_retained = e->retain;
            rx_valid = e->topic_len == 21 && !memcmp(e->topic, "nControl/websocket/rx", 21) && rx_total > 0 &&
                       rx_total <= 128;
        }
        if (rx_valid) {
            if (e->current_data_offset != rx_used || e->data_len < 0 || rx_used + e->data_len > rx_total) {
                rx_valid = false;
                bridge.errors++;
            } else {
                memcpy(rx + rx_used, e->data, e->data_len);
                rx_used += e->data_len;
                if (rx_used == rx_total) {
                    core_receive(&bridge, (uint8_t *)rx, rx_used, rx_retained, now_ms());
                    rx_valid = false;
                }
            }
        }
        break;
    default:
        break;
    }
    UNLOCK();
    // Never acquire the MQTT API lock while holding bridge_mutex: MQTT itself
    // dispatches callbacks with its recursive API lock held.
    if (subscribe_now) {
        int mid = esp_mqtt_client_subscribe(mqtt, "nControl/websocket/rx", 0);
        LOCK();
        subscription = mid;
        if (mid < 0) {
            bridge.errors++;
            strcpy(bridge.last_error, "MQTT subscribe failed");
        }
        UNLOCK();
    }
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    LOCK();
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        bridge.wifi = true;
        bridge.fallback_ap = false;
        snprintf(bridge.ip, sizeof(bridge.ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI("wifi", "Panel http://%s", bridge.ip);
        disconnected_at = 0;
        start_mqtt = true;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        bridge.wifi = false;
        bridge.mqtt = bridge.subscribed = bridge.sync_done = false;
        core_stop_all(&bridge);
        if (!disconnected_at)
            disconnected_at = now_ms();
        next_wifi = now_ms() + 3000;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    UNLOCK();
}
static void fallback(void) {
    wifi_config_t ap = {0};
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf((char *)ap.ap.ssid, 32, "PU-Hub-%02x%02x%02x-setup", mac[3], mac[4], mac[5]);
    strcpy((char *)ap.ap.password, bridge.config.ap_password);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(*bridge.config.ssid ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ap_on = true;
    bridge.fallback_ap = true;
    core_stop_all(&bridge);
    ESP_LOGW("setup", "AP %s: http://192.168.4.1 (hasla: lokalny provisioning.json)", ap.ap.ssid);
}
void network_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif=esp_netif_create_default_wifi_sta();
    uint8_t identity_mac[6];esp_read_mac(identity_mac,ESP_MAC_WIFI_STA);
    snprintf(hostname,sizeof(hostname),"powered-up-hub-control-%02x%02x%02x",identity_mac[3],identity_mac[4],identity_mac[5]);
    snprintf(node_name,sizeof(node_name),"Powered Up Hub Control - %02x%02x%02x",identity_mac[3],identity_mac[4],identity_mac[5]);
    esp_err_t hostname_error=esp_netif_set_hostname(sta_netif,hostname);
    if(hostname_error!=ESP_OK)ESP_LOGW("hostname","Unable to set DHCP hostname: %s",esp_err_to_name(hostname_error));
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
    // Credentials live in local provisioning.json; never log passwords.
    if (*bridge.config.ssid) {
        wifi_config_t sta = {0};
        strcpy((char *)sta.sta.ssid, bridge.config.ssid);
        strcpy((char *)sta.sta.password, bridge.config.wifi_password);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        disconnected_at = now_ms();
    } else
        fallback();
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_err_t mdns_error=mdns_init();
    if(mdns_error==ESP_OK){
        mdns_error=mdns_hostname_set(hostname);
        if(mdns_error==ESP_OK)mdns_error=mdns_instance_name_set(node_name);
        if(mdns_error==ESP_OK)mdns_error=mdns_service_add(NULL,"_http","_tcp",80,NULL,0);
    }
    if(mdns_error!=ESP_OK)ESP_LOGW("mdns","Unable to start mDNS: %s",esp_err_to_name(mdns_error));
    else ESP_LOGI("mdns","Panel http://%s.local",hostname);
    if (bridge.config.mqtt_enabled && *bridge.config.broker) {
        snprintf(uri, sizeof(uri), "mqtt://%s:%d", bridge.config.broker, bridge.config.port);
        snprintf(status_topic, sizeof(status_topic), "%s/status", bridge.config.prefix);
        uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);
        snprintf(node_id,sizeof(node_id),"lego-power-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        snprintf(node_info_topic,sizeof(node_info_topic),"nControl/nodes/%s/info",node_id);
        snprintf(node_status_topic,sizeof(node_status_topic),"nControl/nodes/%s/status",node_id);
        esp_mqtt_client_config_t c = {.broker.address.uri = uri,
                                      .credentials.client_id = bridge.config.client_id,
                                      .credentials.username =
                                          *bridge.config.username ? bridge.config.username : NULL,
                                      .credentials.authentication.password =
                                          *bridge.config.mqtt_password ? bridge.config.mqtt_password : NULL,
                                      .session.keepalive = bridge.config.keepalive,
                                      .session.disable_clean_session = false,
                                      .session.last_will.topic = node_status_topic,
                                      .session.last_will.msg = "{\"online\":false}",
                                      .session.last_will.qos = 1,
                                      .session.last_will.retain = true,
                                      .network.reconnect_timeout_ms = bridge.config.reconnect_ms,
                                      .buffer.size = 1024,
                                      .outbox.limit = 8192};
        mqtt = esp_mqtt_client_init(&c);
        if (mqtt)
            esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    }
    if (mqtt)
        configASSERT(xTaskCreate(publication_task, "mqtt_out", 6144, NULL, 4, NULL) == pdPASS);
}
void network_tick(uint32_t now) {
    if (!bridge.wifi && *bridge.config.ssid && (int32_t)(now - next_wifi) >= 0) {
        next_wifi = now + 5000;
        esp_wifi_connect();
    }
    if (!bridge.wifi && !ap_on && disconnected_at && now - disconnected_at >= 30000)
        fallback();
    if (bridge.wifi && ap_on) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ap_on = false;
        bridge.fallback_ap = false;
        core_stop_all(&bridge);
    }
}
static void publication_task(void *arg) {
    for (;;) {
        char payload[HUBS + 1][192], prefix[128], client_id[65], ip[24];
        uint32_t now = now_ms();
        LOCK();
        bool start = start_mqtt;
        start_mqtt = false;
        bool publish = bridge.mqtt && now - telemetry_at >= 5000;
        bool announce=bridge.mqtt&&bridge.wifi&&node_announce&&(int32_t)(now-node_retry_at)>=0;
        if(announce){node_announce=false;node_retry_at=now+5000;strcpy(client_id,bridge.config.client_id);strcpy(ip,bridge.ip);}
        strcpy(prefix, bridge.config.prefix);
        if (publish) {
            telemetry_at = now;
            int count = 0;
            for (int i = 0; i < HUBS; i++)
                count += bridge.hubs[i].connected;
            snprintf(payload[0], sizeof(payload[0]), "{\"online\":true,\"connected\":%d,\"sync_done\":%s}",
                     count, bridge.sync_done ? "true" : "false");
            for (int i = 0; i < HUBS; i++) {
                hub_state_t *h = &bridge.hubs[i];
                snprintf(payload[i + 1], sizeof(payload[i + 1]),
                         "{\"connected\":%s,\"ready\":%s,\"battery_percent\":%d,\"rssi\":%d,\"reason\":%d}",
                         h->connected ? "true" : "false", h->ready ? "true" : "false", h->battery, h->rssi,
                         h->reason);
            }
        }
        UNLOCK();
        if (start) {
            esp_err_t rc = esp_mqtt_client_start(mqtt);
            if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
                LOCK();
                bridge.errors++;
                strcpy(bridge.last_error, "MQTT start failed");
                UNLOCK();
            }
        }
        if(announce&&announce_node(client_id,ip)<0){LOCK();node_announce=true;UNLOCK();}
        if (publish) {
            esp_mqtt_client_enqueue(mqtt, status_topic, payload[0], 0, 0, 1, true);
            for (int i = 0; i < HUBS; i++) {
                char topic[180];
                snprintf(topic, sizeof(topic), "%s/hubs/%d/state", prefix, i + 1);
                esp_mqtt_client_enqueue(mqtt, topic, payload[i + 1], 0, 0, 0, true);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
