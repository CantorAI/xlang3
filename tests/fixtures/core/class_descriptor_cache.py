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
def read_dict_get():
    return dict.get


def read_object_getattribute():
    return object.__getattribute__


for _ in range(20):
    read_dict_get()
    read_object_getattribute()
print("builtin-descriptors", read_dict_get() is dict.get,
      read_object_getattribute() is object.__getattribute__)


class Meta(type):
    @property
    def token(cls):
        return "metaclass"


class Example(metaclass=Meta):
    token = "class"

    def method(self):
        return "old"

    @staticmethod
    def static_method():
        return "static"

    @classmethod
    def class_method(cls):
        return cls.__name__


class Descriptor:
    def __get__(self, instance, owner):
        return self if instance is None else "bound"


class CustomDescriptor:
    value = Descriptor()


def read_method():
    return Example.method


def read_token():
    return Example.token


for _ in range(20):
    read_method()
    read_token()
print("class-lookup", read_method()(None), read_token(),
      Example.static_method(), Example.class_method(),
      CustomDescriptor.value is CustomDescriptor.__dict__["value"])


def replacement(self):
    return "new"


old_method = read_method()
Example.method = replacement
print("class-write-invalidates", old_method is not read_method(),
      read_method()(None))
