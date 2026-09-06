import gc
import importlib
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, sys.argv[1])
import xlang3

assert importlib.import_module("xlang3") is xlang3
module = xlang3.importModule("bridge_fixture", fromPath=sys.argv[2])
assert xlang3.importModule("bridge_fixture") is module
assert "bridge_fixture" not in sys.modules, "fixture must execute in XLang3"
for value in (None, True, False, -123, -(2**63), 2**63 - 1, 1.25, "", "a\0b", "\u4e2d\U0001f680", b"", b"a\0b", b"x" * (1024 * 1024 + 17)):
    result = module.echo(value)
    assert type(result) is type(value), (type(result), type(value))
    assert result == value
assert module.add(4, right=7) == 11
counter = module.Counter(count=3)
assert counter.add(4) == 7
counter.count = 20
assert counter.count == 20
assert module.echo(counter) is counter
assert len(module.values) == 3
assert module.values[1] == 20
assert module.mapping["key"] == "value"
xlang3.importModule("xlang_json", fromPath=sys.argv[3])
json = xlang3.importModule("json", fromPath="xlang_json")
parsed = json.loads('{"key":[1,2,"native JSON"]}')
assert parsed["key"][2] == "native JSON"
assert json.loads(json.dumps(parsed))["key"][0] == 1
net = xlang3.importModule("xlang_net")
http = xlang3.importModule("http", fromPath="xlang_net")
assert net.http is http
server = http.Server()
assert isinstance(server.StaticIndexFile, str)
try:
    server.StaticIndexFile = "bridge.html"
except AttributeError as error:
    assert "can't set attribute" in str(error)
else:
    raise AssertionError("native read-only properties must reject writes")
for operation, error in (
    (lambda: module.echo(2**100), OverflowError),
    (lambda: module.echo(2**63), OverflowError),
    (lambda: module.echo(-(2**63) - 1), OverflowError),
    (lambda: xlang3.importModule("__missing_bridge_module__"), RuntimeError),
    (lambda: xlang3.importModule(""), ValueError),
    (lambda: module.fail(), RuntimeError),
    (lambda: xlang3.Object(), TypeError),
    (lambda: getattr(counter, "count\0suffix"), ValueError),
    (lambda: module.add(1, **{"right\0suffix": 4}), ValueError),
):
    try:
        operation()
    except error:
        pass
    else:
        raise AssertionError(f"expected {error.__name__}")
assert module.add(1) == 2

def worker(index):
    local = module.Counter(count=index)
    for _ in range(100):
        assert local.add(1) == local.count
    return local.count

with ThreadPoolExecutor(max_workers=8) as pool:
    assert list(pool.map(worker, range(8))) == list(range(100, 108))

del module
gc.collect()
assert counter.add(2) == 22
cpython = xlang3.importModule("cpython")
assert cpython.importModule("sys") is sys
print("CPython -> XLang3: source/native imports, calls, kwargs, properties, identity, binary, threads, errors PASS")
