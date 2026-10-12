"""Proposed exception-preservation checks, unrun by the author."""
import sys

class HashError(Exception):
    pass

cause = ValueError('cause marker')
failure = HashError('preserved')
calls = []

class FailingHash:
    def __hash__(self):
        calls.append('hash')
        raise failure from cause

key = FailingHash()
saved_hash = hash

def plain_call():
    return hash(key)

def expanded_call():
    return hash(*[key])

def saved_call():
    return saved_hash(key)

def tuple_call():
    return hash((key, 'SELECT'))

for callback in (plain_call, expanded_call, saved_call, tuple_call):
    for _ in range(7):
        before = len(calls)
        try:
            callback()
        except HashError as caught:
            assert caught is failure
            assert caught.__cause__ is cause
            assert caught.__suppress_context__
            assert sys.exception() is caught
            traceback = caught.__traceback__
            found_hash = False
            while traceback is not None:
                if traceback.tb_frame.f_code.co_name == '__hash__':
                    found_hash = True
                traceback = traceback.tb_next
            assert found_hash
        else:
            raise AssertionError('hash did not preserve its Python exception')
        assert len(calls) == before + 1
        assert sys.exception() is None
        assert isinstance(hash('SELECT'), int)
print('hash exception identity cause traceback and active context preserved')

class Interrupt(BaseException):
    pass

interrupt = Interrupt('interrupt marker')

class InterruptingHash:
    def __hash__(self):
        raise interrupt

try:
    hash(InterruptingHash())
except Interrupt as caught:
    assert caught is interrupt
else:
    raise AssertionError('BaseException was replaced')
print('hash preserves BaseException callbacks')

class InvalidHash:
    def __hash__(self):
        return 'not an integer'

for invalid in ([], InvalidHash()):
    try:
        hash(invalid)
    except TypeError:
        pass
    else:
        raise AssertionError('invalid hash lost TypeError fallback')

released = memoryview(b'abc')
released.release()
try:
    hash(released)
except ValueError:
    pass
else:
    raise AssertionError('released memoryview lost ValueError fallback')
print('hash keeps TypeError and ValueError fallbacks')
