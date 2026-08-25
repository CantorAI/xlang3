import os
import zipfile

archive = "xlang3_zipfile_sample.zip"
source = "xlang3_zipfile_source.txt"

with open(source, "w") as f:
    f.write("from file")

with zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as zf:
    zf.writestr("hello.txt", "hello zip")
    zf.write(source, "folder/source.txt")
    zf.writestr("deflated.txt", "abc abc abc abc", zipfile.ZIP_DEFLATED)

with zipfile.ZipFile(archive, "r") as zf:
    names = zf.namelist()
    infos = zf.infolist()
    print(names)
    print(infos[0].filename, infos[0].file_size, infos[0].compress_type)
    print(zf.getinfo("deflated.txt").compress_type, zf.getinfo("deflated.txt").compress_size < zf.getinfo("deflated.txt").file_size)
    print(zf.getinfo("hello.txt").filename, zf.getinfo(infos[1]).file_size)
    print(zf.read("hello.txt") == b"hello zip")
    print(zf.read(infos[1]) == b"from file")
    print(zf.read("folder/source.txt") == b"from file")
    print(zf.read("deflated.txt") == b"abc abc abc abc")
    print(zf.testzip() is None)

print(zipfile.is_zipfile(archive), zipfile.is_zipfile(source))

with zipfile.ZipFile(archive, "r") as zf:
    extracted = zf.extract("hello.txt", "xlang3_zip_extract")
    zf.extractall("xlang3_zip_extract_all", ["folder/source.txt"])
    print(extracted)

with open("xlang3_zip_extract/hello.txt", "r") as f:
    print(f.read())

with open("xlang3_zip_extract_all/folder/source.txt", "r") as f:
    print(f.read())

print(zipfile.ZipInfo("manual.txt").filename)
print(zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED, zipfile.BadZipfile is zipfile.BadZipFile)

os.remove("xlang3_zip_extract/hello.txt")
os.remove("xlang3_zip_extract_all/folder/source.txt")
os.remove(source)
os.remove(archive)
