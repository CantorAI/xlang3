import os
import tempfile
from pathlib import Path


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    directory = root / "dist"
    directory.mkdir()
    outside = root / "secret.txt"
    outside.write_text("secret")
    link = directory / "secret.txt"
    os.symlink(outside, link)
    resolved_link = os.path.realpath(link)
    resolved_directory = os.path.realpath(directory)
    print(resolved_link == os.path.realpath(outside))
    print(link.resolve() == outside.resolve())
    print(os.path.commonpath([resolved_link, resolved_directory]) == str(root))
