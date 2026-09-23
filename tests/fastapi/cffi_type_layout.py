"""Check native CFFI type layouts against Trio's unchanged generated cdefs."""

import os
import runpy
import sys


generated = None
for root in sys.path:
    candidate = os.path.join(root, "trio", "_core", "_generated_windows_ffi.py")
    if os.path.isfile(candidate):
        generated = candidate
        break
if generated is None:
    raise RuntimeError("Trio's generated Windows CFFI definitions are unavailable")

ffi = runpy.run_path(generated)["ffi"]
for ctype in (
    "AFD_POLL_HANDLE_INFO",
    "AFD_POLL_INFO",
    "BOOL",
    "BOOLEAN",
    "BYTE",
    "DWORD",
    "HANDLE",
    "LARGE_INTEGER",
    "LPCSTR",
    "LPCVOID",
    "LPCWSTR",
    "LPDWORD",
    "LPOVERLAPPED",
    "LPOVERLAPPED_ENTRY",
    "LPSECURITY_ATTRIBUTES",
    "LPVOID",
    "LPWSAOVERLAPPED",
    "NTSTATUS",
    "OVERLAPPED",
    "OVERLAPPED_ENTRY",
    "PAFD_POLL_HANDLE_INFO",
    "PAFD_POLL_INFO",
    "PULONG",
    "PVOID",
    "SOCKET",
    "UCHAR",
    "UINT_PTR",
    "ULONG",
    "ULONG_PTR",
    "WSAOVERLAPPED",
    "u_long",
    "HANDLE[3]",
    "DWORD *",
    "DWORD[2]",
    "struct _OVERLAPPED",
    "struct _AFD_POLL_INFO",
    "const void *",
):
    print(ctype, ffi.sizeof(ctype))

try:
    ffi.sizeof("not_declared *")
except Exception:
    print("undeclared pointer rejected")

kernel32 = ffi.dlopen("kernel32.dll")
event = kernel32.CreateEventA(ffi.NULL, False, False, ffi.NULL)
print("event created", bool(event))
print("event closed", kernel32.CloseHandle(event))

port = kernel32.CreateIoCompletionPort(ffi.cast("HANDLE", -1), ffi.NULL, 0, 0)
print("completion port created", bool(port))
print("completion port closed", kernel32.CloseHandle(port))

handle_slot = ffi.new("HANDLE *")
print("zeroed handle", handle_slot[0] == ffi.NULL)
dword_slot = ffi.new("DWORD *", 7)
print("initialized dword", dword_slot[0])
handle_array = ffi.new("HANDLE[2]")
print("zeroed handle array", handle_array[1] == ffi.NULL)
print("Windows error", ffi.getwinerror(6))
