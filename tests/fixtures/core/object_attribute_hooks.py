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

class Base:
    pass


def hello(self):
    return self.x + 1


Dyn = type("Dyn", (Base,), {"answer": 7, "hello": hello})
d = Dyn()
d.x = 4
print(Dyn.__name__, Dyn.__base__.__name__, isinstance(d, Base), d.answer, d.hello())


class Fallback:
    def __getattr__(self, name):
        return "missing:" + name


print(Fallback().abc)


class Override:
    def __init__(self):
        self.value = 12

    def __getattribute__(self, name):
        if name == "special":
            return 99
        return object.__getattribute__(self, name)


override = Override()
print(override.special, override.value)


class Setter:
    def __setattr__(self, name, value):
        object.__setattr__(self, "seen_" + name, value + 1)


setter = Setter()
setter.x = 5
print(setter.seen_x)


class Deleter:
    def __init__(self):
        self.x = 3

    def __delattr__(self, name):
        object.__setattr__(self, "deleted", name)
        object.__delattr__(self, name)


deleter = Deleter()
del deleter.x
print(deleter.deleted)


def annotated(x: "int", y=8) -> "int":
    return x + y


print(annotated.__name__, annotated.__defaults__, annotated.__annotations__["x"])
print(d.hello.__self__ is d, d.hello.__func__.__name__)
