/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iptypes.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winioctl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct State { X3PackageHost* host; };

void cleanup(void* data) { delete static_cast<State*>(data); }

X3Status os_error(State* state, X3CallContext* call, const char* operation,
                  DWORD code) {
  return state->host->raise_class_error(
      call, "OSError", (std::string(operation) + " failed with Windows error " +
                        std::to_string(code)).c_str());
}

X3Status append(State* state, X3Value list, X3Value item) {
  const X3Status status = state->host->list_append(state->host->runtime, list, item);
  if (item.tag == X3_TAG_OBJECT) state->host->value_release(item);
  return status;
}

X3Value text(State* state, const std::string& value) {
  return state->host->value_string_utf8(
      state->host->runtime, value.data(), static_cast<uint64_t>(value.size()));
}

X3Value optional_text(State* state, const std::string& value) {
  return value.empty() ? x3_value_none() : text(state, value);
}

X3Value as_tuple(State* state, X3Value list) {
  X3Value tuple_type = x3_value_invalid();
  X3Value tuple = x3_value_invalid();
  if (state->host->builtin_value(state->host, "tuple", &tuple_type) == X3_STATUS_OK)
    state->host->call(state->host->runtime, tuple_type, &list, 1, &tuple);
  if (tuple_type.tag == X3_TAG_OBJECT) state->host->value_release(tuple_type);
  state->host->value_release(list);
  return tuple;
}

std::string utf8(const wchar_t* source) {
  if (!source || !*source) return {};
  const int length = WideCharToMultiByte(CP_UTF8, 0, source, -1, nullptr, 0,
                                        nullptr, nullptr);
  if (length <= 1) return {};
  std::string result(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, source, -1, result.data(), length,
                      nullptr, nullptr);
  result.pop_back();
  return result;
}

X3Status pids(X3CallContext* call, X3Runtime*, void* user_data,
              const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 0) return state->host->raise_class_error(call, "TypeError", "pids() takes no arguments");
  std::vector<DWORD> ids(1024);
  DWORD bytes = 0;
  for (;;) {
    if (!EnumProcesses(ids.data(), static_cast<DWORD>(ids.size() * sizeof(DWORD)), &bytes))
      return os_error(state, call, "EnumProcesses", GetLastError());
    if (bytes < ids.size() * sizeof(DWORD)) break;
    ids.resize(ids.size() * 2);
  }
  X3Value list = state->host->value_list(state->host->runtime);
  if (list.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  for (size_t i = 0; i < bytes / sizeof(DWORD); ++i) {
    if (append(state, list, x3_value_int64(ids[i])) != X3_STATUS_OK) {
      state->host->value_release(list);
      return X3_STATUS_ERROR;
    }
  }
  *result = list;
  return X3_STATUS_OK;
}

X3Status pid_exists(X3CallContext* call, X3Runtime*, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 1 || args[0].tag != X3_TAG_INT64)
    return state->host->raise_class_error(call, "TypeError", "pid_exists() expects a PID");
  const int64_t value = args[0].as.i64;
  if (value < 0 || value > UINT32_MAX) { *result = x3_value_bool(false); return X3_STATUS_OK; }
  if (value == 0) { *result = x3_value_bool(true); return X3_STATUS_OK; }
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(value));
  if (process) {
    DWORD exit_code = 0;
    const bool exists = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(process);
    *result = x3_value_bool(exists);
    return X3_STATUS_OK;
  }
  if (GetLastError() == ERROR_ACCESS_DENIED) {
    // Access-denied system processes are still present in EnumProcesses.
    std::vector<DWORD> ids(1024);
    DWORD bytes = 0;
    do {
      if (!EnumProcesses(ids.data(), static_cast<DWORD>(ids.size() * sizeof(DWORD)), &bytes)) break;
      if (bytes < ids.size() * sizeof(DWORD)) break;
      ids.resize(ids.size() * 2);
    } while (true);
    *result = x3_value_bool(std::find(ids.begin(), ids.begin() + bytes / sizeof(DWORD),
                                      static_cast<DWORD>(value)) != ids.begin() + bytes / sizeof(DWORD));
    return X3_STATUS_OK;
  }
  *result = x3_value_bool(false);
  return X3_STATUS_OK;
}

