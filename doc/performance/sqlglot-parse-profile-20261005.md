# SQLGlot parse profile — 2026-10-05

The current XLang3 Release build completes the official `sqlglot_v2_parse`
benchmark, but it remains a large interpreter-throughput gap. A fresh official
pyperformance 1.14 fast run measured **20.5 ± 0.3 ms**; `pyperf compare_to`
reports **20.26× slower** than the saved CPython 3.14.7 result of **1.01 ms**.
The fast run warns that it lacks enough samples for a stable result, so treat
the exact ratio as directional.

## Runtime evidence

The native profile used the official benchmark's `bench_parse` function,
repeated 500 times in one XLang3 process. The sampler records instruction
pointers; it is diagnostic only and does not provide timing attribution. Of
1,742 total samples, 1,433 landed in `xlang3_runtime.dll`. The most frequent
runtime symbols were:

| Symbol | Samples |
|---|---:|
| `Interpreter::run_function` | 82 |
| VM `load_attr` | 80 |
| `release(Value)` | 79 |
| `unordered_map<string, Value>::_Find_last` | 68 |
| `Value::operator=(const Value&)` | 61 |
| `std::_Fnv1a_append_bytes` | 61 |
| `object_get_attr` | 32 |

A separate 10-parse Release counter run recorded 124,359 `LoadAttr`, 216,293
`LoadLocalAttr`, 66,041 `CallMethod`, 43,681 `Call`, and 23,695
`CallLocalMethod` dispatches. It also recorded 72,238 BoundMethod object
lifecycle allocations and 72,052 final releases. BoundMethod storage uses an
object free list, so this count is wrapper churn, not 72,238 system-heap
allocations. The counters show a promising call/attribute path to investigate,
but do not by themselves prove which allocations correspond to callable method
loads or explain the entire slowdown.

The first follow-up tried caching the class's slot-name lookup at warmed
`CallMethod` sites. The fixed Release gate passed, but the official SQLGlot
run showed no demonstrated end-to-end improvement; details are in
[`callmethod-slot-lookup-cache-20261005.md`](callmethod-slot-lookup-cache-20261005.md).
Do not keep extending caches based on the IP sampler alone. First measure the
cost of instance-shadow checks, descriptor resolution, and wrapper creation at
the actual callsites; preserve dynamic overrides, descriptor semantics, and
method identity in any later fast path. SQLGlot remains pure Python and should
not be replaced with C++.

## Reproduction artifacts

- Official XLang3 result:
  [`pyperformance-xlang3-sqlglot-parse-current-fast-20261005.json`](data/pyperformance-xlang3-sqlglot-parse-current-fast-20261005.json).
- Native samples:
  [`sqlglot-parse-native-samples-repeat500-20261005.json`](data/sqlglot-parse-native-samples-repeat500-20261005.json).
- Release opcode and object counters:
  [`sqlglot-parse-opcode-object-counters-20261005.log`](data/sqlglot-parse-opcode-object-counters-20261005.log).
- Profiling driver:
  [`run_pyperformance_case_for_sampling.py`](../benchmarks/diagnostics/run_pyperformance_case_for_sampling.py).
- CPython 3.14.7 comparison input:
  [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).

The Release benchmark executable and DLL hashes are
`12CB6F3C7C8EE1AEA620DD0E301A533F0653123B8BBECA162E23C29F10FC223E` and
`708B203C9D641BAF0AAB2116922C7D69D933DB18452DAF072A4E9E784776D6CB`.
The optimized RelWithDebInfo runtime used for symbolization has DLL hash
`A0CD91786EFCFB1DFCFF693695C5ACBA96DF740D79271F2625C04B48460E7F81`.

This profile does not close the performance gap. The full-suite report remains
the authoritative overall comparison and failure inventory.
