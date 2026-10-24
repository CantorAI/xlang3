"""Compare builtin-name mapping and Python AST templates; no timing score."""
import ast
import builtins
import sys
from pathlib import Path

sys.path.insert(0, str(Path('venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages').resolve()))
from chameleon.codegen import reverse_builtin_map, template

print('builtin None entry', 'None' in builtins.__dict__)
print('builtin None identity', getattr(builtins, 'None', 'missing') is None)
print('reverse None entry', None in reverse_builtin_map, reverse_builtin_map.get(None))
node = template('quote(VALUE, DEFAULT, MARKER)', VALUE=ast.Constant('text'),
                DEFAULT=None, MARKER=ast.Constant('marker'), mode='eval')
print('template arguments', len(node.args), ast.unparse(node))
