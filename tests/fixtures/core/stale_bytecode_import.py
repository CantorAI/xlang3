"""A source import must reject bytecode from an older runtime build."""

import importlib
import importlib.util
import os
from pathlib import Path
import struct
import sys
import tempfile


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "cache_old_source.py").write_text("value = 1.0\n")
    fresh_source = root / "cache_fresh_source.py"
    fresh_source.write_text("value = 1_000_000.49\n")
    sys.path.insert(0, directory)
    try:
        old = importlib.import_module("cache_old_source")
        body = Path(old.__cached__).read_bytes()[16:]
        cache = Path(importlib.util.cache_from_source(str(fresh_source)))
        cache.parent.mkdir(parents=True, exist_ok=True)
        stat = os.stat(fresh_source)
        magic = importlib.util.MAGIC_NUMBER
        stale_magic = (b"=X\r\n" if sys.implementation.name == "xlang3"
                       else bytes((magic[0] ^ 1,)) + magic[1:])
        cache.write_bytes(stale_magic + b"\0\0\0\0" +
                          struct.pack("<II", int(stat.st_mtime), stat.st_size) + body)
        fresh = importlib.import_module("cache_fresh_source")
        print(fresh.value == 1_000_000.49,
              Path(fresh.__cached__).read_bytes()[:4] == magic)
    finally:
        sys.path.remove(directory)
        sys.modules.pop("cache_old_source", None)
        sys.modules.pop("cache_fresh_source", None)
