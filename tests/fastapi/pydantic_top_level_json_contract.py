from fastapi import FastAPI
from fastapi.testclient import TestClient
from pydantic_core import (
    CoreConfig,
    PydanticSerializationError,
    SchemaValidator,
    ValidationError,
    core_schema,
    from_json,
    to_json,
    to_jsonable_python,
)


class Unknown:
    def __str__(self):
        return "unknown-text"

    def __hash__(self):
        return 1


class BrokenMeta(type):
    def __repr__(self):
        raise ValueError("bad repr")


class BrokenRepr(metaclass=BrokenMeta):
    def __repr__(self):
        raise ValueError("bad repr")


def fallback(value):
    return "fallback:" + type(value).__name__


assert to_json([1, b"x"]) == b'[1,"x"]'
assert to_json(["à"], ensure_ascii=True) == b'["\\u00e0"]'
assert to_jsonable_python({1, 2}) == [1, 2]
assert to_jsonable_python([0, 1, 2], exclude={1}) == [0, 2]
print("basic", to_jsonable_python([1, b"x"]), to_json(["à"], ensure_ascii=True))

for function in (to_json, to_jsonable_python):
    try:
        function(Unknown())
    except PydanticSerializationError as error:
        assert "Unable to serialize unknown type:" in str(error)
    else:
        raise AssertionError("unknown object was serialized without fallback")
    assert function(Unknown(), serialize_unknown=True) in (
        b'"unknown-text"', "unknown-text"
    )
    assert function(Unknown(), fallback=fallback) in (
        b'"fallback:Unknown"', "fallback:Unknown"
    )
    assert function(Unknown(), serialize_unknown=True, fallback=fallback) in (
        b'"fallback:Unknown"', "fallback:Unknown"
    )
print("fallback", "ok")
for function in (to_json, to_jsonable_python):
    try:
        function(BrokenRepr())
    except PydanticSerializationError as error:
        assert str(error) == (
            "Unable to serialize unknown type: "
            "<unprintable BrokenMeta object>"
        )
    else:
        raise AssertionError("broken representation was accepted")
    assert function(BrokenRepr(), serialize_unknown=True) in (
        b'"<Unserializable BrokenRepr object>"',
        "<Unserializable BrokenRepr object>",
    )
print("repr", "ok")
assert to_jsonable_python({Unknown(): 1}, fallback=fallback) == {
    "fallback:Unknown": 1
}
assert to_json({Unknown(): 1}, serialize_unknown=True) == b'{"unknown-text":1}'
print("keys", "ok")

base64_validator = SchemaValidator(
    core_schema.bytes_schema(), config=CoreConfig(val_json_bytes="base64")
)
hex_validator = SchemaValidator(
    core_schema.bytes_schema(), config=CoreConfig(val_json_bytes="hex")
)
assert base64_validator.validate_json('"bm8tcGFkZGluZw"') == b"no-padding"
assert hex_validator.validate_json('"68656c6c6f"') == b"hello"
assert to_json(b"hello", bytes_mode="hex") == b'"68656c6c6f"'
for validator, value, message in (
    (base64_validator, '"wrong!"', "Invalid symbol 33, offset 5."),
    (hex_validator, '"a"', "Odd number of digits"),
    (hex_validator, '"ag"', "Invalid character 'g' at position 1"),
):
    try:
        validator.validate_json(value)
    except ValidationError as error:
        assert message in error.errors(include_url=False)[0]["msg"]
    else:
        raise AssertionError("invalid encoded bytes were accepted")
print("bytes", "ok")
partial = '["aa", "bb", "c'
for source in (partial, partial.encode()):
    try:
        from_json(source)
    except ValueError as error:
        assert str(error) == "EOF while parsing a string at line 1 column 15"
    else:
        raise AssertionError("incomplete JSON was accepted")
    assert from_json(source, allow_partial=True) == ["aa", "bb"]
    assert from_json(source, allow_partial="trailing-strings") == [
        "aa", "bb", "c"
    ]
print("partial", "ok")


app = FastAPI()


@app.post("/serialize")
def serialize():
    return {
        "set": to_jsonable_python({1, 2}),
        "bytes": to_jsonable_python([b"x"]),
        "fallback": to_jsonable_python(Unknown(), fallback=fallback),
        "key": to_jsonable_python({Unknown(): 1}, fallback=fallback),
        "hex": hex_validator.validate_json('"68656c6c6f"').decode(),
        "repr": to_jsonable_python(BrokenRepr(), serialize_unknown=True),
        "partial": from_json(partial, allow_partial=True),
    }


with TestClient(app) as client:
    response = client.post("/serialize")
    assert response.status_code == 200, response.text
    assert response.json() == {
        "set": [1, 2],
        "bytes": ["x"],
        "fallback": "fallback:Unknown",
        "key": {"fallback:Unknown": 1},
        "hex": "hello",
        "repr": "<Unserializable BrokenRepr object>",
        "partial": ["aa", "bb"],
    }
    print("http", response.status_code, response.json())
