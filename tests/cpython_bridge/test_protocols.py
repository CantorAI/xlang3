import sys
sys.path.insert(0, sys.argv[1])
import xlang3
module = xlang3.importModule("protocol_fixture", fromPath=sys.argv[2])
values = module.values
assert list(values) == [1, 2, 3]
assert list(iter(values)) == [1, 2, 3]
assert list(values[1:]) == [2, 3]
values[1] = 17
assert values[1] == 17
values[1:3] = xlang3.importModule("protocol_fixture").values[:1]
assert list(values) == [1, 1]
del values[0]
assert list(values) == [1]
assert values == values
assert bool(values)
assert 1 in values and 2 not in values
assert list(module.generate()) == [3, 4]
iterator = iter(module.generate())
assert next(iterator) == 3
assert next(iterator) == 4
assert next(iterator, 77) == 77
mapping = module.mapping
mapping["c"] = 3
del mapping["a"]
assert sorted(mapping) == ["b", "c"]
try:
    mapping["missing"]
except KeyError:
    pass
else:
    raise AssertionError("missing key must raise KeyError")
obj = module.Object()
assert len(obj) == 2 and not obj
assert obj[4] == 7
obj[4] = 5
assert obj.value == 9
del obj[0]
assert obj.value == 0
obj.extra = 17
del obj.extra
assert not hasattr(obj, "extra")
assert repr(obj) == "BridgeObject" and str(obj) == "bridge object"
print("Object protocols: iteration, generators, slices, mutation, deletion, truth, repr and errors PASS")
