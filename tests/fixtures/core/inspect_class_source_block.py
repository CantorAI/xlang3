import inspect
import tokenize


def source_case():
    if True:
        class Example:
            value: int
            """Field docs"""

    source, first_line = inspect.getsourcelines(Example)
    print('source', first_line == vars(Example)['__firstlineno__'],
          [line.strip() for line in source])


source_case()


def read_lines():
    return iter(('class Example:\n', '    value = 1\n', '  broken = 2\n')).__next__


stream = tokenize.generate_tokens(read_lines())
yielded = []
try:
    while True:
        yielded.append(next(stream).string)
except IndentationError:
    print('deferred', 'class' in yielded, 'value' in yielded)
