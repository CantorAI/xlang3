# Generator object cache trial (2026-10-03)

## Result

Retained as a narrow coroutine improvement. A bounded thread-local cache for
short-lived generator objects reduced the official pyperformance 1.14.0
`coroutines` mean from **159.5 ms** across two control runs to **145.3 ms**
across four candidate runs, about **1.10× faster** (9% lower time). Three of
the four candidate runs were significant against each control run; the fourth
was not. Individual runs still reported substantial host variation, so the
result is a measured improvement, not a claim of stable 1% precision.

This does not close the CPython gap: the candidate mean remains about **8.1×
slower** than CPython 3.14.7's 17.9 ms result. `async_tree_eager` showed a
4% decrease that was not significant. `async_generators` was unchanged within
noise. The first `generators` sample was 5% faster and significant against its
saved full-suite control, but the independent candidate repeat overlapped the
control; treat that result as inconclusive.

## Why this change

An earlier allocation profile for `coroutines` counted **242,785 Generator
allocations** in one Fibonacci-await workload, after Function and Instance
allocations had already fallen to near zero. Each new generator used `new`
and entered the process-wide GC tracking vector; final release removed it
again. The cache keeps at most 256 small-argument generators per thread. It
clears owned references and VM continuations before caching and retains the
zero-reference object's GC index, avoiding the heap allocation and the global
tracker lock on reuse. The existing GC snapshot logic ignores cached
zero-reference objects. The rationale and lifetime constraints are recorded
next to the cache implementation in `src/runtime/generator.cpp` and
`src/runtime/value.cpp`.

This changes XLang3 runtime object management only. It does not replace or
reimplement any CPython pure-Python standard-library module.

## Measurements

All measurements used the fixed Release executable path
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`, CPython 3.14.7's
standard library, pyperformance 1.14.0, the shared dependency site, and the
Windows compatibility shim. Runs used `--fast`; pyperf reported 10 runs and
20 values per benchmark file.

| Benchmark | Control | Candidate | pyperf comparison |
|---|---:|---:|---|
| `coroutines` | 157 ms ± 6; 162 ms ± 19 | 147 ms ± 18; 144 ms ± 12; 138 ms ± 4; 152 ms ± 20 | Three of four candidates significant against both controls; aggregate means 1.10× faster |
| `async_tree_eager` | 3.35 s ± 0.23 | 3.22 s ± 0.21 | Not significant |
| `generators` | 392 ms ± 21 | 374 ms ± 30; 379 ms ± 43 | First candidate was significant; repeat was not |
| `async_generators` | 2.59 s ± 0.27 | 2.57 s ± 0.21 | Not significant |

CPython's saved `coroutines` result is **17.9 ms ± 0.6 ms**. `pyperf
compare_to` marks every XLang3 candidate run significantly slower than this
reference, by 7.75–8.50×. The aggregate XLang3 candidate/control estimate uses
the arithmetic means above and is directional; it is not a pyperf combined
confidence interval.

The control used executable SHA-256
`C10F14F7D876E371F747803770109E76888AF7B704EA8E56973738320D051492` and runtime
DLL SHA-256
`A7278E14500C52312966CB2E92956119F9638D4D6CB42475DAA990C268AAE61A`. The
candidate used executable SHA-256
`69C551F01CA5BA4C0F75E16A52D8D900543E11D155698AA69E8BF84F9A7A9312` and runtime
DLL SHA-256
`A220AF1F95BF4592873414C20AA8E0AFDAC40C91168731F9ACC10C00D55AB8B2`.

Raw pyperf files:

- Controls: [coroutines run 1](data/generator-cache-coroutines-control-r1-20261003.json), [coroutines run 2](data/generator-cache-coroutines-control-r2-20261003.json).
- Candidates: [coroutines run 1](data/generator-cache-coroutines-candidate-r1-20261003.json), [run 2](data/generator-cache-coroutines-candidate-r2-20261003.json), [run 3](data/generator-cache-coroutines-candidate-r3-20261003.json), [run 4](data/generator-cache-coroutines-candidate-r4-20261003.json), [async tree](data/generator-cache-async-tree-candidate-20261003.json), [generators run 1](data/generator-cache-generators-candidate-r1-20261003.json), [generators repeat](data/generator-cache-generators-candidate-r2-20261003.json).
- The matching CPython reference and full-run XLang3 controls are in the [Python 3.14 comparison data](data/pyperformance-xlang3-python314-refcount-release-full-fast-20261003.json) and [CPython 3.14 data](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).

## Correctness and limits

Nine focused fixtures passed: weak-reference invalidation and thread lifetime,
generator frame/result behavior, `yield from`, coroutine syntax, Tasks,
asyncio runtime edges, and suspended async-generator exceptions. The C++
`xlang3_runtime_value_tests` and `xlang3_interpreter_tests` also passed. The
full fixture runner still stops at the existing `ctypes_pointer_return`
failure because libffi is unavailable; no full fixture pass is claimed.

The benchmark still points to larger costs in coroutine execution than object
allocation alone. The next optimization must account for why this bounded
cache helped isolated `coroutines` but did not produce a detectable
`async_tree_eager` gain.
