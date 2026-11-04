#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HUBS 8
#define TRAINS 8
#define PORTS 16
#define SCHEMA 1
typedef struct {
    char label[49];
    char name[49];
    bool hidden;
    int limit, ramp_ms, brightness;
    bool brake;
} train_config_t;
typedef struct {
    char name[49], address[18];
    char motor_name[2][49], lamp_name[49];
    bool hidden;
    int address_type, train, motor[2], polarity[2], lamp, led_rgb;
    bool enabled;
} hub_config_t;
typedef struct {
    int schema;
    bool mqtt_enabled;
    char ssid[33], wifi_password[65], broker[128], username[65], mqtt_password[65];
    char client_id[65], prefix[128], panel_password[65], ap_password[65];
    int port, keepalive, reconnect_ms, stop_gpio;
    train_config_t trains[TRAINS];
    hub_config_t hubs[HUBS];
} config_t;
typedef struct {
    int power, direction, battery;
    bool light, active, driving, fresh;
} train_state_t;
typedef struct {
    bool attached, detected, stop_pending;
    uint8_t id;
    uint16_t device;
    int target, applied, sent, feedback;
    uint32_t last_step, reverse_until;
    bool sent_valid;
} port_state_t;
typedef struct {
    bool connected, ready, enabled, led_mode_ready;
    uint16_t handle, value_handle, svc_start, svc_end, cccd;
    int reason, rssi, battery, attempts, led_sent_rgb, led_reported_rgb, led_setup_attempts, led_probe_attempts;
    uint32_t led_setup_at, led_probe_at;
    char led_error[96];
    uint32_t retry_at, deadline, test_until;
    char firmware[24], error[96], last_tx[64];
    uint8_t model, write_properties;
    port_state_t ports[PORTS];
} hub_state_t;
typedef struct {
    config_t config;
    train_state_t trains[TRAINS];
    hub_state_t hubs[HUBS];
    bool wifi, mqtt, subscribed, syncing, sync_done, fallback_ap, maintenance;
    uint32_t sync_start, last_rx, errors, accepted, ignored;
    char last_message[160], last_error[128], ip[24];
    int last_train;
} bridge_t;
typedef struct {
    int train, value;
    char code;
} command_t;
void config_defaults(config_t *c);
bool config_validate(const config_t *c, char *error, size_t size);
bool utf8_valid(const uint8_t *s, size_t n);
bool parse_command(const config_t *c, const uint8_t *data, size_t len, command_t *out);
void core_init(bridge_t *b, const config_t *c);
void core_stop_train(bridge_t *b, int train);
void core_stop_all(bridge_t *b);
void core_sync(bridge_t *b, uint32_t now);
void core_receive(bridge_t *b, const uint8_t *data, size_t len, bool retained, uint32_t now);
void core_hub_lost(bridge_t *b, int slot, int reason);
port_state_t *core_port(hub_state_t *h, int id, bool create);
bool core_motor(uint16_t device);
void core_lwp(bridge_t *b, int slot, const uint8_t *data, size_t len);
bool core_next(bridge_t *b, int slot, uint32_t now, uint8_t frame[8], port_state_t **port);
void core_sent(port_state_t *p, const uint8_t frame[8], uint32_t now);

bool core_led_next(bridge_t *b, int slot, uint32_t now, uint8_t frame[10]);
void core_led_sent(bridge_t *b, int slot, uint32_t now, const uint8_t frame[10]);

int core_connect_candidate(const bridge_t *b, const bool seen[HUBS],
                           const uint32_t advertised_at[HUBS], uint32_t now, int start);
