# Native I/O and descriptor paths required by NetworkX (2026-10-06)

The preceding function-metadata checkpoints exposed a missing native I/O
protocol: `gzip.GzipFile` could decompress bytes but could not iterate over
lines. After correcting that protocol, small graph probes exposed two generic
attribute/call defects. These changes preserve NetworkX and gzip as their
existing Python implementations.

## Native I/O iterator protocol

XLang3's `_io._IOBase` registered `readline` but omitted `__iter__` and
`__next__`. CPython implements these in its native I/O base; see the
[3.14.7 implementation](https://raw.githubusercontent.com/python/cpython/v3.14.7/Modules/_io/iobase.c).
Its iterator checks the virtual closed attribute and returns the stream;
the next operation calls the dynamic `readline` and ends on an empty result.

The XLang3 counterparts retain stream identity, subclass properties and
attribute hooks, method rebinding, errors from Python readline implementations,
and an empty-argument StopIteration at EOF. Byte and text results need only an
emptiness check; text does not need a UTF-8 character-count scan. Other sized
results use the runtime length primitive. Runtime primitives are independent
of Python rebinding `builtins.getattr` or `builtins.len`.

Implicit native attribute/length operations call the original primitive
callbacks directly, avoiding artificial builtin C-profile events. The actual
Python `GzipFile.readline` implementation still executes normally.

## Getter-capable descriptor precedence

NetworkX stores graph dictionaries through setter-only descriptors that also
clear cached views. XLang3's VM incorrectly returned those descriptor objects
ahead of the initialized dictionaries. A data descriptor needs a getter to
preempt instance storage on a read. Assignment dispatch still honors its
setter.

The VM now caches read descriptors only when they have both data-descriptor
and getter behavior. Changes to a descriptor type's `__get__`, `__set__`, or
`__delete__` invalidate owner/subclass caches on the cold mutation path. This
also handles a previously plain object becoming a descriptor. It avoids a
global generation check on every ordinary cached attribute read. Comments
beside both paths explain the precedence and performance requirements.

## Callable descriptors with keyword arguments

The small graph probe next reached `Graph.number_of_edges`, which calls
`self.degree(weight=weight)`. The keyword method-call fallback attempted to
call the cached-property descriptor itself instead of resolving its result.
It now performs the same descriptor lookup as an attribute read followed by
a function call. Cached ordinary Python-function method calls keep their
existing direct path. A returned bound method retains its own receiver.
Getter exceptions retain their type and Python frames.

## Validation and provenance

The three registered fixtures are:

- [I/O base iteration](../../tests/fixtures/core/io_base_iteration.py): bytes,
  Unicode, EOF, sized results, overrides/hooks, closed streams, and errors.
- [Setter-only descriptors](../../tests/fixtures/core/descriptor_setter_only_reads.py):
  instance storage, cache resetters, live type mutation, and inherited owners.
- [Callable keyword descriptors](../../tests/fixtures/core/descriptor_callable_keyword.py):
  property/cached-property callables, receiver identity, replacement, errors,
  and absence of an artificial getattr profiling event.

All three pass under CPython **3.14.7** and XLang3. The complete fixture suite,
C++ runtime/interpreter tests, SDK stream/call test, and graph producer/consumer
test also pass. The small compressed-graph probe now loads its graph and
checks shortest path, connected components, and k-core successfully; its
[captured output](data/networkx-runtime-protocols-candidate-probes-20261006.txt)
matches CPython. This is correctness evidence, not a timing comparison.

The [complete fixed Release gate](data/networkx-runtime-protocols-fixed-release-gate-20261006.json)
passes all 11 cases with the original 21 repeats, 5 warmups, and 10% threshold.
The largest candidate/baseline ratio is 1.047. Function calls measure 0.973,
properties 0.998, constructors 1.010, and JSON dumps 0.991. These compare
XLang3 builds and do not establish speedups over CPython.

The build uses the existing VS 18 environment and Ninja files. The executable
path remains
`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`.
The fixed accepted baseline in `build-repro/Release` remains unchanged.
CPython is `C:\Python\Python314\python.exe`, version 3.14.7.
Candidate SHA-256:

- Executable: `8764D542781B69A7C71D0EAEE94B1FC67F02313B9332D8FEBBE4B1F20F76277E`.
- Runtime DLL: `5C9FD3500EC4B1F278A9BE49D6C864C743FB1922BE1F488770184DE0D922F191`.

The [official three-case NetworkX run](data/pyperformance-xlang3-runtime-protocols-networkx-fast-20261006.log)
finished with all three cases exceeding the 300-second full-case cap. It
produced no completed timings and cannot establish a speedup. Unlike the
preceding failures, the runs proceed through graph setup and algorithm calls;
the small probe separately verifies their results. The
[provenance](data/pyperformance-xlang3-runtime-protocols-networkx-fast-20261006-provenance.json)
records matching executable/DLL hashes before and after the run.
The local candidate includes existing worktree
changes outside this checkpoint; no unrelated changes will be staged with it.
The goal to materially beat CPython remains open.
