assert b"OSError:2:missing:path".split(b":", 2) == [b"OSError", b"2", b"missing:path"]
assert b"a:b".split(b":", 0) == [b"a:b"]
assert b"  a  b  ".split(None, 0) == [b"a  b  "]
assert b"  a  b  ".split(None, 1) == [b"a", b"b  "]
assert b"a:b:".split(b":", -1) == [b"a", b"b", b""]
assert b"".split(b":", 2) == [b""]
assert b"  ".split(None, 0) == []
assert bytearray(b"a:b:c").split(b":", 1) == [bytearray(b"a"), bytearray(b"b:c")]
try:
    b"abc".split(b"", 0)
except ValueError:
    pass
else:
    raise AssertionError("empty bytes separator accepted")
print("bytes-split-passed")
