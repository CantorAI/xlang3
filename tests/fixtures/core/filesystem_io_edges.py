# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os

path = "xlang3_filesystem_edges.tmp"
other = "xlang3_filesystem_edges_other.tmp"
directory = "xlang3_filesystem_edges_dir"
for item in (path, other):
    try:
        os.remove(item)
    except OSError:
        pass
try:
    os.rmdir(directory)
except OSError:
    pass
os.mkdir(directory)
with open(path, "wb") as stream:
    stream.write(b"abc")

def check_error(label, function, expected_type, expected_errno, filename=False, winerror=None):
    try:
        value = function()
    except BaseException as exc:
        print(
            label,
            type(exc) is expected_type,
            exc.errno == expected_errno,
            (not filename) or type(exc.filename) is str,
            winerror is None or getattr(exc, "winerror", None) == winerror,
        )
    else:
        if label == "os-open-directory" and os.name != "nt":
            os.close(value)
            print(label, True, True, True, True)
        else:
            print(label, False, False, False, False)

check_error("os-open-missing", lambda: os.open("xlang3_filesystem_edges_missing.tmp", os.O_RDONLY), FileNotFoundError, 2, True)
check_error("os-open-exclusive", lambda: os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL), FileExistsError, 17, True)
check_error("os-open-directory", lambda: os.open(directory, os.O_RDONLY), PermissionError, 13, True)

fd = os.open(path, os.O_RDONLY)
check_error("lseek-invalid-how", lambda: os.lseek(fd, 0, 99), OSError, 22)
os.close(fd)

invalid_operations = (
    ("close-invalid", lambda: os.close(-1)),
    ("read-invalid", lambda: os.read(-1, 1)),
    ("write-invalid", lambda: os.write(-1, b"x")),
    ("lseek-invalid", lambda: os.lseek(-1, 0, os.SEEK_SET)),
    ("dup-invalid", lambda: os.dup(-1)),
    ("get-inherit-invalid", lambda: os.get_inheritable(-1)),
    ("set-inherit-invalid", lambda: os.set_inheritable(-1, False)),
)
for label, operation in invalid_operations:
    check_error(label, operation, OSError, 9)
check_error("fstat-invalid", lambda: os.fstat(-1), OSError, 9, winerror=6 if os.name == "nt" else None)

raw_fd = os.open(other, os.O_RDWR | os.O_CREAT | os.O_TRUNC)
wrapped = open(raw_fd, "w+b", buffering=0, closefd=False)
print("fd-wrapper", wrapped.write(b"xy") == 2, wrapped.fileno() == raw_fd)
wrapped.close()
print("closefd-false", os.fstat(raw_fd).st_size == 2)
os.close(raw_fd)

opened = []
def custom_opener(name, flags):
    opened.append((name, flags))
    return os.open(name, flags, 0o600)

with open(other, "w", opener=custom_opener, buffering=8, newline="\r\n") as stream:
    print("custom-opener-write", stream.write("a\nb") == 3)
print("custom-opener-call", opened[0][0] == other, bool(opened[0][1] & os.O_CREAT))
with open(other, "rb") as stream:
    print("buffer-newline", stream.read() == (b"a\r\nb" if os.name == "nt" else b"a\r\nb"))

class Index:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value

index_fd = os.open(other, Index(os.O_RDWR))
print("read-size-index", os.read(index_fd, Index(1)) == b"a")
print("lseek-index", os.lseek(Index(index_fd), Index(0), Index(os.SEEK_SET)) == 0)
print("fstat-index", os.fstat(Index(index_fd)).st_size == 4)
index_dup = os.dup(Index(index_fd))
print("dup-index", isinstance(index_dup, int), os.isatty(Index(index_dup)) is False)
print("inherit-index", os.get_inheritable(Index(index_dup)) is False)
os.set_inheritable(Index(index_dup), False)
os.close(Index(index_dup))
os.close(Index(index_fd))
try:
    os.close(1 << 80)
except OverflowError:
    print("fd-overflow", True)
else:
    print("fd-overflow", False)

for item in (path, other):
    os.remove(item)
os.rmdir(directory)
