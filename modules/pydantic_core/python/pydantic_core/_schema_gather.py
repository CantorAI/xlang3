"""Iterative schema-reference traversal for Pydantic Core's Python facade.

The validation and serialization engines are provided by xlang3's native
``_pydantic_core`` package. This helper only walks Python core-schema objects;
an explicit stack keeps deeply nested schemas safe from Python recursion limits.
"""

from __future__ import annotations

import sys
from typing import Any


class MissingDefinitionError(LookupError):
    """A definition reference points to a schema absent from ``definitions``."""

    def __init__(self, schema_reference: str, /) -> None:
        self.schema_reference = schema_reference
        super().__init__(schema_reference)


MissingDefinitionError.__module__ = "pydantic_core._pydantic_core._schema_gather"


def _optional(schema: dict[str, Any], key: str) -> list[Any]:
    value = schema.get(key)
    return [] if value is None else [value]


def _required(schema: dict[str, Any], key: str) -> Any:
    return schema[key]


def _children(schema: dict[str, Any], schema_type: str) -> list[Any]:
    result: list[Any] = []
    if schema_type == "definitions":
        result.extend((_required(schema, "schema"), *_required(schema, "definitions")))
    elif schema_type in {"list", "deque", "set", "frozenset", "generator"}:
        result.extend(_optional(schema, "items_schema"))
    elif schema_type == "tuple":
        items = schema.get("items_schema")
        if items is not None:
            result.extend(items)
    elif schema_type in {"dict", "frozendict", "ordered-dict", "counter"}:
        result.extend(_optional(schema, "keys_schema"))
        result.extend(_optional(schema, "values_schema"))
    elif schema_type == "union":
        for choice in _required(schema, "choices"):
            result.append(choice[0] if isinstance(choice, tuple) else choice)
    elif schema_type == "tagged-union":
        result.extend(_required(schema, "choices").values())
    elif schema_type == "chain":
        result.extend(_required(schema, "steps"))
    elif schema_type == "lax-or-strict":
        result.extend((_required(schema, "lax_schema"), _required(schema, "strict_schema")))
    elif schema_type == "json-or-python":
        result.extend((_required(schema, "json_schema"), _required(schema, "python_schema")))
    elif schema_type in {"model-fields", "typed-dict"}:
        result.extend(_optional(schema, "extras_schema"))
        computed_fields = schema.get("computed_fields")
        if computed_fields is not None:
            result.extend(computed_fields)
        result.extend(_required(schema, "fields").values())
    elif schema_type == "dataclass-args":
        computed_fields = schema.get("computed_fields")
        if computed_fields is not None:
            result.extend(computed_fields)
        result.extend(_required(schema, "fields"))
    elif schema_type == "named-tuple":
        result.extend(_required(schema, "fields"))
    elif schema_type in {"arguments", "arguments-v3"}:
        for argument in _required(schema, "arguments_schema"):
            result.append(_required(argument, "schema"))
        if schema_type == "arguments":
            result.extend(_optional(schema, "var_args_schema"))
            result.extend(_optional(schema, "var_kwargs_schema"))
    elif schema_type == "call":
        result.extend((_required(schema, "arguments_schema"), *_optional(schema, "return_schema")))
    elif schema_type == "computed-field":
        result.extend(_optional(schema, "return_schema"))
    elif schema_type == "function-before":
        result.extend(_optional(schema, "schema"))
        result.extend(_optional(schema, "json_schema_input_schema"))
    elif schema_type == "function-plain":
        result.extend(_optional(schema, "return_schema"))
        result.extend(_optional(schema, "json_schema_input_schema"))
    elif schema_type == "function-wrap":
        result.extend(_optional(schema, "return_schema"))
        result.extend(_optional(schema, "schema"))
        result.extend(_optional(schema, "json_schema_input_schema"))
    else:
        result.extend(_optional(schema, "schema"))
    return result


def gather_schemas_for_cleaning(
    schema: dict[str, Any], definitions: dict[str, dict[str, Any]]
) -> tuple[dict[str, dict[str, Any] | None], list[dict[str, Any]]]:
    """Validate and collect definition references and deferred discriminators.

    A reference is inlineable only when encountered once across the complete
    schema graph. Shared schema objects are traversed once, while their saved
    reference span is counted again when the object is encountered elsewhere.
    """
    collected: dict[str, dict[str, Any] | None] = {}
    deferred: list[dict[str, Any]] = []
    encountered: list[str] = []
    # id -> (strong reference, start offset, end offset or None while visiting)
    visited: dict[int, tuple[dict[str, Any], int, int | None]] = {}
    steps: list[tuple[str, Any, bool]] = [("enter", schema, True)]

    while steps:
        action, current, traverse_metadata = steps.pop()
        if action == "leave":
            if traverse_metadata:
                metadata = current.get("metadata")
                if metadata is not None and "pydantic_internal_union_discriminator" in metadata:
                    deferred.append(current)
            _, start, _ = visited[id(current)]
            visited[id(current)] = (current, start, len(encountered))
            continue

        if not isinstance(current, dict):
            raise TypeError("core schema nodes must be dictionaries")
        identity = id(current)
        previous = visited.get(identity)
        if previous is not None:
            _, start, end = previous
            for reference in encountered[start : len(encountered) if end is None else end]:
                collected[reference] = None
            continue

        visited[identity] = (current, len(encountered), None)
        schema_type = _required(current, "type")
        if not isinstance(schema_type, str):
            raise TypeError("core schema type must be a string")

        traverse_metadata = True
        if schema_type == "definition-ref":
            reference = _required(current, "schema_ref")
            encountered.append(reference)
            if reference not in collected:
                try:
                    definition = definitions[reference]
                except KeyError:
                    raise MissingDefinitionError(reference) from None
                collected[reference] = current
                children = [definition, *_optional(current, "serialization")]
            else:
                collected[reference] = None
                children = []
                traverse_metadata = False
        else:
            children = _children(current, schema_type)
            children.extend(_optional(current, "serialization"))

        steps.append(("leave", current, traverse_metadata))
        for child in reversed(children):
            steps.append(("enter", child, True))

    return collected, deferred


__all__ = ["MissingDefinitionError", "gather_schemas_for_cleaning"]

_module = sys.modules.get(__name__)
if _module is not None:
    sys.modules.setdefault("pydantic_core._pydantic_core._schema_gather", _module)
