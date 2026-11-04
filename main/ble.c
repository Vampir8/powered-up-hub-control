#include "bridge.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include <stdio.h>
#include <string.h>
_Static_assert(MYNEWT_VAL(BLE_MAX_CONNECTIONS) >= 8,
               "Actual NimBLE connection pool must support eight links");
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x16,
                                                           0xde, 0xef, 0x12, 0x12, 0x23, 0x16, 0, 0);
static const ble_uuid128_t char_uuid = BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x16, 0xde,
                                                        0xef, 0x12, 0x12, 0x24, 0x16, 0, 0);
typedef struct {
    bool used;
    ble_addr_t addr;
    char name[49];
    int rssi;
    uint8_t model;
    uint32_t last;
} seen_t;
static seen_t seen[32];
static uint8_t own_type;
static bool synced, manual_scan;
static int connecting = -1;
static uint32_t next_scan, last_write, last_rssi;
static int round_robin, property_round_robin, connect_cursor;
static uint32_t property_due[HUBS];
static uint8_t property_step[HUBS];
static int gap_event(struct ble_gap_event *event, void *arg);
static void address_text(const ble_addr_t *a, char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a->val[5], a->val[4], a->val[3], a->val[2], a->val[1],
             a->val[0]);
}
static bool address_parse(const hub_config_t *h, ble_addr_t *a) {
    unsigned v[6];
    if (sscanf(h->address, "%x:%x:%x:%x:%x:%x", &v[5], &v[4], &v[3], &v[2], &v[1], &v[0]) != 6)
        return false;
    a->type = h->address_type;
    for (int i = 0; i < 6; i++)
        a->val[i] = v[i];
    return true;
}
static void fail(int slot, int rc) {
    hub_state_t *h = &bridge.hubs[slot];
    snprintf(h->error, sizeof(h->error), "BLE rc=%d", rc);
    bridge.errors++;
    h->reason = rc;
    core_stop_train(&bridge, bridge.config.hubs[slot].train);
    if (h->connected)
        ble_gap_terminate(h->handle, BLE_ERR_REM_USER_CONN_TERM);
    else {
        h->deadline = 0;
        h->retry_at = now_ms() + 1000;
        if (connecting == slot)
            connecting = -1;
    }
}
static int subscribe_cb(uint16_t conn, const struct ble_gatt_error *e, struct ble_gatt_attr *attr,
                        void *arg) {
    int s = (int)(intptr_t)arg;
    LOCK();
    hub_state_t *h = &bridge.hubs[s];
    if (e->status)
        fail(s, e->status);
    else {
        h->ready = true;
        h->deadline = 0;
        h->attempts = 0;
        h->reason = 0;
        h->error[0] = 0;
        if (connecting == s)
            connecting = -1;
        core_stop_train(&bridge, bridge.config.hubs[s].train);
        // Queue property requests rather than bursting unchecked writes here.
        property_step[s] = 0;
        property_due[s] = now_ms() + 100;
    }
    UNLOCK();
    return 0;
}
static int descriptor_cb(uint16_t conn, const struct ble_gatt_error *e, uint16_t chr,
                         const struct ble_gatt_dsc *d, void *arg) {
    int s = (int)(intptr_t)arg;
    LOCK();
    hub_state_t *h = &bridge.hubs[s];
    if (!e->status && ble_uuid_u16(&d->uuid.u) == 0x2902)
        h->cccd = d->handle;
    else if (e->status == BLE_HS_EDONE) {
        if (!h->cccd)
            fail(s, BLE_HS_ENOENT);
        else {
            uint8_t notify[] = {1, 0};
            int rc = ble_gattc_write_flat(conn, h->cccd, notify, 2, subscribe_cb, arg);
            if (rc)
                fail(s, rc);
        }
    } else if (e->status)
        fail(s, e->status);
    UNLOCK();
    return 0;
}
static int characteristic_cb(uint16_t conn, const struct ble_gatt_error *e, const struct ble_gatt_chr *c,
                             void *arg) {
    int s = (int)(intptr_t)arg;
    LOCK();
    hub_state_t *h = &bridge.hubs[s];
    if (!e->status && ble_uuid_cmp(&c->uuid.u, &char_uuid.u) == 0) {
        h->value_handle = c->val_handle;
        h->write_properties = c->properties;
    } else if (e->status == BLE_HS_EDONE) {
        if (!h->value_handle || !(h->write_properties & BLE_GATT_CHR_PROP_NOTIFY) ||
            !(h->write_properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP))
            fail(s, BLE_HS_ENOTSUP);
        else {
            int rc = ble_gattc_disc_all_dscs(conn, h->value_handle, h->svc_end, descriptor_cb, arg);
            if (rc)
                fail(s, rc);
        }
    } else if (e->status)
        fail(s, e->status);
    UNLOCK();
    return 0;
}
static int service_cb(uint16_t conn, const struct ble_gatt_error *e, const struct ble_gatt_svc *svc,
                      void *arg) {
    int s = (int)(intptr_t)arg;
    LOCK();
    hub_state_t *h = &bridge.hubs[s];
    if (!e->status) {
        h->svc_start = svc->start_handle;
        h->svc_end = svc->end_handle;
    } else if (e->status == BLE_HS_EDONE) {
        if (!h->svc_end)
            fail(s, BLE_HS_ENOENT);
        else {
            int rc = ble_gattc_disc_all_chrs(conn, h->svc_start, h->svc_end, characteristic_cb, arg);
            if (rc)
                fail(s, rc);
        }
    } else
        fail(s, e->status);
    UNLOCK();
    return 0;
}
static void advertisement(const struct ble_gap_disc_desc *d) {
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, d->data, d->length_data))
        return;
    bool lego = false;
    for (int i = 0; i < fields.num_uuids128; i++)
        if (ble_uuid_cmp(&fields.uuids128[i].u, &service_uuid.u) == 0)
            lego = true;
    int index = -1, oldest = 0;
    for (int i = 0; i < 32; i++) {
        if (seen[i].used && !memcmp(&seen[i].addr, &d->addr, sizeof(ble_addr_t))) {
            index = i;
            break;
        }
        if (!seen[i].used && index < 0)
            index = i;
        if (seen[i].last < seen[oldest].last)
            oldest = i;
    }
    // A scan response may contain only the name. Accept it only for an address
    // already proven to advertise the LEGO service; names never identify hubs.
    if (!lego && (index < 0 || !seen[index].used || memcmp(&seen[index].addr, &d->addr, sizeof(ble_addr_t))))
        return;
    if (index < 0)
        index = oldest;
    seen_t *s = &seen[index];
    s->used = true;
    s->addr = d->addr;
    s->last = now_ms();
    s->rssi = d->rssi;
    if (fields.name) {
        int n = fields.name_len;
        if (n > 48)
            n = 48;
        memcpy(s->name, fields.name, n);
        s->name[n] = 0;
    }
    if (fields.mfg_data_len >= 4 && fields.mfg_data[0] == 0x97 && fields.mfg_data[1] == 3)
        s->model = fields.mfg_data[3];
}
static int gap_event(struct ble_gap_event *e, void *arg) {
    int slot = (int)(intptr_t)arg;
    LOCK();
    switch (e->type) {
    case BLE_GAP_EVENT_DISC:
        advertisement(&e->disc);
        break;
    case BLE_GAP_EVENT_CONNECT: {
        hub_state_t *h = &bridge.hubs[slot];
        if (e->connect.status) {
            h->attempts++;
            h->deadline = 0;
            h->reason = e->connect.status;
            int shift = h->attempts > 3 ? 3 : h->attempts;
            h->retry_at = now_ms() + (500U << shift);
            if (connecting == slot)
                connecting = -1;
        } else {
            h->connected = true;
            h->handle = e->connect.conn_handle;
            h->svc_start = h->svc_end = h->value_handle = h->cccd = 0;
            h->deadline = now_ms() + 10000;
            core_stop_train(&bridge, bridge.config.hubs[slot].train);
            int rc = ble_gattc_disc_svc_by_uuid(h->handle, &service_uuid.u, service_cb, arg);
            if (rc)
                fail(slot, rc);
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        core_hub_lost(&bridge, slot, e->disconnect.reason);
        bridge.hubs[slot].retry_at = now_ms() + 500;
        bridge.hubs[slot].deadline = 0;
        if (connecting == slot)
            connecting = -1;
        break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        hub_state_t *h = &bridge.hubs[slot];
        if (e->notify_rx.attr_handle == h->value_handle) {
            uint16_t n = OS_MBUF_PKTLEN(e->notify_rx.om);
            uint8_t data[512];
            if (n <= sizeof(data) && !os_mbuf_copydata(e->notify_rx.om, 0, n, data))
                core_lwp(&bridge, slot, data, n);
            else
                bridge.errors++;
        }
        break;
    }
    default:
        break;
    }
    UNLOCK();
    return 0;
}
static void sync_cb(void) {
    LOCK();
    int rc = ble_hs_id_infer_auto(0, &own_type);
    synced = rc == 0;
    UNLOCK();
}
static void reset_cb(int reason) {
    LOCK();
    synced = false;
    connecting = -1;
    for (int i = 0; i < HUBS; i++)
        core_hub_lost(&bridge, i, reason);
    UNLOCK();
}
static void host_task(void *arg) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
void ble_start(void) {
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_LOGI("ble_limits", "actual NimBLE pool=%d controller ble_max_act=%d", MYNEWT_VAL(BLE_MAX_CONNECTIONS),
             controller.ble_max_act);
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = sync_cb;
    ble_hs_cfg.reset_cb = reset_cb;
    ble_svc_gap_device_name_set("nControl MQTT bridge");
    nimble_port_freertos_init(host_task);
}
void ble_scan_request(void) {
    manual_scan = true;
    next_scan = 0;
}
cJSON *ble_seen_json(uint32_t now) {
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < 32; i++)
        if (seen[i].used) {
            cJSON *j = cJSON_CreateObject();
            char addr[18];
            address_text(&seen[i].addr, addr);
            cJSON_AddStringToObject(j, "address", addr);
            cJSON_AddNumberToObject(j, "address_type", seen[i].addr.type);
            cJSON_AddStringToObject(j, "name", seen[i].name);
            cJSON_AddNumberToObject(j, "model", seen[i].model);
            cJSON_AddNumberToObject(j, "rssi", seen[i].rssi);
            cJSON_AddNumberToObject(j, "age_ms", now - seen[i].last);
            cJSON_AddItemToArray(a, j);
        }
    return a;
}
void ble_tick(uint32_t now) {
    if (!synced)
        return;
    bool missing = false;
    for (int i = 0; i < HUBS; i++) {
        hub_state_t *h = &bridge.hubs[i];
        hub_config_t *c = &bridge.config.hubs[i];
        if (h->test_until && (int32_t)(now - h->test_until) >= 0) {
            h->test_until = 0;
            core_stop_train(&bridge, c->train);
        }
        if (!h->enabled && h->connected) {
            bool pending = false;
            for (int p = 0; p < PORTS; p++)
                pending |= h->ports[p].stop_pending;
            if (!pending)
                ble_gap_terminate(h->handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        if (h->deadline && (int32_t)(now - h->deadline) >= 0) {
            h->deadline = 0;
            fail(i, BLE_HS_ETIMEOUT);
            if (!h->connected)
                ble_gap_conn_cancel();
        }
        if (h->enabled && *c->address && !h->connected)
            missing = true;
    }
    if (connecting < 0 && !bridge.maintenance) {
        bool advertised[HUBS] = {0};
        uint32_t advertised_at[HUBS] = {0};
        ble_addr_t addresses[HUBS];
        for (int i = 0; i < HUBS; i++) {
            if (!address_parse(&bridge.config.hubs[i], &addresses[i]))
                continue;
            for (int j = 0; j < 32; j++)
                if (seen[j].used && seen[j].addr.type == addresses[i].type &&
                    !memcmp(seen[j].addr.val, addresses[i].val, 6)) {
                    advertised[i] = true;
                    advertised_at[i] = seen[j].last;
                    break;
                }
        }
        int i = core_connect_candidate(&bridge, advertised, advertised_at, now, connect_cursor);
        if (i >= 0) {
            // NimBLE requires discovery to be cancelled before initiating.
            int rc = ble_gap_disc_active() ? ble_gap_disc_cancel() : 0;
            if (!rc) {
                struct ble_gap_conn_params cp = {.scan_itvl = 160,
                                                 .scan_window = 96,
                                                 .itvl_min = 36,
                                                 .itvl_max = 48,
                                                 .latency = 0,
                                                 .supervision_timeout = 400,
                                                 .min_ce_len = 0,
                                                 .max_ce_len = 0};
                rc = ble_gap_connect(own_type, &addresses[i], 3000, &cp, gap_event,
                                     (void *)(intptr_t)i);
            }
            connect_cursor = (i + 1) % HUBS;
            hub_state_t *h = &bridge.hubs[i];
            if (!rc) {
                connecting = i;
                h->deadline = now + 5000;
            } else {
                h->reason = rc;
                h->retry_at = now + 1000;
            }
        }
        if (connecting < 0 && (manual_scan || missing) &&
            (int32_t)(now - next_scan) >= 0 && !ble_gap_disc_active()) {
            struct ble_gap_disc_params p = {.itvl = 160, .window = 96, .passive = 0,
                                            .filter_duplicates = 0};
            int rc = ble_gap_disc(own_type, manual_scan ? 6000 : 3000, &p, gap_event, NULL);
            if (!rc) {
                next_scan = now + 500;
                manual_scan = false;
            }
        }
    }
    if (now - last_rssi >= 3000) {
        last_rssi = now;
        for (int i = 0; i < HUBS; i++)
            if (bridge.hubs[i].connected) {
                int8_t rssi;
                if (!ble_gap_conn_rssi(bridge.hubs[i].handle, &rssi))
                    bridge.hubs[i].rssi = rssi;
            }
    }
    if (now - last_write < 20)
        return;
    // Across hubs, service STOP before any acceleration. Per-port targets coalesce.
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < HUBS; k++) {
            int i = (round_robin + k) % HUBS;
            hub_state_t *h = &bridge.hubs[i];
            uint8_t rgb_frame[10];
            if (pass == 1 && core_led_next(&bridge, i, now, rgb_frame)) {
                int rc = ble_gattc_write_no_rsp_flat(h->handle, h->value_handle, rgb_frame, rgb_frame[0]);
                last_write = now;
                round_robin = (i + 1) % HUBS;
                if (!rc) {
                    core_led_sent(&bridge, i, now, rgb_frame);
                    if (rgb_frame[2] == 0x22)
                        snprintf(h->last_tx, sizeof(h->last_tx), "LED RGB mode-name request: port=%u", rgb_frame[3]);
                    else if (rgb_frame[2] == 0x41)
                        snprintf(h->last_tx, sizeof(h->last_tx), "LED RGB mode request: port=%u", rgb_frame[3]);
                    else
                        snprintf(h->last_tx, sizeof(h->last_tx), "LED port=%u RGB=#%06X", rgb_frame[3], h->led_sent_rgb);
                } else if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY)
                    fail(i, rc);
                return;
            }
            uint8_t f[8];
            port_state_t *port = NULL;
            if (!core_next(&bridge, i, now, f, &port))
                continue;
            if (pass == 0 && !port->stop_pending)
                continue;
            int rc = ble_gattc_write_no_rsp_flat(h->handle, h->value_handle, f, 8);
            last_write = now;
            round_robin = (i + 1) % HUBS;
            if (!rc) {
                core_sent(port, f, now);
                snprintf(h->last_tx, sizeof(h->last_tx), "08 00 81 %02X 11 51 00 %02X", f[3], f[7]);
            } else {
                h->reason = rc;
                if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY)
                    fail(i, rc);
            }
            return;
        }
    }
    // Telemetry uses idle write slots; STOP and output commands keep priority.
    if (bridge.maintenance)
        return;
    for (int k = 0; k < HUBS; k++) {
        int i = (property_round_robin + k) % HUBS;
        hub_state_t *h = &bridge.hubs[i];
        if (!h->ready || !h->enabled || (int32_t)(now - property_due[i]) < 0)
            continue;
        const uint8_t properties[] = {6, 6, 3, 11};
        uint8_t step = property_step[i];
        uint8_t frame[] = {5, 0, 1, properties[step], step == 0 ? 2 : 5};
        int rc = ble_gattc_write_no_rsp_flat(h->handle, h->value_handle, frame, sizeof(frame));
        last_write = now;
        property_round_robin = (i + 1) % HUBS;
        if (!rc) {
            property_step[i] = (step + 1) % 4;
            property_due[i] = now + (step < 3 ? 100 :
                                     (h->battery < 0 || !h->firmware[0] ? 3000 : 30000));
        } else if (rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY)
            property_due[i] = now + 100;
        else
            fail(i, rc);
        return;
    }
}
