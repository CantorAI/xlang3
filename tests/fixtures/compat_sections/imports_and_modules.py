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

# Native module import, sys.modules, and default import metadata.
import importlib
import importlib.abc
import importlib.machinery
import importlib.util
import os
import sys
import zipfile

print(sys.__name__, sys.__spec__.name, sys.__spec__.origin)
print("sys" in sys.modules, "importlib" in sys.modules)

# Source module import and importlib.find_spec metadata.
import import_target

target_spec = importlib.util.find_spec("import_target")
print(import_target.VALUE, import_target.__name__, import_target.__package__)
print(target_spec.name, target_spec.origin.endswith("import_target.py"), target_spec.parent)
print(import_target.__spec__.name, import_target.__loader__.name)

# Package import, child binding, and relative import_module resolution.
import pkg.module

pkg_again = importlib.import_module(".module", "pkg")
print(pkg.__name__, pkg.__spec__.name, len(pkg.__path__) > 0)
print(pkg.module.VALUE, pkg_again is pkg.module, pkg.module.__package__)

# Loader/finder protocol classes and explicit SourceFileLoader execution.
loader_path = "xlang3_import_loader_exec.py"
with open(loader_path, "w") as f:
    f.write("ANSWER = 41 + 1\n")

loader = importlib.machinery.SourceFileLoader("loader_exec_mod", loader_path)
spec = importlib.util.spec_from_file_location("loader_exec_mod", loader_path, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)
print(importlib.abc.Loader.__name__, importlib.machinery.SourceFileLoader.__name__)
print(loader.name, loader.get_filename("loader_exec_mod"), spec.loader is loader)
print(module.__name__, module.__file__, module.ANSWER, module.__spec__.name)
print(importlib.machinery.PathFinder.find_spec("pkg.module").name)
os.remove(loader_path)

# Namespace/frozen bootstrap facade metadata.
import ns_pkg.module
import _frozen_importlib
import _frozen_importlib_external

ns_spec = importlib.util.find_spec("ns_pkg")
print(ns_pkg.__file__ is None, len(ns_pkg.__path__) > 0, ns_pkg.__spec__.name, ns_spec.loader.__class__.__name__)
print(ns_pkg.module.VALUE, ns_pkg.module.__spec__.parent)
print(_frozen_importlib.__spec__.name, _frozen_importlib_external.__spec__.name)

# Zip archive roots on sys.path can provide source modules.
zip_path = "xlang3_import_section.zip"
with zipfile.ZipFile(zip_path, "w") as zf:
    zf.writestr("zip_section_mod.py", "VALUE = 'zip-source'\n")
sys.path.insert(0, zip_path)
import zip_section_mod
print(zip_section_mod.VALUE, zip_section_mod.__spec__.origin.endswith("zip_section_mod.py"))
sys.path.pop(0)
os.remove(zip_path)
