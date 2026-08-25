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

# Basic string storage, indexing, slicing, concatenation, and repetition.
text = "  Alpha,beta,Alpha  "
print(text[2], text[2:7], "A" + "B", "ha" * 2)

# ASCII case, stripping, finding, partitioning, and replacement methods.
print(text.strip(), text.lstrip(), text.rstrip())
print(text.find("Alpha"), text.rfind("Alpha"), text.index("beta"), text.rindex("Alpha"))
print(text.partition(","), text.rpartition(","))
print(text.replace("Alpha", "A", 1), text.count("Alpha"))

# Prefix/suffix tuples and ASCII classification methods.
word = "XLang3"
print(word.startswith(("XL", "Py")), word.endswith(("3", "4")))
print("abc".isalpha(), "123".isdigit(), "abc123".isalnum(), "   ".isspace())
print("abc".islower(), "ABC".isupper(), "123".islower())
print("abc".isascii(), "é".isascii(), "123".isdecimal(), "123".isnumeric())

# Additional string transforms, padding, prefix/suffix removal, lines, and tabs.
print("hello world".title(), "Hello World".istitle(), "AbC".swapcase(), "AbC".casefold())
print("x".center(5, "-"), "x".ljust(3, "."), "x".rjust(3, "."), "-42".zfill(5))
print("prefix-name".removeprefix("prefix-"), "name.py".removesuffix(".py"))
print("a\tb".expandtabs(4), "a\nb\r\nc".splitlines(), "a\nb\r\nc".splitlines(True))
print("a,b,c".rsplit(","))

# Formatting, joining, splitting, and UTF-8/ascii encode-decode basics.
print("{}:{name}".format("id", name=7))
print("{1}-{0}-{name}".format("zero", "one", name="n"))
print("|".join(["a", "b", "c"]), "a,,b".split(","))
print("Hi".encode("utf-8").decode("utf-8"), "ASCII".encode("ascii").decode("ascii"))
try:
    b"\xff".decode("ascii")
except UnicodeDecodeError:
    print("ascii-decode-error")

# Tokenizer/literal audit: raw strings, bytes escapes, adjacent literals, comments, escaped quotes, and f-strings.
raw_path = r"C:\temp\next"
triple_after_expr = ("prefix:" + """line1
line2""")
quote_heavy = "he said \"'''\" and left"  # triple marker inside normal string
adjacent = "left" "right" r"\raw"
byte_escapes = b"A\n\x42"
name = "XL"
print(raw_path, len(raw_path))
print(triple_after_expr.split("\n")[0], triple_after_expr.split("\n")[1])
print(quote_heavy, adjacent, byte_escapes)
print(f"{name!r}:{3 + 4}:{name=}")

# Unicode audit: UTF-8 strings use code point length/index/slice for scalar access.
unicode_text = "é中🙂"
print(len(unicode_text), ord(unicode_text[0]), ord(unicode_text[1]), ord(unicode_text[2]))
print(len(unicode_text[:2]), len(unicode_text[1:]), ord(unicode_text[-1]))

# Index methods raise catchable ValueError on misses.
try:
    "abc".index("z")
except ValueError:
    print("index-miss")

try:
    "abc".rindex("z")
except ValueError:
    print("rindex-miss")

# Bytes, bytearray, and memoryview basics.
b = b"a,b,c"
print(b.startswith(b"a"), b.endswith(b"c"), b.partition(b","), b",".join(b.split(b",")))
data = b"  ababa  "
print(data.count(b"aba"), data.find(b"ba"), data.rfind(b"ba"), data.index(b"ab"), data.hex())
print(data.strip(), data.lstrip(), data.rstrip(), data.replace(b"ab", b"X", 1))
print(data.startswith((b"  a", b"zz")), data.endswith((b"a  ", b"zz")), data.rpartition(b"b"))
try:
    data.index(b"zz")
except ValueError:
    print("bytes-index-miss")

ba = bytearray(b"ab")
ba.append(99)
ba.extend(b"de")
print(bytes(ba))
print(ba.startswith(b"a"), ba.endswith(b"e"), ba.count(b"b"), ba.find(b"cd"), ba.hex(), ba.decode())
print(ba.partition(b"c"), ba.rpartition(b"c"), ba.replace(b"cd", b"XY"))
ba2 = ba.copy()
print(bytes(ba2))
print(ba.pop(), bytes(ba))
ba.remove(98)
ba.reverse()
print(bytes(ba))
ba.clear()
print(len(ba))
mv_source = bytearray(b"abcde")
mv = memoryview(mv_source)
print(mv.tobytes(), mv[2])
print(mv.readonly, mv.nbytes, mv.itemsize, mv.format, mv.ndim, mv.shape, mv.strides, mv.suboffsets)
print(mv.tolist(), mv.obj is mv_source)
