import ast
import builtins

for name, value in [('None', None), ('False', False), ('True', True)]:
    assert name in builtins.__dict__
    assert builtins.__dict__[name] is value
    assert getattr(builtins, name) is value
    assert name in dir(builtins)
    expression = ast.parse(name, mode='eval').body
    assert isinstance(expression, ast.Constant)
    assert expression.value is value
    assert ast.dump(expression) == ast.dump(ast.parse(name).body[0].value)
print('builtin literal namespace and identity', True)

# Python libraries can build symbol maps from the builtin namespace; a missing
# None entry makes a NodeTransformer interpret the replacement as deletion.
reverse = {value: name for name, value in builtins.__dict__.items()
           if name in ('None', 'False', 'True')}


class Rewrite(ast.NodeTransformer):
    def visit_Name(self, node):
        if node.id == 'DEFAULT':
            assert None in reverse
            return ast.copy_location(ast.Constant(getattr(builtins, reverse[None])), node)
        return node


tree = ast.parse('f(VALUE, DEFAULT, MARKER)', mode='eval')
Rewrite().visit(tree)
assert len(tree.body.args) == 3
assert tree.body.args[1].value is None
assert eval(compile(ast.fix_missing_locations(tree), '<builtin map>', 'eval'),
            {'f': lambda *args: args, 'VALUE': 2, 'MARKER': 3}) == (2, None, 3)
print('Python symbol mapping retains None call arguments', True)

for name, value in [('None', None), ('False', False), ('True', True)]:
    try:
        setattr(builtins, name, 'rebound')
        assert getattr(builtins, name) == 'rebound'
        assert eval(name) is value
        assert eval(compile(ast.parse(name, mode='eval'), '<literal>', 'eval')) is value
    finally:
        setattr(builtins, name, value)
print('keyword literals remain independent of builtin rebinding', True)
