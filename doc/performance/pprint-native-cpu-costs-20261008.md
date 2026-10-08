# CPU costs in the original Python 3.14.7 pprint workload

Selected arithmetic benchmarks do not establish that XLang3 runs every pure-Python workload faster than CPython. The [latest full pyperformance report](pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008.md) still shows substantial slowdowns. This diagnostic identifies costs in one unchanged original workload; it does not replace that comparison.

The original `bm_pprint` safe-repr body was executed once on the C1 Release candidate under a separate, child-only native sampler. It retained the original 100,000-item aliased input and produced the expected 4,200,000-character result and SHA-256. Sampling started after the body's flushed start marker and ended at its completion marker. That final marker follows result hashing/reporting, so a small observable tail is included.

The sampler collected 438 CPU-location samples. These are approximate locations, not exact CPU-time accounting. Polling selected the thread with the largest positive observed CPU delta, suspended it briefly, captured its native stack, and resumed it before writing samples. Suspension perturbed elapsed time; the run is unscored. The 98 source files and 178 Release artifacts remained unchanged before and after capture.

| Exclusive sampled location | Samples | Share of leaf samples |
|---|---:|---:|
| Frame cache cleanup (`clear_cache_if_owned` and `clear_for_pop`) | 39 | 8.9% |
| `Interpreter::run_function` containing range | 20 | 4.6% |
| `module_find_attr_slot` | 18 | 4.1% |
| `class_lookup_attr` | 18 | 4.1% |
| `Value::operator=` containing range | 15 | 3.4% |

Names describe containing compiled function ranges, which can include inlined work. Export aliases are retained rather than arbitrarily choosing one name. Unresolved addresses remain unresolved. Private function names were matched against current Release COFF objects using complete containing-range bytes with relocation bytes masked; no Debug symbols or nearest-export guesses were used.

The inclusive stacks also identify a repeated callback path: ordinary VM calls appeared in 105 stacks (24.0%), and native expanded calls, `sorted_impl`, and its keyword entry appeared in about 91–92 stacks (20.8–21.0%). These paths overlap and their percentages must not be added or treated as exclusive overhead.

Source inspection explains an optimization target. `collect_sorted_entries` calls `runtime_call_callable` for each key. For a Python function, that entry constructs a fresh `Interpreter`. The Python `_safe_tuple` key constructs two `_safe_key` objects at separate call sites, and the ordinary constructor proof is cleared with the returning frame's owning cache. Repeated tiny callbacks therefore redo cold lookup/proof work, even when a long-running arithmetic loop benefits from warm local state and indexed registers. `CallModuleMethod` also performs a name-to-slot lookup for its global module binding before dispatch. Indexed locals alone do not eliminate these costs.

The next optimization must preserve dynamic class/metaclass and initializer-code changes, object lifetimes, native callback behavior, and exceptions. Keeping a weak pointer without proving its current owner and generation would be unsafe. The current trial also uncovered separate correctness problems in compiler-owned class namespace retirement and profiling-setting restoration; those findings do not by themselves explain the full-suite speed gap.

Evidence: [capture receipt](data/pprint-c1-native-cpu-sampling-20261008.json), [raw native samples](data/pprint-c1-native-cpu-sampling-20261008-samples.jsonl), [child output](data/pprint-c1-native-cpu-sampling-20261008-child-stdout.log), and [private-range attribution](data/pprint-c1-native-cpu-coff-r2-20261008.json). The capture's candidate EXE hash is `25b7661acaaf3dbd7e261d196e0d98232bc63bb960a4fff38874174315ce0c42`; its runtime DLL hash is `af0b4a5fa406f81c6584ec9282bc2a18f3ec039ef49c45e6846face5c47d1a36`.

## Unprofiled diagnostic after the correctness repairs

The C3 trial passed all eleven targeted phases, including constructor/object lifetime, nested profiling, namespace retirement, and method annotation scope. It then executed the same original body once per runtime with native sampling and Python profiling disabled. Both outputs matched the original input and result signature, with all 106 source files and 178 Release artifacts unchanged.

| Runtime | Original body elapsed time |
|---|---:|
| CPython 3.14.7, fresh reference | 0.585327 s |
| XLang3 C3, fresh candidate | 9.718005 s |
| XLang3 R5, earlier preserved trial | 32.826230 s |

C3 took about 3.38× less time than the earlier XLang3 trial and about 16.60× more time than the fresh CPython reference. These are single, unscored diagnostic observations; the earlier R5 measurement is unpaired. They establish neither statistical significance nor a whole-suite improvement. The official pprint benchmark and complete fixed regression gate are separate checks.

Evidence: [C3 targeted checks](data/ordinary-canonical-slot-constructor-c3-focused-20261008.json), [C3 original-body receipt](data/pprint-canonical-constructor-c3-original-body-20261008.json), and [R5 original-body receipt](data/pprint-canonical-constructor-baseline-original-body-20261008.json). C3's runtime DLL hash is `433153b9bc449b1e3c6852eaa9a842866834b94c45cb6770682fc1b1669ee6ef`; its EXE hash remains the same as C1.

C3 subsequently passed the complete correctness checks and the unchanged fixed-baseline regression gate, but the original official pprint run timed out at 300 seconds. Its receipt is explicitly `correctness_and_gate_passed_official_failed`; it provides no completed official pprint score. See the [full validation receipt](data/ordinary-canonical-slot-constructor-c3-full-validation-20261008.json) and [fixed gate](data/ordinary-canonical-slot-constructor-c3-full-validation-20261008-fixed-gate.json).

## Constructor plan eligibility

The C4 trial moved the own-slot constructor plan to the live class so separate native-to-Python callbacks can reuse it. Ten Python-focused phases passed, but the added public DLL test failed because the plan was never published. Output correctness alone would have missed the inactive optimization.

