from __future__ import annotations

from typing import Annotated


class Example:
    value: Annotated[str, AfterValidator(lambda x: x.upper())]


def transform(value: Annotated[int, Check(lambda x: x + 1)]) -> list[int]:
    return [value]


print(Example.__annotations__["value"])
print(transform.__annotations__["value"])
print(transform.__annotations__["return"])
