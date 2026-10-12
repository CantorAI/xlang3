# Native string descriptor argument layouts (2026-10-07)

The native repr checkpoint cleared Chameleon's invalid generated-literal
failure. Its official retry then failed in the existing Python Token.replace
override at `str.replace(self, old, new, count)`. The native fast callback
required a leading bound receiver, even though class descriptors pass the
explicit receiver in the register arguments.

The audit found that startswith, endswith, find, count, replace, and join
shared the same bound-only guard. Upper/lower/strip/lstrip/rstrip/split already
handled bound and unbound calls; their argument layout paths remain intact.
The six affected callbacks now preserve their original bound receiver/register
path and use the generic borrowed stack adapter for other layouts. Valid
unbound calls do not allocate a heap argument vector. Invalid arity and
receivers raise native TypeError; join retains exceptions from Python iterators.
Comments beside the guards explain why the class-descriptor layout must be
retained alongside the bound fast path.

The registered fixture covers the actual Chameleon forwarding call, maximum
positional arities, optional bounds/counts, tuple prefixes/suffixes, subclass
receivers, aliases/getattr, starred calls, partials, and ordinary bound calls.
It also checks invalid calls and preservation of an iterator's LookupError.
The [CPython 3.14.7 output](data/native-string-methods-unbound-cpython3147-20261007.txt)
passes; the [control traceback](data/native-string-methods-unbound-control-20261007.txt)
reproduces the original error. The candidate fixture now passes XLang3 too.

The initial descriptor-layout candidate passed the complete fixture suite,
runtime/interpreter C++ tests, SDK stream/call test, and graph producer/consumer.
Its [complete fixed Release gate](data/native-string-unbound-fixed-release-gate-20261007.json)
passed all 11 default cases, with 21 paired repeats and five warmups. Its
[official Chameleon retry](data/pyperformance-xlang3-native-string-unbound-chameleon-fast-20261007.log)
advanced past Token.replace and failed at `str.lstrip(self, None)`. It produced
no timing score. The [provenance](data/pyperformance-xlang3-native-string-unbound-chameleon-fast-20261007-provenance.json)
records unchanged binary hashes and exit 1.

CPython 3.14.7 accepts None as the default whitespace argument for strip,
lstrip, and rstrip. The follow-up normalizes explicit None to the omitted
argument once before each character scan. This preserves the existing bound
and unbound dispatch paths and avoids an extra per-character branch. The
fixture now covers Chameleon's forwarding chain, Unicode whitespace, empty
strings, explicit character sets, and invalid non-string arguments. The
[control failure](data/native-string-strip-none-control-20261007.txt) reproduces
the official error. The follow-up passes the complete fixture runner and all
eight selected CTest cases: runtime/interpreter, SDK stream/call, graph
producer/consumer, and the three serialized-graph rejection cases. See the
[validation log](data/native-string-strip-none-fixtures-20261007.log).

The follow-up [fixed Release gate](data/native-string-strip-none-fixed-release-gate-20261007.json)
passes all 11 default cases. Candidate/baseline time ratios are:
local_slots 0.995, scalar_arithmetic 1.002, range_for 1.005, function_calls 0.979,
class_construct 1.013, list_append 1.004, property_access 0.995, deepcopy_memo
0.885, json_dumps 1.011, gc_traversal 1.010, and subparsers 0.944. These ratios
validate against the fixed XLang3 baseline, not CPython.

The follow-up [official Chameleon attempt](data/pyperformance-xlang3-native-string-strip-none-chameleon-fast-20261007.log)
advances past both native string failures, then fails at
`compile(source, '', mode, ast.PyCF_ONLY_AST)` with
`TypeError: expected Expression node, got Token`. The string subclass is being
treated as an AST argument; that separate compiler compatibility issue remains
open. The attempt returned exit 1, produced no timing score, and preserved its
[binary provenance](data/pyperformance-xlang3-native-string-strip-none-chameleon-fast-20261007-provenance.json).
No package timing gain is claimed.

The validated preceding executable and root DLLs were preserved before editing
under `build-repro/controls/native-append-repr-20261007`. The normal executable
path remains `build-repro/main-verify-20261006/Release/xlang3.exe`, and the fixed
accepted gate baseline remains `build-repro/Release/xlang3.exe`.
Only `C:\Python\Python314\python.exe` (3.14.7) is used for Python comparisons.

Initial descriptor-layout candidate executable SHA-256:
`303F2E8BDCA58C4B555D13ADD7BFC41EEF9E0019F4780A0C9729596CE07DF177`;
runtime DLL:
`043C580D2AB606E8873CF706FB9969B6D95D9659273923F405F16008DD600CD4`.
Final candidate executable SHA-256 is unchanged; runtime DLL:
`29431877C4306B05E43ABA818CB96A6CFEA548D90D55189FB21B8DE6AD0758F3`.
This is a generic native string ABI correction. Chameleon remains its Python
implementation, and no pure-Python library has been replaced with C++.
