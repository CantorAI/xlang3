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

import _imp
import _stat
import os

path = "xlang3_imp_stat.tmp"
with open(path, "w") as f:
    f.write("abc")

info = os.stat(path)
mode = info[_stat.ST_MODE]
print((mode & _stat.S_IFMT) == _stat.S_IFREG)
print(info[_stat.ST_SIZE])
print(_imp.is_builtin("sys"))
print(_imp.is_builtin("definitely_missing"))
print(_imp.is_frozen("sys"))
print(len(_imp.get_magic()))
suffixes = _imp.extension_suffixes()
print(len(suffixes) > 0)
print(_imp.lock_held())
_imp.acquire_lock()
print(_imp.lock_held())
_imp.release_lock()
print(_imp.lock_held())
os.remove(path)
