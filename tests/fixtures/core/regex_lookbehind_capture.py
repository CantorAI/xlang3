import re


patterns = [
    (r"(?P<number>(?<!\w)\-?[0-9]+\.?[0-9]*(e[-+]?\d+?)?\b|0x[0-9a-fA-F]*)", "<3>"),
    (r"(?P<word>(?<!\w)\w+)|(?P<literal>abc)", " abc"),
    (r"(?P<word>(?<!\w)\w+)|(?P<literal>abc)", "!abc"),
    (r"(?P<marked>(?<=#)abc)|(?P<plain>abc)", "abc"),
]
for pattern, text in patterns:
    match = re.search(pattern, text)
    print(match.group(0), match.span(), match.groupdict())

print(
    [match.groupdict() for match in re.finditer(
        r"(?P<marked>(?<=#)abc)|(?P<plain>abc)", "abc abc")]
)
