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
"""Root-only prerequisite reproducer; no SQLAlchemy, profiling or timing.

Run separately with `python` or `operator` as the sole argument. A failure
stops at its first failing stage. The getter-type row is observed metadata,
not an expected CPython/XLang3 identity match. No execution is claimed.
"""
import operator
import sys


class PythonGetter:
    def __call__(self, receiver):
        return receiver.columns


assert len(sys.argv) == 2 and sys.argv[1] in ("python", "operator")
getter = PythonGetter() if sys.argv[1] == "python" else operator.attrgetter("columns")


class Columns:
    columns = ("id", "name")
    c = property(getter)


def ordinary_read(receiver):
    return receiver.c


receiver = Columns()
descriptor = vars(Columns)["c"]
expected = ("id", "name")
print("getter-kind", sys.argv[1], type(getter).__module__, type(getter).__name__, callable(getter))
assert descriptor.fget is getter and callable(getter)
assert getter(receiver) == expected
print("PASS direct-getter")
assert descriptor.__get__(receiver, Columns) == expected
print("PASS explicit-property-get")
assert getattr(receiver, "c") == expected
print("PASS builtin-getattr")
assert ordinary_read(receiver) == expected
print("PASS ordinary-attribute")
