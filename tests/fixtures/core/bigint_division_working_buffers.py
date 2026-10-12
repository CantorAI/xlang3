# Division scratch buffers must remain private even when inputs are retained.
count = 0
for bits in (65, 128, 256):
    for divisor in (1 << bits, (1 << bits) - 1, (1 << bits) + 37):
        for quotient in (0, 7, (1 << 32) - 1, (1 << 64) - 1):
            for remainder in (0, 17, divisor - 1):
                dividend = quotient * divisor + remainder
                for a_sign in (1, -1):
                    for b_sign in (1, -1):
                        a, b = dividend * a_sign, divisor * b_sign
                        retained_a, retained_b = a, b
                        saved_a, saved_b = str(a), str(b)
                        expected_q, expected_r = quotient, remainder * b_sign
                        if a_sign != b_sign:
                            expected_q = -quotient
                            if remainder:
                                expected_q -= 1
                                expected_r = (divisor - remainder) * b_sign
                        q, r = divmod(a, b)
                        assert (q, r) == (expected_q, expected_r)
                        assert a // b == q and a % b == r
                        assert str(retained_a) == saved_a and str(retained_b) == saved_b
                        count += 1

minimum = -(1 << 63)
assert divmod(minimum, -1) == (1 << 63, 0)
assert minimum // -1 == 1 << 63
assert minimum % -1 == 0
assert (-(1 << 63)) // -1 == 1 << 63
assert (-(1 << 63)) % -1 == 0
for numerator in (17, 1 << 128):
    for denominator in (0, False):
        try:
            divmod(numerator, denominator)
        except ZeroDivisionError:
            pass
        else:
            raise AssertionError('divmod must preserve arithmetic exception type')
print('signed division and retained inputs', count)
print('int64 promotion and zero errors ok')
