import ast

names = {"trio": False, "asyncio": True}
trees = (
    ast.Expression(ast.UnaryOp(ast.Not(), ast.Name("trio", ast.Load()))),
    ast.Expression(ast.BoolOp(ast.And(), [
        ast.Name("asyncio", ast.Load()),
        ast.UnaryOp(ast.Not(), ast.Name("trio", ast.Load())),
    ])),
    ast.Expression(ast.IfExp(
        ast.Name("asyncio", ast.Load()), ast.Constant(1), ast.Constant(2))),
)
for tree in trees:
    ast.fix_missing_locations(tree)
    code = compile(tree, "<expression>", "eval")
    print(eval(code, names))

for source in ("not trio", "1 < 2", "2 + 3"):
    tree = ast.parse(source, mode="eval")
    print(eval(compile(tree, "<parsed expression>", "eval"), names))

interactive = ast.fix_missing_locations(ast.Interactive([ast.Expr(ast.Constant(3))]))
exec(compile(interactive, "<interactive>", "single"))

for tree, mode in ((ast.Expression(ast.Constant(1)), "exec"),
                   (ast.Module([], []), "eval")):
    try:
        compile(ast.fix_missing_locations(tree), "<expression>", mode)
    except TypeError as exc:
        print(type(exc).__name__, str(exc))
