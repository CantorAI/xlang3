from pydantic_core import SchemaValidator, core_schema


class Model:
    __slots__ = ('__dict__', '__pydantic_fields_set__', '__pydantic_extra__', '__pydantic_private__')

    def __init__(self, **values):
        self.__dict__.update(values)


calls = []


def before(value, info):
    calls.append((type(value['x']).__name__, value))
    return value


validator = SchemaValidator(
    core_schema.model_schema(
        Model,
        core_schema.with_info_before_validator_function(
            before,
            core_schema.model_fields_schema(
                {
                    'x': core_schema.model_field(core_schema.str_schema()),
                    'y': core_schema.model_field(core_schema.int_schema()),
                }
            ),
        ),
    )
)
model = Model()
validator.validate_python({'x': b'input', 'y': '123'}, self_instance=model)
print('initial:', model.x, model.y)
validator.validate_assignment(model, 'x', b'different')
print('assigned:', model.x, model.y)
print('callback types:', [item[0] for item in calls])
print('callback values:', [item[1] for item in calls])
