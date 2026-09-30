from datetime import date, datetime

from pydantic import TypeAdapter, ValidationError


for kind in (date, datetime):
    adapter = TypeAdapter(kind)
    for value in (10**100, -(10**100), float('nan')):
        try:
            adapter.validate_python(value)
        except ValidationError as error:
            item = error.errors(include_url=False)[0]
            print(kind.__name__, item['type'], item['msg'], item.get('ctx'))
