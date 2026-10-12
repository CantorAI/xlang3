"""Measure a no-argument Python method returning a plain instance attribute."""

import statistics
import time


class PlainBox:
    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value


box = PlainBox(7)


def method_call(count):
    obj = box
    result = None
    for _ in range(count):
        result = obj.get()
    return result


def measure():
    method_call(1_000)
    samples = []
    for _ in range(7):
        start = time.perf_counter()
        result = method_call(100_000)
        samples.append(time.perf_counter() - start)
        if result != 7:
            raise AssertionError(result)
    print(f"plain_instance_getter: median={statistics.median(samples):.6f}s samples=" +
          ",".join(f"{sample:.6f}" for sample in samples))


measure()
