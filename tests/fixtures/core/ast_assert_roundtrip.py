import ast

tree = ast.parse("assert value == 2, 'wrong'\n")
statement = tree.body[0]
print(type(statement).__name__, type(statement.test).__name__,
      type(statement.msg).__name__, statement.lineno)
code = compile(tree, "<ast-assert>", "exec")
exec(code, {"value": 2})
try:
    exec(code, {"value": 1})
except AssertionError as exc:
    print(type(exc).__name__, str(exc))
