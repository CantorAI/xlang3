import re


def remove(match):
    return None


print(re.sub("[aeiou]", remove, "beautiful"))
print(re.sub(b"[0-9]", remove, b"a1b2c3"))
print(re.subn("x", remove, "x-x-x", count=2))
