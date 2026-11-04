# Future nControl integration — design notes only

This document describes proposals, not supported interfaces. The current bridge
uses the legacy `T|...` stream and has no manual ARM step. See the README for
implemented behavior.

## Explicit command sessions

A future dedicated nControl Train backend could publish a separate command stream:

```json
{"session":"new-random-id","sequence":42,"mode":"Operational","train":"Train 1",
 "power":50,"direction":"f","light":true,"kind":"command"}
```

Possible topics: `nControl/bridge/<id>/command`, `/heartbeat` and `/ack`.
Physical commands would require an explicit Operational mode, non-retained QoS 1,
session identity, increasing sequence numbers and a handshake. Configuration
snapshots would never start outputs. Heartbeat expiry would stop outputs and
require a new handshake. MQTT keepalive is not an application command heartbeat.
Acknowledgements would distinguish parser acceptance, BLE queuing and hub feedback;
none of these alone proves physical motor rotation.

The proposed protocol is not implemented in this firmware or supplied as an
nControl application modification. The legacy stream does not provide enough
information to infer all of these semantics.

## Onboard controller and functions

Other discussed extensions include a per-train ESP32, two PU hub connections plus
one LEGO remote connection, local motor/light driver ports A–D, Hall-sensor
localization and F1/F2-style train functions. These remain design concepts.
They are not part of the working eight-hub bridge published here.

No `D`-protocol device emulation or automatic `CIR` response is implemented.
