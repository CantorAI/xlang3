import ast

constant = ast.Constant(1)
print(constant.kind)
print(constant.__dict__)
print(ast.unparse(ast.Subscript(ast.Name("value"), ast.Constant("key"))))
