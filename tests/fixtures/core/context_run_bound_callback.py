import contextvars

state = contextvars.ContextVar("callback-state", default="unset")
state.set("caller")
context = contextvars.copy_context()


class Receiver:
    def read(self, label, number):
        return f"{label}:{number}:{state.get()}"

    def fail(self):
        state.set("inside")
        raise LookupError("boom")


receiver = Receiver()
print(context.run(receiver.read, "item", 9))
print(state.get())
try:
    context.run(receiver.fail)
except LookupError as exc:
    print("caught:", exc)
print(context.run(receiver.read, "after", 3))
print(state.get())
