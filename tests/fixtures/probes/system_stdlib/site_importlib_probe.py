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
import pkgutil
import runpy
import site

# These public modules should come from CPython's Lib sources while XLang3
# provides the runtime/import primitives underneath.
print("importlib-source", importlib.__file__.endswith("__init__.py"))
print("pkgutil-source", pkgutil.__file__.endswith("pkgutil.py"))
print("runpy-source", runpy.__file__.endswith("runpy.py"))
print("site-source", site.__file__.endswith("site.py"))
print("import-module", importlib.import_module("math").__name__)
print("pkgutil-api", hasattr(pkgutil, "iter_modules"), hasattr(pkgutil, "get_data"))

try:
    importlib.import_module("xlang3_missing_site_probe_module")
except ImportError as exc:
    print("import-error-name", exc.name)
