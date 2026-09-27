import ast


source = ("try:\n    raise ValueError('x')\n"
          "except ValueError as error:\n    result = str(error)\n"
          "finally:\n    done = True\n")
tree = ast.parse(source)
print(type(tree.body[0]).__name__, type(tree.body[0].handlers[0]).__name__)
namespace = {}
exec(compile(tree, "<try ast>", "exec"), namespace)
print(namespace["result"], namespace["done"])
