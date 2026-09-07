import pathlib
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory(prefix="xlang3-readlink-") as directory:
    root = pathlib.Path(directory)
    (root / "regular").write_text("test", encoding="utf-8")
    (root / "link").symlink_to("regular")
    long_target = "/".join(["long-target"] * 100)
    (root / "long").symlink_to(long_target)
    source = '''import os
import pathlib
assert os.readlink("link") == "regular"
assert os.readlink(b"link") == b"regular"
assert os.readlink(pathlib.Path("link")) == "regular"
assert os.readlink("long") == "/".join(["long-target"] * 100)
for path in ["regular", "missing"]:
    try:
        os.readlink(path)
    except OSError:
        pass
    else:
        raise AssertionError(path)
try:
    os.readlink("link\\0ignored")
except ValueError:
    pass
else:
    raise AssertionError("embedded null accepted")
print("readlink-passed")
'''
    result = subprocess.run([sys.argv[1], "-c", source], cwd=root,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == "readlink-passed", result.stdout
