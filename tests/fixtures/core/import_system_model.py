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
import importlib.abc
import importlib.machinery
import importlib.util
import sys
import os
import zipimport
import _frozen_importlib
import _frozen_importlib_external

print(sys.__name__, sys.__spec__ is None)
print("sys" in sys.modules, "_builtins" in sys.modules)

import math

print("math" in sys.modules, sys.modules["math"].__name__)

import ns_pkg.module

ns_spec = importlib.util.find_spec("ns_pkg")
mod_spec = importlib.util.find_spec("ns_pkg.module")
print(ns_pkg.__name__, ns_pkg.__file__ is None, len(ns_pkg.__path__) > 0)
print(ns_pkg.module.VALUE, ns_pkg.module.__package__, ns_spec.name, mod_spec.parent)
print(importlib.import_module("ns_pkg.module") is ns_pkg.module)

# importlib loader/finder facade: common classes and SourceFileLoader basics exist for libraries that inspect import protocols.
loader = importlib.machinery.SourceFileLoader("demo_loader", __file__)
file_spec = importlib.util.spec_from_file_location("demo_loader", __file__, loader)
module_from_spec = importlib.util.module_from_spec(file_spec)
print(importlib.abc.Loader.__name__, importlib.machinery.SourceFileLoader.__name__)
print(loader.name, loader.get_filename("demo_loader") == __file__, loader.create_module(file_spec) is None)
print(len(loader.get_data(__file__)) > 0, loader.exec_module(module_from_spec) is None)
print(file_spec.loader is loader, file_spec.origin == __file__, module_from_spec.__name__)
print(importlib.machinery.PathFinder.find_spec("missing") is None)
print(importlib.machinery.SOURCE_SUFFIXES[0], importlib.machinery.BYTECODE_SUFFIXES[0])

# zipimport/frozen bootstrap facade: protocol objects are importable for compatibility probes.
zip_loader = zipimport.zipimporter(__file__)
print(zip_loader.archive == __file__, zip_loader.prefix == "", zip_loader.find_spec("missing") is None, zip_loader.is_package("missing"))
print(zip_loader.get_filename("pkg.mod").endswith("pkg.mod.py"), len(zip_loader.get_data(__file__)) > 0)
print(zipimport.ZipImportError.__name__, isinstance(zipimport._zip_directory_cache, dict))
print(_frozen_importlib.__name__, _frozen_importlib.FrozenImporter.__name__)
print(_frozen_importlib_external.__name__, _frozen_importlib_external.SourceFileLoader.__name__)

stored_zip = bytes([80, 75, 3, 4, 20, 0, 0, 0, 0, 0, 173, 144, 24, 93, 12, 145, 88, 248, 8, 0, 0, 0, 8, 0, 0, 0, 9, 0, 0, 0, 104, 101, 108, 108, 111, 46, 116, 120, 116, 122, 105, 112, 45, 100, 97, 116, 97, 80, 75, 1, 2, 20, 0, 20, 0, 0, 0, 0, 0, 173, 144, 24, 93, 12, 145, 88, 248, 8, 0, 0, 0, 8, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 1, 0, 0, 0, 0, 104, 101, 108, 108, 111, 46, 116, 120, 116, 80, 75, 5, 6, 0, 0, 0, 0, 1, 0, 1, 0, 55, 0, 0, 0, 47, 0, 0, 0, 0, 0])
archive_path = "xlang3_zipimport_stored.zip"
with open(archive_path, "wb") as archive_file:
    archive_file.write(stored_zip)
stored_loader = zipimport.zipimporter(archive_path)
print(stored_loader.get_data(archive_path + "/hello.txt") == b"zip-data")
os.remove(archive_path)
