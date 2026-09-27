import gc
import re

source = 'x' * 1000 + 'abc42 rest'
pattern = re.compile(r'([a-z]+)(\d+)')
match = pattern.match(source, 1000, 1005)
print(match.group(0), match.groups(), match.span(), match.pos, match.endpos)
print(match.string is source, match.expand(r'\2:\1'))
print(pattern.fullmatch(source, 1000, 1005).groups())
short = pattern.match(source, 1000, 1004)
print(short.group(0), short.span())
del source
gc.collect()
print(match.group(1), match.group(2), match.string[-10:])

source = 'é' * 4 + 'abc42'
match = pattern.match(source, 4)
print(match.groups(), match.span())
