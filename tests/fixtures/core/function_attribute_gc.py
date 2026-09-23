import gc
import weakref


def make_cycle():
    def callback():
        return None

    callback.owner = callback
    return callback


callback = make_cycle()
reference = weakref.ref(callback)
print(reference() is callback)
del callback
for _ in range(3):
    if reference() is None:
        break
    gc.collect()
print(reference() is None)
