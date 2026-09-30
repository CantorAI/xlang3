import ast


source = ("class Record:\n    value: int\n    def __init__(self):\n"
          "        self.value = 3\n    def read(self):\n        return self.value\n")
tree = ast.parse(source)
record = tree.body[0]
print(type(record).__name__, type(record.body[0]).__name__, len(record.body))
namespace = {}
exec(compile(tree, "<annotated class>", "exec"), namespace)
print(namespace["Record"]().read())
