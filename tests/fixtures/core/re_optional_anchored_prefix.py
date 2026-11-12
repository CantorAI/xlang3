import re

for pattern, text in [
    (r'^-?\d+$', '1'), (r'^-?\d+$', '-5'),
    (r'\Aa?b', 'b'), (r'^a*b$', 'b'),
    (r'^a{0,2}b$', 'b'), (r'^a|b$', 'b'),
    (r'^a?b$', 'ab'), (r'^ab$', 'ab'),
]:
    compiled = re.compile(pattern)
    assert compiled.match(text) is not None, (pattern, text, 'match')
    assert compiled.search(text) is not None, (pattern, text, 'search')
    assert compiled.fullmatch(text) is not None, (pattern, text, 'fullmatch')
    assert compiled.findall(text) == [text], (pattern, text, 'findall')
    assert compiled.sub('OK', text) == 'OK', (pattern, text, 'sub')
    assert compiled.split(text) == ['', ''], (pattern, text, 'split')

assert re.match(r'^ab$', 'b') is None
assert re.match(r'^a+b$', 'b') is None
assert re.match(r'^a{1,2}b$', 'b') is None
print('optional anchored prefixes: ok')
