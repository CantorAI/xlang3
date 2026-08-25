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

# Formatting, joining, splitting, and UTF-8/ascii encode-decode basics.
print("{}:{name}".format("id", name=7))
print("|".join(["a", "b", "c"]), "a,,b".split(","))
print("Hi".encode("utf-8").decode("utf-8"), "ASCII".encode("ascii").decode("ascii"))

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
ba = bytearray(b"ab")
ba.append(99)
ba.extend(b"de")
mv = memoryview(ba)
print(bytes(ba), mv.tobytes(), mv[2])
