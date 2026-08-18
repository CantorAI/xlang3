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

class Child(Base):
    pass

obj = object()
child = Child()

print(type(1) is int)
print(type(True) is bool, isinstance(True, int))
print(type("abc") is str)
print(type([1, 2]) is list)
print(type({"a": 1}) is dict)
print(type({1, 2}) is set)
print(type((1, 2)) is tuple)
print(type(type) is type, isinstance(type, type))
print(type(object) is type, isinstance(obj, object))
print(type(child) is Child)
print(isinstance(child, Child), isinstance(child, Base), isinstance(child, object))
print(issubclass(Child, Base), issubclass(Child, object), issubclass(bool, int))
print(isinstance(1, (str, int)))
print(type(child).__name__, Child.__base__.__name__)
print(id(child) == id(child), id(Child) == id(Child))
print(str(123), int("42"), bool([]), bool([1]))
print(list((1, 2)), tuple([3, 4]), set([1, 1, 2]))
print(list(range(3)))
