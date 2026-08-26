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

# Generic truth and callable helpers.
print(all([1, True, "x"]), all([1, 0, 2]), any([0, "", "y"]))
print(callable(lambda x: x), callable(42))

# type supports normal inspection and dynamic class construction.
Dynamic = type("Dynamic", (object,), {"kind": "dynamic"})
print(type(Dynamic).__name__, Dynamic.kind, Dynamic().kind)

# Numeric conversion/display helpers.
print(bin(10), oct(10), hex(255), bin(-3))
print(pow(2, 5), pow(2, 5, 7), divmod(17, 5))
print(int("ff", 16), int("0b101", 0), int(b"77", 8), int(bytearray(b"11"), 2))
print(float("2.5"), float(3))
print(round(2.6), round(2.25, 1), abs(-9), sum([1, 2, 3]), min(3, 1, 2), max([3, 1, 2]))

# Core collection/binary constructors.
print(list((1, 2)), tuple([1, 2]), set([1, 1, 2]))
print(dict([("a", 1)])["a"], len(bytes(3)), bytes([65, 66]), len(bytearray(3)), bytearray(b"hi"))
print(dict(a=1, b=2)["b"], dict([("a", 1)], b=2)["b"])
print(bytes("hi", "utf-8"), bytes("hi", encoding="utf-8"))
print(bytearray("hi", "utf-8"), bytearray("hi", encoding="utf-8"))
print(len(memoryview(b"abc")))
print(str(b"hi", "utf-8"), str(bytearray(b"ok"), encoding="utf-8"))

# Character and ordinal helpers.
print(ord("A"), chr(65), chr(0x2603) == "\u2603")
try:
    chr(0x110000)
except ValueError:
    print("chr-range")

# Hashing uses the shared hashability/equality policy.
print(hash(1) == hash(True), hash((1, "a")) == hash((1, "a")))
try:
    hash([])
except TypeError:
    print("hash-unhashable")

# repr/format remain available as builtin functions.
print(repr("x"), format(255, "#x"), format("hi", ">4"))

# Reflection helpers cover module/class/instance namespaces.
class BuiltinProbe:
    kind = "probe"

    def __init__(self):
        self.value = 42

probe = BuiltinProbe()
print("kind" in dir(BuiltinProbe), "value" in dir(probe))
print(vars(BuiltinProbe)["kind"], vars(probe)["value"])

# open supports VFS-backed text/binary forms and context-manager use.
with open("xlang3_builtins_text.tmp", "w", encoding="utf-8") as f:
    f.write("hello")
with open("xlang3_builtins_text.tmp", "r", encoding="utf-8") as f:
    print(f.read())
with open("xlang3_builtins_bin.tmp", "wb") as f:
    f.write(b"xy")
with open("xlang3_builtins_bin.tmp", "rb") as f:
    print(f.read())

# Iterator and dynamic execution helpers.
print(next(iter([7])), next(iter([]), "done"))
print(list(enumerate(["a", "b"], 3)))
print(list(zip([1, 2], ["a", "b", "c"])))
print(list(map(lambda x: x + 1, [1, 2, 3])))
print(list(filter(lambda x: x > 1, [0, 2, 3])))

# User iterator protocol works across builtins and constructors.
class CountingIter:
    def __init__(self, stop):
        self.i = 0
        self.stop = stop

    def __iter__(self):
        return self

    def __next__(self):
        self.i = self.i + 1
        if self.i > self.stop:
            raise StopIteration()
        return self.i

class PairIter:
    def __init__(self, stop):
        self.i = 0
        self.stop = stop

    def __iter__(self):
        return self

    def __next__(self):
        self.i = self.i + 1
        if self.i > self.stop:
            raise StopIteration()
        return (self.i, self.i * 10)

class TextIter:
    def __init__(self, stop):
        self.i = 0
        self.stop = stop

    def __iter__(self):
        return self

    def __next__(self):
        self.i = self.i + 1
        if self.i > self.stop:
            raise StopIteration()
        return "s" + str(self.i)

class ByteIter:
    def __init__(self, stop):
        self.i = 0
        self.stop = stop

    def __iter__(self):
        return self

    def __next__(self):
        self.i = self.i + 1
        if self.i > self.stop:
            raise StopIteration()
        return bytes([64 + self.i])

def add3(a, b, c):
    return a + b + c

print(next(iter(CountingIter(1))))
print(list(CountingIter(3)), tuple(CountingIter(2)), set(CountingIter(2)) == {1, 2})
print(frozenset(CountingIter(2)) == {1, 2}, sum(CountingIter(3)), all(CountingIter(2)), any(CountingIter(0)))
print(list(zip(CountingIter(2), CountingIter(3))))
print(list(map(lambda x: x * 2, CountingIter(3))))
print(list(filter(lambda x: x > 1, CountingIter(3))))
print(sorted(CountingIter(3), reverse=True), min(CountingIter(3)), max(CountingIter(3)))
print(add3(*CountingIter(3)))
extended = [0]
extended.extend(CountingIter(3))
print(extended)
updated = {}
updated.update(CountingIter(0))
updated.update(PairIter(2))
print(updated[1], updated[2], dict.fromkeys(CountingIter(2), "v")[2])
base_set = {1}
base_set.update(CountingIter(3))
print(base_set == {1, 2, 3}, base_set.union(CountingIter(4)) == {1, 2, 3, 4})
print(base_set.intersection(CountingIter(2)) == {1, 2}, base_set.difference(CountingIter(2)) == {3})
print(base_set.symmetric_difference(CountingIter(2)) == {3}, base_set.isdisjoint(CountingIter(0)))
print(base_set.issuperset(CountingIter(2)), {1, 2}.issubset(CountingIter(3)))
print(",".join(TextIter(3)))
print(b"-".join(ByteIter(2)))
with open("xlang3_builtins_text.tmp", "w", encoding="utf-8") as f:
    f.writelines(TextIter(2))
with open("xlang3_builtins_text.tmp", "r", encoding="utf-8") as f:
    print(f.read())

code_eval = compile("1 + 2", "<builtins>", "eval")
code_exec = compile("builtins_exec_value = 9", "<builtins>", "exec")
print(eval(code_eval))
exec(code_exec)
print(builtins_exec_value)
print("builtins_exec_value" in globals(), "code_eval" in locals())
