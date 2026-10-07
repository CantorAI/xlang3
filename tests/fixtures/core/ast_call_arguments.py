import ast


sources = [
    "f(KEY)",
    "f('options')",
    "f(KEY, 7, named=VALUE)",
    "f(*ARGS, named=VALUE, **KWARGS)",
    "f(g(KEY), named=h(VALUE))",
    "factory()(KEY)",
    "f(KEY)['table']",
    "f(KEY) + g(VALUE)",
    "f()",
]
names = dict(KEY=2, VALUE=3, ARGS=(2, 7), KWARGS={'other': 4})
names.update(f=lambda *args, **kwargs: {'table': 9} if args == (2,) and not kwargs else (args, kwargs),
             g=lambda value: value * 2, h=lambda value: value + 1,
             factory=lambda: lambda value: value * 3)
for source in sources:
    expression = ast.parse(source, mode='eval')
    statement = ast.parse(source).body[0].value
    assert ast.dump(expression.body) == ast.dump(statement), source
    # Compare the complete call tree, then exercise compiled argument binding.
    if source != "f(KEY) + g(VALUE)":
        assert eval(compile(expression, '<call AST>', 'eval'), names) == eval(source, names), source
print('eval and exec preserve call trees', True)


class Rewrite(ast.NodeTransformer):
    def visit_Name(self, node):
        return ast.copy_location(ast.Constant('options'), node) if node.id == 'KEY' else node


tree = ast.parse('getname(KEY)', mode='eval')
assert len(tree.body.args) == 1
assert isinstance(tree.body.args[0], ast.Name)
assert tree.body.args[0].id == 'KEY'
assert tree.body.args[0].lineno == 1
assert tree.body.args[0].col_offset == 8
Rewrite().visit(tree)
assert tree.body.args[0].value == 'options'
assert eval(compile(ast.fix_missing_locations(tree), '<rewritten call>', 'eval'),
            {'getname': lambda key: key}) == 'options'
print('Python AST visitors preserve arguments and locations', True)

tree = ast.parse('(lambda value: value)()', mode='eval')
assert isinstance(tree.body.func, ast.Lambda)
assert tree.body.args == []
print('complex empty callees preserve their AST', True)
