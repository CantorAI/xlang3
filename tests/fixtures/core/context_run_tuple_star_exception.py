import contextvars


state = contextvars.ContextVar("tuple-star-state", default="unset")
state.set("outside")
context = contextvars.copy_context()


def callback(*args):
    if args == ("raise",):
        state.set("inside")
        raise LookupError("tuple star failed")
    return f"{args}:{state.get()}"


print(context.run(callback, *("tuple",)))
print(state.get())
try:
    context.run(callback, *("raise",))
except LookupError as exc:
    print("caught:", exc)
print(context.run(callback, *("after",)))
print(state.get())
