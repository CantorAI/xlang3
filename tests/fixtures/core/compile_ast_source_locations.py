import ast

source = "\ndef outer():\n    def inner():\n        return 1\n    return inner\n"
tree = ast.parse(source, filename="source_locations.py")
namespace = {}
exec(compile(tree, "source_locations.py", "exec"), namespace)
outer = namespace["outer"]
inner = outer()
print(tree.body[0].lineno, outer.__code__.co_firstlineno)
print(tree.body[0].body[0].lineno, inner.__code__.co_firstlineno)
