import re


pattern = re.compile(r"(?<![\\\w])x")
for text in ("x", " x", "ax", r"\x", "-x", "éx"):
    matches = [(match.start(), match.group()) for match in pattern.finditer(text)]
    print(repr(text), matches)
