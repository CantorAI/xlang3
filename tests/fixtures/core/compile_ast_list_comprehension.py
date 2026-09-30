import ast


tree = ast.parse("values = [number * 2 for number in range(5) if number % 2]\n")
print(type(tree.body[0].value).__name__, len(tree.body[0].value.generators))
namespace = {}
exec(compile(tree, "<list comprehension ast>", "exec"), namespace)
print(namespace["values"])
