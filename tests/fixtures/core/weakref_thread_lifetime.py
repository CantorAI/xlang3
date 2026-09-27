import gc
import threading
import weakref


class Target:
    def __init__(self, number):
        self.number = number


shared = [None, None]
ready = threading.Event()
observed = threading.Event()
finished = threading.Event()
errors = []


def check_target(ref, expected):
    value = ref()
    if value is not None and value.number != expected:
        errors.append((expected, value.number))


def publish(number):
    value = Target(number)
    shared[0] = value
    shared[1] = weakref.ref(value)


def reader():
    for expected in range(200):
        ready.wait()
        ref = shared[1]
        observed.set()
        for _ in range(100):
            check_target(ref, expected)
        ready.clear()
        finished.set()


worker = threading.Thread(target=reader)
worker.start()
for number in range(200):
    publish(number)
    observed.clear()
    finished.clear()
    ready.set()
    observed.wait()
    shared[0] = None
    gc.collect()
    finished.wait()
    gc.collect()
    if shared[1]() is not None:
        errors.append(("live after collection", number))
worker.join()
assert not errors, errors
print("weakref lifetime ok")
