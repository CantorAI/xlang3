import gc
import weakref


class Token:
    def __init__(self, name):
        self.name = name


def check_yield_ownership(kind):
    key = Token("key")
    value = Token("value")
    key_ref = weakref.ref(key)
    value_ref = weakref.ref(value)
    source = {key: value}
    iterator = iter(getattr(source, kind)())
    del key, value, source
    gc.collect()
    assert key_ref() is not None and value_ref() is not None
    yielded = next(iterator)
    if kind == "keys":
        assert yielded.name == "key"
    elif kind == "values":
        assert yielded.name == "value"
    else:
        assert yielded[0].name == "key" and yielded[1].name == "value"

    # Exhaustion releases the source, while the yielded object(s) stay owned.
    assert next(iterator, None) is None
    gc.collect()
    if kind == "keys":
        assert key_ref() is yielded and value_ref() is None
    elif kind == "values":
        assert key_ref() is None and value_ref() is yielded
    else:
        assert key_ref() is yielded[0] and value_ref() is yielded[1]
    del yielded, iterator
    gc.collect()
    assert key_ref() is None and value_ref() is None


def check_live_values(kind):
    first, old, replacement = Token("first"), Token("old"), Token("new")
    source = {"first": first, "second": old}
    iterator = iter(getattr(source, kind)())
    yielded = next(iterator)
    source["second"] = replacement
    updated = next(iterator)
    if kind == "keys":
        assert yielded == "first" and updated == "second"
    elif kind == "values":
        assert yielded is first and updated is replacement
    else:
        assert yielded == ("first", first)
        assert updated == ("second", replacement)
        source["first"] = replacement
        assert yielded[1] is first
    assert next(iterator, None) is None


def check_source_overwrite():
    source = {"first": Token("first"), "second": Token("second")}
    keys = []
    # The loop target replaces the last external dictionary reference.
    for source in source:
        keys.append(source)
    assert keys == ["first", "second"]
    source = {"first": Token("first"), "second": Token("second")}
    names = []
    for source in source.values():
        names.append(source.name)
    assert names == ["first", "second"]
    source = {"first": Token("first"), "second": Token("second")}
    pairs = []
    for source in source.items():
        pairs.append((source[0], source[1].name))
    assert pairs == [("first", "first"), ("second", "second")]


class DictSubclass(dict):
    pass


for kind in ("keys", "values", "items"):
    check_yield_ownership(kind)
    check_live_values(kind)
    assert next(iter(getattr({}, kind)()), None) is None
    source = DictSubclass({"first": Token("first"), "second": Token("second")})
    yielded = list(getattr(source, kind)())
    if kind == "keys":
        assert yielded == ["first", "second"]
    elif kind == "values":
        assert [value.name for value in yielded] == ["first", "second"]
    else:
        assert [(key, value.name) for key, value in yielded] == [
            ("first", "first"), ("second", "second")]
check_source_overwrite()
print("dict iterator ownership and live values ok")
