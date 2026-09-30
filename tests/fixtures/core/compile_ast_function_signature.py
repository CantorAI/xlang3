import ast


source = (
    "def add(x: int = 3, /, y: int = 4, *values: int, scale: int = 2, **extras: int) -> int:\n"
    "    return (x + y + sum(values) + sum(extras.values())) * scale\n"
)
tree = ast.parse(source)
namespace = {}
exec(compile(tree, "<function signature ast>", "exec"), namespace)
add = namespace["add"]
print(add(), add(1, 2, 3, scale=4, z=5))
print(add.__defaults__, add.__kwdefaults__)
