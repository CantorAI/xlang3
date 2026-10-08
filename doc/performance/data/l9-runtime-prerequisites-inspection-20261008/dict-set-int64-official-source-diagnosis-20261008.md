# DictSet integer construction: bounded source diagnosis

Static only. No Python/AST, interpreter, build, benchmark, or live source change. Current R10 is still a held timing trial. This note does not propose or apply an index change.

`ops/xlang_vm_ops_containers.h:821–838` reserves 64 entries on the first insertion, scans the complete ordered vector for an exact physical Int64 match, then overwrites or appends. A comprehension with n distinct Int64 keys performs n(n−1)/2 key checks. It also misses an equal earlier Bool, Double, or Python integer-subclass key because the scan tests only `ValueTag::Int64`. `DictSetConst` delegates to this same handler. `lower.cpp:6959–6967` emits DictSet for a dictionary comprehension; ordinary subscript assignment emits SetItem.

The existing integer-index setter is already amortized linear for a fresh dictionary containing only native integer keys: `mapping_set_item:1174–1192` calls `ensure_key_indexes`, probes the flat table, appends, and incrementally updates its scalar entry-index table; capacity growth rebuilds it. Entries remain the sole key/value owners. Routing the VM handler through this path would remove the quadratic scan for that proven case.

There is a concrete semantic prerequisite. `mapping_set_item_runtime:1320` delegates every non-Instance query, including exact Int64, to raw `mapping_set_item`. `dict_integer_key:622` accepts `value_int_like_to_i64`, including Instance integer payloads. A stored Python int subclass with overridden equality therefore can be treated as its physical integer by `dict_find_integer_index` without running its Python method. Replacing the VM shortcut with the existing setter alone does not fix this. The stored-key set must be proven callback-free before raw integer probing; unsafe stored keys require runtime equality with owned operands, pending exception preservation, and safe reentry. Existing numeric overwrite paths also assign into vector entries while releasing old values; publication-before-finalizer must be retained or repaired before extending their use. This note does not claim a complete saved-hash or reentrant-dictionary model.

The unchanged official **bpe_tokeniser** body is an actual large DictSet consumer. `bm_bpe_tokeniser/run_benchmark.py:24` builds `_decoder = {token: token_bytes for token_bytes, token in mergeable_ranks.items()}`. Each timed `bench_bpe_tokeniser` iteration calls `train(data)`, which trains exactly 1,024 ranks and then constructs `SimpleBytePairEncoding`. The decoder has 1,024 distinct native integer keys, so the present VM path scans **523,776** existing entries for each decoder. Retained native IR already records `__init__` IP13 as DictSet at line24; no new IR execution is needed to establish that route. This construction is inside the official timer, though its share of the complete training/encoding time has not been measured. No overall speedup is inferred.

Official **comprehensions** also builds integer-key dictionaries in `WidgetTray._add_widgets`, but each map has only 18 entries after filtering the original 24 widgets. It is a useful unchanged secondary case rather than evidence for large-map scaling. The local default 11-case gate does not select `benchmarks/cases/comprehensions.py`; that separate diagnostic builds a map from the existing integer `values`. The gate's deepcopy memo assignments and Python pickle memo writes use ordinary SetItem, so no pure-pickle gain follows from this DictSet finding.

History checked: `dict-runtime-int-index-no-gain-20261004.md` rejects an extra runtime dict.get lookup probe; `dict-flat-int-index-trial-20261004.md` changes integer lookup storage; `dict-intrinsic-index-checkpoint-20261007.md` concerns tuple/bytes writes and guarded reads. These are not trials of the exact VM DictSet Int64 construction shortcut. The held homogeneous-index design explicitly records the same numeric-equivalence prerequisite. No prior exact DictSet construction experiment was found in the searched performance Markdown or proposal/trial patches.

The next action is CPython-first evaluation of the frozen numeric literal/comprehension and subclass-equality prerequisites, preserving any S8 mismatch. Only then choose a narrow indexed DictSet route with explicit whole-key-set eligibility and a safe runtime fallback. Validate the original comprehension output/order, deletion/reuse, and old-value finalizer mutation before timing the unchanged BPE body and secondary comprehensions case. Keep the original default gate unchanged. A BPE decoder-only diagnostic can establish reachability/scaling but cannot substitute for the official workload.

Pinned current sources and retained evidence:

| File | SHA-256 |
| --- | --- |
| `src/executor/xlang_vm/ops/xlang_vm_ops_containers.h` | `9476cd2e6a84986679844fddb6669a092a5e235b9496523226b1926a9b7c404a` |
| `src/runtime/mapping.cpp` | `81dc9042e1e26874b6a02791b3f62656e5147b8321ea9e87fbe80b53fdac2f74` |
| `src/internal/xlang3/mapping.h` | `43f8bbc91e2f224dde767d4f785a64e2c3429b269113ba85dd613eac297c3857` |
| `src/sema/lower.cpp` | `12aae5ef15a98288aa359112ee8a6bd64f0213a228a3b11132849fad0f62fb04` |
| CP3147-installed `bm_bpe_tokeniser/run_benchmark.py` | `c7255345499e118181785370b0996e8cc1056491b9678b3fbeb02421e3f9b2df` |
| Original `bm_bpe_tokeniser/data/frankenstein_intro.txt` | `6607193e67b7b459d082f5f2aea724e470615643d63ad2e205123b3076223da4` |
| `doc/performance/data/captured-lookup-20261007-sources/bpe-callback-ir-source-20261007.ir.txt` | `effecd21d8aaa1e8b471b83fa5f76a936b4ca39b4504e5d2bc3f10a80bf1df8b` |

The installed benchmark is `C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_bpe_tokeniser/run_benchmark.py`. File hashes establish the source read for this diagnosis, not historical benchmark provenance or a completed timing result.
