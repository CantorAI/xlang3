# Function-object freelist trial for `json_dumps` (2026-10-03)

## Result

Rejected. A bounded thread-local cache for short-lived `FunctionObject`
allocations was tested against pyperformance's repeated small-object
`json_dumps` workload. It cleared captured values, defaults, metadata, module
references, and weakrefs before recycling an object, and only retained
functions with small closure/default vectors. This did not establish a
repeatable improvement, so the allocator change was removed.

The two order-balanced `--fast` pairs disagreed: the candidate measured
35.2 ms versus 36.1 ms in pair 1, then 37.4 ms versus 36.0 ms in pair 2.
Rigorous measurements were also inconclusive: pair 1 was 36.9 ms versus
38.0 ms, while the reverse-order pair was 37.8 ms for both builds. `pyperf`
warned that all these samples were unstable or had high variance. The
candidate's per-process freelist state also varied with worker reuse, so a
single favorable pair is not enough reason to retain this allocator path.

The candidate passed five function metadata, weakref, generator-frame, and
JSON fixtures, plus `xlang3_runtime_value_tests.exe` and
`xlang3_interpreter_tests.exe`. The fixed executable and runtime DLL were
restored byte-for-byte afterward. No code change remains from this trial.

## Context and measurement

The existing VM counter profile of `json_dumps` recorded 4,683 `MakeFunction`
operations during its setup and workload pass, near the 4,001 repeated calls
to `json.dumps`. That suggested short-lived nested encoder functions as an
allocation target. The targeted official pyperformance 1.14.0 benchmark did
not confirm a useful gain.

All runs used CPython 3.14.7 as the harness, the repository's Windows pyperf
compatibility shim, and the unchanged pyperformance `json_dumps` definition.
The fixed path remained
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`. The saved CPython 3.14.7
reference is 7.22 ms; XLang3 remains around 36–38 ms (about 5× slower).

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
| --- | --- | --- |
| Restored control | `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA` | `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F` |
| Temporary candidate | `A959E631169992F867ACE9E520584B20F75A8C08532831B2DF14A2D027B7FBD7` | `D9A497E62F0B50BC293391D63AC6353BCB9DBC3DBA504360A92D939C3C650248` |

## Raw pyperf results

`--fast` runs:

- [Candidate, pair 1](data/json-functionobject-freelist-candidate-r1-20261003.json)
- [Control, pair 1](data/json-functionobject-freelist-control-r1-20261003.json)
- [Control, pair 2](data/json-functionobject-freelist-control-r2-20261003.json)
- [Candidate, pair 2](data/json-functionobject-freelist-candidate-r2-20261003.json)

`--rigorous` runs:

- [Candidate, pair 1](data/json-functionobject-freelist-candidate-r1-rigorous-20261003.json)
- [Control, pair 1](data/json-functionobject-freelist-control-r1-rigorous-20261003.json)
- [Control, pair 2](data/json-functionobject-freelist-control-r2-rigorous-20261003.json)
- [Candidate, pair 2](data/json-functionobject-freelist-candidate-r2-rigorous-20261003.json)
