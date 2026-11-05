import io
import os
import tempfile


with tempfile.TemporaryDirectory() as directory:
    path = os.path.join(directory, 'original.bin')
    with io.FileIO(path, 'w+') as stream:
        descriptor = stream.fileno()
        stream.name = descriptor
        print('descriptor-name', stream.name == descriptor)
        marker = {'logical': 'metadata'}
        stream.name = marker
        print('object-name', stream.name is marker)
        stream.name = os.path.join(directory, 'other.bin')
        stream.write(b'original descriptor')
        stream.seek(0)
        print('metadata-not-path', stream.read() == b'original descriptor')
    with open(path, 'rb') as stored:
        print('original-file', stored.read() == b'original descriptor')
    print('no-retarget', not os.path.exists(os.path.join(directory, 'other.bin')))
    with io.FileIO(path, 'r') as stream:
        stream.name = None
        stream.marker = 'kept'
        stream.__init__(path, 'r')
        print('reinitialized-name', stream.name == path, stream.marker == 'kept')
    os.remove(path)

    payload = bytes(range(256)) * 6000
    for size in (1024, 1048576):
        with tempfile.SpooledTemporaryFile(max_size=size, mode='w+b', dir=directory) as stream:
            stream.write(payload[:size])
            print('in-memory', not stream._rolled)
            stream.write(payload[size:])
            position = stream.tell()
            descriptor = stream.fileno()
            stream.seek(0)
            print('spooled', stream._rolled, position == len(payload), stream.read() == payload)
        try:
            os.fstat(descriptor)
            closed = False
        except OSError:
            closed = True
        print('closed-descriptor', closed)
    print('cleaned-up', os.listdir(directory) == [])
