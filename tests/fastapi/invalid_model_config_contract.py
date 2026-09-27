from pydantic import BaseModel, create_model
from pydantic.dataclasses import dataclass
from pydantic_core import SchemaError


try:
    class InvalidModel(BaseModel):
        model_config = {'extra': 'invalid-value'}
except SchemaError as exc:
    print('error', 'Invalid extra_behavior: `invalid-value`' in str(exc))

try:
    create_model('InvalidCreated', __config__={'extra': 'invalid-value'})
except SchemaError as exc:
    print('created-error', 'Invalid extra_behavior: `invalid-value`' in str(exc))

try:
    @dataclass(config={'extra': 'invalid-value'})
    class InvalidDataclass:
        pass
except SchemaError as exc:
    print('dataclass-error', 'Invalid extra_behavior: `invalid-value`' in str(exc))
