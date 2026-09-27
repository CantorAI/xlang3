import inspect


for kind in (str, float, int, bool, list, dict):
    try:
        signature = str(inspect.signature(kind))
    except (TypeError, ValueError):
        signature = 'unavailable'
    print(kind.__name__, signature, repr(getattr(kind, '__text_signature__', None)))
