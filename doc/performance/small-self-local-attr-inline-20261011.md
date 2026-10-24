# Direct instance-attribute reads in small self-method inlining

XLang3 already recognizes very small `self` methods at the VM call site. The
original inliner handled constant, slot, and selected arithmetic/method shapes,
but it fell back to a normal VM frame for a common Python method such as
`def get(self): return self.value`. The IR remains `LoadLocalAttr`; the change
executes that existing operation inside the VM's small-self-method inliner.
This is a VM/runtime optimization, not an IR rewrite.

The new case rechecks the current class lookup on every invocation. It only
reads instance storage directly when the receiver is an exact XLang3 instance
without native attribute hooks or `__getattribute__`/`__getattr__` hooks, and
there is no class descriptor for the requested name. A built-in slot member
descriptor is accepted only when its name and index match the class slot map.
Properties, other descriptors, missing attributes, and unusual lookup errors
fall back to the original Python method. The code comment records why live
storage must be re-read rather than cached: assignments and deletions must
remain visible.

## Focused result

The diagnostic `benchmarks/diagnostics/simple_method_attr_inline_microbench.py`
measures 100,000 calls to a no-argument method returning a regular instance
attribute. Ten order-alternated control/candidate process pairs gave a median
paired speedup of **1.85x** (control median 35.4 ms, candidate median 19.1 ms).
The fixture
`tests/fixtures/core/small_self_local_attr_inline.py` checks updated values,
instance overrides and deletion, and property fallback against CPython 3.14.7.

## Official pyperformance spot check

Official pyperformance 1.14.0 fast-mode runs used the same harness and
CPython 3.14.7 dependency site for the preserved XLang3 control, XLang3
candidate, and CPython 3.14.7. The fast-mode runner warned that the samples
were unstable, so the control/candidate differences are directional evidence
only.

| Benchmark | CPython 3.14.7 | XLang3 control | XLang3 candidate | Candidate vs control | Candidate vs CPython |
| --- | ---: | ---: | ---: | ---: | ---: |
| `raytrace` | 253 ms ± 8 ms | 2.45 s ± 0.14 s | 2.34 s ± 0.10 s | 1.05x faster (pyperf significant) | 9.26x slower |
| `richards` | 37.9 ms ± 1.6 ms | 370 ms ± 18 ms | 379 ms ± 8 ms | no significant change | 9.99x slower |

```text
raytrace (lower is faster; shared linear scale)
CPython 3.14.7   0.253 s  █
XLang3 candidate 2.34 s   █████████

richards (lower is faster; shared linear scale)
CPython 3.14.7   0.0379 s █
XLang3 candidate 0.379 s  ██████████
```

The direct getter loop shows that frame avoidance can help when that exact
shape dominates. The official results show the remaining interpreter cost is
much larger: the candidate is still about 9–10x slower on these workloads.
`raytrace` has a small promising change; `richards` did not move reliably.
This does not establish a broad pyperformance improvement.

The IR and VM counters explain why this optimization did not move `raytrace`'s
instruction count. Hot vector methods such as `Vector.dot`, `Vector.cross`,
and `Vector.__add__` take both `self` and another argument; `dot` has 15 IR
instructions and `cross` has 27. The small-self inliner currently accepts
only one-parameter methods, so those operator bodies still use normal call
frames. The `raytrace` control and candidate each recorded 4,322,262 native
argument transfers and identical counts for every VM opcode. The new getter
case is active in the focused fixture, but it did not eliminate work in this
pyperformance profile. The larger opportunity is guarded inlining of simple
two-argument numeric methods while retaining normal dispatch for overrides,
descriptors, hooks, and exceptional operations.

## Validation and artifacts

- The fixture produced identical output under XLang3 and CPython 3.14.7:
  `7, 8, 99, 8, 11`.
- Release CTest: 54/55 passed. The remaining
  `xlang3_cli_visual_studio_debugpy_launch` test timed out waiting for
  `initialized` after the adapter reported that its child disconnected. This
  is the known machine/profile failure recorded in earlier performance trial
  reports, rather than a new fixture failure.
- The complete default 11-case Release regression gate passed. The largest
  candidate/control ratio was 1.024x, under the 1.10 threshold. Raw report:
  [fixed Release gate](data/small-self-local-attr-inline-fixed-release-gate-20261011.json).
- Official raw measurements: [control](data/small-self-local-attr-inline-pyperf-control-20261011.json),
  [candidate](data/small-self-local-attr-inline-pyperf-candidate-20261011.json),
  and [CPython 3.14.7](data/small-self-local-attr-inline-pyperf-cpython3147-20261011.json).
- Raytrace operation counters: [control](data/small-self-local-attr-inline-raytrace-control-counters-20261011.txt)
  and [candidate](data/small-self-local-attr-inline-raytrace-candidate-counters-20261011.txt).
- Emitted [raytrace IR](data/raytrace-benchmark-ir-20261011/run_benchmark.ir.txt)
  and the [single-run counter probe](../../benchmarks/diagnostics/profile_pyperf_raytrace_once.py).

The fixed Release executable remains at
`build-repro/main-verify-20261006/Release/xlang3.exe`; the control was preserved
at `build-repro/method-call-shadow-bypass-control-20261011/Release/xlang3.exe`.
The overall goal of beating CPython remains open. The next optimization should
target a measured dominant VM path with meaningful whole-benchmark coverage,
not assume that attribute lookup alone explains the gap.
