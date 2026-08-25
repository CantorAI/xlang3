import os
import zipfile

archive = "xlang3_zipfile_sample.zip"
source = "xlang3_zipfile_source.txt"

with open(source, "w") as f:
    f.write("from file")

with zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as zf:
    zf.writestr("hello.txt", "hello zip")
    zf.write(source, "folder/source.txt")

with zipfile.ZipFile(archive, "r") as zf:
    names = zf.namelist()
    infos = zf.infolist()
    print(names)
    print(infos[0].filename, infos[0].file_size, infos[0].compress_type)
    print(zf.read("hello.txt") == b"hello zip")
    print(zf.read("folder/source.txt") == b"from file")

print(zipfile.ZipInfo("manual.txt").filename)
print(zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED, zipfile.BadZipfile is zipfile.BadZipFile)

os.remove(source)
os.remove(archive)
