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

# newline="\r\n" translates text newlines when writing.
with open("xlang3_file_io_newline.tmp", "w", newline="\r\n") as f:
    print(f.write("a\nb"))

with open("xlang3_file_io_newline.tmp", "rb") as f:
    print(f.read())

# newline="" preserves raw newline bytes in text reads.
with open("xlang3_file_io_newline.tmp", "r", newline="") as f:
    print(f.read())

# default text reads use universal newline translation.
with open("xlang3_file_io_newline.tmp", "r") as f:
    print(f.readlines())

# text encodings and error handlers are applied through open().
with open("xlang3_file_io_latin.tmp", "w", encoding="latin-1", errors="replace") as f:
    print(f.write("é中"))

with open("xlang3_file_io_latin.tmp", "rb") as f:
    print(f.read())

with open("xlang3_file_io_latin.tmp", "r", encoding="latin-1") as f:
    text = f.read()
    print(len(text), ord(text[0]), text[1])

# utf-8-sig writes and consumes the BOM through the file layer.
with open("xlang3_file_io_sig.tmp", "w", encoding="utf-8-sig") as f:
    print(f.write("ok"))

with open("xlang3_file_io_sig.tmp", "rb") as f:
    print(len(f.read()))

with open("xlang3_file_io_sig.tmp", "r", encoding="utf-8-sig") as f:
    print(f.read())

# Positional open() options follow CPython's signature order.
with open("xlang3_file_io_positional.tmp", "w", -1, "ascii", "replace", "\n", True, None) as f:
    print(f.write("A中"))

with open("xlang3_file_io_positional.tmp", "rb") as f:
    print(f.read())

# File objects are their own line iterators.
with open(path, "r") as f:
    for line in f:
        print("iter", line.strip())
        break
