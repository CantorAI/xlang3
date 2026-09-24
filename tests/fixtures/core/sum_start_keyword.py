print(sum([1, 2, 3], start=4))
print(sum([[1], [2]], start=[]))

for name, invoke in (
    ("iterable", lambda: sum(iterable=[1])),
    ("missing", lambda: sum(start=2)),
    ("duplicate", lambda: sum([1], 2, start=3)),
    ("unknown", lambda: sum([1], foo=2)),
):
    try:
        invoke()
    except Exception as error:
        print(name, type(error).__name__, str(error))
