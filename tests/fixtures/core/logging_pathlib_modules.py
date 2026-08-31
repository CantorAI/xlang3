import logging
import os
import sys
from pathlib import Path, PurePath


logging.basicConfig(level=logging.INFO, stream=sys.stdout)
logging.debug("hidden")
logging.info("hello %s", "world")

log = logging.getLogger("demo")
log.warning("careful")
log.setLevel(logging.ERROR)
log.warning("hidden")
log.error("boom")
print(logging.getLevelName(logging.WARNING), logging.getLevelName("ERROR"))
print(log.getEffectiveLevel(), log.isEnabledFor(logging.ERROR), log.isEnabledFor(logging.INFO))
handler = logging.NullHandler()
handler.setFormatter(logging.Formatter("%(message)s"))
log.addHandler(handler)
log.removeHandler(handler)
print(isinstance(handler, logging.NullHandler), isinstance(logging.StreamHandler(), logging.StreamHandler))

p = Path("xlang3_pathlib_test.txt")
if p.exists():
    os.remove(p.as_posix())
print(p.as_posix())
print(p.name)
print(p.suffix)
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
