/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr const char* kType = "mmap.mmap";
constexpr int kAccessDefault = 0;
constexpr int kAccessRead = 1;
constexpr int kAccessWrite = 2;
constexpr int kAccessCopy = 3;

struct State {
  X3PackageHost* host = nullptr;
  X3Value klass = x3_value_invalid();
};

struct Mapping {
  std::mutex mutex;
  unsigned char* data = nullptr;
  uint64_t length = 0;
  uint64_t position = 0;
  int access = kAccessDefault;
  int fd = -1;
  bool closed = false;
#ifdef _WIN32
  HANDLE handle = nullptr;
#endif
  ~Mapping() { close(); }
  void close() {
    if (closed) return;
#ifdef _WIN32
    if (data) UnmapViewOfFile(data);
    if (handle) CloseHandle(handle);
    handle = nullptr;
#else
    if (data) munmap(data, static_cast<size_t>(length));
#endif
    data = nullptr;
    closed = true;
  }
};

void cleanup_mapping(void* value) { delete static_cast<Mapping*>(value); }
void cleanup_state(void* value) {
  auto* state = static_cast<State*>(value);
  if (state->klass.tag != X3_TAG_INVALID) state->host->value_release(state->klass);
  delete state;
}

X3Status fail(State* state, X3CallContext* call, const char* klass,
              const std::string& message) {
  return state->host->raise_class_error(call, klass, message.c_str());
}

bool integer(X3Value value, int64_t& out) {
  if (value.tag == X3_TAG_INT64) {
    out = value.as.i64;
    return true;
  }
  if (value.tag == X3_TAG_UINT64 &&
      value.as.u64 <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    out = static_cast<int64_t>(value.as.u64);
    return true;
  }
  return false;
}

Mapping* mapping(State* state, X3Value value) {
  return static_cast<Mapping*>(state->host->instance_get_native_data(value, kType));
}

#ifdef _WIN32
X3Status native_handle(State* state, X3Runtime* runtime, int fd,
                       HANDLE& result) {
  X3Value importer = x3_value_invalid();
  X3Value name = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value getter = x3_value_invalid();
  X3Value handle = x3_value_invalid();
  X3Status status = state->host->builtin_value(state->host, "__import__", &importer);
  if (status == X3_STATUS_OK) {
    name = state->host->value_string(runtime, "msvcrt");
    status = name.tag == X3_TAG_INVALID ? X3_STATUS_ERROR :
        state->host->call(runtime, importer, &name, 1, &module);
  }
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, module, "get_osfhandle", &getter);
  if (status == X3_STATUS_OK) {
    X3Value descriptor = x3_value_int64(fd);
    status = state->host->call(runtime, getter, &descriptor, 1, &handle);
  }
  int64_t numeric = -1;
  if (status == X3_STATUS_OK && !integer(handle, numeric))
    status = X3_STATUS_ERROR;
  if (status == X3_STATUS_OK)
    result = reinterpret_cast<HANDLE>(static_cast<intptr_t>(numeric));
  for (X3Value value : {importer, name, module, getter, handle})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}
#endif

X3Status checked(State* state, X3CallContext* call, X3Value value,
                 Mapping*& out) {
  out = mapping(state, value);
  if (!out) return fail(state, call, "TypeError", "expected mmap object");
  if (out->closed) return fail(state, call, "ValueError", "mmap closed or invalid");
  return X3_STATUS_OK;
}

