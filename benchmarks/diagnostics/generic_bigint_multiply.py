"""Measure shared bigint arithmetic after the full-suite runs terminate.

Totals include Python loop and assignment overhead; these are diagnostic
samples, not official pyperformance scores. Expected values use shift/add
identities rather than the multiplication path under investigation.
"""
import json
import sys
import time


def run():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    assert loops > 0
    rows = []
    cases = [('int64-overflow', 1 << 62, 4, 1 << 64)]
    for bits in (128, 1024, 4096):
        left = (1 << bits) + 37
        right = (1 << bits) + 11
        square = (1 << (2 * bits)) + (74 << bits) + 1369
        product = (1 << (2 * bits)) + (48 << bits) + 407
        scaled = (left << 7) + left
        cases.extend([
            ('big-small-' + str(bits), left, 129, scaled),
            ('small-big-' + str(bits), 129, left, scaled),
            ('big-big-' + str(bits), left, right, product),
            ('square-' + str(bits), left, left, square),
            ('negative-' + str(bits), -left, 129, -scaled),
            ('zero-' + str(bits), left, 0, 0),
        ])
    for name, left, right, expected in cases:
        left_alias = left
        right_alias = right
        left_before = str(left)
        right_before = str(right)
        assert left * right == expected, name
        for _ in range(min(100, loops)):
            left * right
        samples = []
        for _ in range(5):
            start = time.perf_counter()
            for _ in range(loops):
                result = left * right
            samples.append(time.perf_counter() - start)
            assert result == expected, name
            assert left is left_alias and right is right_alias, name
            assert str(left) == left_before and str(right) == right_before, name
        rows.append({'case': name, 'loops': loops, 'seconds': samples,
                     'median_seconds': sorted(samples)[2],
                     'result_bit_length': result.bit_length(),
                     'result_modulo_97': result % 97})
    print(json.dumps({'purpose': 'diagnostic only; includes Python loop/assignment overhead',
                      'rows': rows}))


if __name__ == '__main__':
    run()
