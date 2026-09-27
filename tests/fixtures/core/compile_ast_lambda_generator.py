import ast


source = ("scale = lambda value, factor=2: value * factor\n"
          "values = (number for number in range(4) if number % 2)\n")
tree = ast.parse(source)
print(type(tree.body[0].value).__name__, type(tree.body[1].value).__name__)
namespace = {}
exec(compile(tree, "<lambda generator ast>", "exec"), namespace)
print(namespace["scale"](3), list(namespace["values"]))
