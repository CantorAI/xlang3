# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");

import struct


def outcome(label, fn):
    try:
        value = fn()
        print(label, type(value).__name__, value)
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))


# Python str classification follows Unicode code points.  It is independent of
# the process locale, and Letter_Number (Nl) characters are numeric/alphanumeric
# without being alphabetic.
unicode_cases = [
    "é", "中", "🙂", "²", "¾", "\u0661", "\u2003", "Straße",
    "İ", "Σς", "变量", "a\u0301", "\u0301a", "Ⅳ", "ⅳ",
]
for text in unicode_cases:
    print(
        ascii(text), ascii(text.lower()), ascii(text.upper()), ascii(text.casefold()),
        text.isalpha(), text.isdigit(), text.isdecimal(), text.isnumeric(),
        text.isalnum(), text.isspace(), text.isidentifier(),
        text.islower(), text.isupper(),
    )


# Native one-character memoryview formats decode with the signedness and scalar
# type defined by CPython's buffer protocol.
for fmt, raw in [
    ("b", b"\xff"), ("B", b"\xff"),
    ("h", b"\xfe\xff"), ("H", b"\xfe\xff"),
    ("i", b"\xfe\xff\xff\xff"), ("I", b"\xfe\xff\xff\xff"),
    ("q", b"\xfe\xff\xff\xff\xff\xff\xff\xff"),
    ("Q", b"\xfe\xff\xff\xff\xff\xff\xff\xff"),
    ("f", struct.pack("f", 1.5)), ("d", struct.pack("d", 1.5)),
    ("c", b"A"),
]:
    outcome("cast-" + fmt, lambda fmt=fmt, raw=raw: memoryview(raw).cast(fmt)[0])


owner = bytearray(b"abcdef")
base_view = memoryview(owner)
view = base_view.cast("B", shape=[2, 3])
readonly = view.toreadonly()
print("meta", view.ndim, view.shape, view.strides, view.nbytes, len(view))
print("owners", view.obj is owner, readonly.obj is owner, readonly.readonly)
outcome("tuple-index", lambda: view[1, 2])
outcome("tolist", lambda: view.tolist())
stepped = memoryview(b"abcdef")[::2]
print("stepped", stepped.tolist(), stepped.tobytes(), stepped.strides, stepped.contiguous)


exporter = bytearray(b"abcd")
export = memoryview(exporter)
outcome("resize-active", lambda: exporter.append(101))
export.release()
outcome("resize-released", lambda: exporter.append(101))
export.release()
for label, fn in [
    ("released-len", lambda: len(export)),
    ("released-readonly", lambda: export.readonly),
    ("released-obj", lambda: export.obj),
    ("released-tobytes", lambda: export.tobytes()),
    ("released-hash", lambda: hash(export)),
    ("released-enter", lambda: export.__enter__()),
]:
    outcome(label, fn)


for fmt in ["", "Z", "2B", "<H"]:
    outcome("bad-cast-" + repr(fmt), lambda fmt=fmt: memoryview(b"abcd").cast(fmt))

writable = memoryview(bytearray(b"\x00\x00\x00\x00")).cast("h")
outcome("assign-h", lambda: writable.__setitem__(0, -2))
print("assigned-bytes", writable.tobytes())
