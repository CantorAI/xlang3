import importlib
import importlib.util
import os
import tempfile
import zipfile
import zipimport
from pathlib import Path


with tempfile.TemporaryDirectory() as directory:
    for suffix, package in (("whl", "wheel_probe"), ("data", "archive_probe")):
        archive = Path(directory) / (package + "." + suffix)
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr(package + "/__init__.py", "VALUE = 314\n")
            output.writestr(package + "/feature.py", "ENABLED = True\n")
        import sys

        sys.path.insert(0, str(archive))
        try:
            spec = importlib.util.find_spec(package + ".feature")
            print(type(spec.loader).__name__, spec.loader.get_code(package + ".feature") is not None)
            module = importlib.import_module(package + ".feature")
            package_module = importlib.import_module(package)
            importer = zipimport.zipimporter(str(archive) + os.sep + package)
            print(package_module.VALUE, module.ENABLED, "ENABLED = True" in importer.get_source("feature"))
            print(module.__loader__.archive == str(archive),
                  module.__loader__.prefix.endswith(package + os.sep))
        finally:
            sys.path.pop(0)
