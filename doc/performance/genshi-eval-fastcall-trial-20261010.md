# Genshi eval fast-call trial — 2026-10-10

XLang3's generic `eval` builtin previously had no positional fast-call adapter.
The Genshi rendering profile counted 108,009 native `eval` calls on that slow
path. Registration now routes bounded positional calls through the existing
stack-backed adapter; `builtin_eval` remains the implementation, and keyword
calls continue through the existing keyword callback. The change does not
replace or translate Genshi's Python code.

The full XLang3 fixture suite passed after the change, including live eval
namespace semantics and a new keyword-argument case. The Release candidate was
built with the fixed Visual Studio build script at the existing candidate path.

| Runtime/result | `genshi_text` median | `genshi_xml` median |
|---|---:|---:|
| Prior full candidate run, 20 values | 385.82 ms | 1,056.66 ms |
| Fast-call candidate repeat, 20 values | 349.72 ms | 906.99 ms |
| CPython 3.14.7 reference, 20 values | 19.36 ms | 42.49 ms |

The repeat candidate medians are 9.3% and 14.2% below the prior full-run
medians. These are separate, unpaired runs; the first fast-call trial had high
variance, while the repeat was steadier. The focused `compiled_eval` probe did
not improve (58.58 ms versus 57.77 ms per 10,000 calls), so the full-render
difference is promising but does not yet isolate a causal speedup. Even the
new candidate remains about 18.1× slower on text and 21.4× slower on XML than
the saved CPython reference.

The counter run confirms the intended dispatch change: native fast calls rose
from 388,404 to 496,413, exactly matching the 108,009 eval calls; `eval` no
longer appeared among native slow-call counts. This proves route selection,
not elapsed-time attribution. The main unresolved cost is execution after
native dispatch: eval code still enters a VM frame and calls Genshi's ordinary
Python lookup functions.

## Evidence

- [Prior full candidate suite JSON](data/pyperformance-xlang3-current-candidate-20261010.json)
- [Fast-call trial JSON](data/pyperformance-xlang3-genshi-eval-fastcall-trial-20261010.json)
- [Fast-call repeat JSON](data/pyperformance-xlang3-genshi-eval-fastcall-repeat-20261010.json)
- [CPython 3.14.7 full reference JSON](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json)
- [Pre-change phase probe](data/genshi-runtime-refresh-xlang3-20261010.json)
- [Post-change phase probe](data/genshi-eval-fastcall-xlang3-20261010.json)
- [Pre-change dispatch counters](data/genshi-runtime-refresh-xlang3-counters-20261010.log)
- [Post-change dispatch counters](data/genshi-eval-fastcall-xlang3-counters-20261010.log)

This is a bounded runtime improvement candidate, not a resolution of the full
pyperformance gap. A paired control/candidate measurement is still needed
before attributing the lower full-render medians to this adapter.
