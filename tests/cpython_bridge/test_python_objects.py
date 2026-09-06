import gc
import sys
import weakref
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, sys.argv[1])
import xlang3

module = xlang3.importModule("bridge_fixture", fromPath=sys.argv[2])

class PythonObject:
    def __init__(self):
        self.count = 2

    def __call__(self, value):
        return self.count + value

obj = PythonObject()
assert module.echo(obj) is obj
assert module.mutate_python(obj) == 7
assert obj.count == 7 and obj.extra == "from XLang3"
assert isinstance(module.construct_python(PythonObject), PythonObject)
module.delete_python_attr(obj)
assert not hasattr(obj, "extra")

mapping = {"original": 41}
assert module.echo(mapping) is mapping
assert module.mutate_container(mapping) == 42
assert mapping["changed"] == 42
module.delete_python_item(mapping, "changed")
assert "changed" not in mapping
assert module.python_truth([]) is False
assert module.python_truth([1]) is True
assert module.python_boolean_ops([])[0] is False
assert module.python_boolean_ops([])[1] is True
assert module.python_boolean_ops([1])[2] == 17
assert module.python_boolean_ops([])[3] == 23
class BadTruth:
    def __bool__(self):
        raise ValueError("truth failure")
assert module.catch_python_truth(BadTruth()) == "caught truth error"
cycle = []
cycle.append(cycle)
assert module.echo(cycle) is cycle
before = sys.getrefcount(cycle)
for _ in range(100):
    assert module.first_python(cycle) is cycle
assert sys.getrefcount(cycle) == before, "alias results leaked Python references"
assert module.python_len(cycle) == 1
assert module.iterate_python([1, 2, 3]) == 6
assert module.iterate_python(iter([4, 5, 6])) == 15

def callback(value, *, offset):
    return module.add(value, right=offset)

assert module.echo(callback) is callback
assert module.invoke_python(callback, 39) == 42

def threaded_callback(value, *, offset):
    with ThreadPoolExecutor(max_workers=1) as pool:
        return pool.submit(lambda: module.add(value, right=offset)).result(timeout=5)

assert module.invoke_python(threaded_callback, 39) == 42

def failure():
    raise ValueError("original Python callback failure")

assert module.catch_python_error(failure) == "caught ValueError"
try:
    module.invoke_python(lambda *args, **kwargs: failure(), 1)
except RuntimeError as error:
    assert "original Python callback failure" in str(error), str(error)
else:
    raise AssertionError("callback exception was swallowed")
assert module.invoke_python(callback, 39) == 42

reference = weakref.ref(obj)
module.save_python(obj)
del obj
gc.collect()
assert reference() is not None
assert module.call_saved(3) == 10
module.release_python()
gc.collect()
assert reference() is None, "runtime must release retained Python objects"

def worker(index):
    for _ in range(30):
        assert module.invoke_python(callback, index) == index + 3

with ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(worker, range(4)))

print("Live CPython objects: identity, mutation, cycles, iteration, callbacks, reentry, retention PASS")
