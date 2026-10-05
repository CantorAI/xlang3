# Flat integer-key index for built-in dicts (2026-10-04)

## Result

The exact-dict integer index now uses a flat open-addressed table instead of
`std::unordered_map<int64_t, size_t>`. Dict entries remain the source of truth;
the index stores entry number plus one, with zero marking an empty slot. This
avoids a separately allocated hash node for each indexed integer key and makes
membership and item lookup probe contiguous storage.

On the official pyperformance 1.14.0 pure-Python pickle cases, the change
produced a **significant 1.02x improvement** for `unpickle_pure_python` and no
significant change for `pickle_pure_python`:

| Benchmark | Control | Flat index | CPython 3.14.7 | Flat index vs CPython |
|---|---:|---:|---:|---:|
| `pickle_pure_python` | 5.56 ms ± 0.48 ms | 5.49 ms ± 0.43 ms | 274 µs | 20.07x slower |
| `unpickle_pure_python` | 2.45 ms ± 0.17 ms | 2.39 ms ± 0.10 ms | 205 µs | 11.64x slower |

`pyperf compare_to` hid `pickle_pure_python` as not significant and reported
`unpickle_pure_python` as 1.02x faster. Both runs warned about sample
stability, so the gain is modest. Keep the shared runtime improvement because
the measured unpickle result supports it, while continuing to investigate the
much larger CPython gap.

This follows the pure-Python library rule: `pickle.py` remains Python. The
change is to XLang3's general built-in dict index, not a C++ replacement for
the Pickler implementation.

## Correctness and reproduction

The focused `dict_integer_index_getitem`, `dict_get_missing_semantics`,
`pickle_module`, `io_module_streams`, `math_module`, and
`collections_queue_modules` fixtures passed under CPython 3.14.7's fixture
runner. The `xlang3_runtime_value_tests` and `xlang3_interpreter_tests` CTest
groups also passed.

Both pyperformance runs used CPython **3.14.7**, the same pyperformance 1.14.0
dependency site, and `--rigorous` mode. The fixed executable path remained
`build-repro/Release/xlang3.exe`.

- [Control rigorous pyperf JSON](data/dict-flat-int-index-control-rigorous-20261004.json)
- [Candidate rigorous pyperf JSON](data/dict-flat-int-index-candidate-rigorous-20261004.json)
- [CPython 3.14.7 full-suite reference](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)
- [Pickler membership timing diagnosis](pickle-writer-contains-profile-20261004.md)

Build hashes:

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control | `368E78D4791265512630B9AD1D839F4A9F22C07E3ACBF7915244DE778569F3B5` | `9A5C0DF0395E21503132BAED8CB24A5F453AAC9336FAE62C07F38672E3FDDF09` |
| Candidate | `0BEE56B3872EB85BABDB78E2545E2E2FF1A3D9DEC09E9279D1200D749A437777` | `98E49CE9FBA27366CC8A18ECC9864EAB9889B522B3DBCEDA92F9C6000EB5C345` |
