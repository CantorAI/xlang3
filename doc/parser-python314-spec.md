# Python 3.14 Parser Spec

Status: draft 0

## Target

Syntax compatibility target:

```text
Python 3.14
```

Parser should use the official Python 3.14 grammar as source of truth where possible.

## Parser Design

Do not reuse the current XLang operator-stack parser as the XLang3 parser.

Use one of:

- PEG parser
- generated parser from Python-like grammar
- hand-written recursive descent with grammar-level precedence

Controlled backtracking is allowed. "Consume every token once" is not a requirement.

## Lexer Requirements

The lexer must emit:

```text
NAME
NUMBER
STRING
FSTRING_START/FSTRING_MIDDLE/FSTRING_END or equivalent
NEWLINE
INDENT
DEDENT
OP
COMMENT optional for tooling
ENDMARKER
```

It must handle:

- indentation
- parentheses/brackets/braces suppressing significant newlines
- line continuations
- comments
- string prefixes
- f-strings eventually
- soft keywords

## AST Rule

AST nodes are syntax only.

Forbidden in AST:

- runtime `X3Value`
- runtime `X3Object`
- `Exec()`
- frame access
- package loading
- type slot calls

Allowed in AST:

- node kind
- source span
- child node references
- token text references or interned identifiers
- syntax flags

## AST Node Groups

Required groups:

```text
Module
Statement
Expression
Pattern
Comprehension
Argument
Keyword
Alias
TypeParam
```

Important statements:

```text
FunctionDef
AsyncFunctionDef
ClassDef
Return
Delete
Assign
AugAssign
AnnAssign
For
AsyncFor
While
If
With
AsyncWith
Match
Raise
Try
TryStar
Assert
Import
ImportFrom
Global
Nonlocal
ExprStmt
Pass
Break
Continue
```

Important expressions:

```text
Name
Constant
Tuple
List
Dict
Set
ListComp
SetComp
DictComp
GeneratorExp
Attribute
Subscript
Slice
Call
Lambda
IfExp
BoolOp
BinOp
UnaryOp
Compare
Await
Yield
YieldFrom
NamedExpr
Starred
JoinedStr
FormattedValue
```

## Parser Phases

```text
text -> tokens -> concrete parser -> syntax AST -> parser diagnostics
```

No name binding in parser.

No local/global/nonlocal resolution in parser.

No runtime lowering in parser.

## Unsupported Semantics

If syntax is valid Python 3.14 but runtime support is not implemented, parser should still accept it. Sema or lowering should emit:

```text
UnsupportedFeature(feature_name, source_span)
```

## Error Recovery

Parser should support useful diagnostics but Phase 0 can stop after first syntax error.

Tooling mode later should recover enough to produce partial AST.

## Differences From Current XLang Parser

Discard:

- operator callback registry as grammar engine
- parser-managed execution classes
- precedence table as primary parse model
- parser context probes for inline if/for/else
- pair operators doing semantic interpretation

Retain only as lessons:

- source span tracking
- indentation awareness
- module code fragment tracking if useful for debugging

