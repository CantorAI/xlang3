import builtins


def call_print():
    print("patched")


original = print
events = []
builtins.print = lambda *args, **kwargs: events.append((args, kwargs))
try:
    call_print()
finally:
    builtins.print = original

print(events)
