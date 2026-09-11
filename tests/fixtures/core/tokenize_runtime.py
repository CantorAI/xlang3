import tokenize


source = (
    "name = 'Å'\n"
    "value = f'{item:{width}.{precision}}'\n"
    "continued = 1 + \\\n"
    "    2\n"
    "multiline = f'''{\n"
    "    2  # two\n"
    "}'''\n"
).encode("utf-8")

lines = source.splitlines(keepends=True)
assert len(lines) == 7
tokens = list(tokenize.tokenize(iter(lines + [b""]).__next__))
pairs = [(token.type, token.string) for token in tokens]

assert (tokenize.STRING, "'Å'") in pairs
assert (tokenize.COMMENT, "# two") in pairs
assert (tokenize.FSTRING_START, "f'") in pairs
assert (tokenize.FSTRING_MIDDLE, ".") in pairs
assert pairs.count((tokenize.OP, "{")) == 4
assert pairs.count((tokenize.OP, "}")) == 4

untokenized = tokenize.untokenize([token[:2] for token in tokens])
roundtrip = [token[:2] for token in tokenize.tokenize(iter(untokenized.splitlines(keepends=True) + [b""]).__next__)]
assert roundtrip == [token[:2] for token in tokens]

print("tokenize-runtime", len(tokens), untokenized.startswith(b"name"))
