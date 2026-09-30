import ast


source = (
    "head, *tail = (1, 2, 3)\n"
    "items = [0, *tail, 4]\n"
    "pairs = [*[(i, i + 1) for i in range(2)], (2, 3)]\n"
)
tree = ast.parse(source)
print(type(tree.body[0].targets[0].elts[1]).__name__)
print(type(tree.body[1].value.elts[1]).__name__)
namespace = {}
exec(compile(tree, "<starred ast>", "exec"), namespace)
print(namespace["head"], namespace["tail"], namespace["items"], namespace["pairs"])
