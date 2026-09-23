import ast
import asyncio
import sys
import traceback


try:
    ast.parse("(\nasync with foo:\n)")
except SyntaxError as error:
    print(type(error).__name__)


async def operation():
    try:
        raise RuntimeError("original")
    except BaseException:
        print("RuntimeError: original" in traceback.format_exc())
        await asyncio.sleep(0)
        raise


try:
    asyncio.run(operation())
except BaseException as error:
    print(type(error).__name__, str(error))


try:
    raise RuntimeError("nested")
except RuntimeError as error:
    traceback.format_exception(type(error), error, error.__traceback__)
    print(type(sys.exception()).__name__, "RuntimeError: nested" in traceback.format_exc())
