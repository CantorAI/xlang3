# Rejected KeyError initialization trials for `subparsers` — 2026-10-04

`argparse_subparsers` is about 17× slower than CPython 3.14.7. Earlier call
profiling found 2,009 caught `KeyError` constructions from absent `COLUMNS` and
`LINES`, so I tested two changes to BaseException initialization against that
workload. Neither produced an improvement, and neither performance shortcut
remains in `exception_builtins.cpp`.

## Moving the argument vector

The first trial passed the temporary exception-argument vector to
`Value::tuple` by move instead of copy. A 21-pair order-balanced comparison of
the unchanged `subparsers` body measured candidate/control **1.0013×** (95%
interval **0.9980–1.0057**), so it detected no benefit. A separate focused
pyperf run was 1.01× slower than the prior full-suite result, but the paired
comparison is the stronger attribution and crosses parity. The source change
was removed.

## Exact KeyError family shortcut

The second trial special-cased only the live builtin `KeyError` class with its
expected direct `LookupError`/`Exception` bases, then skipped exception-family
checks that do not apply to it. Subclasses, builtin rebinding, and altered
bases stayed on the general path. The order-balanced comparison measured
candidate/control **1.0060× slower** (95% interval **1.0018–1.0152**), so this
shortcut was also removed.

The fixture [`exception_constructor_args.py`](../../tests/fixtures/core/exception_constructor_args.py)
now covers zero-, one-, and multi-argument `KeyError` construction plus a
subclass. Its output matches CPython 3.14.7. This protects the ordinary
semantics; it does not claim to test or retain either rejected fast path.

## Evidence

- [Vector-move paired samples](data/subparsers-exception-args-move-balanced-20261004.json).
- [KeyError family shortcut paired samples](data/subparsers-keyerror-base-init-fastpath-balanced-20261004.json).
- The focused, unpaired vector-move pyperf runs are retained as
  [candidate](data/pyperformance-xlang3-subparsers-exception-args-move-fast-20261004.json)
  and [runner log](data/pyperformance-xlang3-subparsers-exception-args-move-fast-20261004.log).

These exception paths are not the dominant `subparsers` cost. Do not repeat
either change without a different mechanism and a measured improvement in the
official benchmark.
