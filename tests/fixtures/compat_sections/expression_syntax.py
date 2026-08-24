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

# Names and scalar literals.
name = "XLang3"
i = 7
f = 2.5
print(name, i, f, None, True, False)

# String, raw string, escapes, bytes, and triple-quote tokenizer edge cases.
print("A\nB".split("\n")[0], "\x41\u0042\U00000043")
print(r"a\nb", R"c\td")
print("'''", '"""')
print("""a'''b""", '''c"""d''')
print("a\"b", 'c\'d')
print(b"ABC"[1], b"BC" in b"ABC", 66 in b"ABC")

# F-strings: expressions, escaped braces, conversion, debug fields, and format specs.
value = 5
print(f"{name}:{value + 1}")
print(f"{{{name!s}}}", f"{'abc'!r}", f"{7:03d}", f"{3.14159:.2f}")
print(f"{value=}", f"{7:+d}", f"{15:#x}", "[" + f"{5:>4d}" + "]", "[" + f"{5:<4d}" + "]", "[" + f"{5:^5d}" + "]")
print(f"{3.5:.1%}", f"{name:.3s}", f"{7:{3}d}")

# Unary, binary, power, bitwise, shifts, and invert.
print(+i, -i, not False)
print(7 + 3, 7 - 3, 7 * 3, 7 / 2, 7 % 3, 7 // 3)
print(2 ** 5, 2 ** 3 ** 2)
print(6 & 3, 6 | 3, 6 ^ 3, 1 << 5, 32 >> 2, ~5)

# Comparisons, chained comparisons, identity, membership, and booleans.
items = [1, 2, 3]
same = items
other = [1, 2, 3]
print(1 < 2 <= 2, 3 != 4, items is same, items is not other)
print(2 in items, 4 not in items, "ell" in "hello", "z" not in "hello")
print(True and "yes", False or "fallback")

# Conditional expression.
print("left" if i > 3 else "right")

# Calls: positional, keyword, star args, and kwargs.
def combine(a, b=0, *rest, c=0, **kw):
    total = a + b + c + kw["z"]
    for item in rest:
        total = total + item
    return total

more = (4, 5)
named = {"z": 6}
print(combine(1, 2, *more, c=3, **named))

# Attribute, subscript, slices, and extended slices.
class Box:
    value = 10

seq = [0, 1, 2, 3, 4, 5]
print(Box.value, seq[2], seq[1:4], seq[0:6:2])

# Tuple/list unpacking and starred expression unpacking.
a, b = (8, 9)
head, *middle, tail = [1, 2, 3, 4]
print(a, b, head, middle, tail)
print([0, *middle, 5], (*middle, tail))

# Tuple, list, dict, and set literals.
print((1, 2), [3, 4], {"a": 5}["a"], 6 in {6, 7})

# Comprehensions and generator expressions.
print([x * 2 for x in range(4) if x > 1])
print([[x, y] for x in range(2) for y in range(2) if x != y])
print({x: x + 1 for x in range(3)}[2], 2 in {x for x in range(3)})
print(sum(x for x in range(5)))

# Walrus operator.
print((n := 12), n)
