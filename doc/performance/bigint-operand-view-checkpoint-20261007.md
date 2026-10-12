# Generic bigint operand and loop-order checkpoint — 2026-10-07

Official `pidigits` improved from **397.2 ms** to **291.8 ms**, a nominal **1.3610×** speedup and **26.53% less time**. CPython 3.14.7 measured **174.9 ms** in the fresh full-suite reference: XLang3 still takes **1.669×** as long. These are independent fast-mode runs with stability warnings, not a paired significance test.

## Why operand copies alone were insufficient

The initial read-only-operand trial improved isolated multiplication samples, but official `pidigits` was **399.2 ms**, versus **397.2 ms** before. That trial established no benchmark gain. Its binary, source snapshot, diagnostics and passing fixed gate are preserved separately.

CPython orders multiplication operands by limb count before selecting its schoolbook kernel. XLang3 previously put the source left operand in the outer loop regardless of its size. For a large integer times a scalar, this restarted carry handling at every large-integer limb. The final kernel puts the shorter immutable view outside and traverses the longer operand contiguously inside. The Python operand-conversion order is unchanged.

[CPython 3.14.7 operand ordering](https://github.com/python/cpython/blob/v3.14.7/Objects/longobject.c#L3761). This applies the ordering principle to XLang3’s own arithmetic kernel; no Python library algorithm was translated to C++.

## Ownership and correctness

- `BigIntOperandView` contains only read-only limb pointers and is a different type from owned `BigIntPayload`; cleanup, normalization and compaction cannot accidentally accept a borrowed view.
- Exact Int64, Bool and BigInt operands use borrowed immutable storage or two stack scalar limbs. The result is computed independently before output assignment, including when output aliases either input or both.
- Wrapper/subclass attribute conversion retains owned payload copies in the original order. A C++ reentry test replaces the last external left-operand owner during right-operand conversion and forces allocation reuse; the product must still use the original left value.
- C++ cases cover sole-owner left/right aliases, self-squaring, retained input contents, signs, bools, zero compaction and INT64_MIN boundaries. The full fixture suite and all eight C++/SDK/graph tests passed on the final build.
- Both candidates produce the same 2000 pi digits as CPython. The final digit-byte SHA-256 is `e7cb4bbb129d29f035c29cf1f088673788a82ebbbd61b466257e12ece4871aac`.
- The complete fixed accepted Release gate passed all 11 cases, with default 21 paired repeats, five warmups, 10% tolerance and exit 0. The accepted baseline hashes are unchanged.

Performance comments in the code preserve the no-callback guard, immutable input ownership, alias-safe publication, wrapper reentry fallback and shorter-outer-loop rationale.

## Diagnostic multiplication measurements

The 400-iteration preliminary samples are preserved but excluded from the comparison because their duration was too short. The table uses five samples of 20,000 operations before/after, including Python loop and assignment overhead. These are diagnostic medians, not official pyperformance scores.

| Case | XLang3 before | XLang3 after | CPython 3.14.7 | Before / after |
| --- | ---: | ---: | ---: | ---: |
| `int64-overflow` | 293.2 ns | 237.9 ns | 31.2 ns | 1.232× |
| `big-small-128` | 377.6 ns | 302.6 ns | 30.79 ns | 1.248× |
| `small-big-128` | 401 ns | 285.4 ns | 30.4 ns | 1.405× |
| `big-big-128` | 490.4 ns | 377.6 ns | 49.68 ns | 1.299× |
| `square-128` | 468.1 ns | 385.5 ns | 44.69 ns | 1.214× |
| `negative-128` | 374.6 ns | 313.2 ns | 30.32 ns | 1.196× |
| `zero-128` | 224.1 ns | 194.7 ns | 19.79 ns | 1.151× |
| `big-small-1024` | 437.1 ns | 360.4 ns | 47.08 ns | 1.213× |
| `small-big-1024` | 406.5 ns | 310.8 ns | 47.36 ns | 1.308× |
| `big-big-1024` | 1.316 µs | 1.129 µs | 822 ns | 1.166× |
| `square-1024` | 1.241 µs | 1.167 µs | 492.5 ns | 1.063× |
| `negative-1024` | 450.3 ns | 314 ns | 48.25 ns | 1.434× |
| `zero-1024` | 220.9 ns | 173.7 ns | 20.74 ns | 1.272× |
| `big-small-4096` | 559.9 ns | 425.1 ns | 145.9 ns | 1.317× |
| `small-big-4096` | 504 ns | 419.1 ns | 148.2 ns | 1.202× |
| `big-big-4096` | 13.09 µs | 12.35 µs | 7.005 µs | 1.060× |
| `square-4096` | 13.76 µs | 12.24 µs | 7.419 µs | 1.124× |
| `negative-4096` | 629.4 ns | 420.1 ns | 157.2 ns | 1.498× |
| `zero-4096` | 267.5 ns | 190.9 ns | 21.51 ns | 1.401× |

## Native sampling and limits

The initial candidate executed the unchanged official body twenty times after a fresh import/warmup marker. Exact digits matched CPython. The Windows sampler captured instruction pointers, not call stacks or timing scores; Release symbol resolution was unavailable, so clustered addresses are not assigned to a particular function or claimed as exact CPU-time shares. The loop-order experiment was selected from source comparison and validated with the official benchmark.

The candidate path remains `build-repro/main-verify-20261006/Release/xlang3.exe`. The pre-change factorial Release is preserved at `build-repro/controls/factorial-owned-limbs-checkpoint-20261007`; the initial no-gain trial remains at `build-repro/controls/bigint-operand-view-initial-trial-20261007`.

[Checkpoint proof and all raw input hashes](data/bigint-operand-view-checkpoint-20261007.json) retain both trials. Raw and canonical-LF native-source hashes account for Git line-ending normalization. Targeted results do not replace or splice into the frozen all-97 report. The broad goal to beat CPython remains unfinished.
