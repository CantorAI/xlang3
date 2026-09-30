import ast


tree = ast.parse("left, _, right = (1, 2, 3)\nvalues = {}\nvalues['x'] = right\n")
print(type(tree.body[0]).__name__, type(tree.body[0].targets[0]).__name__)
namespace = {}
exec(compile(tree, "<unpack ast>", "exec"), namespace)
print(namespace["left"], namespace["right"])
print(namespace["values"])
