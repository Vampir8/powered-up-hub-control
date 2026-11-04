import json
import secrets
import sys
from pathlib import Path
root = Path(sys.argv[1]).resolve()
path = root / 'provisioning.json'
if not path.exists():
    path.write_text(json.dumps({'username': 'admin', 'panel_password': secrets.token_hex(12),
                                'ap_password': '12345678'}, indent=2), encoding='utf-8')
data = json.loads(path.read_text(encoding='utf-8'))
for key in ('panel_password', 'ap_password'):
    if not isinstance(data[key], str) or not 8 <= len(data[key]) <= 63:
        raise ValueError('Provisioning passwords must have 8..63 characters')
(root / 'generated').mkdir(exist_ok=True)
content = '#pragma once\n#define PROVISION_PANEL_PASSWORD ' + json.dumps(data['panel_password']) + '\n'
content += '#define PROVISION_AP_PASSWORD ' + json.dumps(data['ap_password']) + '\n'
header = root / 'generated' / 'provisioning.h'
if not header.exists() or header.read_text(encoding='utf-8') != content:
    header.write_text(content, encoding='utf-8')
# A generated C asset works identically with IDF and PlatformIO's SCons build,
# avoiding their different handling of generated .S paths on Windows.
asset = ''
for page, symbol in [('index.html', 'index_start')]:
    data = (root / 'main/web' / page).read_bytes() + b'\0'
    asset += 'const unsigned char ' + symbol + '[] = {\n'
    asset += '\n'.join(','.join(str(x) for x in data[i:i+24])+',' for i in range(0,len(data),24))
    asset += '\n};\n'
asset_path = root / 'generated/web_asset.c'
if not asset_path.exists() or asset_path.read_text(encoding='utf-8') != asset:
    asset_path.write_text(asset, encoding='utf-8')
