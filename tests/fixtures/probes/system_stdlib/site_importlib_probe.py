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
import importlib.resources
import importlib.util
import os
import pkgutil
import runpy
import site
import sys

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

# Source loaders should read and execute a plain .py file through importlib.
loader_path = "xlang3_site_importlib_loader.py"
with open(loader_path, "w") as loader_file:
    loader_file.write("VALUE = 41 + 1\n")
loader = importlib.machinery.SourceFileLoader("xlang3_site_loader_mod", loader_path)
spec = importlib.util.spec_from_file_location("xlang3_site_loader_mod", loader_path, loader=loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)
print("loader-exec", loader.get_filename("xlang3_site_loader_mod") == loader_path, len(loader.get_data(loader_path)) > 0, module.VALUE)
os.remove(loader_path)

# runpy should execute modules and paths using the CPython source implementation.
sys.path.insert(0, "tests/fixtures/core")
module_ns = runpy.run_module("runpy_support")
run_path_file = "xlang3_site_runpy_path.py"
with open(run_path_file, "w") as run_file:
    run_file.write("x = 7\ny = x + 8\n")
path_ns = runpy.run_path(run_path_file, run_name="xlang3_run_path")
print("runpy", module_ns["result"], path_ns["__name__"], path_ns["y"])
os.remove(run_path_file)

# pkgutil/importlib.resources should see package data through normal import roots.
sys.path.insert(0, "tests/fixtures/compat_sections")
import resource_pkg

resource = pkgutil.get_data("resource_pkg", "data.txt")
print("resources", len(resource) > 0, importlib.resources.is_resource(resource_pkg, "data.txt"), importlib.resources.read_text(resource_pkg, "data.txt").strip())
