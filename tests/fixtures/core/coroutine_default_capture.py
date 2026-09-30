async def target(value=11):
    return value


pending = target()
target.__defaults__ = (22,)
try:
    pending.send(None)
except StopIteration as completed:
    print("captured default", completed.value)
