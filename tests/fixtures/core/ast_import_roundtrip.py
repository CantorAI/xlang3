import ast


source = (
    "import math, json as js\n"
    "from collections import deque as Queue, namedtuple\n"
    "from . import widget as alias\n"
    "from ..shared import item\n"
)
tree = ast.parse(source)
for statement in tree.body:
    if isinstance(statement, ast.Import):
        print("import", [(alias.name, alias.asname) for alias in statement.names])
    else:
        print("from", statement.module, statement.level,
              [(alias.name, alias.asname) for alias in statement.names])

namespace = {}
executable = ast.parse(
    "import math, json as js\n"
    "from collections import deque as Queue, namedtuple\n"
    "mapping = {'a': 1, **{'b': 2}}\n"
    "members = {3, 4}\n"
)
exec(compile(executable, "<ast-import>", "exec"), namespace)
print("execute", namespace["math"].sqrt(81),
      namespace["js"].loads("[1, 2]"),
      list(namespace["Queue"]([3, 4])),
      namespace["namedtuple"].__name__)
print("literals", namespace["mapping"], sorted(namespace["members"]))
