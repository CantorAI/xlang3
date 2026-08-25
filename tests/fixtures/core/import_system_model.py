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
