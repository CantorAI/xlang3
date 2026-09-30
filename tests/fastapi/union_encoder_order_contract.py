from decimal import Decimal
from typing import Union

from pydantic import ConfigDict, TypeAdapter


config = ConfigDict(json_encoders={
    Decimal: lambda value: str(value * 2),
    int: lambda value: str(value * 3),
})

for annotation in (Union[Decimal, int], Union[int, Decimal]):
    adapter = TypeAdapter(annotation, config=config)
    print('int', adapter.dump_json(1))
    print('decimal', adapter.dump_json(Decimal('1.1')))
