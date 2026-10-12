"""Measure fresh weakref creation separately from cached-ref reuse."""
import json
import sys
import time
import weakref


class Target:
    pass


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    if count <= 0:
        raise ValueError("count must be positive")

    # Keep distinct targets alive so each timed weakref.ref call must create a
    # new reference instead of finding the callback-free per-target cache.
    targets = [Target() for _ in range(count)]
    make_ref = weakref.ref
    references = []
    start = time.perf_counter()
    for target in targets:
        references.append(make_ref(target))
    create_seconds = time.perf_counter() - start
    assert len(references) == count
    assert all(reference() is target for reference, target in zip(references, targets))
    start = time.perf_counter()
    live = 0
    for reference in references:
        if reference() is not None:
            live += 1
    dereference_seconds = time.perf_counter() - start
    assert live == count
    print(json.dumps({"case": "fresh_weakref_create_and_dereference",
                      "count": count, "create_seconds": create_seconds,
                      "dereference_seconds": dereference_seconds}))


if __name__ == "__main__":
    main()