X3Status ppid_map(X3CallContext* call, X3Runtime*, void* user_data,
                  const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 0) return state->host->raise_class_error(call, "TypeError", "ppid_map() takes no arguments");
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return os_error(state, call, "CreateToolhelp32Snapshot", GetLastError());
  X3Value map = state->host->value_dict(state->host->runtime);
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (state->host->dict_set_item(state->host->runtime, map,
              x3_value_int64(entry.th32ProcessID),
              x3_value_int64(entry.th32ParentProcessID)) != X3_STATUS_OK) {
        CloseHandle(snapshot);
        state->host->value_release(map);
        return X3_STATUS_ERROR;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  *result = map;
  return X3_STATUS_OK;
}

X3Status net_if_addrs(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 0) return state->host->raise_class_error(call, "TypeError", "net_if_addrs() takes no arguments");
  ULONG length = 16384;
  std::vector<unsigned char> buffer(length);
  ULONG code;
  do {
    code = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr,
                                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &length);
    if (code == ERROR_BUFFER_OVERFLOW) buffer.resize(length);
  } while (code == ERROR_BUFFER_OVERFLOW);
  if (code != NO_ERROR) return os_error(state, call, "GetAdaptersAddresses", code);
  X3Value list = state->host->value_list(state->host->runtime);
  if (list.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  auto row = [&](const std::string& name, int family, const std::string& address,
                 const std::string& mask) -> bool {
    X3Value item = state->host->value_list(state->host->runtime);
    if (item.tag == X3_TAG_INVALID) return false;
    const bool okay = append(state, item, text(state, name)) == X3_STATUS_OK &&
        append(state, item, x3_value_int64(family)) == X3_STATUS_OK &&
        append(state, item, text(state, address)) == X3_STATUS_OK &&
        append(state, item, optional_text(state, mask)) == X3_STATUS_OK &&
        append(state, item, x3_value_none()) == X3_STATUS_OK &&
        append(state, item, x3_value_none()) == X3_STATUS_OK;
    if (!okay) {
      state->host->value_release(item);
      return false;
    }
    X3Value tuple = as_tuple(state, item);
    return tuple.tag != X3_TAG_INVALID && append(state, list, tuple) == X3_STATUS_OK;
  };
  for (auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
       adapter; adapter = adapter->Next) {
    const std::string name = utf8(adapter->FriendlyName);
    if (adapter->PhysicalAddressLength) {
      char mac[3 * MAX_ADAPTER_ADDRESS_LENGTH] = {};
      size_t position = 0;
      for (ULONG i = 0; i < adapter->PhysicalAddressLength; ++i) {
        position += static_cast<size_t>(std::snprintf(
            mac + position, sizeof(mac) - position, i ? "-%02X" : "%02X",
            adapter->PhysicalAddress[i]));
      }
      if (!row(name, -1, mac, {})) { state->host->value_release(list); return X3_STATUS_ERROR; }
    }
    for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next) {
      const int family = address->Address.lpSockaddr->sa_family;
      if (family != AF_INET && family != AF_INET6) continue;
      wchar_t wide[INET6_ADDRSTRLEN + 1] = {};
      const void* addr = family == AF_INET
          ? static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr)->sin_addr)
          : static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(address->Address.lpSockaddr)->sin6_addr);
      if (!InetNtopW(family, const_cast<void*>(addr), wide, INET6_ADDRSTRLEN + 1)) continue;
      std::string mask;
      if (family == AF_INET) {
        const ULONG prefix = std::min<ULONG>(address->OnLinkPrefixLength, 32);
        const uint32_t bits = prefix ? ~uint32_t(0) << (32 - prefix) : 0;
        mask = std::to_string((bits >> 24) & 255) + "." +
               std::to_string((bits >> 16) & 255) + "." +
               std::to_string((bits >> 8) & 255) + "." +
               std::to_string(bits & 255);
      }
      if (!row(name, family, utf8(wide), mask)) {
        state->host->value_release(list);
        return X3_STATUS_ERROR;
      }
    }
  }
  *result = list;
  return X3_STATUS_OK;
}

