"""Loopback-only UI fixture. No board or motors; not firmware verification."""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
root = Path(__file__).resolve().parent.parent
config = json.loads((root/'examples/config.json').read_text(encoding='utf-8'))
state = dict(wifi=True, mqtt=False, subscribed=False, syncing=False, sync_done=False,
             fallback_ap=False, maintenance=False, ip='192.168.1.42', uptime_ms=10000,
             free_internal=140000, min_internal=130000, largest_internal=90000,
             host_limit=8, controller_activities=10, idf='5.4.1', errors=0, accepted=0,
             ignored=0, last_train=-1, last_message='', last_error='', connected=0, queue=0,
             seen=[dict(name='LEGO Hub', address='AA:BB:CC:DD:EE:01', address_type=0,
                        model=65, rssi=-52, age_ms=1500)])
state['trains'] = [dict(label=t['label'],driving=False,fresh=False,ui_power=0,direction=1,
                        light=False,active=False,ui_battery_index=0) for t in config['trains']]
state['hubs'] = [dict(slot=i+1, connected=False,ready=False,enabled=True,model=0,
                     firmware='',battery_percent=-1,rssi=0,reason=0,error='',last_tx='',ports=[])
                 for i in range(8)]
for i, hub in enumerate(state['hubs']):
    hub.update(connected=i < 7, ready=i < 7, battery_percent=85-i*8 if i < 7 else -1)
state['connected'] = 7
state['mqtt'] = True
state['hubs'][0]['led_reported_rgb'] = 0x00ff00
config['hubs'][1]['led_rgb'] = 0xff0000
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/':
            content = (root/'main/web'/'index.html').read_bytes()
            mime = 'text/html; charset=utf-8'
        elif self.path == '/api/state':
            content = json.dumps(state).encode()
            mime = 'application/json'
        elif self.path.startswith('/api/config'):
            content = json.dumps(config).encode()
            mime = 'application/json'
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(content)))
        self.end_headers()
        self.wfile.write(content)
    def do_POST(self):
        payload = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if self.path == '/api/action':
            (root/'test-results').mkdir(exist_ok=True)
            (root/'test-results/ui-preview-action.json').write_bytes(payload)
        if self.path == '/api/config':
            (root/'test-results').mkdir(exist_ok=True)
            (root/'test-results/ui-preview-post.json').write_bytes(payload)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'PREVIEW: no board action')
HTTPServer(('127.0.0.1', 8765), Handler).serve_forever()
