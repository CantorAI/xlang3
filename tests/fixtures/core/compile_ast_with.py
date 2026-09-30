import ast
from contextlib import nullcontext


tree = ast.parse("with nullcontext(5) as value:\n    answer = value + 1\n")
print(type(tree.body[0]).__name__, len(tree.body[0].items))
namespace = {"nullcontext": nullcontext}
exec(compile(tree, "<with ast>", "exec"), namespace)
print(namespace["answer"])
