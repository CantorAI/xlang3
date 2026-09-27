import ast


source = "def identity[T](value: 'T'):\n    return value\nclass Box[T]:\n    pass\n"
tree = ast.parse(source)
function, klass = tree.body
print(function.type_params[0].name, klass.type_params[0].name)
print(function.type_params[0].lineno, function.type_params[0].col_offset)
print(klass.type_params[0].lineno, klass.type_params[0].col_offset)
namespace = {}
exec(compile(tree, "<generic ast>", "exec"), namespace)
print(namespace["identity"].__type_params__[0].__name__)
print(namespace["Box"].__type_params__[0].__name__)
print(type(namespace["identity"].__type_params__[0]).__name__,
      type(namespace["Box"].__type_params__[0]).__name__)
