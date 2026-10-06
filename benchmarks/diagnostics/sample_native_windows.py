"""Sample native instruction pointers of a child process on Windows.

This is a user-mode fallback for CPU-stack profilers that require the
SeSystemProfilePrivilege. It suspends each target thread briefly, records the
x64 instruction pointer, then resumes it. The resulting sample profile is
diagnostic only and must never be used as a timing result.
"""

import argparse
import ctypes
import json
import struct
import subprocess
import time
from collections import Counter
from pathlib import Path


DWORD = ctypes.c_uint32
BOOL = ctypes.c_int
HANDLE = ctypes.c_void_p
TH32CS_SNAPTHREAD = 0x00000004
THREAD_SUSPEND_RESUME = 0x0002
THREAD_GET_CONTEXT = 0x0008
THREAD_QUERY_INFORMATION = 0x0040
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
CONTEXT_CONTROL_INTEGER = 0x00100003
AMD64_CONTEXT_SIZE = 1232
AMD64_RIP_OFFSET = 248


class ThreadEntry32(ctypes.Structure):
    _fields_ = [
        ("dwSize", DWORD),
        ("cntUsage", DWORD),
        ("th32ThreadID", DWORD),
        ("th32OwnerProcessID", DWORD),
        ("tpBasePri", ctypes.c_int32),
        ("tpDeltaPri", ctypes.c_int32),
        ("dwFlags", DWORD),
    ]


class ModuleInfo(ctypes.Structure):
    _fields_ = [
        ("lpBaseOfDll", ctypes.c_void_p),
        ("SizeOfImage", DWORD),
        ("EntryPoint", ctypes.c_void_p),
    ]


class FileTime(ctypes.Structure):
    _fields_ = [("low", DWORD), ("high", DWORD)]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
kernel32.CreateToolhelp32Snapshot.argtypes = [DWORD, DWORD]
kernel32.CreateToolhelp32Snapshot.restype = HANDLE
kernel32.Thread32First.argtypes = [HANDLE, ctypes.POINTER(ThreadEntry32)]
kernel32.Thread32First.restype = BOOL
kernel32.Thread32Next.argtypes = [HANDLE, ctypes.POINTER(ThreadEntry32)]
kernel32.Thread32Next.restype = BOOL
kernel32.OpenThread.argtypes = [DWORD, BOOL, DWORD]
kernel32.OpenThread.restype = HANDLE
kernel32.SuspendThread.argtypes = [HANDLE]
kernel32.SuspendThread.restype = DWORD
kernel32.ResumeThread.argtypes = [HANDLE]
kernel32.ResumeThread.restype = DWORD
kernel32.GetThreadContext.argtypes = [HANDLE, ctypes.c_void_p]
kernel32.GetThreadContext.restype = BOOL
kernel32.OpenProcess.argtypes = [DWORD, BOOL, DWORD]
kernel32.OpenProcess.restype = HANDLE
kernel32.CloseHandle.argtypes = [HANDLE]
kernel32.CloseHandle.restype = BOOL
kernel32.GetThreadTimes.argtypes = [HANDLE, ctypes.POINTER(FileTime),
                                    ctypes.POINTER(FileTime), ctypes.POINTER(FileTime),
                                    ctypes.POINTER(FileTime)]
kernel32.GetThreadTimes.restype = BOOL
psapi.EnumProcessModules.argtypes = [HANDLE, ctypes.POINTER(HANDLE), DWORD,
                                     ctypes.POINTER(DWORD)]
psapi.EnumProcessModules.restype = BOOL
psapi.GetModuleInformation.argtypes = [HANDLE, HANDLE,
                                       ctypes.POINTER(ModuleInfo), DWORD]
psapi.GetModuleInformation.restype = BOOL
psapi.GetModuleFileNameExW.argtypes = [HANDLE, HANDLE, ctypes.c_wchar_p, DWORD]
psapi.GetModuleFileNameExW.restype = DWORD


def win_error(operation):
    return OSError(ctypes.get_last_error(), operation)


