import ast


class Token(str):
    __slots__ = ("position",)

    def __new__(cls, text):
        value = str.__new__(cls, text)
        value.position = 0
        return value


for text in ("table", "a'b", 'a"b', "line\nend", "\u03bb", "\ud800"):
    token = Token(text)
    assert repr(token) == repr(text)
    assert str.__repr__(token) == repr(text)

# The AST constant decoder's escaped-surrogate bug is recorded separately.
# Check generated-literal roundtrips here for the supported text cases while
# retaining the native repr comparison above for surrogates too.
for text in ("table", "a'b", 'a"b', "line\nend", "\u03bb"):
    token = Token(text)
    assert ast.literal_eval(repr(token)) == text
    payload = {Token("class"): token}
    assert ast.literal_eval(repr(payload)) == {"class": text}
    assert compile(repr(payload), "<token repr>", "eval", ast.PyCF_ONLY_AST)


class Custom(Token):
    def __repr__(self):
        return "custom token repr"


custom = Custom("value")
assert repr(custom) == "custom token repr"
assert str.__repr__(custom) == repr("value")
assert repr([custom]) == "[custom token repr]"
try:
    str.__repr__(42)
except TypeError:
    pass
else:
    assert False, "native str repr accepted a non-string receiver"
print("native str subclass repr and generated literals ok")
