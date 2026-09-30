closed = []


def inner():
    try:
        try:
            sent = yield "ready"
        except ValueError:
            yield "recovered"
            return "after-throw"
        return sent
    finally:
        closed.append("inner")


def outer():
    result = yield from inner()
    return result


for mode in ("send", "throw", "close"):
    iterator = outer()
    print(mode, "start", next(iterator))
    try:
        if mode == "send":
            print(mode, "return", iterator.send("reply"))
        elif mode == "throw":
            print(mode, "yield", iterator.throw(ValueError("boom")))
            print(mode, "return", iterator.send(None))
        else:
            print(mode, "return", iterator.close())
    except StopIteration as exc:
        print(mode, "return", exc.value)
    print(mode, "closed", len(closed))
