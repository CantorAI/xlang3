import dataclasses

from pydantic import ValidationError
from pydantic.dataclasses import dataclass


@dataclass(kw_only=True)
class Item:
    first: int | None = None
    second: str = ''


print('fields', [(field.name, field.kw_only) for field in dataclasses.fields(Item)])
print('schema', [(field['name'], field.get('kw_only')) for field in Item.__pydantic_core_schema__['schema']['fields']])


@dataclass
class Regular:
    first: int | None = None
    second: str = ''


print('regular-schema', [(field['name'], field.get('kw_only')) for field in Regular.__pydantic_core_schema__['schema']['fields']])
try:
    Item(1, 'x')
except ValidationError as error:
    print('error', [(item['type'], item['loc']) for item in error.errors(include_url=False)])
else:
    print('accepted')
print('keyword', Item(second='ok').second)
