import os
import io
import zipfile

archive = "xlang3_zipfile_sample.zip"
source = "xlang3_zipfile_source.txt"

with open(source, "w") as f:
    f.write("from file")

with zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as zf:
    zf.writestr("hello.txt", "hello zip")
    zf.write(source, "folder/source.txt")
    zf.writestr("deflated.txt", "abc abc abc abc", zipfile.ZIP_DEFLATED)
    zf.comment = b"xlang3 archive"
    info = zipfile.ZipInfo("meta.txt", (2024, 5, 6, 7, 8, 10))
    info.comment = b"member comment"
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, "metadata")
    zf.mkdir("emptydir")

with zipfile.ZipFile(archive, "r") as zf:
    names = zf.namelist()
    infos = zf.infolist()
    print(names)
    print(infos[0].filename, infos[0].file_size, infos[0].compress_type)
    print(zf.getinfo("deflated.txt").compress_type, zf.getinfo("deflated.txt").compress_size < zf.getinfo("deflated.txt").file_size)
    print(zf.getinfo("hello.txt").filename, infos[1].file_size)
    print(zf.read("hello.txt") == b"hello zip")
    print(zf.read(infos[1]) == b"from file")
    print(zf.read("folder/source.txt") == b"from file")
    print(zf.read("deflated.txt") == b"abc abc abc abc")
    print(zf.comment == b"xlang3 archive")
    meta = zf.getinfo("meta.txt")
    print(meta.date_time, meta.comment, meta.compress_type, zf.read(meta) == b"metadata")
    print(zf.getinfo("emptydir/").is_dir())
    with zf.open("hello.txt") as entry:
        print(entry.read(5), entry.read(), entry.closed)
    with zf.open("hello.txt") as entry:
        print(entry.readline())
    root = zipfile.Path(zf)
    child_names = []
    for child in root.iterdir():
        child_names.append(child.at)
    print("hello.txt" in child_names, "folder/" in child_names, "emptydir/" in child_names)
    hello_path = root.joinpath("hello.txt")
    folder_path = root.joinpath("folder")
    print(hello_path.exists(), hello_path.is_file(), hello_path.read_bytes())
    print(folder_path.exists(), folder_path.is_dir())
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

manual = zipfile.ZipInfo("manual.txt", (2025, 1, 2, 3, 4, 6))
print(manual.filename, manual.date_time)
print(zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED, zipfile.BadZipfile is zipfile.BadZipFile)

mem = io.BytesIO()
with zipfile.ZipFile(mem, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as zf:
    zf.writestr("mem.txt", b"in memory")
    with zf.open("opened.txt", "w") as entry:
        print(entry.write(b"via open"))

mem.seek(0)
with zipfile.ZipFile(mem, mode="r") as zf:
    print(zf.read("mem.txt") == b"in memory")
    print(zf.read("opened.txt") == b"via open")

with zipfile.PyZipFile("xlang3_pyzip.zip", "w") as zf:
    zf.writestr("py.txt", "pyzip")

with zipfile.ZipFile("xlang3_pyzip.zip", "r") as zf:
    print(zf.read("py.txt") == b"pyzip")

os.remove("xlang3_zip_extract/hello.txt")
os.remove("xlang3_zip_extract_all/folder/source.txt")
os.remove(source)
os.remove(archive)
os.remove("xlang3_pyzip.zip")
