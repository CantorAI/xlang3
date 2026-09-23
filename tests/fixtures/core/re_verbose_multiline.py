import re


color = re.compile(
    r"""^
\#([0-9a-f]{6})$|
color\(([0-9]{1,3})\)$|
rgb\(([\d\s,]+)\)$
""",
    re.VERBOSE,
)

for value in ("#ff0080", "color(42)", "rgb(249,38,114)", "nope"):
    match = color.match(value)
    print(value, match.groups() if match else None)

commented = re.compile(
    r"""^
    foo       # the prefix
    [ ]bar$   # an intentional space in a class
    """,
    re.VERBOSE,
)
print(bool(commented.match("foo bar")))
print(bool(commented.match("foobar")))
