import ast


source = '''def factory():
    class Holder:
        callback = lambda value: f"wrapped-{value}"
    return Holder.callback
'''
namespace = {}
exec(compile(ast.parse(source), '<ast-fstring>', 'exec'), namespace)
callback = namespace['factory']()
print(callback('foo'), callback.__code__.co_freevars)
