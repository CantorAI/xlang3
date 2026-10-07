# Compile source identity: failure diagnosis before the next engine change

The recorded Chameleon failure is a source-dispatch bug, not a measured
slowdown. The full native-string checkpoint log reports
`TypeError: expected Expression node, got Token`. The existing generic
identity probe reproduces six failing checks on XLang3; CPython 3.14.7 passes
all eight. No fix or new performance result is claimed by this audit.

Evidence: [probe](../../benchmarks/diagnostics/compile_source_identity_probe.py),
[XLang3 output](data/compile-source-identity-control-20261007.log),
[CPython 3.14.7 output](data/compile-source-identity-cpython3147-20261007.log),
and [full checkpoint log](data/pyperformance-xlang3-native-string-checkpoint-full-fast-20261007.log).

| Probe | CPython 3.14.7 | Recorded XLang3 |
| --- | --- | --- |
| Native string | Pass | Pass |
| String subclass with raising `__str__` | Pass | Fails: treated as AST |
| Bytes subclass | Pass | Fails: treated as AST |
| Bytearray subclass | Pass | Fails: treated as AST |
| Latin-1 bytes subclass source | Pass | Fails before decoding |
| Unrelated classes named like source/AST types | Pass | Incorrectly accepts fake AST |
| Public `ast.AST` / `_ast.AST` rebinding | Pass | Pass |
| Genuine `ast.Expression` subclass | Pass | Fails: exact class name required |

## Source findings

At source checkpoint `23d1d443b8b93986549ada9efe1787ec5880858c`,
`builtin_compile` in `src/builtins/functional_builtins.cpp` selects the AST
path whenever `runtime_ast_class_name(source)` is nonempty. That helper
returns the name of any instance's class. Consequently an ordinary
`Token(str)` reaches the AST root validator before source conversion.

`compile_runtime_ast_to_code` then compares that class name to `Expression`,
`Module`, or `Interactive`. This rejects legitimate subclasses and can accept
an unrelated class with the same name and fields. Expression/statement
conversion also uses class-name dispatch; repairing only the entry guard
would leave nested-node spoofing and subclass handling incomplete.

`value_to_source_text` currently recognizes the raw native string, bytes,
and bytearray layouts. Genuine string/bytes subclasses can instead carry
their payload in an `InstanceObject`. The constructors store
`__xlang3_string_value__` and `__xlang3_bytes_value__` in instance storage.
Reading those attributes through arbitrary Python lookup would permit
overrides; reading them without checking actual builtin inheritance would
accept impostors. `class_has_builtin_base_name` itself compares MRO names,
so it is insufficient to prove source-type identity.

The source decoder already handles coding cookies, UTF-8 BOM validation,
and Latin-1 conversion in `src/runtime/modules/system/source_encoding.cpp`.
The recorded encoded-subclass failure happens before this decoder. Reuse
the decoder after correcting dispatch; the evidence does not justify a
second decoding implementation.

`src/runtime/modules/system/ast_module.cpp` already retains canonical AST
classes in private `AstState.ast_base` and `AstState.classes`, with lifetime
managed by native-package cleanup. Canonical identity should come from this
state, rather than a mutable public module attribute, a public helper that
can be rebound, or a process-global table shared between runtimes.

## CPython implementation reference

CPython's compile builtin first asks `PyAST_Check`; that check uses the
interpreter's retained AST type. Root conversion uses subtype checks against
retained mode-specific types. See [compile dispatch](https://github.com/python/cpython/blob/v3.14.7/Python/bltinmodule.c)
and [AST checks/conversion](https://github.com/python/cpython/blob/v3.14.7/Python/Python-ast.c).

Its source conversion accepts Unicode, bytes, bytearray, and buffer inputs,
and reads their payload without calling an overridden `__str__`. See
[`_Py_SourceAsString`](https://github.com/python/cpython/blob/v3.14.7/Python/pythonrun.c).
The eight-case probe covers a subset of this contract; passing it alone
would not establish complete compile compatibility.

## Requirements for the next change

1. Preserve raw native source paths without importing AST or allocating
   temporary Python objects for normal string compilation. Test real source
   subclasses using canonical builtin class identity and retained payloads.
2. Distinguish genuine AST instances by canonical private AST type identity.
   Resolve AST subclasses through their canonical base in the MRO, including
   nested nodes and operators. Reject name/field impostors.
3. Preserve source byte lengths, embedded-NUL rejection, coding-cookie/BOM
   handling, and retained buffer lifetime; check bytearray subclass storage
   and buffer-source behavior before claiming those paths supported.
4. Cover public AST rebinding, source conversion overrides, AST root and
   nested subclasses, fake nested nodes, and the actual Chameleon workload.
   Keep Chameleon's Python algorithms in Python.
5. After the current all-97 timing run finishes, preserve this Release
   binary, implement the generic fix with design comments, and run the
   fixture suite, eight C++ checks, unchanged complete fixed regression gate,
   and official Chameleon benchmark. Record any further compatibility
   failures rather than treating one repaired exception as benchmark success.

The all-97 run currently measures the committed subscription-dispatch binary.
This audit changes documentation only; that binary and its runtime libraries
remain unchanged during measurement.
