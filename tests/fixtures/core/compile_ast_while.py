import ast


source = ("count = 0\nwhile count < 3:\n    count += 1\n"
          "else:\n    finished = True\n")
tree = ast.parse(source)
print(type(tree.body[1]).__name__, len(tree.body[1].orelse))
namespace = {}
exec(compile(tree, "<while ast>", "exec"), namespace)
print(namespace["count"], namespace["finished"])
