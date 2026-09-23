import ast

arguments = ast.arguments([], [], None, [], [], None, [])
function = ast.FunctionDef(
    "generated",
    arguments,
    [ast.Return(ast.Constant("ok"))],
    [],
    None,
    None,
    [],
)
module = ast.Module([function], [])
ast.fix_missing_locations(module)
namespace = {}
exec(compile(module, "<generated>", "exec"), namespace)
print(namespace["generated"]())
