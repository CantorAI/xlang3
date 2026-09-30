# VM instruction-cache storage trials (2026-09-30)

Both sparse-storage experiments were rejected. The unchanged official
pure-Python unpickle benchmark showed no speed improvement. The accepted
Release executable and runtime were restored together and SHA-256 checked.
CPython's pure-Python library code remains Python; these experiments changed
only generic XLang3 interpreter cache storage.

## Measurements

The benchmark is pyperformance 1.14.0's unchanged `bm_pickle/run_benchmark.py`,
with `--fast --pure-python --protocol 5 unpickle`. Each result has 20 measured
values from ten worker processes, plus a calibration process. These are fast
screening runs, not rigorous scores or a complete suite rerun. Pyperf warned
that there were too few samples to establish less than 1% variability.

| Trial | Paired parent | Candidate | Candidate speed, parent = 1.0× |
|---|---:|---:|---:|
| Lazy materialization | 3.8913 ±0.0356 ms | 3.9236 ±0.0398 ms | 0.992× |
| Precomputed dense index | 4.0700 ±0.0522 ms | 4.1486 ±0.1071 ms | 0.981× |

`pyperf compare_to -v` reported respectively 1.01× slower (t = -2.71) and
1.02× slower (t = -2.95). The same parent runtime also varied between the two
pairs. Compare each candidate with its own control; do not compare candidates
across pairs or interpret these small differences as rigorous regression
estimates. Neither result supports retaining the proposed optimization, so
neither candidate proceeded to the full Release performance gate.

Raw pairs: [lazy parent](data/unpickle-lazy-cache-parent-fast-20260930.json),
[lazy candidate](data/unpickle-lazy-cache-candidate-fast-20260930.json),
[precomputed parent](data/unpickle-prebound-cache-parent-fast-20260930.json),
[precomputed candidate](data/unpickle-prebound-cache-candidate-fast-20260930.json).
The corresponding `.log` files retain warnings and worker output.

## Design and correctness

The ordinary runtime allocates a 424-byte `XlangVMInstrCache` record for every
IR instruction. That record holds the adaptive core, global/call/attribute
payloads, and monitoring state, even when an instruction uses none of them.
The [standalone layout diagnostic](../../benchmarks/diagnostics/native_vm_cache_layout.cpp)
measures actual C++ sizes without instrumenting a Python workload.

The lazy trial used an instruction-to-record index and constructed records
only on first access. It reserved enough record capacity for all instructions
because cached references used by fused guards and monitoring must survive
later materialization. Reservation limits the proposed allocation benefit;
the extra lookup/branch also affects hot accesses. This trial does not prove
lower resident memory usage.

The second trial precomputed dense indices in immutable execution metadata
for all eligible cache-owning sites. Ordinary accesses used the index without
a materialization branch; monitoring-only sites used stable map nodes.
The wrapper increased both native frame size and prepared-state size by
80 bytes. It still added an indirection to each ordinary cache access.
Allocation counts and resident memory were not measured. These costs are
possible explanations for the observed timings, not separately proven causes.

Both final trial variants passed all 302 core fixtures, 11 compatibility
sections, and three expected failures. The new
[`vm_cache_materialization` fixture](../../tests/fixtures/core/vm_cache_materialization.py)
warms one branch before activating late cache sites, mutates a method, enables
instruction monitoring with `DISABLE`, restarts events, and checks that an
attribute-cache owner dies after return. Cache cleanup kept instruction order
to preserve finalizer behavior. The restored accepted runtime also passed
the entire fixture runner with this new case. The later scalar-site sections
of that fixture were added during the cleanup follow-up and were not part of
these sparse-storage trial runs.

Saved engine patches apply to parent revision `e4d9e90` using
`git apply --unidiff-zero`:
[lazy storage](data/lazy-cache-storage-trial-20260930.patch),
[precomputed storage](data/prebound-cache-storage-trial-20260930.patch).
Layouts: [parent](data/native-vm-cache-layout-parent-20260930.txt),
[precomputed](data/native-vm-cache-layout-prebound-20260930.txt).
Do not apply both patches together. They are rejected experiments, not engine
changes retained on main.

| Native layout | Parent | Precomputed |
|---|---:|---:|
| `XlangVMInstrCache` | 424 bytes | 424 bytes |
| `XlangVMPreparedFunctionState` | 56 bytes | 136 bytes |
| `XlangVMFrame` | 4,752 bytes | 4,832 bytes |

## Build identities and remaining work

All measurements used Release, with separate import-cache directories and no
concurrent build or benchmark. Parent source was `e4d9e90`; the candidate
patches record all engine changes. Both binaries were preserved for each
trial.

| Build | `xlang3.exe` SHA-256 | `xlang3_runtime.dll` SHA-256 |
|---|---|---|
| Parent / restored | `243F6A336E317FF60942217BFEA77632182B917B7A4DCCD72E7FD333356CC7C0` | `2C371933BDE80E66CE85122C3516278BCDD2674DE209B281A5DC9275C17FBEAA` |
| Lazy | `0BF60F9B3EEB84E1B34AADF3763C25058603B6DD3429E84D604482DE8B09958F` | `88E05765FEE6858840BCE9C37E8495D2D22960D6A5962E2EF0A8F6006741D90F` |
| Precomputed | `921553F822DC52D91912924756433BF127F5DCCD5077CCF340BF32920C712853` | `23735F04019E05BC6302543FA2ABD73EB4D82EC98F7B26D1ADEB3E941A732DD1` |

This investigation does not update the CPython comparison or claim a suite
win. At the time these trials were rejected, the retained rigorous result was
[the MRO reuse result](subclass-mro-reuse-20260930.md):
XLang3 3.9671 ms versus CPython 3.14.7 0.16379 ms, or 24.22× longer
(0.041× speed, CPython = 1.0×). The goal is unmet. The next investigation
measured work performed on every call/return before changing another cache
representation. The subsequent [cleanup follow-up](vm-cache-domain-cleanup-20260930.md)
records that profile's outcome and the later ordinary runtime change.
