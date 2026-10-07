# Native AST constructor defaults: next Chameleon compatibility failure

The compile/source identity candidate now passes three focused fixtures,
the eight-case compile probe, all 353 core and 11 section fixtures, eight
C++/SDK checks, and the unchanged complete fixed Release performance gate.
Its official Chameleon attempt still fails; this is not a benchmark win.

The traceback reaches Python 3.14.7's `_ast_unparse.py` after Chameleon's
string-subclass source has compiled. `get_type_comment` reads
`node.type_comment` on an `Assign` node and receives `AttributeError`.
The targeted log is
`data/pyperformance-xlang3-typecheck-compile-targeted-fast-20261007.log`;
the run finished with two completed definitions and this Chameleon failure.
Its start/end executable and library hashes match.

## XLang3 source cause

In `src/runtime/modules/system/ast_module.cpp`, `ast_class` declares
`Assign._fields = (targets, value, type_comment)` but installs an empty
`_field_types` dictionary and no `type_comment` class default.
`ast_node_init_kw` only writes positional and explicitly supplied keyword
fields. Omitting the optional field therefore leaves no attribute.

Parser-created Assign nodes separately set `type_comment` to None. That
does not cover Python-constructed nodes used by Chameleon's code generator.
Repairing only the parser or changing `_ast_unparse.py` would miss the
native constructor contract.

## CPython 3.14.7 reference

CPython's native [`ast_type_init`](https://github.com/python/cpython/blob/v3.14.7/Python/Python-ast.c#L5149)
tracks fields supplied positionally or by keyword and rejects duplicates.
For omitted fields it reads `_field_types`: optional union fields use a
class-level None default; list fields receive fresh empty lists; expression
context fields use the Load singleton. Missing required fields and unknown
keywords retain 3.14's deprecation-warning behavior. A subclass lacking
`_field_types` retains the older behavior of absent omitted attributes.

These operations belong to CPython's native `_ast` contract. Implementing
their equivalent in XLang3's own `_ast` module follows the project rule;
Chameleon's compiler and Python's AST traversal/unparser remain Python.

## Required repair and validation

Represent the canonical field schema and optional defaults consistently,
rather than special-casing a Chameleon call or one failing Assign instance.
Expose compatible `_field_types` metadata, preserve user subclass metadata
and explicit values, allocate fresh list defaults, preserve context defaults,
and check duplicate/error behavior against CPython 3.14.7. Required fields
must not silently become None merely to advance the workload.

Use constructor/AST-unparse fixtures to test omitted optional fields,
independent list defaults, subclass overrides and duplicate arguments.
After a build, rerun complete correctness checks, the unchanged fixed gate,
and the official Chameleon benchmark. Retain any subsequent failure as a
failure; do not claim that reaching a later traceback establishes a speed
gain or full compile/AST compatibility.
