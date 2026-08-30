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

import _abc
import _frozen_importlib
import abc
import importlib.machinery
import sys


print(
    "abc-module-metadata",
    _abc.__package__ == "",
    repr(_abc.__loader__) == "<class '_frozen_importlib.BuiltinImporter'>",
    _abc.__spec__.name == "_abc",
    _abc.__spec__.loader is _abc.__loader__,
    _abc.__spec__.origin == "built-in",
    _abc.__spec__.has_location is False,
    hasattr(_abc, "__file__") is False,
)

print(
    "abc-public-module-metadata",
    abc.__package__ == "",
    repr(abc.__loader__) == "<class '_frozen_importlib.FrozenImporter'>",
    abc.__spec__.name == "abc",
    abc.__spec__.loader is abc.__loader__,
    abc.__spec__.origin == "frozen",
    abc.__spec__.has_location is False,
    hasattr(abc, "__file__") is True,
)

print(
    "abc-bootstrap-loader-identity",
    sys.__loader__ is importlib.machinery.BuiltinImporter,
    _abc.__loader__ is importlib.machinery.BuiltinImporter,
    abc.__loader__ is importlib.machinery.FrozenImporter,
    _frozen_importlib.__loader__ is importlib.machinery.FrozenImporter,
)

HELPERS = (
    ("get_cache_token", "($module, /)", ("opaque object", "virtual subclasses")),
    ("_abc_init", "($module, self, /)", ("class set-up", "abc module")),
    ("_abc_register", "($module, self, subclass, /)", ("subclasss registration", "abc module")),
    ("_abc_instancecheck", "($module, self, instance, /)", ("instance checks", "abc module")),
    ("_abc_subclasscheck", "($module, self, subclass, /)", ("subclasss checks", "abc module")),
    ("_get_dump", "($module, self, /)", ("cache and registry debugging", "negative cache version")),
    ("_reset_registry", "($module, self, /)", ("reset registry", "refleak.py")),
    ("_reset_caches", "($module, self, /)", ("reset both caches", "refleak.py")),
)


for name, signature, doc_parts in HELPERS:
    helper = getattr(_abc, name)
    print(
        "abc-native-helper-metadata",
        name,
        helper.__name__ == name,
        helper.__qualname__ == name,
        helper.__module__ == "_abc",
        helper.__text_signature__ == signature,
        all(part in helper.__doc__ for part in doc_parts),
    )

print(
    "abc-public-cache-token-metadata",
    abc.get_cache_token.__name__ == "get_cache_token",
    abc.get_cache_token.__qualname__ == "get_cache_token",
    abc.get_cache_token.__module__ == "_abc",
    abc.get_cache_token.__text_signature__ == "($module, /)",
    "opaque object" in abc.get_cache_token.__doc__,
)
