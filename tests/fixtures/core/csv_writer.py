import csv
import io


def show(name, row, **options):
    stream = io.StringIO()
    writer = csv.writer(stream, **options)
    returned = writer.writerow(row)
    print(name, repr(stream.getvalue()), returned)


show('basic', ['one', 'two'])
show('special', ['a,b', 'say "yes"', 'x\ny', None, 12, 1.5])
show('empty', [''])
show('empty-row', [])
show('all', ['a', None, 2], quoting=csv.QUOTE_ALL)
show('nonnumeric', ['a', 2, 1.5, None, ''], quoting=csv.QUOTE_NONNUMERIC)
show('none', ['a,b', 'q"z', 'x\ny'], quoting=csv.QUOTE_NONE, escapechar='\\')
show('strings', ['a', 2, None, ''], quoting=csv.QUOTE_STRINGS)
show('notnull', ['a', 2, None, ''], quoting=csv.QUOTE_NOTNULL)
show('custom', ['a中b', 'q「z', 'x\ny'], delimiter='中', quotechar='「', lineterminator='||')

stream = io.StringIO()
writer = csv.writer(stream)
print('writerows', writer.writerows(([1, 2], ['a,b', None])), repr(stream.getvalue()))
print('dialect', writer.dialect.delimiter, writer.dialect.lineterminator == '\r\n')

class Sink:
    def __init__(self):
        self.value = None
    def write(self, value):
        self.value = value
        return 'written'

sink = Sink()
print('write-result', csv.writer(sink).writerow(['a']), repr(sink.value))

for name, factory in [
    ('no-write', lambda: csv.writer(object())),
    ('noncallable', lambda: csv.writer(type('Sink', (), {'write': 3})())),
    ('no-escape', lambda: csv.writer(io.StringIO(), quoting=csv.QUOTE_NONE).writerow(['a,b'])),
    ('row-type', lambda: csv.writer(io.StringIO()).writerow(3)),
    ('empty-none', lambda: csv.writer(io.StringIO(), quoting=csv.QUOTE_NONE).writerow([''])),
]:
    try:
        factory()
    except Exception as exc:
        print(name, type(exc).__name__, str(exc))
