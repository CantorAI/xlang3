# Expression Decorators

XLang3 native packages can opt into unevaluated decorator arguments:

```python
@cantor.Task(NPU=1 and OS == "Windows")
def work(value):
    return value + 1
```

Register the factory with `APISET().AddExpressionDecorator("Task", &Package::Task)`.
Its member signature is `X::Value Task(std::vector<X::Value> expressions)`.
Return a callable decorator; XLang3 passes the decorated function to that callable
using the normal decorator protocol. The factory flag belongs to the callable,
including aliases and IPC proxies, rather than to its variable name.

The C ABI equivalent is `X3_NATIVE_CAPTURE_EXPRESSIONS` in `X3NativeFunctionDef.flags`.
Expression values have kind `X3_OBJECT_KIND_EXPRESSION`. Use
`x3_expression_inspect` to obtain an independent dictionary with `op`, `value`,
and `children`; no runtime AST pointers are exposed.

`x3_expression_evaluate`, also available through the package host, accepts one
expression or a list of expressions plus a resource-snapshot dictionary. It
returns the expression result and a dictionary of proposed reservations. It
does not mutate the snapshot. Cantor must validate and commit these reservations
under its scheduler lock. Failed branches do not retain reservations.

Supported scheduling expressions include names, scalar literals, arithmetic,
comparisons, comparison chains, `not`, `and`, `or`, and resource assignments.
Names are resolved against the supplied snapshot, not Python locals. Arbitrary
calls, comprehensions, and attribute access are not currently evaluation forms.
Unsupported captured forms report an evaluation error; ordinary decorators
continue evaluating their Python arguments normally. Star argument expansion is
not part of the captured-expression contract.

Capture adds a metadata check at decorator-definition time, not at normal
function-call sites. Immutable expression constants are reused by the compiled
module and survive IR caching and value/IPC serialization. Serialized expressions
are versioned and limited to 128 levels and 4096 nodes when decoded.

This extension requires XLang3; CPython evaluates decorator arguments eagerly.
Native packages must be rebuilt for ABI version 16.
