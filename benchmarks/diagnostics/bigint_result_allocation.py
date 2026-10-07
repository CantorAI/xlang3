"""Diagnostic result construction/destruction; includes Python dispatch costs.

Small nonzero results must become bigints; cancellation results compact to
Int64 and provide a control that does not allocate a bigint object header.
"""
import json
import sys
import time


def run():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 50000
    rows = []
    for bits in (64, 128, 1024, 4096):
        left = (1 << bits) + 37
        equal = (1 << bits) + 37
        for operation in ('add-one', 'multiply-one', 'cancel'):
            expected = (1 << bits) + 38 if operation == 'add-one' else left if operation == 'multiply-one' else 0
            samples = []
            for _ in range(5):
                start = time.perf_counter()
                if operation == 'add-one':
                    for _ in range(loops):
                        value = left + 1
                elif operation == 'multiply-one':
                    for _ in range(loops):
                        value = left * 1
                else:
                    for _ in range(loops):
                        value = left - equal
                samples.append(time.perf_counter() - start)
                assert value == expected
            rows.append({'case': operation + '-' + str(bits), 'loops': loops,
                         'seconds': samples, 'median_seconds': sorted(samples)[2]})
    retained = []
    for i in range(1000):
        retained.append((1 << 128) + i)
    for i in range(1000):
        assert retained[i] == (1 << 128) + i
    retained.clear()
    print(json.dumps({'purpose': 'diagnostic only; includes Python loop/assignment/dispatch', 'rows': rows}))


if __name__ == '__main__':
    run()
