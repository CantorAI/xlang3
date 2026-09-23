# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.

# HTTPX Headers.__setitem__ uses this nested target in its real Python source.
headers = [(b"A", b"a", b"one"), (b"B", b"b", b"two"), (b"A", b"a", b"three")]
idx = "outer-index"
item_key = "outer-key"
print([idx for idx, (_, item_key, _) in enumerate(headers) if item_key == b"a"])
print(idx, item_key)

rows = [(1, [2, 3]), (4, [5, 6])]
print([a + b + c for a, [b, c] in rows])
print({a: b + c for (a, (b, c)) in rows})
print(sorted({a + b + c for [a, [b, c]] in rows}))
print(list(a + b + c for a, (b, c) in rows))
print([a + b + c for a in [10, 20] for b, (c,) in [(1, [2]), (3, [4])] if a > 10])
print([(a, rest) for a, (b, *rest) in [(1, [2, 3, 4]), (5, [6])]])

# Parentheses group a target; a comma creates a tuple, including a singleton.
print([a for (a) in [7, 8]])
print([a for ((a)) in [7, 8]])
print([a for a, in [(7,), (8,)]])
print([a for (a,) in [(7,), (8,)]])
print([a for [a] in [(7,), (8,)]])
for (a) in [9]:
    print(a)
for a, in [(10,)]:
    print(a)

try:
    [a for a, (b, c) in [(1, [2])]]
except ValueError:
    print("nested unpack error")

async def source():
    for row in rows:
        yield row

async def collect():
    return [a + b + c async for a, (b, c) in source()]

coroutine = collect()
try:
    coroutine.send(None)
except StopIteration as result:
    print(result.value)
