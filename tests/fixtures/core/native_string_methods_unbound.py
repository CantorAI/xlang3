from functools import partial


class Token(str):
    def __new__(cls, text):
        return str.__new__(cls, text)

    def replace(self, old, new, count=-1):
        # Chameleon's Python Token uses this exact native descriptor call.
        return str.replace(self, old, new, count)


token = Token("abc abc")
for repeat in range(3):
    assert token.replace("abc", "x") == "x x"
    assert token.replace("abc", "x", 1) == "x abc"
    assert str.replace(token, "abc", "x", 0) == "abc abc"
    assert str.replace(token, "abc", "x", 1) == "x abc"
    assert str.startswith(token, "abc")
    assert str.startswith(token, ("z", "abc"), 4, 7)
    assert str.endswith(token, "abc")
    assert str.endswith(token, ("z", "abc"), 0, 3)
    assert str.find(token, "abc") == 0
    assert str.find(token, "abc", 1) == 4
    assert str.find(token, "abc", 1, 7) == 4
    assert str.count(token, "abc") == 2
    assert str.count(token, "abc", 1) == 1
    assert str.count(token, "abc", 1, 7) == 1
    assert str.join(Token("|"), ["a", "b"]) == "a|b"
    assert str.join("|", ("a", "b")) == "a|b"
    # Ordinary bound calls must keep their existing fast path and semantics.
    assert "abc abc".replace("abc", "x", 1) == "x abc"
    assert "abc".startswith("a") and "abc".endswith("c")
    assert "abc abc".find("abc", 1) == 4
    assert "abc abc".count("abc") == 2
    assert "|".join(["a", "b"]) == "a|b"

replace = str.replace
assert replace(*(token, "abc", "x", 1)) == "x abc"
assert getattr(str, "replace")(token, "abc", "x") == "x x"
assert partial(str.replace, token)("abc", "x", 1) == "x abc"

for method, arguments, expected in (
    (str.upper, ("abc",), "ABC"),
    (str.lower, ("ABC",), "abc"),
    (str.strip, (" abc ",), "abc"),
    (str.lstrip, (" abc ",), "abc "),
    (str.rstrip, (" abc ",), " abc"),
    (str.split, ("a b",), ["a", "b"]),
):
    assert method(*arguments) == expected

# Chameleon's Token.strip forwards its None default to unbound native methods.
class TrimToken(str):
    def lstrip(self, chars=None):
        return str.lstrip(self, chars)

    def rstrip(self, chars=None):
        return str.rstrip(self, chars)

    def strip(self, chars=None):
        return self.lstrip(chars).rstrip(chars)


assert TrimToken(" \t abc \n").strip() == "abc"
for text in (" \t abc \n", "\u2003abc\u2003", "", "abc"):
    for method_name in ("strip", "lstrip", "rstrip"):
        method = getattr(str, method_name)
        expected = method(text)
        assert method(text, None) == expected
        assert getattr(text, method_name)(None) == expected
        assert method(TrimToken(text), None) == expected
        assert getattr(TrimToken(text), method_name)(None) == expected
        try:
            method(text, 42)
        except TypeError:
            pass
        else:
            assert False, "invalid strip chars were accepted"
assert str.strip("aaa", "") == "aaa"
assert str.strip("aabba", "a") == "bb"

for method in (str.startswith, str.endswith, str.find, str.count, str.replace, str.join):
    for arguments in ((), ("abc",), ("abc",) * 6):
        try:
            method(*arguments)
        except TypeError:
            pass
        else:
            assert False, "invalid native method arity was accepted"

for call in (
    lambda: str.startswith(42, "a"),
    lambda: str.endswith(42, "a"),
    lambda: str.find(42, "a"),
    lambda: str.count(42, "a"),
    lambda: str.replace(42, "a", "b"),
    lambda: str.join(42, ["a"]),
):
    try:
        call()
    except TypeError:
        pass
    else:
        assert False, "invalid native string receiver was accepted"


class Broken:
    def __iter__(self):
        return self

    def __next__(self):
        raise LookupError("join iterator failed")


try:
    str.join("|", Broken())
except LookupError as error:
    assert str(error) == "join iterator failed"
else:
    assert False, "native join lost its iterator exception"
print("native string bound and unbound methods ok")
