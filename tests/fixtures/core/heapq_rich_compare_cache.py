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

import heapq
import gc
import weakref


reflected = []


class Parent:
    def __gt__(self, other):
        reflected.append("parent-gt")
        return True


class NewItem:
    def __lt__(self, other):
        reflected.append("new-lt")
        return NotImplemented


heapq.heappush([Parent()], NewItem())
print("heapq-not-implemented-reflected", reflected)


shadow_calls = []


class Shadowed:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        shadow_calls.append("class")
        return self.value < other.value


shadow_parent = Shadowed(2)
shadow_item = Shadowed(1)
shadow_item.__lt__ = lambda other: shadow_calls.append("instance") or False
heapq.heappush([shadow_parent], shadow_item)
print("heapq-special-lookup-ignores-instance", shadow_calls)


inherited_calls = []


class Base:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        inherited_calls.append("base-old")
        return self.value < other.value


class Derived(Base):
    pass


heapq.heappush([Derived(2)], Derived(1))
old_base_method = weakref.ref(Base.__lt__)


def replacement_lt(self, other):
    inherited_calls.append("base-new")
    return self.value < other.value


Base.__lt__ = replacement_lt
gc.collect()
print("heapq-cache-does-not-own-method", old_base_method() is None)
heapq.heappush([Derived(2)], Derived(1))
print("heapq-inherited-method-mutation", inherited_calls)


class DeletedMethod:
    def __lt__(self, other):
        return True


heapq.heappush([DeletedMethod()], DeletedMethod())
del DeletedMethod.__lt__
try:
    heapq.heappush([DeletedMethod()], DeletedMethod())
except TypeError:
    print("heapq-method-deletion-invalidates-cache", True)
else:
    print("heapq-method-deletion-invalidates-cache", False)


mutation_calls = []


class Mutating:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        mutation_calls.append("old")
        if len(mutation_calls) == 1:
            Mutating.__lt__ = replacement_mutating_lt
        return self.value < other.value


def replacement_mutating_lt(self, other):
    mutation_calls.append("new")
    return self.value < other.value


heapq.heapify([Mutating(value) for value in (7, 3, 6, 1, 5, 2, 4, 0)])
print("heapq-method-mutation-during-operation", mutation_calls.count("old"), mutation_calls.count("new"))


descriptor_calls = []


class CompareDescriptor:
    def __get__(self, instance, owner):
        descriptor_calls.append("get")
        return lambda other: True


class DescriptorCompared:
    __lt__ = CompareDescriptor()


heapq.heappush([DescriptorCompared()], DescriptorCompared())
print("heapq-descriptor-fallback", descriptor_calls)
