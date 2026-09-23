from pydantic import BaseModel, TypeAdapter, validate_call
from pydantic_core import ArgsKwargs, MISSING, SchemaSerializer, SchemaValidator, ValidationError, core_schema


complex_adapter = TypeAdapter(complex)
for value in (1, 1.5, "1+2j", complex(2, 3)):
    parsed = complex_adapter.validate_python(value)
    print(repr(parsed), complex_adapter.dump_python(parsed, mode="json"))

instance_validator = SchemaValidator(core_schema.is_instance_schema(dict))
subclass_validator = SchemaValidator(core_schema.is_subclass_schema(BaseException))
callable_validator = SchemaValidator(core_schema.callable_schema())
print(instance_validator.validate_python({}) == {})
print(subclass_validator.validate_python(ValueError).__name__)
print(callable_validator.validate_python(len).__name__)

chain_schema = core_schema.chain_schema(
    [core_schema.int_schema(), core_schema.int_schema(gt=0)]
)
chain_validator = SchemaValidator(chain_schema)
chain_serializer = SchemaSerializer(chain_schema)
print(chain_validator.validate_python("4"), chain_serializer.to_python(4, mode="json"))

lax_schema = core_schema.lax_or_strict_schema(
    core_schema.int_schema(), core_schema.str_schema()
)
lax_validator = SchemaValidator(lax_schema)
print(lax_validator.validate_python("4"), lax_validator.validate_python("word", strict=True))

combined = SchemaValidator(
    core_schema.json_or_python_schema(
        json_schema=core_schema.str_schema(),
        python_schema=core_schema.int_schema(),
    )
)
print(combined.validate_python("7"), combined.validate_json('"seven"'))

json_value = SchemaValidator(
    core_schema.json_schema(core_schema.list_schema(core_schema.int_schema()))
)
print(json_value.validate_python('[1, "2"]'))
print(TypeAdapter(list[int]).validate_json('[3, "4"]'))

custom_schema = core_schema.custom_error_schema(
    core_schema.int_schema(),
    "positive_identifier",
    custom_error_message="identifier {identifier} is invalid",
    custom_error_context={"identifier": 42},
)
custom_validator = SchemaValidator(custom_schema)
custom_serializer = SchemaSerializer(custom_schema)
print(custom_validator.validate_python("8"), custom_serializer.to_json(8))
try:
    custom_validator.validate_python("wrong")
except ValidationError as error:
    print(error.errors(include_url=False))

missing_validator = SchemaValidator(core_schema.missing_sentinel_schema())
print(missing_validator.validate_python(MISSING) is MISSING)
try:
    missing_validator.validate_python(None)
except ValidationError as error:
    print(error.errors(include_url=False)[0]["type"])

arguments_schema = core_schema.arguments_schema(
    [
        core_schema.arguments_parameter(
            "value", core_schema.int_schema(), mode="positional_or_keyword"
        ),
        core_schema.arguments_parameter(
            "scale", core_schema.float_schema(), mode="keyword_only"
        ),
    ]
)
arguments = ArgsKwargs(("4",), {"scale": "2.5"})
print(arguments.args, arguments.kwargs)
print(SchemaValidator(arguments_schema).validate_python(arguments))


@validate_call
def scaled(value: int, *, scale: float = 1.0) -> float:
    return value * scale


print(scaled("4", scale="2.5"))

events = []


def source():
    events.append("start")
    yield "1"
    events.append("middle")
    yield 2


generator_schema = core_schema.generator_schema(
    core_schema.int_schema(), min_length=1, max_length=3
)
validated_generator = SchemaValidator(generator_schema).validate_python(source())
print(type(validated_generator).__name__, validated_generator.index, events)
print(next(validated_generator), validated_generator.index, events)
print(list(validated_generator), validated_generator.index, events)

generator_serializer = SchemaSerializer(generator_schema)
serialized_generator = generator_serializer.to_python(iter([3, 4]))
print(type(serialized_generator).__name__, list(serialized_generator), serialized_generator.index)
print(generator_serializer.to_json(iter([5, 6])))

try:
    list(SchemaValidator(generator_schema).validate_python(iter(["wrong"])))
except ValidationError as error:
    detail = error.errors(include_url=False)[0]
    print(detail["type"], detail["loc"])

try:
    instance_validator.validate_python([])
except ValidationError as error:
    print(error.errors(include_url=False)[0]["type"])

