import re


characters = ":!%&,/;=@_`~-"
for character in characters:
    pattern = re.compile("\\" + character)
    match = pattern.fullmatch(character)
    print(character, match is not None)

windows_glob = re.compile(
    r"D\:[/\\]CantorAI[/\\]xlang3[/\\]scratch[/\\]upstream-compat"
    r"[/\\]fastapi[/\\]tests[/\\]benchmarks[/\\].*\Z",
    re.IGNORECASE,
)
print(windows_glob.match(r"D:\CantorAI\xlang3\scratch\upstream-compat\fastapi\fastapi\__main__.py"))
print(bool(windows_glob.match(r"D:\CantorAI\xlang3\scratch\upstream-compat\fastapi\tests\benchmarks\case.py")))
print(bool(re.fullmatch(r"[a-z]+", "ABC", re.IGNORECASE)))
print(bool(re.fullmatch(r"(a)\1", "aA", re.IGNORECASE)))

absolute_end = re.search(r"(?ms).*?x\s*\z(.*)", "xx\nx\n")
print(absolute_end.span(), absolute_end.span(1), repr(absolute_end.group(1)))
