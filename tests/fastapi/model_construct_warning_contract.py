import warnings

from pydantic import BaseModel


class Payload(BaseModel):
    required: float
    optional: int = 10


model = Payload.model_construct(optional='wrong')
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always')
    print('dump', model.model_dump())

print('warnings', len(caught))
print('field-warning', "field_name='optional'" in str(caught[0].message))
print('count-warning', any('fields but got' in str(item.message) for item in caught))
