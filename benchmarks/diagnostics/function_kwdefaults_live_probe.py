"""Small correctness probe for the lazy decorators used by NetworkX.

This is not a timing benchmark. A mutable __kwdefaults__ dictionary must feed
the next call without replacing the dictionary through attribute assignment.
"""


def wrapper(*args, target=None, **kwargs):
    return target


defaults = wrapper.__kwdefaults__
print("stable dictionary:", defaults is wrapper.__kwdefaults__)
defaults["target"] = wrapper
print("mutation reaches call:", wrapper() is wrapper)
defaults.clear()
try:
    wrapper()
except TypeError:
    print("removing default requires argument:", True)
else:
    print("removing default requires argument:", False)

replacement = {"target": "updated"}
wrapper.__kwdefaults__ = replacement
print("assignment preserves dictionary:", wrapper.__kwdefaults__ is replacement)
replacement["target"] = "changed again"
print("external mutation reaches call:", wrapper() == "changed again")
