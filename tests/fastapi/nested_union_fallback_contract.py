import re
import warnings

from pydantic_core import SchemaSerializer, core_schema


class ModelA:
    pass


class ModelB:
    pass


class ModelCat:
    pass


class ModelDog:
    pass


class ModelAlien:
    def __init__(self):
        self.type_ = 'alien'


def model(cls, fields):
    return core_schema.model_schema(
        cls=cls,
        schema=core_schema.model_fields_schema(
            fields={name: core_schema.model_field(schema) for name, schema in fields.items()}
        ),
    )


serializer = SchemaSerializer(
    core_schema.union_schema(
        [
            core_schema.union_schema(
                [
                    model(ModelA, {'a': core_schema.str_schema(), 'b': core_schema.str_schema()}),
                    model(ModelB, {'c': core_schema.str_schema(), 'd': core_schema.str_schema()}),
                ]
            ),
            core_schema.union_schema(
                [
                    model(ModelCat, {'type_': core_schema.literal_schema(['cat'])}),
                    model(ModelDog, {'type_': core_schema.literal_schema(['dog'])}),
                ]
            ),
        ]
    )
)
value = ModelAlien()
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always')
    result = serializer.to_python(value)
print('python identity:', result is value)
print('warning count:', len(caught))
print('warning models:', re.findall(r'Expected `(Model[^`]+)`', str(caught[0].message)))

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always')
    try:
        serializer.to_python(value, mode='json')
    except Exception as exc:
        print('json exception:', type(exc).__name__)
        print('json unknown type:', 'Unable to serialize unknown type:' in str(exc))
    else:
        print('json exception: none')
