class SuspendOnce:
    def __init__(self, value):
        self.value = value

    def __await__(self):
        yield "pause"
        return self.value


async def evaluate():
    compared = await SuspendOnce(b"a") == b"a"
    combined = await SuspendOnce("left") + "-right"
    selected = (await SuspendOnce(3)) if True else 0
    return compared, combined, selected


coroutine = evaluate()
print(coroutine.send(None))
print(coroutine.send(None))
print(coroutine.send(None))
try:
    coroutine.send(None)
except StopIteration as exc:
    print(exc.value)
