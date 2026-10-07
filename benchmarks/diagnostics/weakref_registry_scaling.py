"""Diagnose native weakref registry scaling; not an official pyperf score."""
import json
import sys
import time
import weakref


class Holder:
    pass


def run():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
    assert loops > 0
    rows = []
    for size in (0, 100, 1000, 5000):
        targets = [Holder() for _ in range(size)]
        refs = [weakref.ref(target) for target in targets]
        probe = Holder()
        probe_ref = weakref.ref(probe)
        for _ in range(1000):
            assert weakref.ref(probe) is probe_ref
            assert probe_ref() is probe
        start = time.perf_counter()
        for _ in range(loops):
            result = weakref.ref(probe)
        create_seconds = time.perf_counter() - start
        assert result is probe_ref
        start = time.perf_counter()
        for _ in range(loops):
            result = probe_ref()
        deref_seconds = time.perf_counter() - start
        assert result is probe
        rows.append({"unrelated_live_refs": size, "loops": loops,
                     "create_seconds": create_seconds,
                     "deref_seconds": deref_seconds})
        del result, probe_ref, probe, refs, targets
    print(json.dumps({"purpose": "diagnostic scaling only; includes loop overhead",
                      "rows": rows}))


if __name__ == "__main__":
    run()
