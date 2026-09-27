from datetime import time

from pydantic import TypeAdapter, ValidationError


adapter = TypeAdapter(time)
for value in ('091500', b'091500', '09:1500', '09:15:90',
              '11:05:00Y', '11:05:00-25:00', '09:15:00'):
    try:
        result = adapter.validate_python(value)
    except ValidationError as error:
        item = error.errors(include_url=False)[0]
        print('error', item['type'], item['msg'])
    else:
        print('valid', result.isoformat())