X3Status make_mapping(X3CallContext* call, X3Runtime* runtime, State* state,
                      const X3Value* args, uint32_t argc,
                      const X3KeywordArg* kwargs, uint32_t kwargc,
                      X3Value* result) {
  if (argc < 1 || argc > 6)
    return fail(state, call, "TypeError", "mmap() requires a fileno and length");
  X3Value fields[5] = {
      x3_value_invalid(), x3_value_invalid(), x3_value_none(),
      x3_value_int64(kAccessDefault), x3_value_int64(0)};
  const char* names[] = {"fileno", "length", "tagname", "access", "offset"};
  bool supplied[5] = {};
  for (uint32_t i = 1; i < argc; ++i) {
    fields[i - 1] = args[i];
    supplied[i - 1] = true;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    bool found = false;
    for (size_t j = 0; j < 5; ++j) {
      if (kwargs[i].name && std::string(kwargs[i].name) == names[j]) {
        if (supplied[j])
          return fail(state, call, "TypeError", "multiple values for mmap argument");
        fields[j] = kwargs[i].value;
        supplied[j] = found = true;
        break;
      }
    }
    if (!found)
      return fail(state, call, "TypeError", "unexpected mmap keyword argument");
  }
  int64_t fd = -1, length = -1, access = kAccessDefault, offset = 0;
  if (!supplied[0] || !supplied[1] || !integer(fields[0], fd) ||
      !integer(fields[1], length) || !integer(fields[3], access) ||
      !integer(fields[4], offset))
    return fail(state, call, "TypeError", "mmap fileno, length, access and offset must be integers");
  if (length < 0 || offset < 0)
    return fail(state, call, "OverflowError", "mmap length and offset must be non-negative");
  if (access < kAccessDefault || access > kAccessCopy)
    return fail(state, call, "ValueError", "mmap invalid access parameter");
  if (fd < -1 || fd > std::numeric_limits<int>::max())
    return fail(state, call, "OSError", "invalid file descriptor");
  auto data = std::make_unique<Mapping>();
  data->fd = static_cast<int>(fd);
  data->access = static_cast<int>(access);
#ifdef _WIN32
  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  if (offset % system_info.dwAllocationGranularity != 0)
    return fail(state, call, "OSError", "mmap offset must be a multiple of ALLOCATIONGRANULARITY");
  HANDLE file = INVALID_HANDLE_VALUE;
  if (fd >= 0) {
    if (native_handle(state, runtime, static_cast<int>(fd), file) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file, &file_size))
      return fail(state, call, "OSError", "mmap could not determine file size");
    if (length == 0) {
      if (offset >= file_size.QuadPart)
        return fail(state, call, "ValueError", "cannot mmap an empty file");
      length = file_size.QuadPart - offset;
    }
    if (access == kAccessRead && static_cast<uint64_t>(offset) +
        static_cast<uint64_t>(length) > static_cast<uint64_t>(file_size.QuadPart))
      return fail(state, call, "ValueError", "mmap length is greater than file size");
  } else if (length == 0) {
    return fail(state, call, "ValueError", "anonymous mmap length must be positive");
  }
  if (static_cast<uint64_t>(length) >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
      static_cast<uint64_t>(offset) >
          std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(length))
    return fail(state, call, "OverflowError", "mmap length is too large");
  const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(length);
  DWORD protection = access == kAccessRead ? PAGE_READONLY :
                     access == kAccessCopy ? PAGE_WRITECOPY : PAGE_READWRITE;
  DWORD view_access = access == kAccessRead ? FILE_MAP_READ :
                      access == kAccessCopy ? FILE_MAP_COPY : FILE_MAP_WRITE;
  std::wstring tag;
  if (fields[2].tag != X3_TAG_NONE) {
    const char* utf8 = state->host->value_to_cstr(runtime, fields[2]);
    if (!utf8) return fail(state, call, "TypeError", "tagname must be a string or None");
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          utf8, -1, nullptr, 0);
    if (count <= 0) return fail(state, call, "UnicodeError", "invalid tagname");
    tag.resize(static_cast<size_t>(count));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                        tag.data(), count);
  }
  data->handle = CreateFileMappingW(file, nullptr, protection,
      static_cast<DWORD>(end >> 32), static_cast<DWORD>(end),
      tag.empty() ? nullptr : tag.c_str());
  if (!data->handle)
    return fail(state, call, "OSError", "CreateFileMapping failed");
  data->data = static_cast<unsigned char*>(MapViewOfFile(
      data->handle, view_access, static_cast<DWORD>(static_cast<uint64_t>(offset) >> 32),
      static_cast<DWORD>(offset), static_cast<SIZE_T>(length)));
  if (!data->data) return fail(state, call, "OSError", "MapViewOfFile failed");
