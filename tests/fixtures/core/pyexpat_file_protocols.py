import io
import os
import platform
import plistlib
import pyexpat
import sys
import tempfile


XML = b'<room><name>meeting</name><people>2</people></room>'


def parse(source):
    events = []
    parser = pyexpat.ParserCreate()
    parser.StartElementHandler = lambda name, attrs: events.append(('start', name))
    parser.EndElementHandler = lambda name: events.append(('end', name))
    result = parser.ParseFile(source)
    return result == 1 and events == [
        ('start', 'room'), ('start', 'name'), ('end', 'name'),
        ('start', 'people'), ('end', 'people'), ('end', 'room'),
    ]


print('bytesio', parse(io.BytesIO(XML)))
with tempfile.TemporaryDirectory() as directory:
    path = os.path.join(directory, 'room.xml')
    with open(path, 'wb') as stream:
        stream.write(XML)
    with open(path, 'rb') as stream:
        print('native-file', parse(stream), not stream.closed)
    value = {'room': 'meeting', 'people': 2, 'present': True}
    with open(path, 'wb') as stream:
        plistlib.dump(value, stream, fmt=plistlib.FMT_XML)
    with open(path, 'rb') as stream:
        print('plist-native-file', plistlib.load(stream) == value)
    large = {'room': 'meeting' * 1000}
    with open(path, 'wb') as stream:
        plistlib.dump(large, stream, fmt=plistlib.FMT_XML)
    with open(path, 'rb') as stream:
        print('large-plist', plistlib.load(stream) == large)


class ShortReader:
    def __init__(self):
        self.parts = [XML[:8], XML[8:23], XML[23:], b'']
        self.sizes = []

    def read(self, size):
        self.sizes.append(size)
        return self.parts.pop(0)


reader = ShortReader()
print('short-reads', parse(reader), len(reader.sizes) == 4,
      all(isinstance(size, int) and size > 0 for size in reader.sizes))


class DelegatedReader:
    def __init__(self):
        self.stream = io.BytesIO(XML)

    def __getattr__(self, name):
        return getattr(self.stream, name)


print('delegation', parse(DelegatedReader()))


class BytesSubclass(bytes):
    def __bytes__(self):
        raise RuntimeError('must not coerce a bytes subclass')


class SubclassReader:
    def __init__(self):
        self.parts = [BytesSubclass(XML), BytesSubclass(b'')]

    def read(self, size):
        return self.parts.pop(0)


print('bytes-subclass', parse(SubclassReader()))


class BufferReader:
    def __init__(self, factory):
        self.parts = [factory(XML), factory(b'')]

    def read(self, size):
        return self.parts.pop(0)


print('bytearray-buffer', parse(BufferReader(bytearray)))


class BadReader:
    def __init__(self, value):
        self.value = value

    def read(self, size):
        return self.value


for label, value in [('text', '<room/>'), ('none', None), ('integer', 7),
                     ('memoryview', memoryview(XML))]:
    try:
        pyexpat.ParserCreate().ParseFile(BadReader(value))
    except TypeError:
        print('bad-read', label, True)
    else:
        print('bad-read', label, False)


try:
    pyexpat.ParserCreate().ParseFile(BadReader(b'x' * 2049))
except ValueError:
    print('oversized-read', True)
else:
    print('oversized-read', False)

try:
    pyexpat.ParserCreate().ParseFile(object())
except TypeError:
    print('missing-read', True)
else:
    print('missing-read', False)


class FailedReader:
    def read(self, size):
        raise RuntimeError('room read failed')


try:
    pyexpat.ParserCreate().ParseFile(FailedReader())
except RuntimeError as error:
    print('read-exception', str(error) == 'room read failed')


class FailedLookup:
    @property
    def read(self):
        raise ValueError('room lookup failed')


try:
    pyexpat.ParserCreate().ParseFile(FailedLookup())
except ValueError as error:
    print('lookup-exception', str(error) == 'room lookup failed')

print('mac-version', sys.platform != 'darwin' or bool(platform.mac_ver()[0]))
