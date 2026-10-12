"""Diagnose AST argument replacement without translating Python visitors."""
import ast
import sys
from pathlib import Path

sys.path.insert(0, str(Path("venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages").resolve()))
from chameleon.codegen import template


class Rewrite(ast.NodeTransformer):
    def visit_Name(self, node):
        return ast.Constant("options") if node.id == "KEY" else node


node = ast.parse("getname(KEY)", mode="eval")
Rewrite().visit(node)
print("plain transform", len(node.body.args), ast.unparse(node))
value = ast.Constant("options")
print("AST membership", isinstance(value, ast.AST), isinstance(value, type))
node = template("getname(KEY)", KEY=value, mode="eval")
print("template transform", len(node.args), ast.unparse(node))
