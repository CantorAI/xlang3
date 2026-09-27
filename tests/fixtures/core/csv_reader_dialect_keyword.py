import csv
import io
import copy
import pickle


print('named', list(csv.reader(['one;two'], dialect='excel', delimiter=';')))
print('class', list(csv.reader(io.StringIO('"a,b",c\n'), dialect=csv.excel)))
print('override', list(csv.reader(['left|right'], dialect=csv.excel, delimiter='|')))
print('no-quote', list(csv.reader(['1,",3,",5'], quotechar=None, escapechar='\\')))
print('blank', list(csv.reader(['', '\n', 'a,b'])))
print('dict-blank', list(csv.DictReader(['name,age\n', '\n', 'Ada,37\n'])))
print('nonnumeric', list(csv.reader([',3,"5",7.3, 9'], quoting=csv.QUOTE_NONNUMERIC)))
print('strings', list(csv.reader([',3,"5",7.3'], quoting=csv.QUOTE_STRINGS)))
print('notnull', list(csv.reader([',a,"",b'], quoting=csv.QUOTE_NOTNULL)))
print('after-quote-escape', list(csv.reader(['a,"b,c"\\'], escapechar='\\')))
stream = io.StringIO(newline='')
csv.writer(stream, quoting=csv.QUOTE_NONE, escapechar='\\').writerow(['\na', 'b\nc', 'd\n'])
stream.seek(0)
print('escaped-newlines', list(csv.reader(stream, quoting=csv.QUOTE_NONE, escapechar='\\')))


class Reentrant:
    def __init__(self):
        self.count = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.count += 1
        if self.count == 1:
            try:
                next(self.reader)
            except StopIteration:
                pass
            return 'a,b'
        if self.count == 2:
            return 'x'
        raise StopIteration


reentrant = Reentrant()
reentrant.reader = csv.reader(reentrant)

for name, factory in (
    ('unknown', lambda: csv.reader([], dialect='missing')),
    ('duplicate', lambda: csv.reader([], 'excel', dialect='excel')),
    ('format', lambda: csv.reader([], dialect='excel', nonsense=True)),
    ('explicit-quote', lambda: csv.reader([], dialect='excel', quotechar=None)),
    ('noniterable', lambda: csv.reader(None)),
    ('list-args', lambda: csv.list_dialects(None)),
    ('get-none', lambda: csv.get_dialect(None)),
    ('unregister-none', lambda: csv.unregister_dialect(None)),
    ('newline', lambda: next(csv.reader(['a,b\nc,d']))),
    ('bad-delimiter', lambda: csv.reader([], delimiter='::')),
    ('bad-escape', lambda: csv.reader([], escapechar='')),
    ('bad-terminator', lambda: csv.reader([], lineterminator=4)),
    ('bad-quoting', lambda: csv.reader([], quoting=42)),
    ('strict-quote', lambda: next(csv.reader(['"ab"c'], strict=True))),
    ('dialect-copy', lambda: copy.copy(csv.get_dialect('excel'))),
    ('dialect-pickle', lambda: pickle.dumps(csv.get_dialect('excel'))),
):
    try:
        factory()
    except Exception as exc:
        print(name, type(exc).__name__, str(exc))

try:
    next(reentrant.reader)
except Exception as exc:
    print('reentrant', type(exc).__name__)
