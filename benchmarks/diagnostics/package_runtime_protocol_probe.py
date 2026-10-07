"""Small generic protocol checks extracted from full-suite failure triggers.

No package implementation is replaced. These checks are not timing results.
Each case reports its own error so one failure does not hide later checks.
"""

import json


def string_subclass_repr():
    class Token(str):
        __slots__ = ("position",)

        def __new__(cls, text):
            value = str.__new__(cls, text)
            value.position = 0
            return value

    attributes = {Token("class"): Token("table")}
    rendered = repr(attributes)
    assert eval(rendered) == {"class": "table"}, rendered
    return rendered


def class_mapping_loop():
    class Struct:
        pass

    class Patterns:
        enum = Struct()
        enum.formats = {"parens": "()", "period": "."}.keys()
        pats = {}
        pats["earlier"] = "present"
        for format in enum.formats:
            pats[format] = format.upper()
        rendered = "%(parens)s|%(period)s" % pats

    assert Patterns.rendered == "PARENS|PERIOD", repr(Patterns.pats)
    return Patterns.rendered


def unbound_append():
    class Items(list):
        def append(self, item):
            list.append(self, item)

    items = Items()
    for value in range(5):
        items.append(value)
    assert items == list(range(5))
    return repr(items)


def native_array_slice():
    from array import array

    original = array("d", [1.25, 2.5, 3.75])
    copied = array("d")
    copied[:] = original[:]
    assert copied.tolist() == [1.25, 2.5, 3.75]
    copied[1:] = array("d", [4.5])
    assert copied.tolist() == [1.25, 4.5]
    assert original.tolist() == [1.25, 2.5, 3.75]
    return repr(copied.tolist())


def attribute_named_of():
    class Object:
        def equal(self, other):
            return self.of is other.of

    first = Object()
    second = Object()
    first.of = second.of = object()
    assert first.equal(second)
    return "valid identifier and comparison"


records = []
for name, check in (
    ("string_subclass_repr", string_subclass_repr),
    ("class_mapping_loop", class_mapping_loop),
    ("unbound_append", unbound_append),
    ("native_array_slice", native_array_slice),
    ("attribute_named_of", attribute_named_of),
):
    try:
        detail = check()
        records.append({"check": name, "ok": True, "detail": detail})
    except Exception as error:
        records.append({"check": name, "ok": False,
                        "error": type(error).__name__, "detail": str(error)})
print(json.dumps(records, indent=2))
