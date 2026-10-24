"""Isolate generic native dictionary calls and Python super forwarding.

Diagnostic totals include loop and function-call overhead; not pyperf scores.
"""
import json
import statistics
import sys
import time


class Scope(dict):
    def get(self, key, default=None):
        return super().get(key, default)


scope = Scope(present=42)
marker = object()
bound_super_get = super(Scope, scope).get


def direct_get(key, default):
    return dict.get(scope, key, default)


def explicit_super_get(key, default):
    return super(Scope, scope).get(key, default)


def run():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    assert loops > 0
    cases = [('native bound dict.get', dict(present=42).get),
             ('native bound super.get', bound_super_get),
             ('Python direct dict.get wrapper', direct_get),
             ('Python zero-argument super.get', scope.get),
             ('Python explicit super.get', explicit_super_get)]
    rows = []
    for name, function in cases:
        assert function('present', marker) == 42
        assert function('missing', marker) is marker
        for _ in range(1000):
            function('present', marker)
        samples = []
        for _ in range(5):
            start = time.perf_counter()
            for _ in range(loops):
                function('present', marker)
            samples.append(time.perf_counter() - start)
        rows.append({'case': name, 'loops': loops, 'seconds': samples,
                     'median_seconds': statistics.median(samples)})
    print(json.dumps({'purpose': 'diagnostic only, loop/call overhead included', 'rows': rows}))


if __name__ == '__main__':
    run()
