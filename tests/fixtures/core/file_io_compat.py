from pathlib import Path


path = Path("xlang3_file_io_compat.tmp")
if path.exists():
    import os
    os.remove(path.as_posix())

with open(path, "w", encoding="utf-8") as f:
    print(f.write("one\n"))
    f.writelines(["two\n", "three"])
    print(f.tell())
    print(f.closed())

with open(path, "r") as f:
    print(f.readline())
    print(f.tell())
    f.seek(0)
    print(f.read(3))
    print(f.readline())
    print(f.readlines())

with open(path, "a+") as f:
    f.write("\nfour")
    f.seek(0)
    print(f.read())

with open(path, "rb") as f:
    data = f.read(3)
    print(data)

with open("xlang3_file_io_binary.tmp", "wb") as f:
    print(f.write(b"xy"))

with open("xlang3_file_io_binary.tmp", "rb") as f:
    print(f.read())