X3Status heap_info(X3CallContext* call, X3Runtime*, void* user_data,
                   const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 0) return state->host->raise_class_error(call, "TypeError", "heap_info() takes no arguments");
  const DWORD heap_count = GetProcessHeaps(0, nullptr);
  if (!heap_count) return os_error(state, call, "GetProcessHeaps", GetLastError());
  std::vector<HANDLE> heaps(heap_count);
  if (!GetProcessHeaps(heap_count, heaps.data()))
    return os_error(state, call, "GetProcessHeaps", GetLastError());
  uint64_t allocated = 0;
  for (HANDLE heap : heaps) {
    if (!HeapLock(heap)) continue;
    PROCESS_HEAP_ENTRY entry{};
    while (HeapWalk(heap, &entry))
      if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) allocated += entry.cbData;
    HeapUnlock(heap);
  }
  uint64_t mmap_used = 0;
  MEMORY_BASIC_INFORMATION info{};
  uintptr_t address = 0;
  while (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
    if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE &&
        (info.AllocationProtect & PAGE_READWRITE) &&
        std::find(heaps.begin(), heaps.end(), info.AllocationBase) == heaps.end())
      mmap_used += info.RegionSize;
    const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (next <= address) break;
    address = next;
  }
  X3Value list = state->host->value_list(state->host->runtime);
  append(state, list, x3_value_uint64(allocated));
  append(state, list, x3_value_uint64(mmap_used));
  append(state, list, x3_value_uint64(heap_count));
  *result = as_tuple(state, list);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status heap_trim(X3CallContext* call, X3Runtime*, void* user_data,
                   const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 0) return state->host->raise_class_error(call, "TypeError", "heap_trim() takes no arguments");
  SetLastError(ERROR_SUCCESS);
  const SIZE_T size = HeapCompact(GetProcessHeap(), 0);
  if (!size && GetLastError() != ERROR_SUCCESS)
    return os_error(state, call, "HeapCompact", GetLastError());
  *result = x3_value_uint64(size);
  return X3_STATUS_OK;
}

