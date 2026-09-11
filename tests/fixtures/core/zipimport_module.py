import os
import sys
import io
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
