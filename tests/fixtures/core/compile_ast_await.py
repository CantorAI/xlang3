import ast
import asyncio


source = "async def result():\n    return await child()\n"
tree = ast.parse(source)
print(type(tree.body[0].body[0].value).__name__)


async def child():
    return 7


namespace = {"child": child}
exec(compile(tree, "<await ast>", "exec"), namespace)
print(asyncio.run(namespace["result"]()))
