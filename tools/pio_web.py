# Executed by PlatformIO/SCons before dependency scanning.
# CMAKE_CONFIGURE_DEPENDS alone is insufficient for PlatformIO incremental builds.
Import("env")
from pathlib import Path
import subprocess
import sys
project = Path(env.subst("$PROJECT_DIR"))
subprocess.run([sys.executable, str(project / "tools/provision.py"), str(project)], check=True)
