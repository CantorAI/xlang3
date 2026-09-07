import sys

sys.path.insert(0, sys.argv[1])
import xlang3

b = xlang3.importModule("builtins")
d = b.dict(answer=42)
assert len(d) == 1 and d["answer"] == 42
d["nested"] = b.dict()
d["nested"]["items"] = b.list()
d["nested"]["items"].append(7)
assert d["nested"]["items"][0] == 7
items = b.list(d)
assert list(items) == ["answer", "nested"]
assert tuple(b.tuple(items)) == ("answer", "nested")
assert set(b.set(items)) == {"answer", "nested"}
assert b.int("42") == 42
assert b.float("2.5") == 2.5
assert b.str() == ""
assert b.bool() is False
assert b.bytes(3) == b"\0\0\0"
assert list(b.range(1, 5, 2)) == [1, 3]
for constructor, args in ((b.list, (1, 2)), (b.dict, (1,)), (b.int, ("not-an-int",))):
    try:
        constructor(*args)
    except Exception:
        pass
    else:
        raise AssertionError("invalid builtin constructor arguments accepted")
print("CPython bridge builtin constructors: real containers and scalar values PASS")
