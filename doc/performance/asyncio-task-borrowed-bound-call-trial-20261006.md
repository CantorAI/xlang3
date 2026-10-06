# Asyncio Task borrowed bound-call trial (2026-10-06)

## Result

Rejected. I added a stack-backed positional fast-call for XLang3's native
`_asyncio.Task._step`, `_eager_step`, and `_wakeup` bound methods. It was guarded
to run only when monitoring, tracing, profiling, and debug stepping were
inactive; other callbacks stayed on the generic path. This removed the
temporary heap `vector<Value>` used to prepend the bound receiver, but it did
not produce a significant official benchmark improvement.

| Official pyperformance 1.14.0 `async_tree_none` | Mean ± standard deviation |
| --- | ---: |
| XLang3 control | 4.46 s ± 0.04 s |
| XLang3 candidate | 4.45 s ± 0.03 s |

`pyperf compare_to` hid the result as statistically insignificant. The
candidate therefore does not justify extra dispatch code. The async-tree gap
remains large; these measurements point toward VM resume and instruction
execution work rather than the bound-argument vector alone.

## Validation and evidence

- The Release candidate built successfully.
- The complete `tests/run_fixtures.py` suite passed on the candidate.
- XLang3 control and candidate used Python 3.14.7 dependencies, the same
  compatibility hooks, and official pyperformance 1.14.0 fast mode.
- [Control JSON](data/pyperformance-async-tree-task-stack-control-20261006.json)
- [Candidate JSON](data/pyperformance-async-tree-task-stack-candidate-20261006.json)
- Control executable SHA-256: `3FF07EF4BDD0BB2F2B0235D4658448935241464039F2072FA33DEE36F30239AD`
- Candidate executable SHA-256: `5AA790F63A3069064C580C0604E98C526D75BA2DC28750A295393E705494FB30`
- Candidate runtime DLL SHA-256: `21FF7FC16CFF76052C727E93B53B8A8CF3533A69DA07FC52D74AAF3F2210B084`

The source shortcut was removed after comparison. It changes only native
XLang3 `_asyncio` call plumbing; it does not implement Python standard-library
code in C++.
