"""Run the actual firmware core on the host using a native C compiler."""
import os
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parent.parent
out = root / 'test-results'
out.mkdir(exist_ok=True)
zig = root / '.pio/host-tools/ziglang/zig.exe'
compiler = [str(zig), 'cc'] if zig.exists() else [os.environ.get('CC', 'cc')]
exe = out / ('core_test.exe' if os.name == 'nt' else 'core_test')
subprocess.run(compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                         '-I'+str(root/'main'), str(root/'main/core.c'), str(root/'tests/core_test.c'),
                         '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], capture_output=True, text=True)
(out/'host-tests.txt').write_text(result.stdout+result.stderr, encoding='utf-8')
print(result.stdout, end='')
print(result.stderr, end='')
result.check_returncode()
