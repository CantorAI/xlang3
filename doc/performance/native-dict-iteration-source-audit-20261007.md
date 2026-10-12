# Dictionary iteration and compile source dispatch: source audit

This audit identifies the next generic runtime changes from measured evidence.
It does not claim a timing gain. The all-97 run of checkpoint `003b3882`
finished with unchanged executable/DLL hashes; its [complete evidence](pyperformance-xlang3-native-string-checkpoint-full-fast-20261007.md)
is committed in `20dc9e95`. The dictionary candidate was built only after
collection finished. The original benchmark and preserved control are intact.

## Dictionary iteration

The [NetworkX body profile](networkx-body-native-profile-20261007.md) places
23.8% of runtime DLL instruction-pointer samples in `mapping_iter_next`.
The profile excludes graph construction and leaves Python profiling hooks
disabled. It is a target-selection diagnostic, not a speed measurement or
an estimate of total inclusive cost.

In the preserved checkpoint's `src/runtime/mapping.cpp`, `mapping_iter_next` copies
`dict->entries[index]` into an owning `std::pair<Value, Value>` before
checking whether this is a key, value, or item iterator. A key iterator thus
retains and releases an unrequested object value. For object keys it also
retains a temporary key before separately retaining the output key.
`Value` copy/destruction in `src/internal/xlang3/value.h` perform ownership
updates; non-immortal object references use atomic refcounts.

