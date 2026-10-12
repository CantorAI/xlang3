"""Proposed correctness fixture; not run by the proposal author.

The candidate changes only the exact StringObject primitive branch. Numeric
payloads and all Python subclasses/callbacks retain the original dispatch.
No cross-runtime equality of salted hash integers is assumed.
"""
import builtins

original_hash = builtins.hash
texts = ('', 'SELECT', 'a\x00b', 'caf\u00e9\U0001f642', 'long value ' * 128)
for text in texts:
    same_text = ''.join([text[:len(text) // 2], text[len(text) // 2:]])
    expected = original_hash(text)
    assert original_hash(same_text) == expected
    assert all(original_hash(text) == expected for _ in range(19))
    assert {text: 17}[same_text] == 17
    assert len({text, same_text}) == 1
    assert original_hash((text,)) == original_hash((same_text,))
print('exact text variants pass')

class StringOverride(str):
    calls = 0
    def __hash__(self):
        StringOverride.calls += 1
        return 173

overridden = StringOverride('SELECT')
assert [original_hash(overridden) for _ in range(19)] == [173] * 19
assert StringOverride.calls == 19
print('str subclass callback preserved')

class PythonKey:
    calls = 0
    def __init__(self, text):
        self.text = text
    def __hash__(self):
        PythonKey.calls += 1
        return original_hash(self.text)

key = PythonKey('SELECT')
expected = original_hash(key.text)
assert [original_hash(key) for _ in range(19)] == [expected] * 19
assert PythonKey.calls == 19
mapping = {key: 23}
before_lookup = PythonKey.calls
assert mapping[key] == 23
assert PythonKey.calls > before_lookup
print('Python hash callbacks preserved')

class HashError(Exception):
    pass

class FailingHash:
    def __hash__(self):
        raise HashError('preserved')

try:
    original_hash(FailingHash())
except HashError as exception:
    assert str(exception) == 'preserved'
else:
    raise AssertionError('hash exception suppressed')

try:
    original_hash([])
except TypeError:
    pass
else:
    raise AssertionError('list became hashable')
print('hash failures preserved')

def through_global(value):
    return hash(value)

assert through_global('SELECT') == expected
try:
    builtins.hash = lambda value: 997
    assert through_global('SELECT') == 997
    assert original_hash('SELECT') == expected
finally:
    builtins.hash = original_hash
assert through_global('SELECT') == expected
print('builtin rebinding preserved')
