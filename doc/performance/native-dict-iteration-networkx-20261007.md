# Dictionary iterator ownership and official NetworkX improvement

Key/value dictionary iteration now retains only the selected result instead of copying an entire owning key/value pair. It advances iterator state before replacing output, and promotes a borrowed output to an owned result. This preserves aliased-output lifetime while removing unused value refcount traffic. NetworkX remains Python; no algorithm was replaced with C++.

## Official pyperformance results

Both XLang3 runs used pyperformance 1.14.0 fast mode, CPython 3.14.7 as manager, the same dependency site and compatibility shim, and a 600-second complete-case cap. The control scores come from the completed checkpoint `003b3882` full run. All three sources have 20 measurement values per listed subtest; warmups and calibration are excluded.

| Subtest | CPython 3.14.7 | XLang3 control | Candidate | Control/candidate speed | CPython/candidate speed |
|---|---:|---:|---:|---:|---:|
| `shortest_path` | 0.469990 s | 1.738244 s | 1.554071 s | 1.1185x | 0.3024x |
| `connected_components` | 0.423194 s | 1.625978 s | 1.463450 s | 1.1111x | 0.2892x |

`pyperf compare_to` reports 1.12x faster for shortest-path, 1.11x for connected-components, and a 1.11x geometric mean on these two cases. Both rows are reported rather than hidden as insignificant. These are separate fast-mode runs with stability warnings; they do not establish a whole-suite gain or beating CPython. CPython still takes about one third of the candidate time. K-core was not rerun and has no new score.

## Correctness and fixed baseline

- Complete fixture suite: exit 0, including the new dictionary ownership fixture. That fixture also passed on CPython 3.14.7 and the preserved control.
- Eight C++/SDK/serialization checks: all passed, including runtime ownership, interpreter, SDK calls, graph producer/consumer, and three graph rejection cases.
- Complete default regression gate: exit 0; all 11 cases, 21 paired repeats, five warmups, unchanged 10% tolerance. The fixed baseline executable and runtime DLL retain their accepted hashes.
- The standalone C++ control probe reproduced exactly two borrowed-output ownership failures. Its control mode skips source-confirmed undefined iterator self-replacement; the normal candidate suite exercises those cases.

## Evidence

- [Control full-run report](pyperformance-xlang3-native-string-checkpoint-full-fast-20261007.md) and [raw JSON](data/pyperformance-xlang3-native-string-checkpoint-full-fast-20261007.json).
- [Candidate raw JSON](data/pyperformance-xlang3-native-dict-iteration-networkx-fast-20261007.json), [log](data/pyperformance-xlang3-native-dict-iteration-networkx-fast-20261007.log), and [unchanged start/end binary provenance](data/pyperformance-xlang3-native-dict-iteration-networkx-fast-20261007-provenance.json).
- [pyperf comparison](data/native-dict-iteration-networkx-compare-20261007.log).
- [Fixed-baseline gate JSON](data/release-native-dict-iteration-fixed-gate-20261007.json) and [gate log](data/release-native-dict-iteration-fixed-gate-20261007.log).
- [C++/SDK/serialization log](data/native-dict-iteration-cpp-sdk-serialization-20261007.log).
- [Control C++ probe failures](data/native-dict-iteration-ownership-control-cpp-20261007.log).
- [Preserved-control identity](data/native-dict-iteration-preserved-control-20261007.json) and [source/ownership audit](native-dict-iteration-source-audit-20261007.md).
