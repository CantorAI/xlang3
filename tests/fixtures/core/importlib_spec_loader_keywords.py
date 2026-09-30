import importlib.util


class Loader:
    pass


def describe(spec):
    return (spec.name, spec.origin, spec.has_location, spec.cached,
            spec.parent, spec.submodule_search_locations)


loader = Loader()
print(describe(importlib.util.spec_from_loader('sample.child', loader)))
print(describe(importlib.util.spec_from_loader(
    'sample.child', loader, origin='generated', is_package=True)))
for call in (
    lambda: importlib.util.spec_from_loader('sample.child', loader, 'extra'),
    lambda: importlib.util.spec_from_loader('sample.child', loader, extra=True),
):
    try:
        call()
    except TypeError as exc:
        print(type(exc).__name__)