An additive diagnostic linked against the unchanged C4 Release confirmed that the class, initializer and default metaclass guards were eligible. The stored `object.__new__` was an exact `StaticMethod` wrapping the native `object.__new__`; testing the stored entry directly as a `NativeFunction` therefore always declined. The C5 correction inspects that exact wrapper's stored function, then retains the same native-target certificate. It calls no descriptors and changes no warm-path or mutation guards. The original eligibility assertion remains unchanged.

See [C4 focused failure](data/class-constructor-plan-c4-focused-idle-resume-20261008.json) and [public DLL diagnostic output](data/class-canonical-publication-c4-diagnostic-loader-fixed-20261008.stderr.log). C5 passed all eleven focused phases, including the unchanged test that the same plan survives 32 separate native-to-Python callbacks without adding class or initializer owners, and that class/metaclass/code mutation still invalidates its proof. See the [C5 focused receipt](data/class-constructor-plan-c5-focused-20261008.json).

The unchanged original pprint body took 9.499208 seconds on C5 and 0.579763 seconds on fresh CPython 3.14.7. C5 is close to the earlier C3 observation of 9.718005 seconds. These single, unpaired observations do not establish a significant incremental gain from the persistent plan. Activating the cache does not remove the remaining workload costs; constructor setup alone does not explain the slowdown. See the [C5 body receipt](data/pprint-canonical-constructor-c5-original-body-20261008.json).

C5 subsequently passed the complete correctness run: 396 core cases, 11 compatibility sections, three expected failures, the nine required CTests and both manual SQLite APIs. The complete fixed-baseline gate returned exit 2, `inconclusive`, because `list_append` first had a median ratio of 1.0261 with a 95% interval of [0.9847, 1.1074], then passed its confirmation with a ratio of 1.0368 and interval [1.0074, 1.0941]. The unchanged conservative gate retains `inconclusive` when the two attempts disagree. Ten other cases passed. This is neither a confirmed regression nor a passed gate. The original failed receipt remains preserved; only the gate needs fresh idle measurements, followed by the official pprint attempt. See the [C5 full correctness and inconclusive gate receipt](data/ordinary-canonical-slot-constructor-c5-full-validation-20261008.json) and [raw gate report](data/ordinary-canonical-slot-constructor-c5-full-validation-20261008-fixed-gate.json).

The fresh complete gate subsequently passed all eleven cases with the same accepted baseline, 21 repeats, five warmups and 10% limit. The resume retained only the exact sixteen passed correctness phases for the unchanged candidate; it did not reuse or relabel the inconclusive gate. The original official pprint attempt then timed out at the unchanged 300-second full-case limit, with no final score or partial result. All 106 source files and 178 Release artifacts stayed unchanged. This is a correctness and regression-gate checkpoint, with `full_validated=false`; no official pprint improvement or overall CPython win is claimed. See the [terminal resume receipt](data/ordinary-canonical-slot-constructor-c5-gate-idle-resume-20261008.json), [passing fixed gate](data/ordinary-canonical-slot-constructor-c5-gate-idle-resume-20261008-fixed-gate.json), and [official timeout log](data/ordinary-canonical-slot-constructor-c5-gate-idle-resume-20261008-official-pprint.stdout.log).

## Separate pure-Python pickle diagnostic

The original `bm_pickle` function also remained Python. Both fresh runtimes blocked `_pickle`, used Python `_Pickler`/`_Unpickler`/`_dumps`/`_loads`, and executed the same original protocol-5 body once: 41 outer loops, three seeded benchmark objects and 20 unrolled dumps per object, totaling 2,460 dumps. Serialized byte lengths and SHA-256 values matched across runtimes; pure-Python roundtrips and input identities passed before and after timing.

| Original body, single unscored run | CPython 3.14.7 | XLang3 C5 | XLang3 speed, CPython = 1× |
|---|---:|---:|---:|
| pprint safe repr | 0.579763 s | 9.499208 s | 0.061× (16.4× slower) |
| pure-Python pickle dumps | 0.212135 s | 4.257495 s | 0.050× (20.1× slower) |

Speed is CPython elapsed time divided by XLang3 elapsed time. Values below 1× mean XLang3 is slower. These original-body diagnostics are not official pyperf scores, exclude imports/setup, and do not update the full-suite matrix. See the [pickle body receipt](data/pickle-original-pure-c5-body-20261008.json).

A separate child-only native sample of the unchanged pickle body collected 190 CPU locations with all current 106 source files and 178 Release artifacts pinned before and after. Leaf locations included `Interpreter::run_function` (12), frame cache cleanup (8), frame pop cleanup (6), `Value::operator=` (6), `object_get_attr` (5), and `module_find_attr_slot` (4). Windows heap and lock locations also occurred. These containing ranges include inlined work; unresolved locations remain unnamed and inclusive stacks overlap. The sample identifies several costs to investigate, not one established dominant cause or removable overhead. See the [pickle sampling receipt](data/pickle-original-pure-c5-native-sampling-20261008.json) and [raw samples](data/pickle-original-pure-c5-native-sampling-20261008-samples.jsonl).

An additional file-only comparison against eleven pinned current COFF objects resolved 35 more runtime leaf locations using complete compiled ranges with explicit relocation masks. It identified VM attribute access and dispatch, class lookup, and destruction of the existing owning cross-thread inspection snapshot. Snapshot destruction/publication accounted for seven distinct leaf locations out of 190. This suggests a separate bounded experiment in reusing unchanged snapshot owners, while preserving cross-thread inspection and the current path for depth or owner changes. It does not establish a predicted speed gain. See the [exact private-range attribution](data/pickle-original-pure-c5-native-coff-attribution-20261008.json).
