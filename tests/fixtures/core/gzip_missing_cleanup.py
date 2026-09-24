import gzip
import os
import tempfile


with tempfile.TemporaryDirectory() as directory:
    missing = os.path.join(directory, "missing.gz")
    for constructor in (
        lambda: gzip.GzipFile(missing),
        lambda: gzip.open(missing, "rb"),
    ):
        try:
            constructor()
        except FileNotFoundError:
            print("missing")
        print("continued")
