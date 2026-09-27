from itertools import zip_longest


shared = iter('abcd')
print('shared', list(zip_longest(shared, shared)))
print('fill', list(zip_longest([1, 2], [3], fillvalue=9)))
print('empty', list(zip_longest()))

events = []


def source():
    events.append('start')
    yield 7


lazy = zip_longest(source(), [8])
print('lazy-before', events)
print('lazy-next', next(lazy), events)
try:
    next(lazy)
except StopIteration:
    print('lazy-stop')
