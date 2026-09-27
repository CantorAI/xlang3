import json
import os
import stat
import tempfile
from pathlib import Path

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/broken-link")
def broken_link():
    with tempfile.TemporaryDirectory() as directory:
        link = Path(directory) / "link"
        link.symlink_to(Path(directory) / "missing")
        target_dir = Path(directory) / "target-dir"
        target_dir.mkdir()
        dir_link = Path(directory) / "dir-link"
        dir_link.symlink_to(target_dir, target_is_directory=True)
        with os.scandir(directory) as iterator:
            entries = {entry.name: entry for entry in iterator}
        broken_entry = entries["link"]
        dir_entry = entries["dir-link"]
        return {
            "exists": os.path.exists(link),
            "lexists": os.path.lexists(link),
            "is_file": os.path.isfile(link),
            "is_dir": os.path.isdir(link),
            "path_exists_follow": link.exists(follow_symlinks=True),
            "path_exists_no_follow": link.exists(follow_symlinks=False),
            "entry_broken_is_file": broken_entry.is_file(),
            "entry_broken_is_file_no_follow": broken_entry.is_file(follow_symlinks=False),
            "entry_dir_follow": dir_entry.is_dir(),
            "entry_dir_no_follow": dir_entry.is_dir(follow_symlinks=False),
            "entry_dir_stat_is_link": stat.S_ISLNK(dir_entry.stat(follow_symlinks=False).st_mode),
            "entry_dir_stat_is_dir": stat.S_ISDIR(dir_entry.stat().st_mode),
        }


response = TestClient(app).get("/broken-link")
print(response.status_code, json.dumps(response.json(), sort_keys=True))
