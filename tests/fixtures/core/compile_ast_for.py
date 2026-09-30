import ast


source = ("total = 0\nfor key, value in [(1, 2), (3, 4)]:\n"
          "    total += key + value\nelse:\n    finished = True\n")
tree = ast.parse(source)
print(type(tree.body[1]).__name__, type(tree.body[1].target).__name__)
namespace = {}
exec(compile(tree, "<for ast>", "exec"), namespace)
print(namespace["total"], namespace["finished"])
