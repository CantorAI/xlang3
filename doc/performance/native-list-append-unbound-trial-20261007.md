# Native list append: unbound argument layout (2026-10-07)

The last complete all-97 run failed `html5lib` in its Python
`ActiveFormattingElements.append` override, at `list.append(self, node)`.
The native fast callback returned `list.append expected 1 argument`.

XLang3 registers the same native callback for the bound instance method and
the unbound list-class descriptor. The fast callback previously required one
leading receiver and one register argument. The unbound descriptor supplies
both arguments in registers, which is a valid Python call but fails that
layout check.

The prepared runtime fix keeps the existing direct bound-append path and
uses the generic stack argument adapter for other layouts. Both the ordinary
callback and the fast callback raise native TypeError for invalid calls or
receivers. The successful bound path continues to append from VM registers;
it does not allocate a heap argument vector. A code comment records the
bound/unbound distinction so future optimizations retain both forms.

The registered fixture covers a Python list-subclass override using the exact
html5lib call, super dispatch, an unbound alias, getattr, starred arguments,
functools.partial, repeated ordinary bound calls, and invalid calls without
mutation. The fixture passes under CPython 3.14.7; the current XLang3 control
fails on its first valid subclass append with exactly the html5lib error.
The [CPython output](data/native-list-append-unbound-cpython3147-20261007.txt)
and [control traceback](data/native-list-append-unbound-control-20261007.txt)
establish the reproducer. These small correctness probes ran during a fresh
graph worker's loading phase; they are not timing measurements.
The candidate has now been built with the existing VS/Ninja configuration.
The append fixture passes XLang3 too. The complete fixture suite, runtime and
interpreter C++ tests, SDK stream/call test, and graph producer/consumer pass.
The [unchanged fixed Release gate](data/native-append-repr-fixed-release-gate-20261007.json)
passes all 11 cases with 21 repeats, 5 warmups, and the original 10% threshold.
Its largest candidate/baseline ratio is 1.041. Native append measures 0.994,
function calls 0.987, constructors 0.994, and properties 1.020.
These are XLang3 build comparisons, not CPython speedups. The official
html5lib and Chameleon retry is terminal. Html5lib now completes with a raw
mean of **0.788733 s**, versus **0.049610 s** for CPython 3.14.7: **0.062899x
CPython speed**, or about **15.9x slower**. Both means use 20 fast-mode values;
the run carries a sample-stability warning. The old failure supplied no timing
to establish a previous-XLang3 speed ratio. Chameleon passes the repr failure
but then rejects an unbound `str.replace(self, old, new, count)` call.
That is the next native argument-layout defect, separate from this append fix.
The [raw JSON](data/pyperformance-xlang3-native-append-repr-html-chameleon-fast-20261007.json),
[log](data/pyperformance-xlang3-native-append-repr-html-chameleon-fast-20261007.log),
and [provenance](data/pyperformance-xlang3-native-append-repr-html-chameleon-fast-20261007-provenance.json)
retain the successful case, failure traceback, exit code 1, and unchanged hashes.

The current validated control executable and all root DLLs were preserved
before editing, under `build-repro/controls/set-growing-index-20261006`.
The executable/run path will remain
`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`.
CPython remains `C:\Python\Python314\python.exe`, version 3.14.7.

This fixes the generic native list ABI; html5lib remains Python code.
