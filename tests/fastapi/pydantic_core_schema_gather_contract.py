"""Focused xlang3 contract for the Pydantic Core 2.50 schema gather API."""

import importlib
import os
import sys
import types


module_root = next(
    path
    for path in sys.path
    if os.path.isfile(os.path.join(path, "pydantic_core", "_schema_gather.py"))
)
package = types.ModuleType("pydantic_core")
package.__path__ = [os.path.join(module_root, "pydantic_core")]
sys.modules["pydantic_core"] = package
native = importlib.import_module("pydantic_core._pydantic_core")
assert native.__version__ == "2.50.1"
_schema_gather = native._schema_gather


definitions = {"shared": {"type": "str"}}
single = {"type": "definition-ref", "schema_ref": "shared"}
collected, deferred = _schema_gather.gather_schemas_for_cleaning(single, definitions)
assert collected == {"shared": single}
assert deferred == []

first = {"type": "definition-ref", "schema_ref": "shared"}
second = {"type": "definition-ref", "schema_ref": "shared"}
root = {"type": "tuple", "items_schema": [first, second]}
collected, _ = _schema_gather.gather_schemas_for_cleaning(root, definitions)
assert collected == {"shared": None}

shared_ref = {"type": "definition-ref", "schema_ref": "shared"}
shared_container = {"type": "list", "items_schema": shared_ref}
root = {"type": "tuple", "items_schema": [shared_container, shared_container]}
collected, _ = _schema_gather.gather_schemas_for_cleaning(root, definitions)
assert collected == {"shared": None}

recursive = {"type": "definition-ref", "schema_ref": "recursive"}
definitions["recursive"] = {"type": "list", "items_schema": recursive}
collected, _ = _schema_gather.gather_schemas_for_cleaning(recursive, definitions)
assert collected == {"recursive": None}

deferred_schema = {
    "type": "str",
    "metadata": {"pydantic_internal_union_discriminator": "kind"},
}
serialized_ref = {"type": "definition-ref", "schema_ref": "shared"}
root = {"type": "nullable", "schema": deferred_schema, "serialization": serialized_ref}
collected, deferred = _schema_gather.gather_schemas_for_cleaning(root, definitions)
assert collected == {"shared": serialized_ref}
assert deferred == [deferred_schema]

try:
    _schema_gather.gather_schemas_for_cleaning(
        {"type": "definition-ref", "schema_ref": "absent"}, definitions
    )
except _schema_gather.MissingDefinitionError as error:
    assert error.schema_reference == "absent"
else:
    raise AssertionError("a missing definition reference was accepted")

deep = {"type": "str"}
for _ in range(5000):
    deep = {"type": "nullable", "schema": deep}
collected, deferred = _schema_gather.gather_schemas_for_cleaning(deep, {})
assert collected == {} and deferred == []

print("pydantic-core schema gather passed")
