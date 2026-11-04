#include "core.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
void config_defaults(config_t *c) {
    memset(c, 0, sizeof(*c));
    c->schema = SCHEMA;
    c->mqtt_enabled = true;
    c->port = 1883;
    c->keepalive = 15;
    c->reconnect_ms = 5000;
    c->stop_gpio = -1;
    strcpy(c->prefix, "nControl/bridge/esp32s3");
    for (int i = 0; i < TRAINS; i++) {
        snprintf(c->trains[i].label, 49, "Train %d", i + 1);
        c->trains[i].limit = 100;
        c->trains[i].ramp_ms = 1500;
        c->trains[i].brightness = 60;
    }
    for (int i = 0; i < HUBS; i++) {
        hub_config_t *h = &c->hubs[i];
        snprintf(h->name, 49, "Hub %d", i + 1);
        h->train = i;
        h->motor[0] = 0;
        h->motor[1] = -1;
        h->lamp = -1;
        h->led_rgb = -1;
        h->polarity[0] = h->polarity[1] = 1;
        h->enabled = true;
    }
}
bool utf8_valid(const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n;) {
        uint32_t ch = s[i++];
        int extra = 0;
        uint32_t min = 0;
        if (ch < 0x80) {
            if (ch < 0x20 || ch == 0x7f)
                return false;
            continue;
        }
        if (ch >= 0xc2 && ch <= 0xdf) {
            extra = 1;
            min = 0x80;
            ch &= 0x1f;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            extra = 2;
            min = 0x800;
            ch &= 15;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            extra = 3;
            min = 0x10000;
            ch &= 7;
        } else
            return false;
        if (i + (size_t)extra > n)
            return false;
        while (extra--) {
            if ((s[i] & 0xc0) != 0x80)
                return false;
            ch = (ch << 6) | (s[i++] & 63);
        }
        if (ch < min || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff))
            return false;
    }
    return true;
}
static bool address_ok(const char *s) {
    if (!*s)
        return true;
    if (strlen(s) != 17)
        return false;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (s[i] != ':')
                return false;
        } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
                     (s[i] >= 'A' && s[i] <= 'F')))
            return false;
    }
    return true;
}
bool config_validate(const config_t *c, char *error, size_t size) {
#define BAD(msg)                                                                                             \
    do {                                                                                                     \
        snprintf(error, size, "%s", msg);                                                                    \
        return false;                                                                                        \
    } while (0)
    if (c->schema != SCHEMA)
        BAD("Unsupported schema version");
    if (c->port < 1 || c->port > 65535 || c->keepalive < 5 || c->keepalive > 300 || c->reconnect_ms < 1000 ||
        c->reconnect_ms > 60000)
        BAD("MQTT settings out of range");
    if (strlen(c->panel_password) < 8 || strlen(c->ap_password) < 8 || strlen(c->ap_password) > 63)
        BAD("Panel/AP passwords require at least 8 characters");
    if (!*c->client_id || strpbrk(c->client_id, "#+/"))
        BAD("Client ID");
    if (strncmp(c->prefix, "nControl/bridge/", 16) != 0 || strpbrk(c->prefix, "#+"))
        BAD("Prefix must start with nControl/bridge/");
    if (c->stop_gpio < -1 || c->stop_gpio > 48)
        BAD("GPIO out of range");
    for (int i = 0; i < TRAINS; i++) {
        const train_config_t *t = &c->trains[i];
        size_t n = strlen(t->label);
        if (!n || !utf8_valid((const uint8_t *)t->label, n) || strpbrk(t->label, "|,"))
            BAD("Invalid train label");
        if (!utf8_valid((const uint8_t*)t->name,strlen(t->name)))BAD("Invalid train node name");
        if (t->limit < 1 || t->limit > 100 || t->brightness < 0 || t->brightness > 100 || t->ramp_ms < 0 ||
            t->ramp_ms > 30000)
            BAD("Train settings out of range");
        for (int j = 0; j < i; j++)
            if (!t->hidden&&!c->trains[j].hidden&&!strcmp(t->label, c->trains[j].label))
                BAD("Duplicate train label");
    }
    for (int i = 0; i < HUBS; i++) {
        const hub_config_t *h = &c->hubs[i];
        if (!utf8_valid((const uint8_t*)h->name,strlen(h->name))||!utf8_valid((const uint8_t*)h->lamp_name,strlen(h->lamp_name)))BAD("Invalid hub/light name");
        for(int k=0;k<2;k++)if(!utf8_valid((const uint8_t*)h->motor_name[k],strlen(h->motor_name[k])))BAD("Invalid motor name");
        if(h->hidden&&(h->enabled||h->train>=0||h->motor[0]>=0||h->motor[1]>=0||h->lamp>=0))BAD("A removed hub must be disabled and unassigned");
        if(h->train>=0&&h->train<TRAINS&&c->trains[h->train].hidden)BAD("Hub assigned to a removed train");
        if (!address_ok(h->address) || h->address_type < 0 || h->address_type > 1 || h->train < -1 ||
            h->train >= TRAINS)
            BAD("Invalid hub address, address type or train assignment");
        if (h->led_rgb < -1 || h->led_rgb > 0xffffff)
            BAD("Invalid LED RGB color");
        if (h->lamp < -1 || h->lamp > 255)
            BAD("Invalid light port");
        for (int m = 0; m < 2; m++)
            if (h->motor[m] < -1 || h->motor[m] > 255 || (h->polarity[m] != 1 && h->polarity[m] != -1))
                BAD("Invalid motor port or polarity");
        if ((h->motor[0] >= 0 && (h->motor[0] == h->motor[1] || h->motor[0] == h->lamp)) ||
            (h->motor[1] >= 0 && h->motor[1] == h->lamp))
            BAD("A port has multiple owners");
        for (int j = 0; j < i; j++)
            if (*h->address && !strcasecmp(h->address, c->hubs[j].address))
                BAD("Duplicate hub address");
    }
    return true;
#undef BAD
}
bool parse_command(const config_t *c, const uint8_t *d, size_t n, command_t *o) {
    if (n < 5 || n > 128 || !utf8_valid(d, n) || d[0] != 'T' || d[1] != '|')
        return false;
    size_t sep = 2;
    while (sep < n && d[sep] != '|')
        sep++;
    if (sep == n || sep == 2 || sep - 2 > 48)
        return false;
    o->train = -1;
    for (int i = 0; i < TRAINS; i++)
        if (!c->trains[i].hidden && strlen(c->trains[i].label) == sep - 2 && !memcmp(d + 2, c->trains[i].label, sep - 2))
            o->train = i;
    if (o->train < 0)
        return false;
    size_t rem = n - sep - 1;
    if (!rem)
        return false;
    const uint8_t *p = d + sep + 1;
    o->code = p[0];
    o->value = 0;
    if ((p[0] == 'f' || p[0] == 'r') && rem == 1)
        return true;
    if (rem < 3 || rem > 5 || p[1] != ',' || !strchr("plba", p[0]))
        return false;
    for (size_t i = 2; i < rem; i++) {
        if (p[i] < '0' || p[i] > '9')
            return false;
        o->value = o->value * 10 + p[i] - '0';
    }
    if (rem > 3 && p[2] == '0')
        return false;
    return o->value <= (p[0] == 'p' ? 100 : p[0] == 'b' ? 5 : 1);
}
void core_init(bridge_t *b, const config_t *c) {
    memset(b, 0, sizeof(*b));
    b->config = *c;
    b->last_train = -1;
    for (int i = 0; i < TRAINS; i++)
        b->trains[i].direction = 1;
    for (int i = 0; i < HUBS; i++) {
        b->hubs[i].handle = 0xffff;
        b->hubs[i].battery = -1;
        b->hubs[i].led_sent_rgb = -2;
        b->hubs[i].led_reported_rgb = -2;
        b->hubs[i].enabled = c->hubs[i].enabled;
    }
}
port_state_t *core_port(hub_state_t *h, int id, bool create) {
    if (id < 0)
        return NULL;
    for (int i = 0; i < PORTS; i++)
        if (h->ports[i].attached && h->ports[i].id == id)
            return &h->ports[i];
    if (create)
        for (int i = 0; i < PORTS; i++)
            if (!h->ports[i].attached) {
                memset(&h->ports[i], 0, sizeof(port_state_t));
                h->ports[i].id = id;
                h->ports[i].attached = true;
                h->ports[i].feedback = -1;
                return &h->ports[i];
            }
    return NULL;
}
bool core_motor(uint16_t d) {
    switch (d) {
    case 1:
    case 2:
    case 38:
    case 39:
        return true;
    default:
        return false;
    }
}
static void stop_outputs(bridge_t *b, int t) {
    for (int i = 0; i < HUBS; i++)
        if (b->config.hubs[i].train == t) {
            hub_config_t *c = &b->config.hubs[i];
            for (int m = 0; m < 2; m++) {
                port_state_t *p = core_port(&b->hubs[i], c->motor[m], b->hubs[i].ready);
                if (p) {
                    int stop_code = b->config.trains[t].brake ? 127 : 0;
                    if (p->target || p->applied || !p->sent_valid || p->sent != stop_code)
                        p->stop_pending = true;
                    p->target = 0;
                    p->applied = 0;
                    p->reverse_until = 0;
                }
            }
        }
}
void core_stop_train(bridge_t *b, int t) {
    if (t < 0 || t >= TRAINS)
        return;
    b->trains[t].driving = false;
    b->trains[t].fresh = false;
    stop_outputs(b, t);
    for (int i = 0; i < HUBS; i++)
        if (b->config.hubs[i].train == t) {
            b->hubs[i].test_until = 0;
            port_state_t *p = core_port(&b->hubs[i], b->config.hubs[i].lamp, b->hubs[i].ready);
            if (p)
                p->target = 0;
        }
}
void core_stop_all(bridge_t *b) {
    for (int t = 0; t < TRAINS; t++)
        core_stop_train(b, t);
}
void core_sync(bridge_t *b, uint32_t now) {
    core_stop_all(b);
    b->syncing = false;
    b->sync_done = true;
    b->sync_start = b->last_rx = now;
}
static void targets(bridge_t *b, int t, uint32_t now) {
    train_state_t *s = &b->trains[t];
    train_config_t *tc = &b->config.trains[t];
    for (int i = 0; i < HUBS; i++)
        if (b->config.hubs[i].train == t && b->hubs[i].ready && b->hubs[i].enabled) {
            hub_config_t *hc = &b->config.hubs[i];
            for (int m = 0; m < 2; m++) {
                port_state_t *p = core_port(&b->hubs[i], hc->motor[m], true);
                if (!p)
                    continue;
                int target =
                    s->driving && s->fresh ? s->direction * s->power * tc->limit / 100 * hc->polarity[m] : 0;
                if (p->target != target) {
                    p->last_step = now;
                    if (!target || p->applied * target < 0) {
                        p->stop_pending = true;
                        p->applied = 0;
                    }
                    p->target = target;
                }
            }
            port_state_t *l = core_port(&b->hubs[i], hc->lamp, true);
            if (l)
                l->target = s->driving && s->light ? tc->brightness : 0;
        }
}
void core_receive(bridge_t *b, const uint8_t *d, size_t n, bool retained, uint32_t now) {
    command_t c;
    if (!b->config.mqtt_enabled) {
        b->ignored++;
        return;
    }
    b->last_rx = now;
    b->last_train = -1;
    if (n < sizeof(b->last_message) && utf8_valid(d, n)) {
        memcpy(b->last_message, d, n);
        b->last_message[n] = 0;
    } else
        strcpy(b->last_message, "[invalid payload]");
    if (!parse_command(&b->config, d, n, &c)) {
        b->ignored++;
        return;
    }
    b->accepted++;
    b->last_train = c.train;
    train_state_t *s = &b->trains[c.train];
    // Retained is never a live physical command, including direction/light.
    if (retained) {
        b->ignored++;
        return;
    }
    if (!b->wifi || !b->mqtt || !b->subscribed || b->fallback_ap || b->maintenance)
        return;
    switch (c.code) {
    case 'p':
        s->power = c.value;
        if (!c.value) {
            s->fresh = false;
            stop_outputs(b, c.train);
        } else {
            s->driving = true;
            s->fresh = true;
        }
        break;
    case 'f':
    case 'r':
        s->direction = c.code == 'f' ? 1 : -1;
        break;
    case 'l':
        s->driving = true;
        s->light = c.value;
        break;
    case 'b':
        s->battery = c.value;
        return;
    case 'a':
        s->active = c.value;
        return;
    }
    targets(b, c.train, now);
}
void core_hub_lost(bridge_t *b, int slot, int reason) {
    hub_state_t *h = &b->hubs[slot];
    h->connected = h->ready = false;
    h->handle = 0xffff;
    h->led_sent_rgb = h->led_reported_rgb = -2;
    h->led_mode_ready = false;
    h->led_setup_attempts = 0;
    h->led_probe_attempts = 0;
    h->led_error[0] = 0;
    h->reason = reason;
    core_stop_train(b, b->config.hubs[slot].train);
    memset(h->ports, 0, sizeof(h->ports));
}
void core_lwp(bridge_t *b, int slot, const uint8_t *d, size_t n) {
    hub_state_t *h = &b->hubs[slot];
    size_t off = 0;
    while (off < n) {
        const uint8_t *p = d + off;
        size_t avail = n - off, head = 1, len;
        if (avail < 3)
            goto invalid;
        len = p[0];
        if (len & 128) {
            if (avail < 4)
                goto invalid;
            len = (len & 127) | ((size_t)p[1] << 7);
            head = 2;
        }
        if (len < head + 2 || len > avail || p[head] != 0)
            goto invalid;
        const uint8_t *v = p + head + 2;
        size_t vn = len - head - 2;
        uint8_t type = p[head + 1];
        if (type == 4 && vn >= 2) {
            port_state_t *q = core_port(h, v[0], v[1] != 0);
            if (v[1] == 0) {
                core_stop_train(b, b->config.hubs[slot].train);
                if (q)
                    q->attached = false;
            } else if (q && v[1] == 1 && vn >= 12) {
                q->device = v[2] | v[3] << 8;
                q->detected = true;
                if (q->device == 0x17) {
                    h->led_sent_rgb = h->led_reported_rgb = -2;
                    h->led_mode_ready = false;
                    h->led_setup_attempts = 0;
                    h->led_probe_attempts = 0;
                    h->led_error[0] = 0;
                }
                if (core_motor(q->device)) {
                    q->stop_pending = true;
                    core_stop_train(b, b->config.hubs[slot].train);
                }
            }
        } else if (type == 1 && vn >= 3 && v[1] == 6) {
            if (v[0] == 6)
                h->battery = v[2];
            if (v[0] == 3 && vn >= 6)
                snprintf(h->firmware, sizeof(h->firmware), "%02x%02x%02x%02x", v[5], v[4], v[3], v[2]);
            if (v[0] == 11)
                h->model = v[2];
        } else if (type == 0x44 && vn >= 6 && v[0] == 50 && v[1] == 1 && v[2] == 0 &&
                   v[3] == 'R' && v[4] == 'G' && v[5] == 'B') {
            // Some train hubs omit the internal LED Attached I/O notification.
            // Identify it by its RGB mode name before sending any output.
            port_state_t *q = core_port(h, v[0], true);
            if (q && (!q->detected || q->device == 0x17)) {
                q->device = 0x17;
                q->detected = true;
                h->led_error[0] = 0;
            }
        } else if (type == 0x47 && vn >= 7) {
            port_state_t *q = core_port(h, v[0], false);
            if (q && q->detected && q->device == 0x17 && v[0] >= 50 && v[0] <= 100) {
                h->led_mode_ready = v[1] == 1 && v[6] == 1;
                if (h->led_mode_ready)
                    h->led_error[0] = 0;
            }
        } else if (type == 0x45 && vn == 4 && h->led_mode_ready) {
            port_state_t *q = core_port(h, v[0], false);
            if (q && q->detected && q->device == 0x17 && v[0] >= 50 && v[0] <= 100)
                h->led_reported_rgb = (v[1] << 16) | (v[2] << 8) | v[3];
        } else if (type == 0x82) {
            for (size_t j = 0; j + 1 < vn; j += 2) {
                port_state_t *q = core_port(h, v[j], false);
                if (q)
                    q->feedback = v[j + 1];
            }
        } else if (type == 5 && vn >= 2 && v[1] > 2) {
            snprintf(h->error, sizeof(h->error), "LEGO error type=0x%02x code=0x%02x", v[0], v[1]);
            b->errors++;
            core_stop_train(b, b->config.hubs[slot].train);
        }
        off += len;
    }
    return;
invalid:
    b->errors++;
    strcpy(h->error, "Invalid LWP frame");
    core_stop_train(b, b->config.hubs[slot].train);
}
bool core_next(bridge_t *b, int slot, uint32_t now, uint8_t f[8], port_state_t **result) {
    hub_state_t *h = &b->hubs[slot];
    hub_config_t *hc = &b->config.hubs[slot];
    if (!h->ready)
        return false;
    int t = hc->train;
    train_config_t *tc = t >= 0 ? &b->config.trains[t] : NULL;
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < PORTS; j++) {
            port_state_t *p = &h->ports[j];
            if (!p->attached)
                continue;
            bool motor = p->id == hc->motor[0] || p->id == hc->motor[1],
                 lamp = p->id == hc->lamp;
            if (!motor && !lamp && !p->stop_pending)
                continue;
            if (pass == 0 && !p->stop_pending)
                continue;
            if (pass == 1 && p->stop_pending)
                continue;
            int power = p->applied;
            if (p->stop_pending)
                power = tc && tc->brake && motor ? 127 : 0;
            else {
                if (p->reverse_until && (int32_t)(now - p->reverse_until) < 0)
                    continue;
                if (motor && tc && tc->ramp_ms > 0) {
                    uint32_t dt = now - p->last_step;
                    int step = (int)(dt * 100 / tc->ramp_ms);
                    if (step < 1 && power != p->target)
                        continue;
                    if (step > 100)
                        step = 100;
                    if (power < p->target) {
                        power += step;
                        if (power > p->target)
                            power = p->target;
                    }
                    if (power > p->target) {
                        power -= step;
                        if (power < p->target)
                            power = p->target;
                    }
                } else
                    power = p->target;
                if (motor && tc && tc->brake && !power && !p->target)
                    power = 127;
                if (p->sent_valid && p->sent == power)
                    continue;
            }
            uint8_t frame[8] = {8, 0, 0x81, p->id, 0x11, 0x51, 0, (uint8_t)(int8_t)power};
            memcpy(f, frame, 8);
            *result = p;
            return true;
        }
    return false;
}
void core_sent(port_state_t *p, const uint8_t f[8], uint32_t now) {
    bool stop = p->stop_pending;
    p->sent = (int8_t)f[7];
    p->sent_valid = true;
    p->last_step = now;
    if (stop) {
        p->stop_pending = false;
        p->applied = 0;
        if (p->target) {
            p->reverse_until = now + 150;
            p->last_step = p->reverse_until;
        }
    } else
        p->applied = p->sent == 127 ? 0 : p->sent;
}

