import ast


source = '''from __future__ import annotations
class Box:
    value: Annotated[str, AfterValidator(lambda x: x.upper())]
'''
namespace = {}
exec(compile(ast.parse(source), '<ast-lambda>', 'exec'), namespace)
print(namespace['Box'].__annotations__['value'])
