# Integer-index probe in runtime `dict.get()` (2026-10-04)

## Result

The additional fast path in `mapping_get_item_runtime()` did not improve either
pure-Python pickle benchmark, so it was removed. The measured version skipped
runtime hash/equality work for successful integer-key lookups on an exact
built-in dict, while retaining the generic path on misses. This is not worth
keeping as an optimization: pyperf found no significant change in
`pickle_pure_python`, and `unpickle_pure_python` was 1.02x slower with the
candidate (also not significant).

Both runs used the official pyperformance 1.14.0 benchmark definitions in
`--rigorous` mode, the same XLang3 executable and CPython 3.14.7 benchmark
dependencies. The first candidate had the VM `GetItem` integer-index fast path;
the tested candidate added the `dict.get()`/runtime lookup fast path.

| Benchmark | First candidate | Added runtime fast path | CPython 3.14.7 | Candidate vs CPython |
|---|---:|---:|---:|---:|
| `pickle_pure_python` | 5.46 ms ± 0.29 ms | 5.48 ms ± 0.30 ms | 274 µs | 20.03x slower |
| `unpickle_pure_python` | 2.42 ms ± 0.14 ms | 2.46 ms ± 0.14 ms | 205 µs | 12.00x slower |

The CPython column is the saved full-suite Python 3.14.7 result and is a
cross-run reference; the A/B decision uses the two same-session XLang3 runs.
`pyperf compare_to` hid `pickle_pure_python` because the change was not
significant. It reported `unpickle_pure_python` as 1.02x slower, likewise not
significant. Both runs warned that sample variation remained above pyperf's
stability target. The small difference is noise, not evidence of a regression.

## Why this did not help

The optimization targeted integer probes used by `pickle._Pickler.save()`'s
memo lookup. Yet its total benchmark time did not move. The bottleneck is
therefore elsewhere in the pure-Python pickling path, or the lookup fast path
is not reached often enough to matter. Do not extend this optimization based
on the source-level presence of `memo.get(id(obj))`; first profile the actual
`pickle_pure_python` workload with an uninstrumented benchmark and VM-side
opcode accounting.

The earlier VM `GetItem` integer-index fast path remains separate: its
`unpickle_pure_python` result was 1.36x faster than the fixed Release control
with a significant pyperf comparison. The no-gain `dict.get()` extension was
removed, and the Release target rebuilt afterward.

## Reproduction evidence

- [First-candidate rigorous pyperf JSON](data/dict-runtime-int-index-first-candidate-rigorous-20261004.json)
- [Added-runtime-fast-path rigorous pyperf JSON](data/dict-runtime-int-index-candidate-rigorous-20261004.json)
- [CPython 3.14.7 full-suite pyperf JSON](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)

The tested executable SHA-256 was
`4A5B5323EEB81B6970D0058C2B5B18D273339088B9BC3B7659D6776FC814A820`; its
tested runtime DLL SHA-256 was
`58478C58E9171BFD14746B6AADE609D6A74CBFF32FB78ABC518C60B370E216BC`.
After removing the extension, the rebuilt runtime DLL SHA-256 was
`D90E104CB999B37653BBBA54465F76DB02EE4D15952EB059D044010D4F213936`.
The executable and DLL stayed at `build-repro/Release/`.

The four focused fixtures `dict_integer_index_getitem`,
`dict_get_missing_semantics`, `pickle_module`, and `io_module_streams` passed
with Python 3.14.7 driving the fixture runner.
