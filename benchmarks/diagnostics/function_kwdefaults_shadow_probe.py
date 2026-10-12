"""Check data-attribute precedence when a decorator copies function __dict__."""


def wrapper(*, target=1):
    return target


live_defaults = wrapper.__kwdefaults__
wrapper.__dict__.update({"__kwdefaults__": {"target": 99}})
print("metadata beats dictionary shadow:", wrapper.__kwdefaults__ is live_defaults)
wrapper.__kwdefaults__["target"] = 2
print("mutation reaches actual binding:", wrapper() == 2)
