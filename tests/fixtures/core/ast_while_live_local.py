import ast


source = '''
class Scope:
    def __init__(self, parent=None, cancelled=False):
        self.parent = parent
        self.cancelled = cancelled

    def ancestor_cancelled(self):
        cursor = self.parent
        visits = 0
        while cursor is not None:
            visits += 1
            if cursor.cancelled:
                return visits, True
            cursor = cursor.parent
        return visits, False
'''

namespace = {}
exec(compile(ast.parse(source), "<ast-while-oracle>", "exec"), namespace)
Scope = namespace["Scope"]
root = Scope()
print(Scope(root).ancestor_cancelled())
print(Scope(Scope(root, cancelled=True)).ancestor_cancelled())
print(Scope(Scope(root), cancelled=False).ancestor_cancelled())
