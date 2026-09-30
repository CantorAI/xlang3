from datetime import timedelta

from pydantic import TypeAdapter, ValidationError


adapter = TypeAdapter(timedelta)
for value in ('30', '-1', 'broken'):
    try:
        adapter.validate_python(value)
    except ValidationError as error:
        item = error.errors(include_url=False)[0]
        print(value, item['type'], item['msg'])

for value in ('"errordata"', '"broken"', '"12345"', '"12:xx"'):
    try:
        adapter.validate_json(value)
    except ValidationError as error:
        item = error.errors(include_url=False)[0]
        print(value, item['type'], item['msg'], item.get('ctx'))
