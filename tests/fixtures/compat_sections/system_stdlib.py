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

# CPython system stdlib source import probes. These must exercise the real
# Python files under C:/Python/Python314/Lib rather than public native facades.
import abc
import collections
import codecs
import encodings
import enum
import io
import types


def source_lib_module(module):
    path = module.__file__.replace("\\", "/")
    return path.endswith("/Lib/" + module.__name__ + ".py")


def source_lib_package(module):
    path = module.__file__.replace("\\", "/")
    return path.endswith("/Lib/" + module.__name__ + "/__init__.py")


print(
    "system-stdlib-abc",
    source_lib_module(abc),
    abc.ABCMeta.__module__ == "abc",
    abc.ABC.__module__ == "abc",
    abc.abstractmethod.__module__ == "abc",
)
print(
    "system-stdlib-types",
    source_lib_module(types),
    types.WrapperDescriptorType is type(object.__init__),
    types.MethodWrapperType is type(object().__str__),
    types.MethodDescriptorType is type(str.join),
    types.ModuleType is type(types),
)
print(
    "system-stdlib-collections",
    source_lib_package(collections),
    collections.deque.__module__ == "collections",
)
print(
    "system-stdlib-enum",
    source_lib_module(enum),
    enum.Enum.__module__ == "enum",
    enum.StrEnum.__module__ == "enum",
    [item.value for item in enum.FlagBoundary],
)
print(
    "system-stdlib-io-codecs",
    source_lib_module(io),
    source_lib_module(codecs),
    source_lib_package(encodings),
    codecs.lookup("utf-8").name,
    "x".encode("utf-8"),
    bytes([121]).decode("utf-8"),
)
