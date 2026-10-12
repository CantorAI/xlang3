# C5 original pure-Python pickle native findings

The unchanged original body performed 2,460 protocol-5 dumps of the original seeded objects. Fresh CPython3.14.7 and C5 produced identical bytes and successful round trips. The single unprofiled bodies were approximately 0.2121s and 4.2575s. These are unscored diagnostic invocations, not official pyperformance scores or evidence of an isolated optimization gain.

Pinned inputs:

- Body receipt: `doc/performance/data/pickle-original-pure-c5-body-20261008.json`, SHA256 `2d6a79f263d3e20f86f494bc13dfef795e395fba0b81773a2bc238472d811665`.
- Sample receipt: `doc/performance/data/pickle-original-pure-c5-native-sampling-20261008.json`, SHA256 `e5919bc0ec2840d3300888f1fb54dd10ac66cb96a66734d47a05322d26f5a0a7`.
- Raw samples: `doc/performance/data/pickle-original-pure-c5-native-sampling-20261008-samples.jsonl`, SHA256 `d2d93d48eec095765066cc53af03cc14340a2b2b83bd2b6852c36adacac2ee88`.
- Exact-range attribution: `scratch/performance/pickle-c5-native-coff-attribution-20261008.json`, SHA256 `f13ca5768fc3a55a581ea8a70af924b584666a6b4d9793e750436c58ed5eceab`.
- Current runtime image: SHA256 `89ad2994c6fbb247ca59a7813042acaad6b2bcfb3b957a3bc13fa61517d2ed05`; source inventory106 SHA256 `69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937`.

One authorized CP3.14.7 file-only attribution command finished successfully in about 4.9s. It compared complete containing `.pdata` code ranges with current COFF function prefixes, masking only explicit relocation fields. Eleven object files and all source/receipt/image inputs were unchanged before/after. It did not load the runtime image, execute XLang, build, modify engine code, or run another benchmark.

## Exclusive leaf locations

There are 190 samples: 149 in the runtime DLL and 41 in system libraries. Export/range anchors identify 90 DLL leaves; exact COFF matches identify another35. The remaining24 DLL leaves are unresolved. A match preserves all compatible aliases and does not prove relocation-target identity or identify inlined source work.

The following buckets contain distinct leaf locations. Overlapping inclusive stacks are not added to these counts.

| Source bucket | Leaf samples /190 | Specific locations |
| --- | ---: | --- |
| VM dispatch and function/frame entry | 25 | `run_function`12, `call_method`4, ordinary `call`4, `call_user_function`3, pushed-frame lambda2 |
| Attribute/module lookup | 20 | VM `load_attr`5, `load_local_attr`1, `object_get_attr`5, `class_lookup_attr`4, `module_find_attr_slot`4, `module_get_attr`1 |
| VM frame/cache cleanup and reset | 18 | `clear_cache_if_owned`8, `clear_for_pop`6, `reset`2, two small-buffer reset ranges1 each |
| Published inspection snapshot destruction/publication | 7 | `PublishedRuntimeFrameView` range destruction5, `publish_current_frame_state`2 |
| Value ownership handoffs | 8 | Value copy assignment6, assign helper1, move helper1 |
| Explicit allocator locations | 11 | `RtlFreeHeap`6, `_malloc_base`1, thread-bucket lookup2, bucket allocate1 and release1 |
| Explicit system mutex operations | 12 | SRW acquire7 and release5; caller attribution is not assumed |
| Interned-string lookup | 3 | `find_interned_string`, exact full-range COFF match |

This is not a complete partition: hash/index work, generic STL aliases, TLS frame views, memoryview tracking, string C helpers and unresolved locations are retained separately in the receipts. In particular, unnamed ntdll ranges are not automatically labelled heap allocation, and the whole `run_function` range is not labelled argument binding or frame allocation.

New exact private matches include:

| RVA | Function | Leaf | Inclusive stack |
| --- | --- | ---: | ---: |
| `0x61d830` | `_Destroy_range<PublishedRuntimeFrameView>` | 5 | 5 |
| `0x7490e0` | VM `load_attr` | 5 | 18 |
| `0x726270` | VM `call_method` | 4 | 84 |
| `0x718940` | VM ordinary `call` | 4 | 24 |
| `0x5bfeb0` | `class_lookup_attr` | 4 | 5 |
| `0x733350` | `call_user_function` | 3 | 14 |
| `0x589ed0` | `find_interned_string` | 3 | 3 |
| `0x643d40` | `publish_current_frame_state` | 2 | 11 |
| `0x76fa90` | pushed-frame lambda | 2 | 14 |
| `0x604d60` | `ensure_runtime_hash_index` | 1 | 36 |
| `0x71e510` | `call_builtin_method_spec` | 0 | 60 |

The last column explains routes; it is not additive CPU cost. No nearby-export name, Debug PDB, old-image address map or sampled-body time extrapolation was used.

## One distinct next target

`src/runtime/runtime.cpp:183` publishes an owning inspection snapshot under the registry mutex. It clears `owned_frames` and `frame_stack`, then resizes and repopulates all published module/globals owners and derived frame views. The exact5-leaf destructor range operates on this PublishedRuntimeFrameView vector, not the VM frame’s adaptive caches.

Publication is already selective. `set_current_frame_stack` only changes the thread-local borrowed view. The owning registry copy runs from the suspension callback in `xlang_vm_loop.cpp:1264` after a frame-stack generation change, or periodic suspension refresh. It must remain correct when a native operation releases the execution lock. Removing publication or using an inactive-observer flag would lose cross-thread stack inspection.

A bounded future experiment can reuse an unchanged live prefix of the published snapshot instead of destroying/reconstructing its module/globals owners. It should preserve the registry mutex and active frame count, compare live activation and module/globals identities, repair view pointers after any capacity change, publish the complete replacement before retiring changed suffix owners, and keep temporary incoming owners through reentrant cleanup. Shrink, module replacement, nested callbacks, generator/pause, `_current_frames`, stopped-thread inspection and finalizer/pending-exception boundaries need explicit validation. The optimization must not retain locals, closure cells, functions or adaptive caches after return.

This differs from rejected scoped Interpreter reuse and weak-module VM cache persistence: it changes only the existing owning inspection snapshot’s redundant rebuilding. The sample supports inspecting this target; it does not yet prove a speedup or justify an applied patch. Generic lookup/call costs remain separate and should be revisited only with a concrete cache-miss/eligibility signal, not a guessed interpretation of the84 inclusive `call_method` samples.

The sample window includes the three post-body dump/load audits before the terminal marker. It has only190 points, so small differences of a few samples are uncertain. No samples were filtered and no additional capture was run.