#else
  if (fields[2].tag != X3_TAG_NONE)
    return fail(state, call, "TypeError", "tagname is only available on Windows");
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0 || offset % page_size != 0)
    return fail(state, call, "OSError", "mmap offset must be page-aligned");
  if (fd >= 0) {
    struct stat statbuf{};
    if (fstat(static_cast<int>(fd), &statbuf) != 0)
      return fail(state, call, "OSError", std::strerror(errno));
    if (length == 0) {
      if (offset >= statbuf.st_size)
        return fail(state, call, "ValueError", "cannot mmap an empty file");
      length = statbuf.st_size - offset;
    }
    if (access == kAccessRead && offset + length > statbuf.st_size)
      return fail(state, call, "ValueError", "mmap length is greater than file size");
  } else if (length == 0) {
    return fail(state, call, "ValueError", "anonymous mmap length must be positive");
  }
  if (static_cast<uint64_t>(length) >
      static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return fail(state, call, "OverflowError", "mmap length is too large");
  const int protection = access == kAccessRead ? PROT_READ : PROT_READ | PROT_WRITE;
  const int flags = (access == kAccessCopy ? MAP_PRIVATE : MAP_SHARED) |
                    (fd < 0 ? MAP_ANONYMOUS : 0);
  void* view = ::mmap(nullptr, static_cast<size_t>(length), protection,
                      flags, static_cast<int>(fd), static_cast<off_t>(offset));
  if (view == MAP_FAILED)
    return fail(state, call, "OSError", std::strerror(errno));
  data->data = static_cast<unsigned char*>(view);
#endif
  data->length = static_cast<uint64_t>(length);
  if (state->host->instance_set_native_data(args[0], kType, data.get(),
                                             cleanup_mapping) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status init(X3CallContext* call, X3Runtime* runtime, void* user_data,
              const X3Value* args, uint32_t argc, X3Value* result) {
  return make_mapping(call, runtime, static_cast<State*>(user_data),
                      args, argc, nullptr, 0, result);
}
X3Status init_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                 const X3Value* args, uint32_t argc,
                 const X3KeywordArg* kwargs, uint32_t kwargc,
                 X3Value* result) {
  return make_mapping(call, runtime, static_cast<State*>(user_data),
                      args, argc, kwargs, kwargc, result);
}

X3Status length_method(X3CallContext* call, X3Runtime*, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  Mapping* data = nullptr;
  if (argc != 1 || checked(state, call, args[0], data) != X3_STATUS_OK)
    return argc != 1 ? fail(state, call, "TypeError", "mmap.__len__() takes no arguments") : X3_STATUS_ERROR;
  *result = x3_value_int64(static_cast<int64_t>(data->length));
  return X3_STATUS_OK;
}

X3Status tell_method(X3CallContext* call, X3Runtime*, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  Mapping* data = nullptr;
  if (argc != 1 || checked(state, call, args[0], data) != X3_STATUS_OK)
    return argc != 1 ? fail(state, call, "TypeError", "mmap.tell() takes no arguments") : X3_STATUS_ERROR;
  std::lock_guard lock(data->mutex);
  *result = x3_value_int64(static_cast<int64_t>(data->position));
  return X3_STATUS_OK;
}

X3Status seek_method(X3CallContext* call, X3Runtime*, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  Mapping* data = nullptr;
  if (argc < 2 || argc > 3 || checked(state, call, args[0], data) != X3_STATUS_OK)
    return argc < 2 || argc > 3 ? fail(state, call, "TypeError", "mmap.seek() expects pos and whence") : X3_STATUS_ERROR;
  int64_t offset = 0, whence = 0;
  if (!integer(args[1], offset) || (argc == 3 && !integer(args[2], whence)))
    return fail(state, call, "TypeError", "seek position and whence must be integers");
  std::lock_guard lock(data->mutex);
  int64_t base = whence == 0 ? 0 : whence == 1 ?
      static_cast<int64_t>(data->position) : whence == 2 ?
      static_cast<int64_t>(data->length) : -1;
  if (base < 0) return fail(state, call, "ValueError", "unknown seek type");
  if ((offset > 0 && base > std::numeric_limits<int64_t>::max() - offset) ||
      (offset < 0 && base < -offset))
    return fail(state, call, "ValueError", "seek out of range");
  const int64_t position = base + offset;
  if (position < 0 || static_cast<uint64_t>(position) > data->length)
    return fail(state, call, "ValueError", "seek out of range");
  data->position = static_cast<uint64_t>(position);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status read_method(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  Mapping* data = nullptr;
  if (argc < 1 || argc > 2 || checked(state, call, args[0], data) != X3_STATUS_OK)
    return argc < 1 || argc > 2 ? fail(state, call, "TypeError", "mmap.read() takes optional size") : X3_STATUS_ERROR;
  int64_t requested = -1;
  if (argc == 2 && !integer(args[1], requested))
    return fail(state, call, "TypeError", "read size must be integer");
  std::lock_guard lock(data->mutex);
  const uint64_t available = data->length - data->position;
  const uint64_t count = requested < 0 ? available :
      std::min(available, static_cast<uint64_t>(requested));
  *result = state->host->value_bytes(runtime, data->data + data->position, count);
  if (result->tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  data->position += count;
  return X3_STATUS_OK;
}

X3Status write_method(X3CallContext* call, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  Mapping* data = nullptr;
  if (argc != 2 || checked(state, call, args[0], data) != X3_STATUS_OK)
    return argc != 2 ? fail(state, call, "TypeError", "mmap.write() expects bytes") : X3_STATUS_ERROR;
  if (data->access == kAccessRead)
    return fail(state, call, "TypeError", "mmap can't modify a readonly memory map");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) != X3_STATUS_OK)
    return fail(state, call, "TypeError", "mmap.write() requires a bytes-like object");
  std::lock_guard lock(data->mutex);
  if (info.size > data->length - data->position) {
    state->host->buffer_release(buffer);
    return fail(state, call, "ValueError", "data out of range");
  }
  std::memcpy(data->data + data->position, info.data, static_cast<size_t>(info.size));
  data->position += info.size;
  state->host->buffer_release(buffer);
  *result = x3_value_int64(static_cast<int64_t>(info.size));
  return X3_STATUS_OK;
}

X3Status flush_method(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  Mapping* data = nullptr;
  if (argc != 1 || checked(state, call, args[0], data) != X3_STATUS_OK)
    return argc != 1 ? fail(state, call, "TypeError", "mmap.flush() takes no arguments") : X3_STATUS_ERROR;
  std::lock_guard lock(data->mutex);
#ifdef _WIN32
  if (data->access != kAccessCopy && !FlushViewOfFile(data->data, static_cast<SIZE_T>(data->length)))
    return fail(state, call, "OSError", "FlushViewOfFile failed");
#else
  if (data->access != kAccessCopy &&
      msync(data->data, static_cast<size_t>(data->length), MS_SYNC) != 0)
    return fail(state, call, "OSError", std::strerror(errno));
#endif
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status close_method(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "mmap.close() takes no arguments");
  Mapping* data = mapping(state, args[0]);
  if (!data) return fail(state, call, "TypeError", "expected mmap object");
  std::lock_guard lock(data->mutex);
  data->close();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status exit_method(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  if (argc != 4)
    return fail(static_cast<State*>(user_data), call, "TypeError", "__exit__ expects exception arguments");
  const X3Value self[] = {args[0]};
  return close_method(call, runtime, user_data, self, 1, result);
}

X3Status enter_method(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  Mapping* data = nullptr;
  if (argc != 1 || checked(state, call, args[0], data) != X3_STATUS_OK)
    return argc != 1 ? fail(state, call, "TypeError", "__enter__ takes no arguments") : X3_STATUS_ERROR;
  state->host->value_retain(args[0]);
  *result = args[0];
  return X3_STATUS_OK;
}

X3Status closed_property(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<State*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "closed getter arguments");
  Mapping* data = mapping(state, args[0]);
  if (!data) return fail(state, call, "TypeError", "expected mmap object");
  *result = x3_value_bool(data->closed);
  return X3_STATUS_OK;
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new State();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_state) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "mmap", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", init, state, 1, 6, 0, init_kw},
      {sizeof(X3NativeFunctionDef), "__len__", length_method, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "tell", tell_method, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "seek", seek_method, state, 2, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "read", read_method, state, 1, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "write", write_method, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "flush", flush_method, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "close", close_method, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__enter__", enter_method, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__exit__", exit_method, state, 4, 4, 0, nullptr},
  };
  if (host->module_add_class(module, "mmap", methods, 10,
                             &state->klass) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value descriptor = x3_value_invalid();
  if (host->property_create(host->runtime, "closed", closed_property, nullptr,
                            state, &descriptor) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status property_status = host->class_add_value(
      state->klass, "closed", descriptor);
  host->value_release(descriptor);
  if (property_status != X3_STATUS_OK) return X3_STATUS_ERROR;
  int64_t page_size = 4096;
  int64_t allocation_granularity = 4096;
#ifdef _WIN32
  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  page_size = system_info.dwPageSize;
  allocation_granularity = system_info.dwAllocationGranularity;
#else
  const long native_page_size = sysconf(_SC_PAGESIZE);
  if (native_page_size > 0) page_size = allocation_granularity = native_page_size;
#endif
  for (const auto& constant : {
           std::pair<const char*, int64_t>{"ACCESS_DEFAULT", kAccessDefault},
           {"ACCESS_READ", kAccessRead}, {"ACCESS_WRITE", kAccessWrite},
           {"ACCESS_COPY", kAccessCopy}, {"PAGESIZE", page_size},
           {"ALLOCATIONGRANULARITY", allocation_granularity}
       }) {
    if (host->module_add_value(module, constant.first,
                               x3_value_int64(constant.second)) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  }
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version =
    X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "mmap");
  return register_module(host);
}
