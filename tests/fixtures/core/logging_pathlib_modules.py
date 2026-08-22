import logging
import os
from pathlib import Path, PurePath


logging.basicConfig(logging.INFO)
logging.debug("hidden")
logging.info("hello", "world")

log = logging.getLogger("demo")
log.warning("careful")
log.setLevel(logging.ERROR)
log.warning("hidden")
log.error("boom")

p = Path("xlang3_pathlib_test.txt")
if p.exists():
    os.remove(p.as_posix())
print(p.as_posix())
print(p.name())
print(p.suffix())
print(p.parent.as_posix())
print(p.exists())
p.write_text("abc")
print(p.exists())
print(p.is_file())
print(p.read_text())
print(p.joinpath("child.py").as_posix())
print(PurePath("a", "b").as_posix())
print(str(p))
print(os.fspath(p))
