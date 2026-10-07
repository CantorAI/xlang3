# Live function keyword defaults block NetworkX (2026-10-06)

The full rebuilt Release run failed all three NetworkX definitions before
timing their workloads: `networkx`, `networkx_connected_components`, and
`networkx_k_core`. Their tracebacks reach `networkx.utils.decorators.argmap`
and fail when `_lazy_compile` reads `func.__argmap__`.

NetworkX creates a Python wrapper with a keyword-only default, then updates
`func.__kwdefaults__` in place to make that default reference the wrapper
itself. The tested Release build's `object_model.cpp` constructs a fresh dictionary
on every `__kwdefaults__` read. Assignment also copies dictionary entries into
the function's separate defaults storage. Thus neither mutation of the
returned dictionary nor later mutation of an assigned dictionary reaches the
argument binder. This is a generic function-metadata defect; NetworkX should
continue executing its own Python decorator code.

## Verified small reproduction

Run [the correctness probe](../../benchmarks/diagnostics/function_kwdefaults_live_probe.py)
with each executable:

```powershell
& 'C:\Python\Python314\python.exe' benchmarks\diagnostics\function_kwdefaults_live_probe.py
& '.\build-repro\main-verify-20261006\Release\xlang3.exe' benchmarks\diagnostics\function_kwdefaults_live_probe.py
```

| Check | CPython 3.14.7 | XLang3 Release |
| --- | --- | --- |
| Repeated reads return the same dictionary | True | False |
| In-place mutation reaches the next call | True | False |
| Removing a default makes the argument required | True | False |
| Assignment preserves the assigned dictionary's identity | True | False |
| Later external mutation reaches the next call | True | False |

The [captured output](data/function-kwdefaults-live-probe-20261006.txt)
records both runs. This is a short correctness check, not a speed measurement.
It ran during the existing full-suite process; no performance claim comes
from it.

The fixed executable SHA-256 is
`A36B8CF5087BCC4E5FCF12F2A81BD528B6104A36663DFEB49B80D5B61B33C7E0`;
the runtime DLL SHA-256 is
`828F125B1C6BEE78519F54A3EAA4D5E4F8356C25530ED038FD26A8EC9B06CA4A`.

## Required runtime design

The runtime needs one authoritative mutable dictionary shared by introspection,
assignment, and keyword-only default binding. Preserve fast indexed default
binding for functions whose defaults have never been exposed or reassigned;
do not rebuild dictionaries or rescan defaults on every ordinary Python call.
Once exposed, calls must observe additions, replacement values, deletion,
`clear()`, aliases shared between functions, and `__kwdefaults__ = None`.
Changing positional defaults must preserve keyword-only defaults.

This finding proves the dictionary defect and identifies the corresponding
NetworkX decorator operation. It does not prove that fixing it will complete
all NetworkX benchmarks; later generated-code or runtime failures may remain.
After the ongoing all-97 run completes, implement and validate the generic
runtime correction, run the affected official NetworkX cases, and pass the
complete fixed Release regression gate before committing an engine change.
The live benchmark executable must stay unchanged until that run finishes.

## Candidate prepared; build and timing validation pending

The working-tree candidate adds a lazily materialized `kwdefaults_dict` to
function metadata. Function calls carry a non-owning view of that stable field
through the existing argument view; only omitted keyword-only parameters read
the exposed dictionary. Unexposed functions retain the existing indexed
default values. Materialization clears the old indexed keyword values and
metadata copies so deleting a default also releases it. Assignment preserves
dictionary identity, including dict subclasses, and changing positional
defaults preserves keyword defaults.

The candidate includes the dictionary in reference traversal and version 3
function serialization. Versions 1 and 2 remain readable. A serialization
fixture checks that two functions and a separate graph entry retain one shared
dictionary after restoration, and that later mutation changes both calls.

`tests/fixtures/core/function_kwdefaults_live.py` is registered in the full
fixture runner. Its CPython 3.14.7 reference run passed. It covers direct and
expanded calls, bound methods, generator argument binding at creation,
dictionary mutation/aliasing/deletion, dict subclasses, `types.FunctionType`,
and release of removed defaults. It also reproduces NetworkX's lazy-wrapper
pattern: copying positional defaults and installing the self-reference under
the mangled keyword-only name. Constructors, properties, coroutine argument
binding, and collection of a self-default cycle are included. The expanded reference fixture passed on CPython 3.14.7. The initial
keyword-default candidate is now validated as recorded below. Its official
NetworkX attempts still failed, leading to the separate code-replacement
investigation; neither compatibility fix alone establishes a speedup.

## Candidate validation and remaining NetworkX failure

The candidate now builds in the existing Release directory using the VS 18
environment and its existing Ninja build. No CMake configuration was rerun.
The expanded fixture passes under XLang3 and CPython 3.14.7. The complete
fixture runner, runtime/interpreter C++ tests, SDK stream/call tests, and the
graph producer/consumer test also pass. The graph consumer validates shared
dictionary identity, dict subclasses, and subsequent mutation without the
producer's Python source.

Two test failures were corrected before broader validation: invalid defaults
assignment now raises TypeError through both direct assignment and setattr;
the isolated function-cycle collector now considers keyword-default
dictionaries as well as attribute dictionaries. It retains the fast rejection
for ordinary unexposed defaults and verifies internal reference counts before
clearing cyclic metadata.

The complete fixed Release performance gate passed every case. Its largest
candidate/baseline ratio was 1.024 (about 2.4% slower), with the original 10%
threshold and all default cases. The report is
[here](data/keyword-defaults-live-fixed-release-gate-20261006.json).
Candidate runtime DLL SHA-256:
`5D02C1CEA281BAB4308B00F6600A9D64E16D24572BC48041A4ADEBFDE4A55FBC`.

The three official NetworkX cases were attempted again and still fail at
`__argmap__`; see the [runner log](data/pyperformance-xlang3-keyword-defaults-networkx-fast-20261006.log).
The generic keyword-default defect is corrected, but this has **not unblocked
the official workloads**. No new NetworkX timing or speedup is claimed.

The next required lazy-wrapper operation also has a verified runtime defect:
assigning `function.__code__` changes neither warmed calls nor calls at a new
site. [The small code-swap probe](../../benchmarks/diagnostics/function_code_swap_probe.py)
returns the replacement's result under CPython 3.14.7 and the original result
under XLang3. Implementing replacement must preserve active frames, positional
and keyword defaults, closure compatibility, and invalidate specializations
derived from the old body before rerunning the official cases.

The combined keyword-default/code-replacement candidate now passes the full
correctness suite and fixed Release gate. See the [code-replacement checkpoint](function-code-replacement-networkx-20261006.md)
for the exact hashes, retained code flags, serialization format 4, and the
remaining inner NetworkX wrapper failure.
