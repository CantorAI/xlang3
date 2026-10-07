# Dictionary iteration and compile source dispatch: source audit

This audit identifies the next generic runtime changes from measured evidence.
It does not claim a timing gain. The all-97 run of checkpoint `003b3882` is
still using its unchanged Release executable and DLL; no engine changes or
builds were made during that run.

## Dictionary iteration

The [NetworkX body profile](networkx-body-native-profile-20261007.md) places
23.8% of runtime DLL instruction-pointer samples in `mapping_iter_next`.
The profile excludes graph construction and leaves Python profiling hooks
disabled. It is a target-selection diagnostic, not a speed measurement or
an estimate of total inclusive cost.

In `src/runtime/mapping.cpp`, `mapping_iter_next` copies
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

Both candidates belong in the generic compiler/runtime. CPython pure-Python
package sources remain Python. This is an audit for subsequent implementation;
the checkpoint benchmark binary contains neither proposed change.
