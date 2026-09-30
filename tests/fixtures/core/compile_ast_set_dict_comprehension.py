import ast


source = ("keys = {number for number in range(4) if number % 2}\n"
          "values = {str(number): number * 2 for number in range(3)}\n")
tree = ast.parse(source)
print(type(tree.body[0].value).__name__, type(tree.body[1].value).__name__)
namespace = {}
exec(compile(tree, "<set dict comprehension ast>", "exec"), namespace)
print(sorted(namespace["keys"]), namespace["values"])
