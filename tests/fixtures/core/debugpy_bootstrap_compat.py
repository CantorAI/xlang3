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
import os
from collections import namedtuple
import fnmatch
import glob
import signal
from importlib.util import module_from_spec, spec_from_file_location

print(0x80, 0b1010, 0o77, 1_234)

values = {"main": 7}
alias = values["copy"] = values["main"]
print(alias, values["copy"])

items = [("a", 1), ("b", 2)]
print([(k, v) for k, v in items])

Point = namedtuple("Point", "x, y")
p = Point(3, 4)
print(p.x, p.y, Point._fields)

print(os.path.splitext("demo.test.py"))
print(os.path.splitdrive("C:/demo/test.py"))
print(fnmatch.fnmatch("alpha.py", "*.py"), glob.has_magic("*.py"))

spec = spec_from_file_location("__main__", "demo.py")
mod = module_from_spec(spec)
print(spec.name, mod.__name__)
print(signal.SIGINT)

import debugpy
import debugpy.server
import debugpy.server.api
import debugpy.server.cli
from _pydevd_bundle import pydevd_utils
from _pydevd_bundle.pydevd_breakpoints import ExceptionBreakpoint

print(debugpy.__version__ != "")
print(pydevd_utils.get_non_pydevd_threads())
print(ExceptionBreakpoint)
