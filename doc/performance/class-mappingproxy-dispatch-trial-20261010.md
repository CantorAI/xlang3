# Class and instance dictionary dispatch trials — 2026-10-10

Two narrowly guarded runtime changes were tested against the slow
`inspect.getattr_static` path in `typing_runtime_protocols`. The first moved
instance/class `__dict__` handling ahead of the long object-kind dispatch in
`object_get_attr`. The second let class mapping-proxy string-key lookups skip
generic hashability validation. Both preserved the Python implementations of
`typing` and `inspect`.

The primitive profile showed that class-dictionary proxy creation and access
remain much slower than CPython. The early dispatch reduced one unpaired
10,000-call class-proxy getter sample from 11.01 ms to about 10.26 ms, but the
five-sample full static-lookup profile changed by only about 1–2%. An official
pyperformance 1.14 fast run measured **2.77 ± 0.06 ms** and
`pyperf compare_to` hid the difference against the preceding candidate as not
significant.

The class-mapping string-key fast path then changed cached-proxy `__getitem__`
from **6.92 ms** to **6.82 ms** per 10,000 diagnostic calls. The broader
`inspect.getattr_static` primitive moved from **209.51 ms** to **212.14 ms**;
the official benchmark measured **2.79 ± 0.13 ms**, again with no significant
difference. Both runtime changes were removed. The semantic fixtures for
ordinary, property-backed, and slotted instance `__dict__`, plus class
mapping-proxy value identity, remain in `object_attribute_hooks.py`.

Raw results are in [the early-dispatch profile](data/inspect-static-primitives-early-dict-dispatch-20261010.json),
[the class-mapping lookup profile](data/inspect-class-mappingproxy-string-get-20261010.json),
[the official early-dispatch sample](data/typing-runtime-protocols-early-dict-dispatch-xlang3-20261010.json),
and [the official class-mapping sample](data/typing-runtime-protocols-class-mappingproxy-string-get-xlang3-20261010.json).
This trial does not change the accepted full-suite result or establish a
performance win. The repeated Python-to-runtime operations remain the larger
cost, so future work should target a broader VM/runtime mechanism.
