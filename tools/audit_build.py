"""Verify effective SDK configuration, binaries and partition sizes after build."""
import argparse
import json
import re
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('--build', default='.pio/build/esp32s3')
p.add_argument('--sdkconfig', default='sdkconfig.esp32s3')
args = p.parse_args()
root = Path(__file__).resolve().parent.parent
build = root / args.build
sdk = root / args.sdkconfig
if not sdk.exists():
    sdk = root / 'sdkconfig'
content = sdk.read_text(encoding='utf-8')
required = {'CONFIG_IDF_TARGET': '"esp32s3"', 'CONFIG_BT_ENABLED': 'y',
            'CONFIG_BT_NIMBLE_ENABLED': 'y', 'CONFIG_BT_NIMBLE_ROLE_CENTRAL': 'y',
            'CONFIG_BT_NIMBLE_ROLE_OBSERVER': 'y', 'CONFIG_BT_NIMBLE_MAX_CONNECTIONS': '8',
            'CONFIG_BT_CTRL_BLE_MAX_ACT': '10', 'CONFIG_BT_CTRL_BLE_MAX_ACT_EFF': '10',
            'CONFIG_ESP_COEX_SW_COEXIST_ENABLE': 'y', 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB': 'y'}
result = {}
commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
pool_commands = [x for x in commands if x['file'].replace('\\', '/').endswith('/ble_hs_conn.c')]
assert pool_commands, 'Actual NimBLE host connection pool must be compiled'
for entry in pool_commands:
    command = entry.get('command', ' '.join(entry.get('arguments', [])))
    assert 'CONFIG_NIMBLE_MAX_CONNECTIONS=CONFIG_BT_NIMBLE_MAX_CONNECTIONS' in command
result['nimble_pool_macro_mapping'] = 'Verified in actual ble_hs_conn.c compile command'
for key, expected in required.items():
    match = re.search(r'^' + key + r'=(.*)$', content, re.M)
    actual = match.group(1) if match else None
    assert actual == expected, (key, expected, actual)
    result[key] = actual
header = (build / 'config/sdkconfig.h').read_text(encoding='utf-8')
for key in ('CONFIG_BT_NIMBLE_MAX_CONNECTIONS', 'CONFIG_BT_CTRL_BLE_MAX_ACT',
            'CONFIG_BT_CTRL_BLE_MAX_ACT_EFF'):
    assert re.search(r'^#define ' + key + ' ' + required[key] + r'$', header, re.M), key
binary = build / 'firmware.bin'
if not binary.exists():
    binary = build / 'lego_mqtt_bridge.bin'
assert binary.stat().st_size < 0x300000
assert (root/'main/web/index.html').read_bytes() + b'\0' in binary.read_bytes(), 'Stale or missing WWW asset'
assert not (root/'main/web/settings.html').exists(), 'Only the current UI should be packaged'
result['web_ui'] = 'Single graph UI with integrated tools'
assert any('/espressif__mdns/' in x['file'].replace('\\','/') for x in commands), 'mDNS component must be compiled'
result['mdns'] = 'espressif/mdns 1.8.2; HTTP on port 80'
result['firmware_bytes'] = binary.stat().st_size
result['hardware_8_hub_test'] = 'NOT PERFORMED'
result['runtime_heap'] = 'Read /api/state and UART on the actual board'
report = root / 'test-results/build-audit.json'
report.parent.mkdir(exist_ok=True)
report.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
