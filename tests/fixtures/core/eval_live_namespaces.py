import ast
import builtins
import sys

namespace = {'x': 1}
def mutate():
    namespace['x'] = 2
    return 0
namespace['mutate'] = mutate
print('live callback', eval(compile('mutate() + x', '<live>', 'eval'), namespace, {}))
print('identity', eval('globals()', namespace, {}) is namespace)
print('builtins inserted', namespace['__builtins__'] is builtins.__dict__)
callback = eval('lambda: x', namespace, {})
namespace['x'] = 3
print('returned function', callback(), callback.__globals__ is namespace)
locals_mapping = {'x': 7}
print('locals', eval('x', namespace, locals_mapping), eval('locals()', namespace, locals_mapping) is locals_mapping)
namespace.update(sys=sys, ns=namespace, lm=locals_mapping)
print('frame namespaces', eval('sys._getframe().f_globals is ns', namespace, locals_mapping),
      eval('sys._getframe().f_locals is lm', namespace, locals_mapping))

custom = {'len': lambda value: 42}
custom_globals = {'__builtins__': custom}
custom_globals.update(sys=sys, wanted=custom)
print('frame builtins', eval('sys._getframe().f_builtins is wanted', custom_globals, {}))
print('custom len', eval('len([1, 2])', custom_globals, {}))
print('AST custom len', eval(compile(ast.parse('len([])', mode='eval'), '<AST>', 'eval'), custom_globals, {}))
custom_function = eval('lambda: len([])', custom_globals, {})
custom_globals['__builtins__'] = {'len': lambda value: 99}
print('captured function builtins', custom_function())
custom['len'] = lambda value: 43
print('captured mapping remains live', custom_function())
def replace_builtins():
    custom_globals['__builtins__'] = {'len': lambda value: 100}
    return 0
custom_globals['replace'] = replace_builtins
print('active frame keeps builtins', eval('replace() + len([])', custom_globals, {}))

try:
    eval('abs(-1)', {'__builtins__': {}}, {})
except NameError:
    print('empty builtins blocks fallback')
print('locals builtin override', eval('len([])', namespace, {'len': lambda value: 8}))
def change_len():
    locals_mapping['len'] = lambda value: 5
    return []
locals_mapping.update(len=lambda value: 4, produce=change_len)
print('callee before arguments', eval('len(produce())', namespace, locals_mapping))

written_globals = {'x': 1}
written_locals = {}
print('walrus result', eval('(x := 9) + x', written_globals, written_locals))
print('walrus target', written_globals['x'], written_locals.get('x'))

class LocalsMapping:
    def __init__(self):
        self.reads = []
    def __getitem__(self, name):
        self.reads.append(name)
        if name == 'dynamic':
            return len(self.reads)
        raise KeyError(name)
mapping = LocalsMapping()
print('dynamic locals', eval('dynamic + dynamic + x', namespace, mapping), mapping.reads)

try:
    eval('1 / 0', namespace, {})
except ZeroDivisionError:
    print('failure unwinds locals')
print('after failure', eval('x', namespace, {'x': 11}))
