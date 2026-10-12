# XLang3 performance goal: portable agent prompt

Recorded on 2026-10-12. This is a consolidated working prompt from the user's
performance goal and instructions, intended to start or continue the job on
another computer. Paths and historical timings below describe the original
machine; establish the new machine's own setup and measurements.

The user paused optimization to complete the write-up and source handoff.
Read the [complete source checkpoint](source-transfer-checkpoint-20261012.md).
This prompt describes the job to resume when the user authorizes continuation;
it does not override a current pause.

## Prompt to give the agent

Work on the CantorAI/XLang Foundation XLang3 repository:
<https://github.com/CantorAI/xlang3>. Continue the performance job from the
committed `main` branch. Read `AGENTS.md` and the performance evidence before
editing the engine.

### Goal

The recorded goal is:

> Diagnose and materially reduce XLang3's full pyperformance slowdowns and
> failures versus CPython 3.14, preserve benchmark evidence and add durable
> performance design comments/tests, then validate the improvements against
> the fixed Release baseline.

The user's intended outcome is stronger: make XLang3 substantially faster and
beat **CPython 3.14.7** on a defensible full pyperformance comparison. Seek
large, repeatable improvements to real workloads. A passing local regression
gate or a large win in a few custom microbenchmarks does not finish this goal.
Keep the goal open until the evidence supports the claimed outcome.

### Set up this computer

1. Inspect the checkout, current branch, commit and local changes. Fetch the
   latest `main` and incorporate upstream fixes without discarding local work.
   Record the exact source commit and any additional patch being tested.
2. Locate **CPython 3.14.7** and verify the executable's actual version. Use it
   as the comparison runtime and benchmark manager. Never silently substitute
   Python 3.13, another 3.14 patch release, or a different standard library.
   If 3.14.7 is unavailable, resolve that setup requirement before reporting
   benchmark comparisons. Verify the stdlib and dependency paths too.
3. Inspect the current CMake configuration and available toolchain. Build
   XLang3 in Release mode. On Windows use the installed Visual C++ developer
   environment; on macOS/Linux use the supported native compiler configuration.
   Reuse an existing suitable build directory. Choose a fixed run path once
   if this is a fresh checkout, and keep it stable during the job.
4. Define and record the host's actual repository, CPython executable, fixed
   XLang3 Release executable, preserved Release baseline and evidence paths.
   The original Windows paths are examples, not requirements on this computer.
5. Preserve the accepted Release executable and all required runtime libraries
   before rebuilding a candidate. On this computer, establish fresh CPython
   and XLang3 results using the same workloads and measurement settings.
   Historical measurements from another machine provide context, not a fresh
   same-machine speed comparison.

Original Windows setup, for reference:

```text
Repository: D:\CantorAI\xlang3
CPython: C:\Python\Python314\python.exe  (3.14.7)
Fixed XLang3: build-repro\main-verify-20261006\Release\xlang3.exe
Saved r5 baseline: build-repro\r5-opcode-profile-baseline\Release\xlang3.exe
Build command: cmd.exe /c doc\performance\data\vm-active-code-view-reproduction-build-command-20261009.cmd
```

That command contains machine-specific paths. Use it on the original machine;
adapt the toolchain paths deliberately on a new host. Avoid repeatedly changing
folders or rerunning failed CMake setup commands without diagnosing the failure.

### Implementation rules

- Keep CPython's pure-Python libraries in Python. Speed them up through XLang3's
  compiler, lowering, IR, VM, Value representation and general runtime paths.
  Do not replace a pure-Python library implementation with C++ to win a benchmark.
- When CPython itself uses a native module such as `_json` or `_pickle`, an
  XLang3-owned native replacement is allowed. Preserve the same import name and
  compatible Python-facing API/ABI. Do not use CPython's native implementation
  or a CPython bridge as XLang3's execution fallback to manufacture a win.
- Preserve Python semantics, including subclass overrides, descriptors, dynamic
  rebinding, code/default changes, exceptions and tracebacks, monitoring,
  finalizers, weak references and buffer lifetimes. Guard optimizations and keep
  the correct general path when assumptions fail.
- Inspect actual memory-management code before making GC claims. Preserve
  required reference counting and cycle handling; do not disable correctness
  features to improve a score.
- Add comments beside accepted performance designs explaining the avoided cost,
  the guard assumptions, ownership/lifetime requirements, invalidation and
  fallback. Make those comments useful to future agents maintaining the code.

### Investigation method

Start from slow official workloads and measured cost. Identify costs shared by
several workloads, especially ordinary Python calls, VM frame execution,
attribute/container operations and Value ownership. Indexed names and tagged
scalars are advantages, but their presence alone does not prove a fast hot path.

Read matching prior trials and preserved patches in `doc/performance` before
starting an experiment. Do not repeat a rejected mechanism under a new name.
Revisit it only when new evidence changes the expected effect. Consult the exact
CPython 3.14.7 source where useful and compare the executed paths, guards,
allocation and ownership work.

