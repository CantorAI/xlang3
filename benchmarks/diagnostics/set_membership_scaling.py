"""Compare miss cost as an exact built-in set grows.

The generated classes keep hashing and equality on the same object-identity
path used by ``copy.deepcopy``'s ``_atomic_types`` membership check. Set
construction and class creation are outside the timed region.
"""

import time


SIZES = (4, 8, 16, 32, 64, 128)
REPEATS = 5
LOOPS = 5000


def main():
    for size in SIZES:
        members = {
            type("SetProbe%d" % index, (), {})
            for index in range(size)
        }
        missing = type("MissingSetProbe", (), {})
        samples = []
        for _ in range(REPEATS):
            started = time.perf_counter()
            for _ in range(LOOPS):
                missing in members
            samples.append(time.perf_counter() - started)
        median = sorted(samples)[len(samples) // 2]
        print("size=%d median_ns=%.1f" % (size, median * 1e9 / LOOPS))


main()
