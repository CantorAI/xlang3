import ast


tree = ast.parse("items = [1, 2, 3, 4]\nresult = items[1:3]\n")
print(type(tree.body[1].value.slice).__name__)
namespace = {}
exec(compile(tree, "<slice ast>", "exec"), namespace)
print(namespace["result"])
