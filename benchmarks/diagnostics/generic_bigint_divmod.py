"""Check signed division and measure generic operators, including loop overhead.

These are diagnostic samples, not official pyperformance scores. Construct
expected results using known positive quotient/remainder identities, then apply
Python's floor/sign rule independently of the division being tested.
"""
import json
import sys
import time


def run():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    cases = []
    for bits in (65, 128, 1024, 4096):
        divisor = (1 << bits) + (1 << (bits // 2)) + 37
        for qbits in (0, 3, 32, 64):
            quotient = 0 if qbits == 0 else (1 << qbits) - 1
            for remainder in (0, 17, divisor - 1):
                dividend = quotient * divisor + remainder
                for left_sign in (1, -1):
                    for right_sign in (1, -1):
                        expected_q = quotient
                        expected_r = remainder * right_sign
                        if left_sign != right_sign:
                            expected_q = -quotient
                            if remainder:
                                expected_q -= 1
                                expected_r = (divisor - remainder) * right_sign
                        left, right = dividend * left_sign, divisor * right_sign
                        saved_left, saved_right = str(left), str(right)
                        actual_q, actual_r = divmod(left, right)
                        assert (actual_q, actual_r) == (expected_q, expected_r)
                        assert left // right == expected_q and left % right == expected_r
                        assert str(left) == saved_left and str(right) == saved_right
        # A small quotient with a large divisor resembles pidigits extraction.
        cases.append(('small-quotient-' + str(bits), divisor * 7 + 17, divisor, 7, 17))
        cases.append(('wide-quotient-' + str(bits), divisor * ((1 << 64) - 1) + 17,
                      divisor, (1 << 64) - 1, 17))
    boundaries = [(0, -3, 0, 0), (True, True, 1, 0)]
    skip_boundary = '--skip-known-boundaries' in sys.argv
    if not skip_boundary:
        boundaries.append((-(1 << 63), -1, 1 << 63, 0))
    for left, right, q, r in boundaries:
        assert divmod(left, right) == (q, r)
        assert left // right == q and left % right == r
    if not skip_boundary:
        for left in (17, 1 << 128):
            try:
                divmod(left, 0)
            except ZeroDivisionError:
                pass
            else:
                raise AssertionError('division by zero must fail')
    rows = []
    for name, left, right, q, r in cases:
        for operation in ('floor', 'divmod'):
            samples = []
            for _ in range(100):
                left // right
            for _ in range(5):
                start = time.perf_counter()
                if operation == 'floor':
                    for _ in range(loops):
                        result = left // right
                    assert result == q
                else:
                    for _ in range(loops):
                        result = divmod(left, right)
                    assert result == (q, r)
                samples.append(time.perf_counter() - start)
            rows.append({'case': name + '-' + operation, 'loops': loops,
                         'seconds': samples, 'median_seconds': sorted(samples)[2]})
    print(json.dumps({'correctness_signed_cases': 192,
                      'int64_overflow_boundary_checked': not skip_boundary,
                      'purpose': 'diagnostic only; includes loop and call overhead', 'rows': rows}))


if __name__ == '__main__':
    run()
