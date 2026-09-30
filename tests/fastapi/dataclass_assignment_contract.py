from pydantic import ConfigDict, ValidationError
from pydantic.dataclasses import dataclass


@dataclass(config=ConfigDict(validate_assignment=True))
class Item:
    value: int


item = Item(1)
item.value = '7'
print('validated', item.value)
try:
    item.value = 'bad'
except ValidationError as error:
    print('error', error.errors(include_url=False)[0]['loc'])
print('preserved', item.value)
