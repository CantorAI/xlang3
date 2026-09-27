from typing import Generic, TypeVar, get_args, get_origin

from pydantic import TypeAdapter, ValidationError
from pydantic.dataclasses import dataclass


T = TypeVar('T')


@dataclass
class Box(Generic[T]):
    value: T


adapter = TypeAdapter(Box[int])
print('alias', get_origin(Box[int]) is Box, get_args(Box[int]) == (int,), type(Box[int]).__name__)
print('alias-dict-schema', Box[int].__dict__.get('__pydantic_core_schema__') is not None)
field = adapter.core_schema['schema']['fields'][0]['schema']
print('schema-type', field['type'])
try:
    adapter.validate_python({'value': 'bad'})
except ValidationError as error:
    print('error', [(item['type'], item['loc']) for item in error.errors(include_url=False)])
else:
    print('accepted')
print('good', adapter.validate_python({'value': '7'}).value)
