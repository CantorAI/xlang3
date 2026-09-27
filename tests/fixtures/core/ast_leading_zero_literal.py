import ast


source = "zero = 00000000000000000000000000\nhex_value = 0xA1A2\n"
tree = ast.parse(source)
print([statement.value.value for statement in tree.body])
namespace = {}
exec(compile(tree, "<leading zeros>", "exec"), namespace)
print(namespace["zero"], namespace["hex_value"])
