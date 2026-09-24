import gc
from weakref import WeakValueDictionary

from pydantic_core import SchemaValidator, core_schema


class Model:
    pass


class Iterable:
    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration


validator = SchemaValidator(
    core_schema.model_schema(
        Model,
        core_schema.model_fields_schema(
            {
                "stream": core_schema.model_field(
                    core_schema.generator_schema(core_schema.int_schema())
                )
            }
        ),
    )
)


def check_collection():
    cache = WeakValueDictionary()
    for _ in range(1000):
        value = Iterable()
        cache[id(value)] = value
        validator.validate_python({"stream": value})
        del value
    gc.collect()
    return len(cache)


value = Iterable()
print("iterator-identity", iter(value) is value)
print("retained", check_collection())