// Configure the RGB mode and wait for the hub's 0x47 reply before writing RGB.
bool core_led_next(bridge_t *b, int slot, uint32_t now, uint8_t f[10]) {
    hub_state_t *h = &b->hubs[slot];
    int rgb = b->config.hubs[slot].led_rgb;
    if (!h->ready || !h->enabled || b->maintenance || rgb < 0 || rgb > 0xffffff || h->led_sent_rgb == rgb)
        return false;
    for (int i = 0; i < PORTS; i++) {
        port_state_t *p = &h->ports[i];
        if (!p->attached || !p->detected || p->id < 50 || p->id > 100 || p->device != 0x17)
            continue;
        if (!h->led_mode_ready) {
            if (h->led_setup_attempts && now - h->led_setup_at < 1000)
                return false;
            if (h->led_setup_attempts >= 3) {
                strcpy(h->led_error, "RGB mode not acknowledged; select a color to retry");
                return false;
            }
            uint8_t setup[10] = {10, 0, 0x41, p->id, 1, 1, 0, 0, 0, 1};
            memcpy(f, setup, 10);
        } else {
            uint8_t frame[10] = {10, 0, 0x81, p->id, 0x11, 0x51, 1,
                                (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
            memcpy(f, frame, 10);
        }
        return true;
    }
    if (h->led_probe_attempts && now - h->led_probe_at < 1000)
        return false;
    if (h->led_probe_attempts >= 3) {
        strcpy(h->led_error, "Hub did not confirm the RGB LED; select a color to retry");
        return false;
    }
    // Read-only mode-name request for the standard train hub LED port.
    uint8_t probe[10] = {6, 0, 0x22, 50, 1, 0};
    memcpy(f, probe, sizeof(probe));
    return true;
}
void core_led_sent(bridge_t *b, int slot, uint32_t now, const uint8_t f[10]) {
    hub_state_t *h = &b->hubs[slot];
    if (f[2] == 0x22) {
        h->led_probe_at = now;
        h->led_probe_attempts++;
    } else if (f[2] == 0x41) {
        h->led_setup_at = now;
        h->led_setup_attempts++;
    } else if (f[2] == 0x81)
        h->led_sent_rgb = (f[7] << 16) | (f[8] << 8) | f[9];
}

// A powered-off early slot must not block an advertising later slot.
int core_connect_candidate(const bridge_t *b, const bool seen[HUBS],
                           const uint32_t advertised_at[HUBS], uint32_t now, int start) {
    if (b->maintenance)
        return -1;
    for (int k = 0; k < HUBS; k++) {
        int i = (start + k) % HUBS;
        const hub_state_t *h = &b->hubs[i];
        if (h->enabled && !h->connected && b->config.hubs[i].address[0] && seen[i] &&
            now - advertised_at[i] <= 3000 && (int32_t)(now - h->retry_at) >= 0)
            return i;
    }
    return -1;
}
