true_first = True
float_first = float("1.0")
true_literal = {true_first: "first", 1: "last"}
float_literal = {float_first: "first", 1: "last"}
for result, first, key_type in (
    (true_literal, true_first, bool),
    (float_literal, float_first, float),
):
    assert len(result) == 1 and list(result.values()) == ["last"]
    actual = next(iter(result))
    assert actual is first and type(actual) is key_type
    assert result[True] == result[1] == result[1.0] == "last"
print("PASS equivalent-numeric-literal-first-key")


for first, key_type in ((true_first, bool), (float_first, float)):
    pairs = [(first, "first"), (1, "last")]
    result = {key: value for key, value in pairs}
    assert len(result) == 1 and list(result.values()) == ["last"]
    actual = next(iter(result))
    assert actual is first and type(actual) is key_type
    assert result[True] == result[1] == result[1.0] == "last"
print("PASS equivalent-numeric-comprehension-first-key")


