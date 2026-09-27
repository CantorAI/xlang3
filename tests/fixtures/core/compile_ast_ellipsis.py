import ast


tree = ast.parse("marker = ...\n")
print(type(tree.body[0].value.value).__name__)
namespace = {}
exec(compile(tree, "<ellipsis ast>", "exec"), namespace)
print(namespace["marker"] is Ellipsis)
