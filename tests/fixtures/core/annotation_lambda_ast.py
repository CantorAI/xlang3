import ast

source = (
    'from __future__ import annotations\n'
    'from typing import Annotated\n'
    'class C:\n'
    '    p: Annotated[int, lambda x: x > 0]\n'
    '    q: Annotated[int, lambda x: x % 3 == 0]\n'
)
namespace = {}
exec(compile(ast.parse(source), '<annotation-ast>', 'exec'), namespace)
print(namespace['C'].__annotations__)

runtime_source = (
    'from typing import Annotated\n'
    'class D:\n'
    '    p: Annotated[int, lambda x: x > 0]\n'
)
runtime_namespace = {}
exec(compile(ast.parse(runtime_source), '<annotation-ast-runtime>', 'exec'),
     runtime_namespace)
print(runtime_namespace['D'].__annotations__['p'].__metadata__[0].__qualname__)
