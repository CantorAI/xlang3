import gc
import sys
import weakref

sys.path.insert(0, sys.argv[1])
import xlang3

class Holder:
    pass

module = xlang3.importModule("bridge_fixture", fromPath=sys.argv[2])
holder = Holder()
holder.back = module.Counter()
reference = weakref.ref(holder)
module.save_python(holder)
del holder
gc.collect()
assert reference() is not None, "XLang3 module still owns the Python object"

# Python holder -> XLang3 proxy -> runtime -> Python holder, with no outside roots.
del module
del sys.modules["xlang3"]
del xlang3
for _ in range(3):
    gc.collect()
assert reference() is None, "cross-engine cycle kept the runtime and Python object alive"

# A new module instance must not use stale objects or the old runtime context.
import xlang3
module = xlang3.importModule("bridge_fixture", fromPath=sys.argv[2])
assert module.add(40, right=2) == 42
print("CPython bridge: cross-engine cycle collection and fresh import PASS")
