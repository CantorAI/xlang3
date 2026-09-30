import ast


tree = ast.parse("values = {'x': 1, 'y': 2}\ndel values['x']\n")
print(type(tree.body[1]).__name__, len(tree.body[1].targets))
namespace = {}
exec(compile(tree, "<delete ast>", "exec"), namespace)
print(namespace["values"])
