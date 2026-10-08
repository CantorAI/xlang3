# Performance is a release requirement

Preserve both Python correctness and runtime performance. A passing fixture
suite alone is insufficient for changes to the parser, lowering, IR, VM,
runtime, object model, builtins, or build optimization settings.

Before engine changes, preserve a known Release executable and its runtime
libraries in a separate directory. Never benchmark a Debug or sanitizer build
against Release. Finish builds before measuring; do not run benchmarks in
parallel with builds or other benchmark processes.

After correctness validation, run:

```text
python benchmarks/check_regression.py --baseline <preserved-release-executable> --candidate build/Release/xlang3.exe
```

Use the complete default suite. Exit 0 passes. Exit 1 means a confirmed
regression. Exit 2 means invalid/inconclusive measurements; investigate and
rerun on an idle machine. Neither 1 nor 2 permits claiming performance
validation passed. Keep the JSON report with the change's validation evidence.

Do not commit an engine change with a failed or missing performance gate.
Do not weaken thresholds, remove cases, shorten workload loops, disable
correctness features, or update the baseline to make a regression disappear.
A deliberately accepted slowdown requires explicit user approval, a measured
reason, and a documented baseline change. The default 10% per-case tolerance
accounts for measurement variability; it is not a budget to spend repeatedly.
Keep a fixed accepted baseline to detect cumulative degradation.

When changing instruction fusion or IR shapes, verify affected call,
constructor, property, and arithmetic fast paths still recognize the new
shapes. Test optimization eligibility as well as output correctness. When a
shortcut is semantically invalid, provide a guarded alternative and measure
its cost; do not silently remove it. Keep inactive monitoring, debugging,
signal, finalizer, and buffer-lifetime costs out of per-instruction paths where
possible, without removing required semantics.

The September 2026 slowdown is still under investigation. A current-build
baseline prevents further degradation; it does not mean August performance
has been restored. Do not replace the historical comparisons with this gate.

Before starting a performance experiment, search `doc/performance` for the
affected function, fields, and mechanism, and read prior matching trials.
Check the preserved patches as well as their titles. A different spelling,
new test coverage, or a new measurement protocol does not make the same
runtime optimization a new hypothesis. Revisit a rejected mechanism only
with concrete evidence explaining why its expected effect has changed.
In particular, callee-module owner selection at Python call entry was
rejected on September 30; see
`doc/performance/call-module-owner-call-entry-trial-20260930.md`.

Keep CPython pure-Python standard-library modules implemented in Python.
Improve their performance through XLang3's compiler, IR, VM, and generic runtime
paths. A native XLang3 module may replace a CPython module only when CPython
itself implements that module natively; preserve its Python-visible import name
and compatible module API/ABI.

For Python method-forwarding fast paths, preserve dynamic override dispatch
through subclasses and keep the forwarding method in error tracebacks. Require
guards that prove both properties, with the original Python call as fallback
for overrides or fallible operations. When a change targets a pyperformance
case, verify its effect with that official benchmark as well as the fixed local
regression gate; a local workload replica alone does not establish a suite win.
