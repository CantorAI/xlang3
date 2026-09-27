import gc
import weakref


class Token:
    pass


def verify():
    token = Token()
    reference = weakref.ref(token)
    result = [item for item in [token]]
    del result, token
    gc.collect()
    print('comprehension target released:', reference() is None)


verify()
functions = [lambda: item for item in [1, 2]]
print('comprehension closure values:', [function() for function in functions])
