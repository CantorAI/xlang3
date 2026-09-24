print(int("ff", base=16))
print(int(b"101", base=2))
print(int("12", **{"base": 8}))

for call in (
    lambda: int(base=16),
    lambda: int(1, base=16),
    lambda: int("1", 2, base=16),
):
    try:
        call()
    except TypeError as exc:
        print(str(exc))
