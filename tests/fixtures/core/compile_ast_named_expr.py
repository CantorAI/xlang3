import ast


source = "result = (item := 3) + item\nflag = (n := 2) and n > 1\n"
tree = ast.parse(source)
print(type(tree.body[0].value.left).__name__)
namespace = {}
exec(compile(tree, "<named expression ast>", "exec"), namespace)
print(namespace["result"], namespace["flag"], namespace["item"], namespace["n"])
