class CustomGroup(BaseExceptionGroup):
    pass


for group in (
    BaseExceptionGroup("ordinary", [ValueError("one"), TypeError("two")]),
    BaseExceptionGroup("base", [KeyboardInterrupt("stop")]),
    BaseExceptionGroup("nested", [ExceptionGroup("inner", [ValueError("one")])]),
    CustomGroup("custom", [ValueError("one")]),
):
    print(type(group).__name__, isinstance(group, ExceptionGroup), len(group.exceptions))

try:
    ExceptionGroup("invalid", [KeyboardInterrupt("stop")])
except TypeError as error:
    print(type(error).__name__)
