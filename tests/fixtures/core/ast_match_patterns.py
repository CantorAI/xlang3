import ast


source = '''
def classify(value):
    match value:
        case 1 | 2:
            return "number"
        case [head, *tail] if head:
            return "sequence"
        case {"kind": kind, **rest}:
            return "mapping"
        case str(prefix=part):
            return "class"
        case None:
            return "none"
        case _ as other:
            return "other"
'''

tree = ast.parse(source)
match = tree.body[0].body[0]
print(type(match).__name__, len(match.cases))
for case in match.cases:
    pattern = case.pattern
    print(type(pattern).__name__, tuple(pattern._fields))
    print(type(case.guard).__name__ if case.guard is not None else "no guard")

print("or", len(match.cases[0].pattern.patterns))
print("sequence", [type(part).__name__ for part in match.cases[1].pattern.patterns])
print("mapping", len(match.cases[2].pattern.keys), match.cases[2].pattern.rest)
print("class", match.cases[3].pattern.kwd_attrs)
print("singleton", match.cases[4].pattern.value is None)
print("capture", match.cases[5].pattern.name)

namespace = {}
exec(compile(tree, "<match-oracle>", "exec"), namespace)
for value in (1, [3, 4], {"kind": "item"}, None, object()):
    print(namespace["classify"](value))
