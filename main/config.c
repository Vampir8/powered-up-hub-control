#include "bridge.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "provisioning.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
void config_load(config_t *c) {
    config_defaults(c);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(c->client_id, sizeof(c->client_id), "powered-up-hub-control-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    strcpy(c->panel_password, PROVISION_PANEL_PASSWORD);
    strcpy(c->ap_password, "12345678");
    nvs_handle_t h;
    if (nvs_open("bridge", NVS_READONLY, &h) == ESP_OK) {
        size_t n = 0;
        if (nvs_get_str(h, "json", NULL, &n) == ESP_OK && n <= 16384) {
            char *s = malloc(n);
            if (s && nvs_get_str(h, "json", s, &n) == ESP_OK) {
                cJSON *j = cJSON_Parse(s);
                config_t *temp=malloc(sizeof(*temp));
                char error[128];
                if(temp){
                    *temp=*c;
                    if(j&&config_from_json(j,temp,error,sizeof(error)))*c=*temp;
                    free(temp);
                }
                cJSON_Delete(j);
            }
            free(s);
        }
        nvs_close(h);
    } else
        config_save(c);
    if (nvs_open("bridge", NVS_READWRITE, &h) == ESP_OK) {
        uint8_t migrated = 0;
        if (nvs_get_u8(h, "simple_v1", &migrated) != ESP_OK || !migrated) {
            strcpy(c->ap_password, "12345678");
            if (config_save(c)) {
                nvs_set_u8(h, "simple_v1", 1);
                nvs_commit(h);
            }
        }
        uint8_t named=0;
        if(nvs_get_u8(h,"hub_names_v1",&named)!=ESP_OK||!named){
            snprintf(c->client_id,sizeof(c->client_id),"powered-up-hub-control-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            if(config_save(c)){nvs_set_u8(h,"hub_names_v1",1);nvs_commit(h);}
        }
        nvs_close(h);
    }
}
bool config_save(const config_t *c) {
    cJSON *j = config_json(c, true);
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!s)
        return false;
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open("bridge", NVS_READWRITE, &h) == ESP_OK) {
        ok = nvs_set_str(h, "json", s) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    free(s);
    return ok;
}
cJSON *config_json(const config_t *c, bool secrets) {
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
#define NUM(key) cJSON_AddNumberToObject(j, #key, c->key)
#define STR(key) cJSON_AddStringToObject(j, #key, c->key)
    NUM(schema);
    cJSON_AddBoolToObject(j, "mqtt_enabled", c->mqtt_enabled);
    STR(ssid);
    STR(broker);
    STR(username);
    STR(client_id);
    STR(prefix);
    NUM(port);
    NUM(keepalive);
    NUM(reconnect_ms);
    NUM(stop_gpio);
    if (secrets) {
        STR(wifi_password);
        STR(mqtt_password);
        STR(panel_password);
        STR(ap_password);
    }
#undef NUM
#undef STR
    cJSON *ts = cJSON_AddArrayToObject(j, "trains"), *hs = cJSON_AddArrayToObject(j, "hubs");
    for (int i = 0; i < TRAINS; i++) {
        const train_config_t *t = &c->trains[i];
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "label", t->label);
        cJSON_AddStringToObject(x, "name", t->name);
        cJSON_AddBoolToObject(x, "hidden", t->hidden);
        cJSON_AddNumberToObject(x, "limit", t->limit);
        cJSON_AddNumberToObject(x, "ramp_ms", t->ramp_ms);
        cJSON_AddNumberToObject(x, "brightness", t->brightness);
        cJSON_AddBoolToObject(x, "brake", t->brake);
        cJSON_AddItemToArray(ts, x);
    }
    for (int i = 0; i < HUBS; i++) {
        const hub_config_t *h = &c->hubs[i];
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "name", h->name);
        cJSON_AddBoolToObject(x, "hidden", h->hidden);
        cJSON_AddStringToObject(x, "lamp_name", h->lamp_name);
        cJSON *names=cJSON_AddArrayToObject(x,"motor_name");
        for(int k=0;k<2;k++)cJSON_AddItemToArray(names,cJSON_CreateString(h->motor_name[k]));
        cJSON_AddStringToObject(x, "address", h->address);
        cJSON_AddNumberToObject(x, "address_type", h->address_type);
        cJSON_AddNumberToObject(x, "train", h->train);
        cJSON_AddNumberToObject(x, "lamp", h->lamp);
        cJSON_AddNumberToObject(x, "led_rgb", h->led_rgb);
        cJSON_AddBoolToObject(x, "enabled", h->enabled);
        cJSON *m = cJSON_AddArrayToObject(x, "motor"), *p = cJSON_AddArrayToObject(x, "polarity");
        for (int k = 0; k < 2; k++) {
            cJSON_AddItemToArray(m, cJSON_CreateNumber(h->motor[k]));
            cJSON_AddItemToArray(p, cJSON_CreateNumber(h->polarity[k]));
        }
        cJSON_AddItemToArray(hs, x);
    }
    return j;
}
static bool getstr(const cJSON *j, const char *key, char *dst, size_t n) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!v)
        return true;
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= n)
        return false;
    strcpy(dst, v->valuestring);
    return true;
}
static bool getnum(const cJSON *j, const char *key, int *dst) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!v)
        return true;
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble != v->valueint)
        return false;
    *dst = v->valueint;
    return true;
}
static bool getbool(const cJSON *j, const char *key, bool *dst) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!v)
        return true;
    if (!cJSON_IsBool(v))
        return false;
    *dst = cJSON_IsTrue(v);
    return true;
}
bool config_from_json(const cJSON *j, config_t *c, char *error, size_t n) {
    if (!cJSON_IsObject(j))
        goto invalid;
#define S(key)                                                                                               \
    if (!getstr(j, #key, c->key, sizeof(c->key)))                                                            \
    goto invalid
#define N(key)                                                                                               \
    if (!getnum(j, #key, &c->key))                                                                           \
    goto invalid
    if (!getbool(j, "mqtt_enabled", &c->mqtt_enabled))
        goto invalid;
    S(ssid);
    S(wifi_password);
    S(broker);
    S(username);
    S(mqtt_password);
    S(client_id);
    S(prefix);
    S(panel_password);
    S(ap_password);
    N(schema);
    N(port);
    N(keepalive);
    N(reconnect_ms);
    N(stop_gpio);
#undef S
#undef N
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(j, "trains"),
                *hs = cJSON_GetObjectItemCaseSensitive(j, "hubs");
    if (ts) {
        if (!cJSON_IsArray(ts) || cJSON_GetArraySize(ts) != TRAINS)
            goto invalid;
        for (int i = 0; i < TRAINS; i++) {
            const cJSON *x = cJSON_GetArrayItem(ts, i);
            train_config_t *t = &c->trains[i];
            if (!cJSON_IsObject(x) || !getstr(x, "label", t->label, sizeof(t->label)) ||
                !getstr(x,"name",t->name,sizeof(t->name)) || !getbool(x,"hidden",&t->hidden) ||
                !getnum(x, "limit", &t->limit) || !getnum(x, "ramp_ms", &t->ramp_ms) ||
                !getnum(x, "brightness", &t->brightness) || !getbool(x, "brake", &t->brake))
                goto invalid;
        }
    }
    if (hs) {
        if (!cJSON_IsArray(hs) || cJSON_GetArraySize(hs) != HUBS)
            goto invalid;
        for (int i = 0; i < HUBS; i++) {
            const cJSON *x = cJSON_GetArrayItem(hs, i);
            hub_config_t *h = &c->hubs[i];
            if (!cJSON_IsObject(x) || !getstr(x, "name", h->name, sizeof(h->name)) ||
                !getstr(x, "address", h->address, sizeof(h->address)) ||
                !getbool(x,"hidden",&h->hidden) || !getstr(x,"lamp_name",h->lamp_name,sizeof(h->lamp_name)) ||
                !getnum(x, "address_type", &h->address_type) || !getnum(x, "train", &h->train) ||
                !getnum(x, "lamp", &h->lamp) || !getnum(x, "led_rgb", &h->led_rgb) || !getbool(x, "enabled", &h->enabled))
                goto invalid;
            const cJSON *names=cJSON_GetObjectItemCaseSensitive(x,"motor_name");
            if(names){
                if(!cJSON_IsArray(names)||cJSON_GetArraySize(names)!=2)goto invalid;
                for(int k=0;k<2;k++){const cJSON *v=cJSON_GetArrayItem(names,k);if(!cJSON_IsString(v)||strlen(v->valuestring)>=sizeof(h->motor_name[k])||!utf8_valid((const uint8_t*)v->valuestring,strlen(v->valuestring)))goto invalid;strcpy(h->motor_name[k],v->valuestring);}
            }
            const char *keys[] = {"motor", "polarity"};
            int *out[] = {h->motor, h->polarity};
            for (int k = 0; k < 2; k++) {
                const cJSON *a = cJSON_GetObjectItemCaseSensitive(x, keys[k]);
                if (a) {
                    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) != 2)
                        goto invalid;
                    for (int m = 0; m < 2; m++) {
                        const cJSON *v = cJSON_GetArrayItem(a, m);
                        if (!cJSON_IsNumber(v) || v->valuedouble != v->valueint)
                            goto invalid;
                        out[k][m] = v->valueint;
                    }
                }
            }
        }
    }
    if (!config_validate(c, error, n))
        return false;
    if (c->stop_gpio >= 0 && !GPIO_IS_VALID_GPIO(c->stop_gpio)) {
        snprintf(error, n, "Ten numer GPIO nie istnieje na ESP32-S3");
        return false;
    }
    return true;
invalid:
    snprintf(error, n, "Invalid JSON format or field too long");
    return false;
}
