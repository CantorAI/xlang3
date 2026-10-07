"""Diagnostic arithmetic samples; includes loop/assignment and dispatch costs.

Expected results use independent power-of-two identities. Equal operands are
constructed separately so equality cannot win merely from object identity.
"""
import json
import sys
import time


def run():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 50000
    rows = []
    for bits in (128, 1024, 4096):
        left = (1 << bits) + 37
        right = (1 << bits) + 11
        equal = (1 << bits) + 37
        cases = [('add-small', left, 129, (1 << bits) + 166),
                 ('add-big', left, right, (1 << (bits + 1)) + 48),
                 ('add-opposite', left, -right, 26),
                 ('subtract-small', left, 129, (1 << bits) - 92),
                 ('subtract-big', left, right, 26),
                 ('subtract-opposite', left, -right, (1 << (bits + 1)) + 48),
                 ('compare-small', left, 129, True),
                 ('compare-big', left, right, True),
                 ('compare-equal', left, equal, True)]
        for name, a, b, expected in cases:
            saved_a, saved_b = str(a), str(b)
            samples = []
            for _ in range(5):
                start = time.perf_counter()
                if name.startswith('add'):
                    for _ in range(loops):
                        result = a + b
                elif name.startswith('subtract'):
                    for _ in range(loops):
                        result = a - b
                elif name == 'compare-equal':
                    for _ in range(loops):
                        result = a == b
                else:
                    for _ in range(loops):
                        result = a > b
                samples.append(time.perf_counter() - start)
                assert result == expected, name
                assert str(a) == saved_a and str(b) == saved_b, name
            rows.append({'case': name + '-' + str(bits), 'loops': loops,
                         'seconds': samples, 'median_seconds': sorted(samples)[2]})
    print(json.dumps({'purpose': 'diagnostic only; loop/assignment and dispatch included', 'rows': rows}))


if __name__ == '__main__':
    run()
