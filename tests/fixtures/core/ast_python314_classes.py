import ast

print(ast.Lambda._fields)
print(ast.IfExp._fields)
print(ast.TypeVar._fields)
print(ast.ParamSpec._fields)
print(ast.TypeVarTuple._fields)
print(ast.FunctionDef._fields)
print(ast.ClassDef._fields)
print(ast.match_case.__bases__ == (ast.AST,))
print(ast.match_case._fields)
print(ast.match_case._attributes)
