# Nested named-tuple hashes in set lookup (2026-10-10)

The `set_nested_namedtuple_hash` fixture exposed a disagreement between set
insertion and lookup. For equal tuples containing distinct named-tuple
instances, insertion used `runtime_value_hash_key()` recursively and
deduplicated the values, while `in` and `set.__contains__()` used
`value_hash_key()` for the query tuple. That non-runtime helper hashed the
nested named-tuple instance by identity, so lookup searched a different hash
bucket and incorrectly returned false.

Both runtime set lookup paths now use the same recursive hash helper as set
insertion. The method path is shared by `contains`, `remove`, and `discard`.
The class-body loop fixture remains a separate compiler limitation and is not
part of this fix.

Validation used CPython **3.14.7** at `C:\Python\Python314`. The focused
XLang3 fixtures for positional-call invalidation, closure-frame reuse, native
coverage tracing, heap comparison caching, native callback slots, nested
named-tuple set hashes, and multiprocessing spawn all passed. A direct
membership/discard check produced `True` then set length `0` on both runtimes.
No performance gain is claimed for this correctness repair.

Source comments beside the shared hash calls preserve the invariant: set
insertion, lookup, and removal must hash recursive values through the same
runtime-aware path so user-defined `__hash__` behavior participates at every
tuple level.