X3Status disk_io_counters(X3CallContext* call, X3Runtime*, void* user_data,
                          const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 0) return state->host->raise_class_error(call, "TypeError", "disk_io_counters() takes no arguments");
  X3Value map = state->host->value_dict(state->host->runtime);
  if (map.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  for (unsigned index = 0; index < 64; ++index) {
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(index);
    HANDLE disk = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (disk == INVALID_HANDLE_VALUE) continue;
    DISK_PERFORMANCE performance{};
    DWORD bytes = 0;
    const BOOL ok = DeviceIoControl(disk, IOCTL_DISK_PERFORMANCE, nullptr, 0,
                                    &performance, sizeof(performance), &bytes, nullptr);
    CloseHandle(disk);
    if (!ok) continue;
    X3Value fields = state->host->value_list(state->host->runtime);
    for (uint64_t value : {static_cast<uint64_t>(performance.ReadCount),
                           static_cast<uint64_t>(performance.WriteCount),
                           static_cast<uint64_t>(performance.BytesRead.QuadPart),
                           static_cast<uint64_t>(performance.BytesWritten.QuadPart),
                           static_cast<uint64_t>(performance.ReadTime.QuadPart / 10000000),
                           static_cast<uint64_t>(performance.WriteTime.QuadPart / 10000000)})
      append(state, fields, x3_value_uint64(value));
    X3Value tuple = as_tuple(state, fields);
    X3Value key = text(state, "PhysicalDrive" + std::to_string(index));
    const X3Status status = state->host->dict_set_item(state->host->runtime, map, key, tuple);
    state->host->value_release(key);
    state->host->value_release(tuple);
    if (status != X3_STATUS_OK) { state->host->value_release(map); return status; }
  }
  *result = map;
  return X3_STATUS_OK;
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new State{host};
  if (host->package_set_cleanup(host, state, cleanup) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "psutil._psutil_windows", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef functions[] = {
      {sizeof(X3NativeFunctionDef), "pids", pids, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "pid_exists", pid_exists, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "ppid_map", ppid_map, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "net_if_addrs", net_if_addrs, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "heap_info", heap_info, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "heap_trim", heap_trim, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "disk_io_counters", disk_io_counters, state, 0, 0, 0, nullptr},
  };
  for (const auto& function : functions)
    if (host->module_add_function(module, &function) != X3_STATUS_OK) return X3_STATUS_ERROR;
  for (const auto& entry : {
           std::pair<const char*, int64_t>{"version", 722},
           {"ABOVE_NORMAL_PRIORITY_CLASS", ABOVE_NORMAL_PRIORITY_CLASS},
           {"BELOW_NORMAL_PRIORITY_CLASS", BELOW_NORMAL_PRIORITY_CLASS},
           {"HIGH_PRIORITY_CLASS", HIGH_PRIORITY_CLASS},
           {"IDLE_PRIORITY_CLASS", IDLE_PRIORITY_CLASS},
           {"NORMAL_PRIORITY_CLASS", NORMAL_PRIORITY_CLASS},
           {"REALTIME_PRIORITY_CLASS", REALTIME_PRIORITY_CLASS},
           {"MIB_TCP_STATE_CLOSED", MIB_TCP_STATE_CLOSED},
           {"MIB_TCP_STATE_LISTEN", MIB_TCP_STATE_LISTEN},
           {"MIB_TCP_STATE_SYN_SENT", MIB_TCP_STATE_SYN_SENT},
           {"MIB_TCP_STATE_SYN_RCVD", MIB_TCP_STATE_SYN_RCVD},
           {"MIB_TCP_STATE_ESTAB", MIB_TCP_STATE_ESTAB},
           {"MIB_TCP_STATE_FIN_WAIT1", MIB_TCP_STATE_FIN_WAIT1},
           {"MIB_TCP_STATE_FIN_WAIT2", MIB_TCP_STATE_FIN_WAIT2},
           {"MIB_TCP_STATE_CLOSE_WAIT", MIB_TCP_STATE_CLOSE_WAIT},
           {"MIB_TCP_STATE_CLOSING", MIB_TCP_STATE_CLOSING},
           {"MIB_TCP_STATE_LAST_ACK", MIB_TCP_STATE_LAST_ACK},
           {"MIB_TCP_STATE_TIME_WAIT", MIB_TCP_STATE_TIME_WAIT},
           {"MIB_TCP_STATE_DELETE_TCB", MIB_TCP_STATE_DELETE_TCB},
           {"PSUTIL_CONN_NONE", 128},
           {"ERROR_ACCESS_DENIED", ERROR_ACCESS_DENIED},
           {"ERROR_INVALID_NAME", ERROR_INVALID_NAME},
           {"ERROR_SERVICE_DOES_NOT_EXIST", ERROR_SERVICE_DOES_NOT_EXIST},
           {"ERROR_PRIVILEGE_NOT_HELD", ERROR_PRIVILEGE_NOT_HELD},
           {"INFINITE", INFINITE},
       })
    if (host->module_add_value(module, entry.first, x3_value_int64(entry.second)) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version =
    X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "psutil._psutil_windows");
  return register_module(host);
}
