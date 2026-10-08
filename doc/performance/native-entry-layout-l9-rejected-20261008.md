# L9 immutable entry layout: rejected

The L9 trial was rejected by its predeclared performance controls. The callback sorting case improved, but plain sorting missed the permitted 5% elapsed-time limit. The exact S8 source and Release bytes have been restored.

These are paired XLang3 diagnostics against the preserved S8 checkpoint. Speed is S8 elapsed time divided by L9 elapsed time; S8 is 1×. They are not CPython comparisons or official benchmark scores.

| Case | Median paired speed | Bootstrap 95% interval | Role |
| --- | ---: | ---: | --- |
| Loop | 0.973805× | [0.954482, 1.026579] | Control |
| Small calls | 0.980254× | [0.968309, 1.010488] | Control |
| Branch calls | 0.985184× | [0.962683, 1.050281] | Control |
| Plain sorting | 0.927019× | [0.919406, 1.039008] | Control: fails declared median floor |
| Sorting with Python callback | 1.058568× | [1.041612, 1.110103] | Selected signal: passes |

The selected signal required a median above 1.02× and a lower interval endpoint above 1.0. Each control required a median of at least 1/1.05, or 0.952381×. Plain sorting's median implies about 7.87% more elapsed time. Its interval includes 1, so this does not establish a slowdown; rejection follows the declared median rule. The thresholds and sample count were unchanged after measurement.

The [terminal paired receipt](data/native-entry-layout-l9-callback-paired-strict-idle-20261008.json) retains seven alternating pairs, all 14 child runs, and all 210 timed values: five cases, three samples per case, 50,000 operations per sample. One warmup per case ran outside the timed samples. No values were trimmed, removed, retried or replaced. All child outputs matched, all read-only timing watchers were valid, and source and binary hashes stayed unchanged. The watcher polls once per second and can miss a process entirely between observations.

The [fresh focused receipt](data/native-entry-layout-l9-focused-correctness-r2-20261008.json) passed all 17 phases, including the public C++ immutable-layout proof and existing thread, generator, frame and monitoring checks. This is targeted correctness evidence. L9 did not run original pprint or pickle bodies, the full correctness suite, the default gate, or official benchmarks after the performance rejection.

Two failed preflights remain intact. The [first focused attempt](data/native-entry-layout-l9-focused-20261008.json) refused an MSBuild worker before any test phase. The [worker capture](data/verified-idle-msbuild-l9-20261008-capture.json) failed before the quiet interval because the worker's recorded parent PID had exited. It produced no timing samples or successful worker admission. The worker later disappeared; the actual pairs used the unchanged strict idle guard, with no worker exception or orphan policy.

The [restoration receipt](data/native-entry-layout-l9-rejected-restored-s8-20261008.json) records complete preservation of rejected L9's 111 source files and 178 Release files outside Git, then exact restoration of S8's 110 source files and 178 Release files. Five owned source files were restored and the new C++ helper was removed. Unrelated tracked dirty bytes were preserved, the accepted gate baseline stayed unchanged, and restored source timestamps force replacement of stale objects on a future Ninja build. No post-restoration rebuild is claimed here.

The measured candidate was the exact source111 worktree and Release178 identified by its receipts. The rejected proposal and six source copies are inspection evidence only; no L9 engine or test changes belong to this publication. The [S8 checkpoint report](r6-r7-s8-checkpoint-20261008.md) remains the retained result. Its earlier full-suite matrix predates this trial; no new whole-suite result or causal performance claim follows from L9.
