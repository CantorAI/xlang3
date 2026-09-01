# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import select
import socket
import subprocess
import sys

# subprocess must use CPython's subprocess.py over native process primitives,
# and sys.executable must point back to the active XLang3-compatible launcher.
print("subprocess-source", subprocess.__file__.endswith("subprocess.py"))
print("executable", sys.executable.endswith("xlang3.exe") or sys.executable.endswith("python.exe"))
result = subprocess.run(
    [sys.executable, "-c", "print(123)"],
    capture_output=True,
    text=True,
    timeout=10,
)
print("subprocess-run", result.returncode, result.stdout.strip(), result.stderr.strip())

# socket.py should be source-backed while delegating the low-level object to
# the native _socket dependency.
print("socket-source", socket.__file__.endswith("socket.py"))
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
print("socket-basics", sock.family == socket.AF_INET, sock.type == socket.SOCK_STREAM, sock.gettimeout())
sock.settimeout(0.25)
print("socket-timeout", sock.gettimeout())
sock.close()

# select is still minimal, but this covers the CPython-shaped empty readiness
# contract needed by source modules that probe it.
print("select-empty", select.select([], [], [], 0) == ([], [], []))
