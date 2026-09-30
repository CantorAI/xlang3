"""Diagnostic weakref lifetime probe, not a passing fixture or benchmark.

The preserved XLang3 parent and preliminary cleanup variant both kept this
owner alive, while CPython released it. It does not isolate an optimization
regression; investigate globals snapshots/ownership separately.
"""
import weakref


class Target:
    def __call__(self):
        return 73


def probe():
    target = Target()
    reference = weakref.ref(target)
    namespace = {"target": target}
    exec("def invoke():\n    global target\n    result = target()\n    del target\n    return result", namespace)
    del target
    assert namespace["invoke"]() == 73
    return reference


print("mapping-owner-released", probe()() is None)
