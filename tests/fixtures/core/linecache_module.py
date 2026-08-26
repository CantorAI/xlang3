import linecache
import os

path = "xlang3_linecache_sample.py"
with open(path, "w") as f:
    f.write("first\nsecond\nthird")

print(linecache.getline(path, 1).strip())
print(linecache.getline(path, 2).strip(), linecache.getline(path, 3))
print(linecache.getline(path, 99) == "")

lines = linecache.getlines(path)
print(len(lines), lines[0].strip(), lines[2])

updated = linecache.updatecache(path)
print(updated[1].strip())
print(linecache.checkcache() is None)
print(linecache.lazycache(path, {}) in (False, True))

linecache.clearcache()
print(linecache.getline("missing-linecache-file.py", 1) == "")

os.remove(path)

latin_path = "xlang3_linecache_latin1.py"
with open(latin_path, "wb") as f:
    f.write(b"# coding: latin-1\nword='caf\xe9'\n")

latin_line = linecache.getline(latin_path, 2).strip()
print(latin_line[:9], ord(latin_line[-2]))
linecache.clearcache()
os.remove(latin_path)

bom_path = "xlang3_linecache_bom.py"
with open(bom_path, "wb") as f:
    f.write(b"\xef\xbb\xbfvalue=7\n")

print(linecache.getline(bom_path, 1).strip())
linecache.clearcache()
os.remove(bom_path)
