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
