import ast


source = "type Alias[T] = list[T]"
alias = ast.parse(source).body[0]
print(type(alias).__name__, alias.name.id, type(alias.name.ctx).__name__)
print(type(alias.type_params[0]).__name__, alias.type_params[0].name)
print(ast.unparse(alias))
print(alias.lineno, alias.col_offset, alias.end_lineno, alias.end_col_offset)
print(alias.name.col_offset, alias.name.end_col_offset)
print(alias.type_params[0].col_offset, alias.type_params[0].end_col_offset)
namespace = {}
exec(compile(ast.parse(source), "<alias ast>", "exec"), namespace)
print(type(namespace["Alias"]).__name__, len(namespace["Alias"].__type_params__), repr(namespace["Alias"].__value__))
