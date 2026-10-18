import errno
import io
import os
import subprocess
import sys
import tempfile
import zipfile
import zipimport


try:
    os.fstat(-1)
except OSError as error:
    print("fstat-errno", type(error) is OSError, error.errno == errno.EBADF)
else:
    print("fstat-errno", False, False)


def normalized(path):
    return os.path.normcase(os.path.realpath(path))


with tempfile.TemporaryDirectory(prefix="xlang3-local-") as directory:
    path = os.path.join(directory, "original.tmp")
    raw = io.FileIO(path, "w")
    raw.name = 123
    print("integer-name", raw.name == 123)
    marker = object()
    raw.name = marker
    print("object-name", raw.name is marker)
    display_path = os.path.join(directory, "display-only.tmp")
    raw.name = display_path
    raw.write(b"abc")
    raw.close()
    with open(path, "rb") as stream:
        print("display-name-storage", stream.read() == b"abc" and not os.path.exists(display_path))

    with tempfile.TemporaryFile() as stream:
        stream.write(b"temporary")
        stream.seek(0)
        print("temporary-file", stream.read() == b"temporary")

    archive = os.path.join(directory, "modules.zip")
    with zipfile.ZipFile(archive, "w") as zipped:
        zipped.writestr("pkg/mod.py", "VALUE = 314\n")
    loader = zipimport.zipimporter(archive + os.sep + "pkg")
    expected = archive + os.sep + "pkg" + os.sep + "mod.py"
    print("zip-origins", loader.get_filename("mod") == expected
          and loader.get_code("mod").co_filename == expected
          and loader.find_spec("mod").origin == expected)

    original = os.getcwd()
    nested = os.path.join(directory, "nested")
    os.mkdir(nested)
    os.chdir(directory)
    try:
        command = [sys.executable, "-c", "import os; print(os.getcwd())"]
        inherited = subprocess.check_output(command, text=True).strip()
        relative = subprocess.check_output(command, cwd="nested", text=True).strip()
        print("subprocess-cwd", normalized(inherited) == normalized(directory))
        print("relative-cwd", normalized(relative) == normalized(nested))
        print("parent-cwd", normalized(os.getcwd()) == normalized(directory))
    finally:
        os.chdir(original)
