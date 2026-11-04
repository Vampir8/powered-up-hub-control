"""Capture UART boot/resource diagnostics, without printing secrets."""
import argparse
import time
from pathlib import Path
import serial
p = argparse.ArgumentParser()
p.add_argument('--port', default='COM5')
p.add_argument('--seconds', type=float, default=25)
p.add_argument('--reset', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parent.parent
with serial.Serial(a.port, 115200, timeout=0.2) as port:
    port.dtr = False
    port.rts = False
    if a.reset:
        port.rts = True
        time.sleep(0.1)
        port.rts = False
    end = time.monotonic() + a.seconds
    chunks = []
    while time.monotonic() < end:
        chunks.append(port.read(4096))
text = b''.join(chunks).decode('utf-8', errors='replace')
out = root/'test-results/uart-boot.txt'
out.parent.mkdir(exist_ok=True)
out.write_text(text, encoding='utf-8')
print(text.encode('ascii', 'replace').decode())
