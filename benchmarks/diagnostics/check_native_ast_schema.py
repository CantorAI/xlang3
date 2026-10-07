"""Compare native _ast metadata against an exported CPython schema."""
import _ast
import json
import sys
import types


def describe_type(value):
    if isinstance(value, types.UnionType):
        return "|".join(describe_type(item) for item in value.__args__)
    if isinstance(value, types.GenericAlias):
        return value.__origin__.__name__ + "[" + ",".join(
            describe_type(item) for item in value.__args__) + "]"
    return value.__name__


with open(sys.argv[1], encoding="utf-8") as stream:
    reference = json.load(stream)
for expected in reference["classes"]:
    cls = getattr(_ast, expected["name"])
    assert list(cls._fields) == expected["fields"], (expected["name"], "fields")
    assert cls.__bases__[0].__name__ == expected["base"], (expected["name"], "base")
    observed = getattr(cls, "_field_types", None)
    assert (observed is not None) == expected["has_field_types"], expected["name"]
    if observed is not None:
        assert {key: describe_type(value) for key, value in observed.items()} == expected["field_types"], expected["name"]
        assert cls.__annotations__ is observed, (expected["name"], "annotations")
        for key, value in observed.items():
            if isinstance(value, types.UnionType):
                assert getattr(cls, key) is None, (expected["name"], key)
print("native AST class schemas match", len(reference["classes"]))
