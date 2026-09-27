import ast


class Value:
    def __repr__(self):
        return 'Value()'


item = {'key': Value()}
namespace = {}
tree = ast.parse("def render(value):\n    return f'{value}'\n")
exec(compile(tree, '<container-format>', 'exec'), namespace)
print(namespace['render'](item))
print(format(item))
