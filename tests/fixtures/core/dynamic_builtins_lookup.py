import builtins


def call_print():
    print("patched")


def call_dict(value):
    return dict(value)


print(call_dict([("value", 0)]))
dict = lambda value: {"value": 17}
print(call_dict(None))
del dict
print(call_dict([("value", 0)]))

original_dict = builtins.dict
builtins.dict = lambda value: {"value": 23}
try:
    print(call_dict(None))
finally:
    builtins.dict = original_dict


original = print
events = []
call_print()
builtins.print = lambda *args, **kwargs: events.append((args, kwargs))
try:
    call_print()
finally:
    builtins.print = original

print(events)
