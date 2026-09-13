"""The leading module string is metadata, while nested strings are statements."""

events = []

try:
    events.append("try")
except Exception:
    events.append("except")

if True:
    events.append("if")

def nested_suite():
    """Function metadata."""
    try:
        return "function-try"
    except Exception:
        return "function-except"

print(__doc__.startswith("The leading module string"))
print(events)
print(nested_suite.__doc__, nested_suite())
