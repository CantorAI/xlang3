import contextvars


value = contextvars.ContextVar("value", default="outer")
context = contextvars.copy_context()


def reenter():
    try:
        context.run(lambda: None)
    except RuntimeError:
        print("same context re-entry rejected")
    else:
        print("same context re-entry allowed")


context.run(reenter)
print(context.run(value.get))
