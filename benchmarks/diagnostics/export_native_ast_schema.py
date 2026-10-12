"""Export native CPython 3.14.7 AST metadata, without Python ast.py algorithms."""
import _ast
import argparse
import json
import pathlib
import sys
import types


def describe_type(value):
    if isinstance(value, types.UnionType):
        return "|".join(describe_type(item) for item in value.__args__)
    if isinstance(value, types.GenericAlias):
        return value.__origin__.__name__ + "[" + ",".join(
            describe_type(item) for item in value.__args__) + "]"
    return value.__name__


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--reference", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if sys.version_info[:3] != (3, 14, 7):
        parser.error("use CPython 3.14.7")
    classes = []
    for name in sorted(dir(_ast)):
        cls = getattr(_ast, name)
        if not isinstance(cls, type) or not issubclass(cls, _ast.AST) or name != cls.__name__:
            continue
        field_types = getattr(cls, "_field_types", None)
        classes.append(dict(name=name, base=cls.__bases__[0].__name__,
                            fields=list(cls._fields), has_field_types=field_types is not None,
                            field_types={key: describe_type(value) for key, value in (field_types or {}).items()}))
    lines = ["// Native _ast API metadata exported by export_native_ast_schema.py on CPython 3.14.7.",
             "// No Python ast.py visitor, unparser, or library algorithm is translated here.",
             "static constexpr NativeAstFieldSchema kNativeAstFieldSchemas[] = {"]
    for cls in classes:
        schema = ";".join(key + "=" + value for key, value in cls["field_types"].items())
        schema_literal = json.dumps(schema) if cls["has_field_types"] else "nullptr"
        lines.append("    {" + json.dumps(cls["name"]) + ", " + schema_literal + "},")
    lines.append("};")
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    args.reference.write_text(json.dumps(dict(version=sys.version,
        scope="Only native _ast exports; excludes Python ast.py aliases and algorithms", classes=classes),
        indent=2) + "\n", encoding="utf-8", newline="\n")
    print("Exported", len(classes), "native AST class schemas")


if __name__ == "__main__":
    main()
