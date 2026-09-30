import ast


for source in ("1+2j", 'assert a == f"{b}"'):
    ast.NodeVisitor().visit(ast.parse(source))
    print("visited", source)
