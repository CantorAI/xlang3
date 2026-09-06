import math
import tensor as T


def check(value, shape, expected):
    assert value.shape == shape, (value.shape, shape)
    actual = value.tolist()
    assert len(actual) == len(expected)
    for i in range(len(actual)):
        assert abs(actual[i] - expected[i]) < 1e-10, (i, actual[i], expected[i])


# Independent scalar-loop oracle, including zero extents and non-square shapes.
cases = 0
for m in range(5):
    for k in range(5):
        for n in range(5):
            left = [((i * 7 + 3) % 11 - 5) * 0.25 for i in range(m * k)]
            right = [((i * 3 + 1) % 13 - 6) * 0.5 for i in range(k * n)]
            expected = []
            for i in range(m):
                for j in range(n):
                    total = 0.0
                    for p in range(k):
                        total += left[i * k + p] * right[p * n + j]
                    expected.append(total)
            a = T.tensor(left, shape=[m, k], dtype=T.float64)
            b = T.tensor(right, shape=[k, n], dtype=T.float64)
            check((a @ b).eval(), (m, n), expected)
            # Reversing the physical layout must not alter the result.
            bt = T.tensor([right[p * n + j] for j in range(n) for p in range(k)],
                          shape=[n, k], dtype=T.float64)
            bview = (bt * T.unary_op("permute", axes=[1, 0])).eval()
            check((a @ bview).eval(), (m, n), expected)
            cases += 2

for batch in range(1, 4):
    for rows in range(1, 4):
        for columns in range(1, 4):
            left = [i - 3 for i in range(batch * rows)]
            right = [j + 2 for j in range(columns)]
            a = T.tensor(left, shape=[batch, rows, 1], dtype=T.float64)
            b = T.tensor(right, shape=[1, 1, columns], dtype=T.float64)
            expected = [left[z * rows + i] * right[j]
                        for z in range(batch) for i in range(rows) for j in range(columns)]
            result = (a * b).eval()
            check(result, (batch, rows, columns), expected)
            totals = [sum(expected[(z * rows + i) * columns:(z * rows + i + 1) * columns])
                      for z in range(batch) for i in range(rows)]
            check((result * T.unary_op("sum", axis=-1)).eval(), (batch, rows), totals)
            cases += 2

a = T.tensor([-2.0, -0.5, 0.0, 0.5, 2.0], dtype=T.float64)
check((a * T.unary_op("exp")).eval(), (5,), [math.exp(x) for x in a.tolist()])
check((a * T.unary_op("relu")).eval(), (5,), [0, 0, 0, 0.5, 2])
check((T.tensor(3.0, dtype=T.float64) + a).eval(), (5,), [1, 2.5, 3, 3.5, 5])
check((T.tensor(3, dtype=T.int64) / 2).eval(), (), [1.5])
rank_limit = T.tensor([7], shape=[1] * 32, dtype=T.int64)
check((rank_limit * T.unary_op("permute", axes=list(range(31, -1, -1)))).eval(),
      (1,) * 32, [7])


@T.fusion(name="invalid_data_branch")
def data_branch(x):
    if x:
        return x + 1
    return x - 1


failed = False
try:
    data_branch(T.input("branch", shape=[1]))
except Exception:
    failed = True
assert failed, "symbolic truth must not silently select a branch"
print("tensor-cpu-reference-passed", cases + 5)
