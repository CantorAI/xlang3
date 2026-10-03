# August 20 vs current XLang3 core throughput (2026-10-02)

The current Release runtime is substantially faster than the preserved August
20 runtime on six small, identical VM workloads. This comparison helps explain
why earlier primitive-operation results looked strong while the complete
pyperformance comparison remains weak: the broad gap is concentrated in
stdlib-heavy workloads and incomplete compatibility, rather than a general
regression in scalar arithmetic, loops, or function calls.

## Method

- The older tree is based on revision `40382a008f730e7de3c912854347bf8147643079`
  (`2026-08-20`, “Advance Python compatibility runtime foundations”). Its
  retained `build/Release-hoisted/xlang3.exe` was built on 2026-09-27, so it is
  a later performance build of the August checkout, not a binary built from
  the untouched August commit. That checkout has one tracked local edit in
  `xlang_vm_loop.cpp`; the adjacent `build-no-per-op-globals.log` records a
  rebuild of that file before the later profiling edit. The preserved build is
  the user's August-era comparison artifact, but its exact source snapshot is
  not available for a clean rebuild.
- The current tree's HEAD is
  `319a9a59564161a9247648e28ae9c39d6d852153` (`2026-10-02`, “Document
  exact-call argument transfer trial”), with additional uncommitted changes.
  The current measured executable is `build-repro/Release/xlang3.exe`; binary
  hashes below identify the tested builds more exactly than the HEAD alone.
- Both CMake caches specify MSVC Release `/O2 /Ob2 /DNDEBUG`. The August build
  is Visual Studio 18; the current build is Ninja Multi-Config using the same
  installed Visual Studio toolchain.
- Each case used the current repository's unchanged `benchmarks/cases/*.py`
  source on both executables, ten `main()` calls per process, two warmups, and
  seven AB/BA paired samples. Output matched for every case.
- Process wall time includes executable startup. The August executable takes
  about 6 ms to run a trivial `print(1)` while the current executable takes
  about 64 ms. To prevent that startup difference from distorting the workload
  ratios, each case also ran a same-source `range(0)` control; its median was
  subtracted from the ten-workload sample. Each executable used a distinct
  `PYTHONPYCACHEPREFIX` so their cached IR could not collide.
- Measured build SHA-256 pairs (executable / runtime DLL): August
  `7DEDF00C07BE04DD3E9C1ADC861374D24F543FD01F36BB8F2C79BC583CA659CF` /
  `E6699951C18976CD88A8BB0106540202C40894375627836FF833025B49898DE0`; current
  `E0E46ED011542F262ADE92263679C5E4AABFDE86756F256CF2B1C0683406EC78` /
  `17DE90B570985D98165C17884A3A0404E85A1D9E492D079CF44EB6CDC0245498`.

Because this uses external process timing and subtracts a control, treat it as
a directional interpreter-throughput diagnostic. It is not a pyperformance
score or a direct CPython comparison. The raw per-sample timings, output
digests, executable hashes, and bootstrap intervals are in
[`august-release-hoisted-vs-current-startup-adjusted-20261002.json`](data/august-release-hoisted-vs-current-startup-adjusted-20261002.json).
The trivial-process startup samples are retained in
[`august-current-startup-probe-20261002.json`](data/august-current-startup-probe-20261002.json).

## Results

Speedup is August net workload time divided by current net workload time.
The interval is a paired bootstrap 95% interval over the seven AB/BA pairs.

| Workload | August 20 → current speedup | Paired 95% interval |
|---|---:|---:|
| `local_slots` | 2.58× | 2.50–2.63× |
| `scalar_arithmetic` | 9.07× | 8.98–9.33× |
| `range_for` | 6.81× | 6.63–7.06× |
| `function_calls` | 15.53× | 15.00–16.81× |
| `class_construct` | 1.31× | 1.30–1.33× |
| `list_append` | 22.75× | 16.02–26.22× |

The large loop and call improvements are consistent with the later VM/runtime
optimizations. The modest class-construction gain and wider list-append
interval are still positive in this diagnostic. A first one-call-per-process
comparison appeared to show several severe regressions; it was invalid for
throughput because the current executable's startup dominated those short
workloads. That preliminary measurement is intentionally excluded from the
results above.

## Implication for the active performance work

These results do not erase the CPython gap. The latest retained complete
pyperformance attempt is still the 97-definition bounded run: 30 definitions
matched, XLang3 won 3, and the matched geometric mean was 0.20821×. See the
[full report](pyperformance-xlang3-current-quantize-control-full-fast-bounded-20261002.md).
The same report records large gaps in `telco`, `subparsers`, pure-Python
pickle, and `deltablue`, plus 67 definitions that died or timed out. The August
comparison shows that further generic loop-speed tuning is unlikely to close
those gaps by itself. The next optimization should be selected from a
case-specific profile of those Python-library call/attribute paths, and each
candidate still needs the fixed Release regression gate.

No performance code changed for this diagnostic. It identifies the scope for
the next experiment and avoids treating process-startup cost as steady-state
benchmark throughput.