[CPython 3.14.7 dictionary iteration](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c#L4888-L5067)
selects the key or value and takes one returned reference. Its iterator state
advances before returning. Its size and remaining-entry checks also detect
invalid dictionary mutations. XLang3's current function has no corresponding
size-change guard; removing ownership overhead must not conceal this existing
compatibility gap.

The proposed optimization is a direct dictionary-storage branch that retains
only the selected key/value, advances the iterator, then moves that owned
result into the output. Items still yield an owning tuple. Module/class proxy
enumeration remains a distinct path: it synthesizes entries and has different
ordering and namespace semantics. Avoid borrowing dictionary entry memory
into the output, since replacing an output can release another owner or run
finalization. Do not access the iterator after an output assignment that can
destroy an aliased iterator.

Before retaining any change, exercise key/value/item ownership, deletion of
external source references, output/source or output/iterator aliasing at the
C++ boundary, insertion order, updated values, and iterator exhaustion.
Run the complete correctness suites and unchanged fixed Release gate. Measure
the official NetworkX shortest-path and connected-components cases against
the preserved checkpoint, with the same worker count and a sufficient full-
case setup cap. Reject a change whose official effect is inconclusive; the
profile alone cannot justify a speed claim.

## Chameleon compile source dispatch

The [official Chameleon attempt](data/pyperformance-xlang3-native-string-strip-none-chameleon-fast-20261007.log)
fails at `compile(source, '', mode, ast.PyCF_ONLY_AST)` where `source` is a
Python `Token(str)` instance. It reached this point after the native string
descriptor and None-default corrections. It still has no timing score.

`builtin_compile` in `src/builtins/functional_builtins.cpp` currently calls
`runtime_ast_class_name(source)` first. That helper returns the class name for
every ordinary instance, so a string subclass enters the AST compiler before
`value_to_source_text` can inspect its input. That helper only recognizes
native string/bytes/bytearray objects: `value_as_string` and `value_as_bytes`
check the exact native object kind and do not unwrap a Python subclass.
Consequently, merely skipping the AST branch for string subclasses would
still leave a text-conversion failure. Native string methods already extract
the `__xlang3_string_value__` payload separately. The compiler needs equivalent
payload support with a genuine builtin-base guard; no Chameleon-specific
implementation is needed.

[CPython 3.14.7 compile dispatch](https://github.com/python/cpython/blob/v3.14.7/Python/bltinmodule.c#L789-L834)
tests actual AST membership before AST conversion, and otherwise converts the
source text. The XLang3 correction must distinguish text/buffer subclasses
from genuine AST instances, retain AST compilation, and reject unrelated
instances even if named `Expression` or equipped with fake AST-looking
attributes. A subclass's overridden `__str__` must not replace its source
payload. Validate positional and keyword compile calls, AST-only and normal
code modes, native string/bytes subclasses, and genuine/custom AST nodes
before the official Chameleon retry.

`class_has_builtin_base_name` is insufficient as the genuine-base guard:
its implementation compares MRO class **names**, so an unrelated class named
`str` can match. The source conversion should use `class_is_subclass` with the
runtime's actual builtin class identity, then read the native payload without
calling a user descriptor or `__str__`. AST membership likewise needs the
original native `_ast.AST` identity retained independently of reassignable
module attributes. The module's `AstState::ast_base` currently holds that
identity privately; rebinding `_ast.AST` or naming a class `Expression` must
not change compile's classification.

`benchmarks/diagnostics/compile_source_identity_probe.py` prepares independent
checks for native strings, string/bytes/bytearray subclasses, original payload
use despite overridden `__str__`, a byte-source encoding declaration, fake
source identities, AST module-attribute rebinding, and an AST root subclass.
It reports each case independently so one classification failure cannot hide
the remaining results. This draft probe is not registered in the main fixture
suite. It passed all eight checks under CPython 3.14.7; the preserved control
passed two and failed six: source subclasses, byte-source encoding, fake source
identity rejection, and an AST root subclass. Native strings and AST module
rebinding passed. The [CPython log](data/compile-source-identity-cpython3147-20261007.log)
and [control log](data/compile-source-identity-control-20261007.log) retain those
results. No compile implementation change has been made yet.

Both candidates belong in the generic compiler/runtime. CPython pure-Python
package sources remain Python. This is an audit for subsequent implementation;
the checkpoint benchmark binary contains neither proposed change.

## Comparing full runs without changing the timed population

`benchmarks/diagnostics/compare_pyperformance_common_subtests.py` produces a
companion comparison after the full run finishes. It intersects completed
subtest names from the before, after, and CPython-reference JSONs, and uses
that same set for both CPython-relative geometric means and the old/new
XLang3 ratio. Previously missing scores stay outside this comparison; the
complete all-97 report remains the source for statuses and newly completed
cases. Above 1x in the old/new column means a faster new XLang3 result, while
above 1x in the CPython/new-XLang3 column means beating CPython.

The helper rejects an active after run, missing/changed candidate hashes,
a manager other than Python 3.14.7, or a reference filename that differs
from the completed-run provenance. The report records raw-input SHA-256s,
measurement counts, and excluded names. It does not infer significance from
nominal ratios or substitute these shared-set ratios for the whole suite.

The [synthetic validation](data/common-subtest-comparison-validation-20261007.log)
checks two shared cases whose means change from 10 to 5 seconds and 20 to
40 seconds, respectively, against CPython means of 5 and 10 seconds. Their
old/new geometric mean is exactly 1x and both CPython-relative geometric
means are 0.5x. Adding an old-only and a new-only result does not alter those
values. Deliberately large warmup values are excluded; measurement counts
and raw-input hashes are retained. Active-run, changed-binary, wrong-manager,
and mismatched-reference records are rejected. Synthetic files were created
in a temporary directory and are not benchmark evidence.

Ownership tests for the proposed dictionary optimization have been prepared
in `tests/fixtures/core/dict_iterator_ownership.py` and
`tests/cpp/mapping_iterator_ownership_cases.h`. They are registered locally in the
fixture/C++ runners. The Python fixture passed on CPython 3.14.7 and the
preserved control. After building the candidate, the complete fixture suite
and all eight selected C++/SDK/serialization tests passed, including the new
ownership cases. The C++ cases include a borrowed destination already pointing to the
selected object, which must acquire an owned result before source owners
are destroyed.

The candidate source now has the direct dictionary branch and moves only
the selected owned result. Its index advances before destination cleanup;
exhaustion moves the source out before clearing an aliased output. Module/class
paths still synthesize entries and now also advance before output cleanup.
The [preserved-control identity](data/native-dict-iteration-preserved-control-20261007.json)
records the validated executable and root DLLs copied before editing. The
benchmark's original binary hashes still identify checkpoint `003b3882`.
The candidate runtime DLL SHA-256 is
`1791D6F683B78E27933EBA459AB4C7B96D27FD94BFBB7576D334D88283CE04F1`.
The [complete fixed Release gate](data/release-native-dict-iteration-fixed-gate-20261007.json)
passed with exit 0: all 11 default cases, 21 paired repeats, five warmups, and
the unchanged 10% tolerance. The [official NetworkX comparison](native-dict-iteration-networkx-20261007.md)
then completed both cases: shortest-path measured 1.1185x the control speed,
connected-components 1.1111x. Both still lag CPython, at 0.3024x and 0.2892x
of its speed respectively. `pyperf compare_to` reports both improvements;
the separate fast-mode runs do not establish a whole-suite win.

The standalone C++ ownership probe accepts `--control` to test the preserved
runtime without invoking the old self-replacement paths: source inspection
shows those paths access the iterator after its sole owner is destroyed.
This diagnostic mode still checks borrowed-output ownership and namespace
proxy results. The regular C++ runtime suite always exercises all cases,
including self-replacement on yield and exhaustion. The preserved-control
probe compiled successfully and returned exit 1 with exactly two borrowed-
output ownership failures. It did not execute the known undefined self-
replacement paths. The regular candidate suite exercised all cases and passed.
