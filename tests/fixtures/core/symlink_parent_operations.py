import os
from pathlib import Path
import stat
import tempfile


# Set up through the canonical directory so this case can distinguish creating
# a directory through a symlink from the setup that creates the symlink itself.
canonical_temp = Path(tempfile.gettempdir()).resolve()
with tempfile.TemporaryDirectory(dir=str(canonical_temp)) as temporary:
    base = Path(temporary)
    target = base / "target"
    alias = base / "alias"
    target.mkdir()
    os.symlink(target, alias, target_is_directory=True)

    os.mkdir(alias / "direct")
    os.makedirs(alias / "nested" / "deep")
    print("parent-directories", (target / "direct").is_dir(), (target / "nested" / "deep").is_dir())

    payload = b"symlink-parent-data"
    file_path = alias / "payload.bin"
    file_path.write_bytes(payload)
    with open(file_path, "rb") as stream:
        read_back = stream.read()
    print("parent-file-io", read_back == payload, (target / "payload.bin").read_bytes() == payload)

    old_tempdir = tempfile.tempdir
    try:
        tempfile.tempdir = str(alias)
        with tempfile.TemporaryDirectory() as nested_temporary:
            nested = Path(nested_temporary)
            print("parent-default-temp", nested.is_dir(), os.path.samefile(nested.parent, alias))
    finally:
        tempfile.tempdir = old_tempdir

    leaf = base / "leaf"
    os.symlink(target / "payload.bin", leaf)
    print("leaf-stat", os.stat(leaf).st_size == len(payload), stat.S_ISLNK(os.lstat(leaf).st_mode), stat.S_ISDIR(os.stat(alias).st_mode))
    with os.scandir(base) as entries:
        alias_entry = next(entry for entry in entries if entry.name == "alias")
        print("direntry", alias_entry.is_symlink(), alias_entry.is_dir(), not alias_entry.is_dir(follow_symlinks=False), not alias_entry.is_file())
        print("leaf-no-follow", stat.S_ISLNK(alias_entry.stat(follow_symlinks=False).st_mode), stat.S_ISLNK(os.stat(alias, follow_symlinks=False).st_mode))

    dangling = base / "dangling"
    os.symlink(base / "missing", dangling)
    print("dangling-link", stat.S_ISLNK(os.lstat(dangling).st_mode), os.path.lexists(dangling), not os.path.exists(dangling))

    os.rmdir(alias / "nested" / "deep")
    os.unlink(alias)
    print("unlink-alias", not os.path.lexists(alias), target.is_dir() and (target / "payload.bin").read_bytes() == payload)
