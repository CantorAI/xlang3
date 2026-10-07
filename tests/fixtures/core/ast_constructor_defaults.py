import ast
import warnings

assert ast.Assign.type_comment is None
assert ast.Constant.kind is None
assert ast.Assign._field_types["type_comment"] == str | type(None)
assert ast.Assign.__annotations__ is ast.Assign._field_types
assert ast.Assign._field_types["targets"] == list[ast.expr]
left, right = ast.Assign(value=ast.Constant(42)), ast.Assign(value=ast.Constant(42))
assert left.type_comment is None and left.targets == []
left.targets.append(ast.Name(id="answer"))
assert right.targets == [] and left.targets is not right.targets
assert isinstance(left.targets[0].ctx, ast.Load)
assert ast.Name(id="other").ctx is left.targets[0].ctx
print("native field metadata and defaults", True)

node = ast.Assign(targets=[ast.Name(id="answer", ctx=ast.Store())],
                  value=ast.Constant(42))
assert ast.unparse(ast.fix_missing_locations(node)) == "answer = 42"
module = ast.Module(body=[node])
assert module.type_ignores == []
namespace = {}
exec(compile(module, "<defaults>", "exec"), namespace)
assert namespace["answer"] == 42
assert ast.Return().value is None
assert ast.arguments().vararg is None and ast.arguments().kwarg is None
assert ast.Interpolation(value=ast.Name(id="x"), str="x", conversion=-1).format_spec is None
assert ast.TemplateStr().values == []
print("constructor unparse and compile", True)

for create in (lambda: ast.Name("x", id="y"),
               lambda: ast.Name("x", ast.Load(), 3)):
    try:
        create()
    except TypeError:
        pass
    else:
        raise AssertionError("invalid constructor arguments accepted")
print("duplicate and excess arguments", True)

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always", DeprecationWarning)
    missing = ast.Name()
    assert not hasattr(missing, "id")
    assert len(caught) == 1 and "missing 1 required" in str(caught[0].message)
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always", DeprecationWarning)
    extra = ast.Name(id="x", extra=42)
    assert extra.extra == 42
    assert len(caught) == 1 and "unexpected keyword" in str(caught[0].message)
with warnings.catch_warnings():
    warnings.simplefilter("error", DeprecationWarning)
    try:
        ast.Name()
    except DeprecationWarning:
        pass
    else:
        raise AssertionError("required-field warning did not propagate")
print("required and unknown field warnings", True)


class Legacy(ast.AST):
    _fields = ("payload",)


class Override(ast.Name):
    _field_types = {"id": str, "ctx": list[int]}


class AttributeHooks(ast.Name):
    changes = []

    def __setattr__(self, name, value):
        self.changes.append(name)
        object.__setattr__(self, name, value)


with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always", DeprecationWarning)
    assert not hasattr(Legacy(), "payload")
    assert caught == []
assert Override(id="x").ctx == []
hooked = AttributeHooks(id="x")
assert hooked.changes == ["id", "ctx"]
print("subclass metadata and attribute hooks", True)