Investigate the August checkpoint and subsequent changes with the same workloads
where they are supported. The August 31 checkpoint `8beae11d` had no official
pyperformance case marked supported in its support list. Do not claim that an
August microbenchmark win proves an August full-suite win or a measured
August-to-current regression.

Use small fixtures and diagnostics to localize a hypothesis, then return to the
unchanged official benchmark. Rank work by potential effect on total workload
time, not just how frequently a function appears in a counter report. Distinguish
opcode frequency from time attribution and instrumented profiles from normal
Release timings. Evaluate dispatch changes or native execution/JIT only through
measured experiments; the existing AsmJit design document is a proposal, not a
completed backend or a proven speedup.

### Benchmark and validation requirements

- Use the **whole pyperformance suite**. The historical suite is pyperformance
  1.14.0 with 97 definitions; pin that version for comparable follow-up results.
  Attempt every definition, retain failures/timeouts and report the complete
  case list. A failed case is an acceptable recorded outcome, not permission
  to omit it or treat partial results as successful benchmark scores.
- Run the same official Python benchmark source and workload on both runtimes.
  Audit any necessary harness/platform shim; it must not change the timed
  computation, its size or its semantics. Keep CPython's manager role separate
  from the runtime whose worker is being timed.
- Keep builds and other benchmark processes out of timed runs. Use matched,
  order-balanced candidate/control measurements on an idle machine. Preserve
  raw pyperf data and warnings. Fast-mode results are directional; use stronger
  repeated measurements when making a material speedup claim.
- After focused correctness tests, run the complete default
  `benchmarks/check_regression.py` suite against the fixed accepted Release
  baseline. Current defaults are 21 repeats, five warmups and a 0.10 threshold.
  Exit 0 passes; exit 1 is regression; exit 2 is inconclusive. Do not weaken the
  gate, shorten workloads, remove cases or move the baseline to hide a slowdown.
- Measure an optimization targeting a pyperformance case with that official
  case as well as the fixed local gate. Validate affected fast-path eligibility
  and dynamic invalidation, not only output. Run relevant fixtures/native tests
  and document unresolved failures accurately.
- Re-run the full suite at meaningful checkpoints and retain all-case status,
  matched timing tables and charts. Record source/patch identities, executable
  and runtime-library hashes, CPython/stdlib/dependency versions, platform,
  commands and benchmark settings so results can be reproduced.

Display speed as **CPython time / XLang3 time**: 2.0x means XLang3 is twice as
fast; 0.5x means XLang3 is twice as slow; 0.05x means twenty times as slow.
Label the ratio consistently. Use horizontal bars where helpful. Aggregate only
eligible completed matched cases, disclose the denominator and failures, and
never mix failed partial values or different machines into a speed claim.

### Checkpoints and communication

Keep progressing toward the performance goal and give concise updates showing
what was learned, measured results and the next uncertainty to resolve. Avoid
invented completion percentages, repeated unchanged checks and claims that a
microbenchmark improvement closes the full-suite gap.

At a validated checkpoint, commit the **actual source code**, its necessary
dependencies, tests, performance comments and reproducible evidence to `main`,
then push to GitHub. Review staged contents so unrelated work and generated
build artifacts are not included accidentally. State which source changes the
checkpoint owns and disclose any remaining uncommitted work. A checkpoint can
be a narrow improvement; label its scope honestly. Follow `AGENTS.md` before
committing engine changes, including its required passing performance gate.

Collect and maintain repository data/docs for the XLang Foundation's future
website. No webpage implementation is requested in this job.

## Starting evidence available in GitHub

- [Repository performance rules](../../AGENTS.md).
- [Corrected full r5 comparison](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r5-vs-cpython3147.md):
  97 attempted, 61 completed, 36 failures/timeouts, 70 matched subtests,
  one nominal win, geometric mean speed ratio 0.14125x (about 7.1x slower).
  These are historical Windows fast-mode results, not this computer's baseline.
- [CPython VM comparison](cpython314-vm-comparison-20260930.md).
- [Previously rejected call-owner trial](call-module-owner-call-entry-trial-20260930.md).
- [Context.run tuple checkpoint](context-run-tuple-star-fastpath-trial-20261012.md),
  committed as `568523e5`: source, fixture and evidence are included. The candidate
  showed about 1.25x improvement in a focused tuple-call diagnostic but no
  significant official async-tree improvement. The report records the limits of
  attributing aggregate working-build results to a source subset.
- [Existing IR execution / AsmJit proposal](../ir_execution_performance_asmjit.md).

The subsequent [complete source checkpoint](source-transfer-checkpoint-20261012.md)
includes the remaining live engine source, required build dependencies, tests
and linked trial evidence. A new clone can build that source state. The original
Windows binaries, build directories, package caches and unlinked scratch
artifacts are not transferred; inspect and rebuild the actual committed sources
on the new computer.

## Short launch message

Read `doc/performance/performance-goal-agent-prompt.md` and carry out its
performance goal on this computer. Use CPython 3.14.7, establish fresh comparable
Release baselines, improve the shared execution costs, preserve Python semantics
and performance design comments, and publish validated source/evidence checkpoints
to GitHub `main`.
