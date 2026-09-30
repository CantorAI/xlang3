import ast


source = ("result = 0\n"
          "def outer():\n    count = 0\n    def bump():\n"
          "        nonlocal count\n        count += 1\n"
          "    bump()\n    return count\n"
          "def update():\n    global result\n    result += outer()\n"
          "update()\n")
tree = ast.parse(source)
print(type(tree.body[1].body[1].body[0]).__name__)
namespace = {}
exec(compile(tree, "<scope ast>", "exec"), namespace)
print(namespace["result"])
