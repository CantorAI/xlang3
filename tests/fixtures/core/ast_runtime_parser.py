import ast

source = """if kwargs:
    params = self._encode_query_vars(kwargs)
    q = "?" if params else ""
else:
    q = params = ""
"""
tree = ast.parse(source)
node = tree.body[0]
print(type(node).__name__, node.lineno, node.end_lineno)
print([type(item).__name__ for item in node.body])
print([type(item).__name__ for item in node.orelse])
print([type(item).__name__ for item in ast.walk(node)][-4:])
