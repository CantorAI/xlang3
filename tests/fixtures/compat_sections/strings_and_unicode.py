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
decoded_replace = b"a\xffb".decode("ascii", "replace")
print("éx".encode("ascii", "ignore"), "éx".encode("ascii", "replace"), len(decoded_replace), ord(decoded_replace[1]))

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
readonly_mv = memoryview(b"abc")
print(readonly_mv == b"abc", readonly_mv == bytearray(b"abc"), hash(readonly_mv) == hash(b"abc"))
print({readonly_mv: "ok"}[b"abc"])
try:
    hash(mv)
except TypeError:
    print("memoryview-unhashable")
with memoryview(b"xy") as released_mv:
    print(released_mv.tobytes())
try:
    released_mv.tobytes()
except Exception:
    print("memoryview-released")

# Unicode database module foundation: names, categories, numeric values, and normalization.
import unicodedata
acute = chr(0x0301)
print(unicodedata.name("é"), unicodedata.lookup("latin small letter e with acute") == "é")
print(unicodedata.category("中"), unicodedata.bidirectional("🙂"), unicodedata.east_asian_width("中"))
print(unicodedata.combining(acute), unicodedata.decimal("7"), unicodedata.digit("²"), unicodedata.numeric("¾"))
print(ord(unicodedata.normalize("NFC", "e" + acute)), unicodedata.is_normalized("NFD", "e" + acute))
print(ord(unicodedata.normalize("NFD", "Å")[0]), ord(unicodedata.normalize("NFD", "Å")[1]))
fraction = unicodedata.normalize("NFKD", "¾")
print(unicodedata.normalize("NFKC", "²"), ord(fraction[0]), ord(fraction[1]), ord(fraction[2]))

# Unicode decomposition and lookup aliases for table-backed compatibility.
angstrom = "\u212b"
roman_four = "\u2163"
print(unicodedata.decomposition("é"), unicodedata.decomposition("¾"), unicodedata.decomposition(roman_four))
print(unicodedata.lookup("LF") == "\n", unicodedata.lookup("LINE FEED") == "\n")
print(unicodedata.name("\n", "control"), unicodedata.category("\n"), unicodedata.bidirectional("\n"))
print(unicodedata.name(angstrom), ord(unicodedata.normalize("NFC", angstrom)), unicodedata.normalize("NFKC", roman_four))
try:
    unicodedata.lookup("NO SUCH")
except KeyError:
    print("unicode-lookup-error")

# Codec registry foundation: normalized lookup and callable CodecInfo paths.
import codecs
ascii_info = codecs.lookup("ASCII")
utf8_info = codecs.lookup("utf-8")
print(ascii_info.name, codecs.encode("éx", "ascii", "ignore"), codecs.decode(b"a\xffb", "ascii", "ignore"))
print(ascii_info.encode("éx", "replace"), utf8_info.decode("Hi".encode("utf-8")))
latin = "é中".encode("latin-1", "replace")
sig = "ok".encode("utf-8-sig")
latin_text = latin.decode("latin_1")
print(latin, len(latin_text), ord(latin_text[0]), latin_text[1], len(sig), sig.decode("utf-8-sig"))
print(codecs.lookup("cp65001").name, codecs.lookup("iso-8859-1").name)
print(codecs.getencoder("latin-1")("é中", "replace"), codecs.getdecoder("utf-8-sig")(sig))
print(codecs.lookup_error("replace") is not None)
def my_errors(exc):
    return ("?", 1)
codecs.register_error("xlang3_test", my_errors)
print(codecs.lookup_error("xlang3-test") is my_errors)
