from pydantic import ValidationError
from pydantic.dataclasses import dataclass


@dataclass
class Input:
    first: int
    second: int


for args, kwargs in [((1, 'bad'), {}), ((1,), {'second': 'bad'}), ((), {'first': 1, 'second': 'bad'})]:
    try:
        Input(*args, **kwargs)
    except ValidationError as error:
        print(error.errors(include_url=False)[0]['loc'])
