# Check each operation against power-of-two identities, not its inverse.
count = 0
for bits in (65, 128, 1024, 4096):
    positive_a = (1 << bits) + 37
    positive_b = (1 << bits) + 11
    positive_sum = (1 << (bits + 1)) + 48
    for a_sign in (1, -1):
        for b_sign in (1, -1):
            a, b = positive_a * a_sign, positive_b * b_sign
            retained_a, retained_b = a, b
            saved_a, saved_b = str(a), str(b)
            expected_sum = positive_sum * a_sign if a_sign == b_sign else 26 * a_sign
            expected_difference = 26 * a_sign if a_sign == b_sign else positive_sum * a_sign
            assert a + b == expected_sum
            assert a - b == expected_difference
            expected_greater = a_sign > b_sign if a_sign != b_sign else a_sign > 0
            assert (a > b) == expected_greater and (a >= b) == expected_greater
            assert (a < b) == (not expected_greater) and (a <= b) == (not expected_greater)
            assert a != b and not (a == b)
            equal = ((1 << bits) + 37) * a_sign
            assert a == equal and not (a != equal)
            assert a <= equal and a >= equal and not (a < equal) and not (a > equal)
            assert a + 0 == a and 0 + a == a and a - 0 == a and 0 - a == -a
            assert str(retained_a) == saved_a and str(retained_b) == saved_b
            count += 1
    chain = (1 << bits) - 1
    if bits % 4 == 0:
        assert chain == int('f' * (bits // 4), 16)
    assert chain + 1 == 1 << bits
    assert (1 << bits) - 1 == chain
assert (1 << 63) - 1 == 9223372036854775807
assert (-(1 << 63)) - 1 == -9223372036854775809
assert (1 << 63) + (-1) == 9223372036854775807
assert (1 << 128) + True == (1 << 128) + 1
assert (1 << 128) - False == 1 << 128
print('signed add subtract compare and retained inputs', count)