try:
    TypeAdapter(int).validate_json("{bad")
except ValidationError as error:
    print(error.errors(include_url=False)[0]["type"])

try:
    TypeAdapter(__import__("pydantic").HttpUrl).validate_python("not a valid url")
except ValidationError as error:
    detail = error.errors(include_url=False)[0]
    print(detail["type"], detail["ctx"])


class CustomInitModel:
    def __init__(self, **values):
        self.raw_values = values


custom_init_schema = core_schema.model_schema(
    CustomInitModel,
    core_schema.model_fields_schema(
        fields={"value": core_schema.model_field(core_schema.int_schema())}
    ),
    custom_init=True,
)
custom_init_model = SchemaValidator(custom_init_schema).validate_python(
    {"value": "12"}
)
print(
    type(custom_init_model).__name__,
    custom_init_model.raw_values["value"],
    type(custom_init_model.raw_values["value"]).__name__,
)


class AttributeOutput:
    def __init__(self, **values):
        raise AssertionError("custom init must not run for attribute input")


class AttributeSource:
    value = "12"


attribute_fields = core_schema.model_fields_schema(
    fields={"value": core_schema.model_field(core_schema.int_schema())},
    from_attributes=False,
)
attribute_model = SchemaValidator(
    core_schema.model_schema(AttributeOutput, attribute_fields, custom_init=True)
).validate_python(AttributeSource(), from_attributes=True)
print("AttributeModel", attribute_model.value, type(attribute_model.value).__name__)


class DumpModel:
    pass


dump_model = DumpModel()
dump_model.first = 1
dump_model.second = 2
dump_model.__pydantic_fields_set__ = {"first"}
dump_fields = core_schema.model_fields_schema(
    fields={
        "first": core_schema.model_field(core_schema.int_schema()),
        "second": core_schema.model_field(core_schema.int_schema()),
    }
)
dump_schema = core_schema.model_schema(DumpModel, dump_fields)
print(
    "ExcludeUnset",
    SchemaSerializer(dump_schema).to_python(dump_model, exclude_unset=True),
)


class BroadUnionChoice:
    pass


class SpecificUnionChoice:
    pass


broad_union_schema = core_schema.model_schema(
    BroadUnionChoice,
    core_schema.model_fields_schema(
        fields={
            "kind": core_schema.model_field(core_schema.str_schema()),
            "scheme": core_schema.model_field(
                core_schema.with_default_schema(
                    core_schema.str_schema(), default="fallback"
                )
            ),
        }
    ),
    config={"extra_fields_behavior": "allow"},
)
specific_union_schema = core_schema.model_schema(
    SpecificUnionChoice,
    core_schema.model_fields_schema(
        fields={
            "kind": core_schema.model_field(core_schema.str_schema()),
            "location": core_schema.model_field(core_schema.str_schema()),
            "name": core_schema.model_field(core_schema.str_schema()),
        }
    ),
)
smart_union_result = SchemaValidator(
    core_schema.union_schema([broad_union_schema, specific_union_schema])
).validate_python({"kind": "apiKey", "location": "header", "name": "key"})
print(
    "SmartUnion",
    type(smart_union_result).__name__,
    sorted(smart_union_result.__pydantic_fields_set__),
)


class ExtraAllowModel(BaseModel):
    value: int
    model_config = {"extra": "allow"}


class ExtraForbidModel(BaseModel):
    value: int
    model_config = {"extra": "forbid"}


extra_allow = ExtraAllowModel.model_validate(
    {"value": "7", "note": "kept"}, from_attributes=True
)
print(
    "ExtraAllow",
    extra_allow.model_dump(),
    extra_allow.__pydantic_extra__,
    extra_allow.note,
)
try:
    ExtraForbidModel.model_validate(
        {"value": "7", "note": "rejected"}, from_attributes=True
    )
except ValidationError as exc:
    error = exc.errors()[0]
    print(
        "ExtraForbid",
        error["type"],
        error["loc"],
        error["msg"],
        error["input"],
    )


pattern_validator = SchemaValidator(core_schema.str_schema(pattern="^password$"))
print("StringPattern", pattern_validator.validate_python("password"))
try:
    pattern_validator.validate_python("client_credentials")
except ValidationError as exc:
    error = exc.errors()[0]
    print(
        "StringPatternError",
        error["type"],
        error["msg"],
        error["ctx"],
        error["input"],
    )
