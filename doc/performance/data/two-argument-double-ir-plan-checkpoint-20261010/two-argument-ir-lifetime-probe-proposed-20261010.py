# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
#
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
"""Unregistered CP-first lifetime probe; no instrumentation or timing claim."""
import sys

events = []


class Components:
    def __init__(self, label, a, b, c):
        self.label = label
        self.a, self.b, self.c = a, b, c

    def ready(self):
        return self

    def combine(self, other):
        other.ready()
        return self.a * other.a + self.b * other.b + self.c * other.c

    def __del__(self):
        caller = sys._getframe(1)
        events.append((self.label, caller.f_code.co_name,
                       caller.f_locals.get('result', 'missing')))
        del caller


def invoke_temporary():
    result = -999.0
    result = Components('self', 1.0, 2.0, 3.0).combine(
        Components('other', 4.0, 5.0, 6.0))
    assert result == 32.0
    # CALL retires temporary operands before the caller stores its result.
    assert sorted(events) == [('other', 'invoke_temporary', -999.0),
                              ('self', 'invoke_temporary', -999.0)], events


invoke_temporary()
print('PASS temporary inputs retire in caller before assignment')
events.clear()


def invoke_alias():
    holder = Components('alias', 1.0, 2.0, 3.0)
    result = holder.combine(holder)
    assert result == 14.0 and events == []
    del holder
    assert events == [('alias', 'invoke_alias', 14.0)], events


invoke_alias()
print('PASS aliased receiver and argument retire once')
events.clear()


def invoke_replacement(first, second):
    result = Components('displaced', 0.0, 0.0, 0.0)
    result = first.combine(second)
    assert result == 32.0
    # STORE_FAST publishes the replacement before retiring its old owner.
    assert events == [('displaced', 'invoke_replacement', 32.0)], events


# These owners survive the caller so only its old output retires in the check.
first = Components('retained self', 1.0, 2.0, 3.0)
second = Components('retained other', 4.0, 5.0, 6.0)
invoke_replacement(first, second)
print('PASS displaced output sees published arithmetic result')
del first
del second
