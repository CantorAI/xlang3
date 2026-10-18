import os
import stat
import tempfile
from pathlib import Path


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    room = root / 'room'
    room.mkdir()
    source = room / 'clip.txt'
    source.write_text('room recording')
    other = room / 'other.txt'
    other.write_text('different recording')
    alias = root / 'room-alias'
    os.symlink(room, alias, target_is_directory=True)

    print('alias-samefile', os.path.samefile(source, alias / source.name))
    print('canonical-samefile', os.path.samefile(source, source.resolve()))
    print('different-files', not os.path.samefile(source, other))
    original = os.getcwd()
    try:
        os.chdir(alias)
        print('alias-cwd', os.path.samefile(os.getcwd(), room),
              os.path.samefile('clip.txt', source))
    finally:
        os.chdir(original)
    info = os.stat(source)
    with open(source, 'rb') as stream:
        descriptor = os.fstat(stream.fileno())
        print('descriptor-identity', info.st_ino == descriptor.st_ino,
              info.st_dev == descriptor.st_dev)
    with os.scandir(room) as entries:
        entry = next(entry for entry in entries if entry.name == source.name)
        print('entry-identity', entry.inode() == info.st_ino)
        if os.name == 'posix':
            # Windows DirEntry.stat deliberately omits identity fields.
            print('posix-entry-stat-identity', entry.stat().st_ino == info.st_ino,
                  entry.stat().st_dev == info.st_dev)

    if os.name == 'posix':
        fields = ['st_mode', 'st_dev', 'st_nlink', 'st_uid', 'st_gid',
                  'st_size', 'st_mtime_ns', 'st_ctime_ns']
        print('posix-kernel-fields', all(getattr(info, field) ==
              getattr(descriptor, field) for field in fields),
              info.st_nlink > 0)
        link = os.lstat(alias)
        print('posix-link-identity', stat.S_ISLNK(link.st_mode),
              (link.st_dev, link.st_ino) != (info.st_dev, os.stat(room).st_ino))
        dangling = root / 'missing-link'
        os.symlink(root / 'not-created', dangling)
        print('posix-dangling-lstat', stat.S_ISLNK(os.lstat(dangling).st_mode))
        try:
            os.stat(dangling)
        except FileNotFoundError:
            print('posix-dangling-stat', True)
        else:
            print('posix-dangling-stat', False)
