import ast


source = ("def values():\n    yield 1\n    yield 2\n"
          "def more():\n    yield from values()\n")
tree = ast.parse(source)
print(type(tree.body[0].body[0].value).__name__,
      type(tree.body[1].body[0].value).__name__)
namespace = {}
exec(compile(tree, "<yield ast>", "exec"), namespace)
print(list(namespace["more"]()))
