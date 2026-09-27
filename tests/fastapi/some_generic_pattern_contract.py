from typing import Any

from pydantic_core import Some


print('generic identity:', Some[Any] is Some, Some[int] is Some)


def describe(value):
    match value:
        case Some(1):
            return 'one'
        case Some(int(item)):
            return f'int {item}'
        case Some(value=item):
            return f'value {item}'


print('patterns:', describe(Some(1)), describe(Some(3)), describe(Some('x')))
