import ast


source = (
    "items = []\n"
    "for number in range(5):\n"
    "    if number == 0:\n"
    "        continue\n"
    "    if number == 4:\n"
    "        break\n"
    "    items.append(number)\n"
    "else:\n"
    "    items.append(99)\n"
    "for number in range(2):\n"
    "    pass\n"
    "else:\n"
    "    items.append(10)\n"
)
tree = ast.parse(source)
print(type(tree.body[1].body[0].body[0]).__name__, type(tree.body[1].body[1].body[0]).__name__)
namespace = {}
exec(compile(tree, "<break continue ast>", "exec"), namespace)
print(namespace["items"])
