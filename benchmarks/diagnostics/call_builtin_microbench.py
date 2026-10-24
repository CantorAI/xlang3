import statistics
import time
VALUE = 1
CALLS = 10000
REPEATS = 9
CASES = (("type_call", lambda: type(VALUE)), ("class_attr", lambda: VALUE.__class__), ("id_builtin", lambda: id(VALUE)), ("len_builtin", lambda: len((1,))), ("hash_builtin", lambda: hash(VALUE)))
for name, fn in CASES:
    fn()
    samples = []
    for _ in range(REPEATS):
        start = time.perf_counter()
        for _ in range(CALLS): fn()
        samples.append(time.perf_counter() - start)
    m=statistics.median(samples)
    print(f"{name} us/call={m*1e6/CALLS:.3f} median={m:.6f}s")
