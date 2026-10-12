# Generic runtime failure reproducers (2026-10-07)

The last complete pyperformance run had 22 definitions that died with errors,
separate from its timeout cases. The
[small protocol probe](../../benchmarks/diagnostics/package_runtime_protocol_probe.py)
extracts generic triggers and reports each independently. It does not replace
package code or measure speed. Its [CPython 3.14.7 results](data/package-runtime-protocols-cpython3147-20261007.json)
pass all five checks. The [XLang3 control results](data/package-runtime-protocols-control-20261007.json)
reproduce four defects:

| Trigger | XLang3 control | Relevant full-suite failure |
| --- | --- | --- |
| String-subclass keys/values in repr-generated literals | Invalid representation cannot be evaluated | chameleon |
| Dictionary population in a class-body for loop | Key `parens` never inserted | docutils, sphinx |
| Native `list.append(self, item)` inside a Python override | Fast callback rejects valid argument layout | html5lib |
| Native array slicing/copy assignment | Slice is incorrectly sent to integer-index conversion | scimark |
| Attribute named `of` followed by an identity comparison | Passes | SQLAlchemy's earlier error needs a different reproducer |

The string failure has a concrete bootstrap cause: native string methods
install a payload-aware `str.__repr__`, then a later generic representation
registration overwrites it with `builtin_object_repr`. The prepared fix keeps
an already installed concrete slot. A comment records why this decision
belongs at bootstrap, avoiding extra dispatch checks on every repr call.
The registered string fixture covers inherited/native repr, dictionary
literals, escapes, Unicode, surrogates, explicit Python overrides, and an
invalid receiver. It passes CPython 3.14.7 and fails the current XLang3 control;
the candidate now passes the native repr fixture in both runtimes. Complete
correctness checks and the fixed performance gate also pass. The official
Chameleon retry gets past its invalid-repr error, then exposes the separate
bound-only `str.replace` fast callback. A broader audit found the same guard
pattern in startswith, endswith, find, count, and join. The remaining callbacks
need unbound adapters while retaining their existing bound-call hot paths.
That fixture also exposed a separate pre-existing AST decoder issue:
`ast.literal_eval(repr("\ud800"))` yields the literal backslash escape instead
of the surrogate. Native repr itself matches CPython for that case. The
[independent probe](../../benchmarks/diagnostics/ast_surrogate_literal_probe.py)
and [XLang3 output](data/ast-surrogate-literal-candidate-20261007.json), versus
[CPython output](data/ast-surrogate-literal-cpython3147-20261007.json), record
the unresolved AST defect. Generated-literal roundtrips in the repr fixture
cover non-surrogate text; repr comparisons still include surrogates.

The class-loop failure is also concrete: `lower_class_body_stmt` handles
assignments, conditionals, and with statements but omits for statements.
It silently emits no loop. Correcting this needs class namespace binding and
control-flow fixtures, including empty loops, break/continue, and loop else.
It must preserve the normal compiler paths instead of substituting native
code for the Docutils parser.

The native append fix and its validation plan are
[recorded separately](native-list-append-unbound-trial-20261007.md).
Array slicing is absent in the existing native module. Separately, its scalar
getitem currently copies the entire byte buffer through `sync_array_bytes`;
the [prepared scaling probe](../../benchmarks/diagnostics/native_array_scalar_scaling_probe.py)
measured the cost before changing that implementation. With the same 256 reads,
XLang3 control medians were 0.118 ms, 0.261 ms, and 2.236 ms as the array grew
from 256 to 4,096 to 65,536 elements. CPython remained near 0.007 ms. Input
construction was outside the timer; all five samples per size are retained in
[XLang3 data](data/native-array-scalar-control-20261007.json) and
[CPython data](data/native-array-scalar-cpython3147-20261007.json).
This is a focused scaling diagnostic, not an official SciMark result. No array
implementation change or gain is claimed yet. CPython implements array natively, so an XLang3 native array
counterpart is within the project rule; SciMark remains Python code.

All control probes used the validated Release executable at the unchanged
`build-repro/main-verify-20261006/Release/xlang3.exe` path, executable SHA-256
`303F2E8BDCA58C4B555D13ADD7BFC41EEF9E0019F4780A0C9729596CE07DF177` and DLL
`4AA9D085D74402A8AF62A7F4E4605B45B0840B28D895225778DD74C7F7FD8FD1`.
These small correctness probes do not establish a performance improvement.
Engine candidates require complete correctness checks and the unchanged fixed
Release gate before committing, plus the relevant official benchmarks.
