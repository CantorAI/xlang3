values = [
    '🐈 Hello \ud800World',
    '\udfff',
    'emoji 🐍',
    '\x00\x1f\x7f',
    "single'quote",
    'double"quote',
]
for value in values:
    print(repr(value))
