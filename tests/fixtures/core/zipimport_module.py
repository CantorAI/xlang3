import os
import sys
import _imp
import io
import importlib.util
import inspect
import marshal
import struct
import time
import zipfile
import zipimport

archive = "xlang3_zipimport_fixture.zip"
with zipfile.ZipFile(archive, "w") as zf:
    zf.writestr("pkg/__init__.py", "PACKAGE = True\n")
    zf.writestr("pkg/mod.py", "VALUE = 314\n")
    zf.writestr("pkg/data.bin", b"zip data")

loader = zipimport.zipimporter(archive + os.sep + "pkg")
root_loader = zipimport.zipimporter(archive)
print(loader.archive == archive, loader.prefix.endswith("pkg" + os.sep))
print(root_loader.is_package("pkg"), loader.get_source("mod").strip())
print(loader.get_data("pkg/data.bin"))
print(loader.get_code("mod").co_filename.endswith("pkg" + os.sep + "mod.py"))
print(loader.get_filename("mod").endswith("pkg" + os.sep + "mod.py"))
spec = loader.find_spec("mod")
print(spec.name, spec.loader is loader, spec.origin.endswith("pkg" + os.sep + "mod.py"))
print("pkg" + os.sep in loader._get_files(), "pkg" + os.sep + "mod.py" in loader._get_files())

sys.path.insert(0, archive)
import pkg.mod
print(pkg.mod.VALUE, pkg.PACKAGE)
sys.path.pop(0)

loader.invalidate_caches()
os.remove(archive)
loader.invalidate_caches()
print(loader._get_files() == [], zipimport._zip_directory_cache.get(loader.archive) is None)

stale_archive = "xlang3_zipimport_stale.zip"
stale_source = b"VALUE = 'fresh'\n"
zip_time = (2025, 1, 2, 3, 4, 6)
source_mtime = int(time.mktime(zip_time + (0, 0, -1)))
stale_code = compile("VALUE = 'stale'\n", "stale_zip_mod.py", "exec")
stale_pyc = (importlib.util.MAGIC_NUMBER +
             struct.pack("<III", 0, source_mtime + 20, len(stale_source)) +
             marshal.dumps(stale_code))
with zipfile.ZipFile(stale_archive, "w", zipfile.ZIP_DEFLATED) as zf:
    source_info = zipfile.ZipInfo("stale_zip_mod.py", zip_time)
    source_info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(source_info, stale_source)
    pyc_info = zipfile.ZipInfo("stale_zip_mod.pyc", zip_time)
    pyc_info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(pyc_info, stale_pyc)
sys.path.insert(0, stale_archive)
import stale_zip_mod
print(stale_zip_mod.VALUE, stale_zip_mod.__file__.endswith("stale_zip_mod.py"))
sys.path.pop(0)
os.remove(stale_archive)

hash_archive = "xlang3_zipimport_hash.zip"
old_hash_source = b"VALUE = 'old-hash'\n"
new_hash_source = b"VALUE = 'new-hash'\n"
source_hash = importlib.util.source_hash(old_hash_source)
hash_code = compile(old_hash_source, "hash_zip_mod.py", "exec")
hash_pyc = (importlib.util.MAGIC_NUMBER + struct.pack("<I", 1) + source_hash +
            marshal.dumps(hash_code))
with zipfile.ZipFile(hash_archive, "w", zipfile.ZIP_DEFLATED) as zf:
    zf.writestr("hash_zip_mod.py", new_hash_source)
    zf.writestr("hash_zip_mod.pyc", hash_pyc)
old_hash_policy = _imp.check_hash_based_pycs
_imp.check_hash_based_pycs = "always"
sys.path.insert(0, hash_archive)
import hash_zip_mod
print(hash_zip_mod.VALUE, hash_zip_mod.__file__.endswith("hash_zip_mod.py"),
      len(source_hash), source_hash != importlib.util.source_hash(new_hash_source))
sys.path.pop(0)
_imp.check_hash_based_pycs = old_hash_policy
os.remove(hash_archive)

cruft_archive = "xlang3_zipimport_cruft.zip"
cruft_data = io.BytesIO()
with zipfile.ZipFile(cruft_data, "w") as zf:
    zf.writestr("pre/fix/cruft_zip_mod.py", "VALUE = 'cruft-ok'\n")
with open(cruft_archive, "wb") as file:
    file.write(b"self extracting prefix" + cruft_data.getvalue())
sys.path.insert(0, cruft_archive + os.sep + "pre" + os.sep + "fix")
import cruft_zip_mod
print(cruft_zip_mod.VALUE, "cruft-ok" in inspect.getsource(cruft_zip_mod))
sys.path.pop(0)
os.remove(cruft_archive)

bad_magic_archive = "xlang3_zipimport_bad_magic.zip"
with zipfile.ZipFile(bad_magic_archive, "w") as zf:
    zf.writestr("bad_magic_zip_mod.pyc", b"BAD!" + bytes(12))
sys.path.insert(0, bad_magic_archive)
try:
    import bad_magic_zip_mod
except zipimport.ZipImportError as exc:
    print(type(exc.__cause__).__name__, "magic number" in exc.__cause__.msg)
sys.path.pop(0)
os.remove(bad_magic_archive)

class NonSeekableArchive(io.BytesIO):
    def seekable(self):
        return False
    def seek(self, *args):
        raise OSError("not seekable")

descriptor_data = NonSeekableArchive()
with zipfile.ZipFile(descriptor_data, "w", zipfile.ZIP_DEFLATED) as zf:
    zf.writestr("descriptor_mod.py", "VALUE = 'descriptor'\n")
descriptor_archive = "xlang3_zipimport_descriptor.zip"
with open(descriptor_archive, "wb") as file:
    file.write(descriptor_data.getvalue())
descriptor_loader = zipimport.zipimporter(descriptor_archive)
print(descriptor_loader.get_source("descriptor_mod").strip())
os.remove(descriptor_archive)
