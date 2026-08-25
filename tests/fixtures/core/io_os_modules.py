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

import _io
import os

path = "xlang3_io_os.tmp"

with _io.open(path, mode="w", encoding="utf-8") as f:
    print(f.write("hello"))

with open(path, encoding="utf-8") as f:
    print(f.read())

info = os.stat(path)
print(info[6])
print(path in os.listdir("."))
scan_seen = False
scan_size = -1
scan_path = ""
for entry in os.scandir("."):
    if entry.name == path:
        scan_seen = isinstance(entry, os.DirEntry) and entry.is_file() and not entry.is_dir()
        scan_size = entry.stat().st_size
        scan_path = entry.path
print(scan_seen, scan_size, len(scan_path) > 0)
print(os.fspath(path))
print(os.getenv("__XLANG3_MISSING_ENV__", "fallback"))
os.remove(path)
print(path in os.listdir("."))
