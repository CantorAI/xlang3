import re


specifier = re.compile(r"(?<!==|!=|~=)(?:<=|>=|<|>)\s*[0-9]+(?:\.[0-9]+)*")
print(bool(specifier.search("setuptools>=43")))
print(bool(specifier.search("setuptools==>=43")))
print(bool(specifier.search("setuptools~= >=43")))

# setuptools' vendored packaging uses these nested branches: the identity
# branch can consume a version first, but only when its preceding operator is
# exactly ===. A rejected deferred assertion must let the next version branch
# run and must preserve captures in the later branches.
packaging_specifier = re.compile(
    r"(?P<operator>(~=|==|!=|<=|>=|<|>|===))"
    r"(?P<version>(?:"
    r"(?<====)\s*[^\s;)]*"
    r"|(?<===|!=)\s*[0-9]+(?:\.[0-9]+)*(?:\.\*)?"
    r"|(?<=~=)\s*[0-9]+(?:\.[0-9]+)+"
    r"|(?<!==|!=|~=)\s*[0-9]+(?:\.[0-9]+)*"
    r"))",
    re.X,
)
for source in (">=43", "==43.*", "~=3.6", "===vendor", "<43"):
    match = packaging_specifier.match(source)
    print(match.group("operator"), match.group("version") if match else None)
