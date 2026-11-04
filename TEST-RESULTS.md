# Verification

## English publication — 4 November 2026

Scope: English web UI, color names, help, import/export and OTA tools, firmware
validation/error messages and project documentation. MQTT commands, JSON keys,
stored user labels and device behavior retain their existing meaning.

The shared CSS remains byte-identical to the neighboring I/O controller project.
Only the shared color-name labels were synchronized; BLE logic was not copied.

- Native firmware core tests: **PASS**.
- ESP32-S3 firmware build: **PASS** — 1,302,144-byte binary, 62,780 bytes
  static RAM; 16 MB flash configuration and two 3 MB app partitions.
- Firmware audit: **PASS** — exact current HTML embedded, eight actual NimBLE
  host connections, ten controller activities and mDNS component compiled.
- Baklava graph/model tests and English HTML/palette checks: **PASS**.
- Browser checks: **PASS** — 26 nodes, English document language and labels,
  24 English color presets, JSON preview and English OTA validation feedback.
- Both the Powered Up and I/O panels were rebuilt and visually inspected;
  their shared CSS files remain byte-identical.
- Publication file scan: **PASS** — provisioning secrets, generated headers,
  local logs, archives and build outputs are excluded from Git.

Firmware build and audit verify the embedded HTML and configured BLE limits;
they do not prove long-term hardware reliability. This publication does not
flash any connected device.

## Existing behavior covered by tests

- Native core: command parsing, output routing, STOP, reconnect behavior, ramps,
  direction changes, validation and LEGO protocol handling.
- Graph model: multiple hubs per train, dynamic outputs, invalid connections,
  exclusive port assignments, deletion protection, node aliases and MQTT labels.
- Layout: current 24 px grid, negative-coordinate snapping and orthogonal routes.
- Editor: status-only nodes, shared color presets and explicit save behavior.

Earlier development sessions connected eight physical hubs. Motor tests and
individual fixes were checked during development, but this is not a claim that
the final English build passed the full 30-minute eight-hub procedure.

Use [the hardware test procedure](docs/HARDWARE-TEST.md) for a sustained run.
