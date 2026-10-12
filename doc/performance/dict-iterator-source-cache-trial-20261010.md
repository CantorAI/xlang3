# Built-in dictionary iterator source cache trial — 2026-10-10

The working Release build was profiled on the unchanged NetworkX shortest-path
body with setup excluded. This first current-build sample collected 3,210 native
instruction pointers inside `xlang3_runtime.dll` over 20 body calls. The older
2026-10-07 profile had placed 23.8% of runtime samples in
`mapping_iter_next`, but that profile predates the selected-value ownership
change and is stale for the current source. The current sample was not
symbolized because `llvm-symbolizer` is unavailable in this environment; it
does not justify attributing current time to `mapping_iter_next`.

A focused diagnostic then compared 100,000 built-in dictionary yields per
sample on the current candidate and CPython **3.14.7**. It measured keys,
values, items, and a list-iteration control. The candidate medians were 3.026,
3.103, 12.466, and 4.071 ms; CPython medians were 1.760, 1.833, 2.212, and
2.031 ms. These results put current dict iteration at 1.7x CPython for keys
and values, and 5.6x for items.

## Candidate and decision

The candidate cached a non-owning `DictObject*` in each iterator while
retaining its `source` Value as the lifetime owner, then bypassed repeated
mapping-proxy/storage decoding. A second revision also passed the already
classified iterator pointer from `sequence_iter_next`, avoiding a duplicate
ObjectKind check. The implementation kept indexing the live entries vector so
updates and vector growth would remain visible; it never cached `entries.data()`.

The pointer-only revision measured 3.382 ms for keys, 3.217 ms for values,
12.309 ms for items, and 4.123 ms for the list control. A later typed-dispatch
revision measured 4.090/4.326/14.858/4.861 ms, then 3.119/3.445/13.088/4.513
ms on a repeat. The list control moved by a similar amount, so those later
samples were noisy. Neither revision demonstrated a repeatable win; both
runtime changes were removed without running official pyperformance. Do not
keep or cite this as an optimization.

The existing ownership fixture passed with the candidate and is now registered
in `tests/run_fixtures.ps1`, alongside its existing registration in the Python
fixture runner. This ensures key/value/item ownership, value replacement,
iterator exhaustion, aliases, and dict-subclass fallback cases run in the
PowerShell fixture path. The comments in `mapping_iter_next` preserve the
separate selected-value ownership rationale that produced the earlier
validated NetworkX improvement.

## Evidence

- [Current candidate primitive results](data/dict-iterator-primitives-current-20261010.json)
- [CPython 3.14.7 primitive results](data/dict-iterator-primitives-cpython3147-20261010.json)
- [Pointer-only trial](data/dict-iterator-primitives-cached-source-r1-20261010.json)
- [Typed-dispatch trial](data/dict-iterator-primitives-known-kind-r2-20261010.json)
- [Typed-dispatch repeat](data/dict-iterator-primitives-known-kind-r2-repeat-20261010.json)
- [Candidate fixture run](data/dict-iterator-cached-source-r2-fixtures-20261010.log)
- [Current native samples](data/networkx-dict-iterator-current-r2-samples-20261010.json)
- [First cache build](data/build-dict-iterator-cached-source-r1-Release-20261010.log)
- [Typed-dispatch build](data/build-dict-iterator-known-kind-r2-Release-20261010.log)

The candidate executable and runtime DLL were SHA-256
`860F56F77F8EEC9F381A89A67EC742EE531A0196659AE43B395FC68BC67BDAB4` and
`BC37C90465148D9C9EB4BD915FA843BF66128C27AEFDDF62EAE3D971491885D6`.
