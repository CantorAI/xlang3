# Lazy function qualified-name trial (2026-09-30)

I tested another generic function-object allocation reduction: leave the
default qualified name in immutable IR and copy it into each function object
only if Python assigns `__qualname__`. The candidate updated the attribute,
generator, `typing` fast-path, and snapshot readers to resolve that name
without changing Python's visible metadata. The fixture suite passed.

Official pyperformance 1.14.0 `json_dumps --rigorous` measured **39.0 ±0.5 ms**
for the candidate and **39.1 ±0.4 ms** for the immediate control.
`pyperf compare_to` rounds the result to **1.00× faster**, which is not a
material or reliable improvement. I removed the change and rebuilt the
accepted runtime. The preserved [candidate](data/json-lazy-qualname-candidate-rigorous-20260930.json)
and [control](data/json-lazy-qualname-control-rigorous-20260930.json) raw files
show the result; this optimization should not be repeated without a new
implementation or materially different evidence.

This was a generic XLang3 function metadata experiment. It did not implement
`json`, `json.encoder`, or any other CPython pure-Python library in C++.