def target_modules(process_handle):
    capacity = 128
    while True:
        modules = (HANDLE * capacity)()
        needed = DWORD()
        if psapi.EnumProcessModules(process_handle, modules, ctypes.sizeof(modules),
                                   ctypes.byref(needed)):
            break
        error = ctypes.get_last_error()
        if error == 299:  # Module list changed while the child was starting.
            return None
        if needed.value > ctypes.sizeof(modules):
            capacity = needed.value // ctypes.sizeof(HANDLE) + 16
            continue
        raise OSError(error, "EnumProcessModules")
    result = []
    count = min(needed.value // ctypes.sizeof(HANDLE), len(modules))
    for module in modules[:count]:
        info = ModuleInfo()
        if not psapi.GetModuleInformation(process_handle, module,
                                          ctypes.byref(info), ctypes.sizeof(info)):
            continue
        filename = ctypes.create_unicode_buffer(32768)
        if not psapi.GetModuleFileNameExW(process_handle, module, filename,
                                         len(filename)):
            continue
        result.append({"path": filename.value,
                       "base": int(info.lpBaseOfDll),
                       "size": int(info.SizeOfImage)})
    return result


def process_threads(pid):
    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    invalid_handle = ctypes.c_void_p(-1).value
    if snapshot == invalid_handle:
        raise win_error("CreateToolhelp32Snapshot")
    try:
        entry = ThreadEntry32()
        entry.dwSize = ctypes.sizeof(entry)
        found = kernel32.Thread32First(snapshot, ctypes.byref(entry))
        while found:
            if entry.th32OwnerProcessID == pid:
                yield int(entry.th32ThreadID)
            found = kernel32.Thread32Next(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)


def open_thread(thread_id):
    access = THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION
    return kernel32.OpenThread(access, False, thread_id)


def thread_cpu_time(thread):
    creation, exit_time = FileTime(), FileTime()
    kernel, user = FileTime(), FileTime()
    if not kernel32.GetThreadTimes(thread, ctypes.byref(creation), ctypes.byref(exit_time),
                                   ctypes.byref(kernel), ctypes.byref(user)):
        return None
    return ((kernel.high << 32) | kernel.low) + ((user.high << 32) | user.low)


def sample_thread_handle(thread):
    if kernel32.SuspendThread(thread) == 0xFFFFFFFF:
        return None
    try:
        context = ctypes.create_string_buffer(AMD64_CONTEXT_SIZE)
        struct.pack_into("<I", context, 48, CONTEXT_CONTROL_INTEGER)
        if not kernel32.GetThreadContext(thread, context):
            return None
        return struct.unpack_from("<Q", context, AMD64_RIP_OFFSET)[0]
    finally:
        kernel32.ResumeThread(thread)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--interval-ms", type=float, default=5.0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.command:
        parser.error("provide a child command after --")
    command = args.command[1:] if args.command[0] == "--" else args.command
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        parser.error("the sampler requires 64-bit Python on 64-bit Windows")

    child = subprocess.Popen(command)
    process_handle = kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, child.pid)
    if not process_handle:
        child.kill()
        raise win_error("OpenProcess")
    samples = Counter()
    failures = Counter()
    modules = []
    thread_handles = {}
    prior_cpu_times = {}
    try:
        deadline = time.perf_counter()
        while child.poll() is None:
            if not modules:
                modules = target_modules(process_handle) or []
                if not modules:
                    time.sleep(0.02)
                    continue
            for tid in process_threads(child.pid):
                if tid not in thread_handles:
                    thread = open_thread(tid)
                    if thread:
                        thread_handles[tid] = thread
            cpu_deltas = []
            for tid, thread in thread_handles.items():
                cpu_time = thread_cpu_time(thread)
                if cpu_time is not None:
                    previous = prior_cpu_times.get(tid, cpu_time)
                    cpu_deltas.append((cpu_time - previous, cpu_time, tid, thread))
                    prior_cpu_times[tid] = cpu_time
            if cpu_deltas:
                # Sample the thread that did the most CPU work in this window;
                # sampling every idle event-loop/helper thread would drown the
                # interpreter's executing thread in wait frames.
                _, _, tid, thread = max(cpu_deltas)
                for _ in range(10):
                    pc = sample_thread_handle(thread)
                    if pc is None:
                        failures["context_unavailable"] += 1
                        continue
                    for module in modules:
                        if module["base"] <= pc < module["base"] + module["size"]:
                            samples[(tid, module["path"], pc - module["base"])] += 1
                            break
                    time.sleep(args.interval_ms / 1000.0)
            else:
                failures["no_threads"] += 1
                time.sleep(0.05)
            deadline += 0.05
            time.sleep(max(0.0, deadline - time.perf_counter()))
        return_code = child.wait()
    finally:
        for thread in thread_handles.values():
            kernel32.CloseHandle(thread)
        kernel32.CloseHandle(process_handle)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    rows = [{"thread_id": tid, "module": path, "rva": hex(rva), "samples": count}
            for (tid, path, rva), count in samples.most_common()]
    args.output.write_text(json.dumps({
        "command": command,
        "return_code": return_code,
        "sample_interval_ms": args.interval_ms,
        "modules": modules,
        "sample_count": sum(samples.values()),
        "failures": dict(failures),
        "samples": rows,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"exit={return_code} samples={sum(samples.values())} "
          f"unavailable={failures['context_unavailable']} output={args.output}")
    for row in rows[:20]:
        print(f"{row['samples']:6d} tid={row['thread_id']} {row['module']}+{row['rva']}")
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
