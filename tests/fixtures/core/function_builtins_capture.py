import builtins

def ordinary():
    return len(())

def documented():
    """Created from code-owned doc text."""
    return None

print(type(ordinary.__builtins__).__name__)
print(ordinary.__builtins__ is builtins.__dict__)
print(documented.__doc__)
documented.__doc__ = "updated doc"
print(documented.__doc__)
print(ordinary.__dict__)
ordinary.__dict__["__builtins__"] = "shadow"
print(ordinary.__builtins__ is builtins.__dict__)

try:
    ordinary.__builtins__ = {}
except AttributeError:
    print("set readonly")

try:
    del ordinary.__builtins__
except AttributeError:
    print("delete readonly")

custom_builtins = {"answer": 42}
custom_globals = {"__name__": "custom", "__builtins__": custom_builtins}
exec("def custom_function(): return answer", custom_globals)
custom_function = custom_globals["custom_function"]
print(custom_function.__builtins__ is custom_builtins)

show = print
before_rebind = ordinary
replacement_builtins = {"print": show, "builtins": builtins, "answer": 42}
__builtins__ = replacement_builtins

def after_rebind():
    return answer

show(before_rebind.__builtins__ is builtins.__dict__)
show(after_rebind.__builtins__ is replacement_builtins)
