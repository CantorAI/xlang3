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

class Descriptor:
    def __get__(self, obj, owner):
        if obj is None:
            return "class:" + owner.__name__
        return obj.storage + 1

    def __set__(self, obj, value):
        obj.storage = value * 2

    def __delete__(self, obj):
        obj.storage = 0


class Box:
    value = Descriptor()

    def __init__(self):
        self.storage = 3


box = Box()
print(box.value)
box.value = 5
print(box.storage, box.value)
print(Box.value)
del box.value
print(box.storage)


class NonData:
    def __get__(self, obj, owner):
        return "descriptor"


class Holder:
    item = NonData()

    def __init__(self):
        self.item = "instance"


holder = Holder()
print(holder.item)
del holder.item
print(holder.item)
