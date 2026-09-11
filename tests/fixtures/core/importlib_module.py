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

import importlib
import importlib.machinery
import importlib.metadata
import importlib.resources
import importlib.util
import sys

math_mod = importlib.import_module("math")
print(math_mod.sqrt(81))

spec = importlib.util.find_spec("math")
print(spec.name)
print(spec.origin)

missing = importlib.util.find_spec("definitely_missing_importlib_probe")
print(missing)

print(importlib.invalidate_caches())

print(importlib.__file__.replace("/", "\\").endswith("importlib\\__init__.py"))
print(importlib.metadata.__file__.replace("/", "\\").endswith("importlib\\metadata\\__init__.py"))
print(importlib.resources.__file__.replace("/", "\\").endswith("importlib\\resources\\__init__.py"))

class ProbeFinder:
    called = False

    @staticmethod
    def find_spec(name, path=None, target=None):
        if name == "fixture_probe_module":
            return importlib.machinery.ModuleSpec(name, (name, path))
        return None

    @staticmethod
    def invalidate_caches():
        ProbeFinder.called = True

sys.meta_path.insert(0, ProbeFinder)
try:
    probe = importlib.util.find_spec("fixture_probe_module", ["fixture-path"])
    print(probe.name, probe.loader)
    importlib.invalidate_caches()
    print(ProbeFinder.called)
finally:
    sys.meta_path.remove(ProbeFinder)
