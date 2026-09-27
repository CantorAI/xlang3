from pydantic import ConfigDict, Field, ValidationError
from pydantic.dataclasses import dataclass


def alias(name):
    return 'alias_' + name


@dataclass(config=ConfigDict(alias_generator=alias))
class Item:
    name: str
    score: int = Field(alias='my_score')


try:
    Item(name='n', score=2)
except ValidationError as error:
    print([(item['type'], item['loc']) for item in error.errors(include_url=False)])
print('valid', Item(alias_name='n', my_score=2).score)
