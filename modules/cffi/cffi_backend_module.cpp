/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <cstdint>
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef XLANG_CFFI_HAS_LIBFFI
#include "ffi.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

constexpr const char* kCDataType = "_cffi_backend.CData";
constexpr const char* kFfiType = "_cffi_backend.FFI";
constexpr const char* kLibraryType = "_cffi_backend.Library";

struct PackageState;

struct FieldDescription {
  uint32_t type_op = 0;
  std::string name;
};

struct StructDescription {
  uint32_t type_index = 0;
  uint32_t flags = 0;
  std::string name;
  std::vector<FieldDescription> fields;
};

struct GlobalDescription {
  uint32_t type_op = 0;
  std::string name;
};

struct FfiState {
  PackageState* package = nullptr;
  std::string module_name;
  std::vector<uint32_t> types;
  std::unordered_map<std::string, uint32_t> typenames;
  std::vector<uint32_t> typename_indices;
  std::vector<StructDescription> structs;
  std::unordered_map<std::string, GlobalDescription> globals;
};

struct CDataState {
  PackageState* package = nullptr;
  std::string ctype;
  uintptr_t address = 0;
  X3Value owner_value = x3_value_invalid();
  std::shared_ptr<FfiState> ffi;
  uint32_t function_index = UINT32_MAX;
  std::unique_ptr<unsigned char[]> owned;
  size_t owned_size = 0;
  size_t element_size = 0;
  std::string element_name;
};

struct LibraryState {
  PackageState* package = nullptr;
#ifdef _WIN32
  HMODULE handle = nullptr;
#else
  void* handle = nullptr;
#endif
  std::string name;
  std::shared_ptr<FfiState> ffi;
};

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value ffi_class = x3_value_invalid();
  X3Value cdata_class = x3_value_invalid();
  X3Value ctype_class = x3_value_invalid();
  X3Value library_class = x3_value_invalid();
  X3Value null_value = x3_value_invalid();
};

#ifdef _WIN32
thread_local DWORD cffi_last_error = 0;
#endif

void cleanup_ffi(void* pointer) { delete static_cast<FfiState*>(pointer); }
void cleanup_cdata(void* pointer) {
  auto* state = static_cast<CDataState*>(pointer);
  if (!state) return;
  if (state->owner_value.tag != X3_TAG_INVALID)
    state->package->host->value_release(state->owner_value);
  delete state;
}
void cleanup_library(void* pointer) {
  auto* state = static_cast<LibraryState*>(pointer);
  if (!state) return;
#ifdef _WIN32
  if (state->handle) FreeLibrary(state->handle);
#endif
  delete state;
}

void cleanup_package(void* pointer) {
  auto* state = static_cast<PackageState*>(pointer);
  if (!state) return;
  state->host->value_release(state->ffi_class);
  state->host->value_release(state->cdata_class);
  state->host->value_release(state->ctype_class);
  state->host->value_release(state->library_class);
  state->host->value_release(state->null_value);
  delete state;
}

bool string_value(PackageState* state, X3Runtime* runtime, X3Value value,
                  std::string& output) {
  const char* data = nullptr;
  uint64_t size = 0;
  if (state->host->value_string_data(runtime, value, &data, &size) != X3_STATUS_OK)
    return false;
  output.assign(data, static_cast<size_t>(size));
  return true;
}

bool integer_value(X3Value value, int64_t& output) {
  if (value.tag == X3_TAG_INT64) { output = value.as.i64; return true; }
  if (value.tag == X3_TAG_UINT64 &&
      value.as.u64 <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    output = static_cast<int64_t>(value.as.u64);
    return true;
  }
  if (value.tag == X3_TAG_BOOL) { output = value.as.b ? 1 : 0; return true; }
  return false;
}

uint32_t big_endian_word(const unsigned char* data) {
  return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
         (uint32_t(data[2]) << 8) | uint32_t(data[3]);
}

bool bytes_value(PackageState* state, X3Runtime* runtime, X3Value value,
                 std::string& output) {
  const void* data = nullptr;
  uint64_t size = 0;
  if (state->host->value_bytes_data(runtime, value, &data, &size) != X3_STATUS_OK)
    return false;
  output.assign(static_cast<const char*>(data), static_cast<size_t>(size));
  return true;
}

bool sequence_item(PackageState* state, X3Runtime* runtime, X3Value sequence,
                   uint64_t index, X3Value& item) {
  return state->host->get_item(runtime, sequence,
      x3_value_uint64(index), &item) == X3_STATUS_OK;
}

bool load_type_context(PackageState* package, X3Runtime* runtime,
                       const X3KeywordArg* kwargs, uint32_t kwargc,
                       FfiState& ffi, std::string& error) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (!kwargs[i].name || kwargs[i].value.tag == X3_TAG_INVALID) {
      error = "invalid FFI keyword argument";
      return false;
    }
    const std::string name(kwargs[i].name);
    X3Value value = kwargs[i].value;
    if (name == "_version") {
      int64_t version = 0;
      if (!integer_value(value, version) || version < 0x2601 || version > 0x28ff) {
        error = "unsupported CFFI type-context version";
        return false;
      }
    } else if (name == "_types") {
      std::string bytes;
      if (!bytes_value(package, runtime, value, bytes) || bytes.size() % 4 != 0) {
        error = "invalid CFFI type table";
        return false;
      }
      ffi.types.reserve(bytes.size() / 4);
      for (size_t offset = 0; offset < bytes.size(); offset += 4)
        ffi.types.push_back(big_endian_word(
            reinterpret_cast<const unsigned char*>(bytes.data() + offset)));
    } else if (name == "_typenames") {
      uint64_t count = 0;
      if (package->host->len(runtime, value, &count) != X3_STATUS_OK) {
        error = "invalid CFFI typename table";
        return false;
      }
      for (uint64_t index = 0; index < count; ++index) {
        X3Value item = x3_value_invalid();
        std::string bytes;
        if (!sequence_item(package, runtime, value, index, item)) {
          error = "invalid CFFI typename entry";
          return false;
        }
        const bool valid = bytes_value(package, runtime, item, bytes);
        package->host->value_release(item);
        if (!valid || bytes.size() < 5) {
          error = "invalid CFFI typename entry";
          return false;
        }
        ffi.typenames.emplace(bytes.substr(4), big_endian_word(
            reinterpret_cast<const unsigned char*>(bytes.data())));
        ffi.typename_indices.push_back(big_endian_word(
            reinterpret_cast<const unsigned char*>(bytes.data())));
      }
    } else if (name == "_globals") {
      uint64_t count = 0;
      if (package->host->len(runtime, value, &count) != X3_STATUS_OK || count % 2 != 0) {
        error = "invalid CFFI global table";
        return false;
      }
      for (uint64_t index = 0; index < count; index += 2) {
        X3Value item = x3_value_invalid();
        std::string bytes;
        if (!sequence_item(package, runtime, value, index, item)) {
          error = "invalid CFFI global entry";
          return false;
        }
        const bool valid = bytes_value(package, runtime, item, bytes);
        package->host->value_release(item);
        if (!valid || bytes.size() < 5) {
          error = "invalid CFFI global entry";
          return false;
        }
        GlobalDescription global;
        global.type_op = big_endian_word(
            reinterpret_cast<const unsigned char*>(bytes.data()));
        global.name = bytes.substr(4);
        ffi.globals.emplace(global.name, std::move(global));
      }
    } else if (name == "_struct_unions") {
      uint64_t count = 0;
      if (package->host->len(runtime, value, &count) != X3_STATUS_OK) {
        error = "invalid CFFI struct table";
        return false;
      }
      for (uint64_t index = 0; index < count; ++index) {
        X3Value description = x3_value_invalid();
        if (!sequence_item(package, runtime, value, index, description)) {
          error = "invalid CFFI struct entry";
          return false;
        }
        uint64_t field_count = 0;
        if (package->host->len(runtime, description, &field_count) != X3_STATUS_OK ||
            field_count == 0) {
          package->host->value_release(description);
          error = "invalid CFFI struct entry";
          return false;
        }
        StructDescription entry;
        bool valid = true;
        for (uint64_t field_index = 0; field_index < field_count; ++field_index) {
          X3Value field = x3_value_invalid();
          std::string bytes;
          if (!sequence_item(package, runtime, description, field_index, field)) {
            valid = false;
            break;
          }
          valid = bytes_value(package, runtime, field, bytes);
          package->host->value_release(field);
          if (!valid || bytes.size() < (field_index == 0 ? 9u : 5u)) {
            valid = false;
            break;
          }
          const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
          if (field_index == 0) {
            entry.type_index = big_endian_word(data);
            entry.flags = big_endian_word(data + 4);
            entry.name = bytes.substr(8);
          } else {
            const uint32_t op = big_endian_word(data);
            const size_t name_offset = (op & 0xffu) == 17u ? 4 : 8;
            if (bytes.size() <= name_offset) { valid = false; break; }
            entry.fields.push_back({op, bytes.substr(name_offset)});
          }
        }
        package->host->value_release(description);
        if (!valid) {
          error = "invalid CFFI struct entry";
          return false;
        }
        ffi.structs.push_back(std::move(entry));
      }
    } else if (name == "_enums" || name == "_includes") {
      // Enum and include resolution is independent of typed DLL calls.
    } else {
      error = "unexpected FFI keyword argument: " + name;
      return false;
    }
  }
  return true;
}

struct CTypeLayout {
  size_t size = 0;
  size_t alignment = 1;
};

bool primitive_layout(uint32_t primitive, CTypeLayout& layout) {
  switch (primitive) {
    case 1: case 2: case 3: case 4: case 17: case 18: case 30: case 31:
      layout = {1, 1}; return true;
    case 5: case 6: case 16: case 19: case 20: case 32: case 33: case 50:
      layout = {2, 2}; return true;
    case 7: case 8: case 9: case 10: case 13: case 21: case 22:
    case 34: case 35: case 40: case 41: case 42: case 43: case 51:
      layout = {4, 4}; return true;
    case 11: case 12: case 14: case 23: case 24: case 25: case 26:
    case 27: case 28: case 29: case 36: case 37: case 44: case 45:
    case 46: case 47:
      layout = {8, 8}; return true;
    default: return false;
  }
}

bool layout_at(const FfiState& ffi, size_t index, CTypeLayout& layout, unsigned depth);

bool layout_opcode(const FfiState& ffi, uint32_t opcode, size_t index,
                   CTypeLayout& layout, unsigned depth) {
  if (depth > 64) return false;
  const uint32_t operation = opcode & 0xffu;
  const uint32_t argument = opcode >> 8;
  if (operation == 1) return primitive_layout(argument, layout);
  if (operation == 3 || operation == 13) {
    layout = {sizeof(void*), alignof(void*)};
    return true;
  }
  if (operation == 17) return layout_at(ffi, argument, layout, depth + 1);
  if (operation == 5) {
    CTypeLayout element;
    if (index + 1 >= ffi.types.size() ||
        !layout_at(ffi, argument, element, depth + 1)) return false;
    const size_t length = ffi.types[index + 1];
    if (length > std::numeric_limits<size_t>::max() / element.size) return false;
    layout = {length * element.size, element.alignment};
    return true;
  }
  if (operation == 9) {
    if (argument >= ffi.structs.size()) return false;
    const auto& description = ffi.structs[argument];
    if (description.flags & (0x08u | 0x10u)) return false;
    const bool is_union = (description.flags & 1u) != 0;
    size_t size = 0;
    size_t alignment = 1;
    for (const auto& field : description.fields) {
      if ((field.type_op & 0xffu) != 17u) return false;
      CTypeLayout field_layout;
      if (!layout_at(ffi, field.type_op >> 8, field_layout, depth + 1)) return false;
      alignment = std::max(alignment, field_layout.alignment);
      if (is_union) size = std::max(size, field_layout.size);
      else {
        if (size > std::numeric_limits<size_t>::max() - (field_layout.alignment - 1))
          return false;
        size = (size + field_layout.alignment - 1) & ~(field_layout.alignment - 1);
        if (size > std::numeric_limits<size_t>::max() - field_layout.size) return false;
        size += field_layout.size;
      }
    }
    if (size > std::numeric_limits<size_t>::max() - (alignment - 1)) return false;
    layout = {(size + alignment - 1) & ~(alignment - 1), alignment};
    return true;
  }
  return false;
}

bool raw_integer_value(X3Value value, uint64_t& output) {
  if (value.tag == X3_TAG_INT64) { output = static_cast<uint64_t>(value.as.i64); return true; }
  if (value.tag == X3_TAG_UINT64) { output = value.as.u64; return true; }
  if (value.tag == X3_TAG_BOOL) { output = value.as.b ? 1 : 0; return true; }
  return false;
}

bool layout_at(const FfiState& ffi, size_t index, CTypeLayout& layout, unsigned depth) {
  if (index >= ffi.types.size()) return false;
  return layout_opcode(ffi, ffi.types[index], index, layout, depth);
}

bool pointee_layout_at(const FfiState& ffi, size_t index, CTypeLayout& layout,
                       unsigned depth = 0) {
  if (depth > 64 || index >= ffi.types.size()) return false;
  const uint32_t opcode = ffi.types[index];
  const uint32_t operation = opcode & 0xffu;
  const uint32_t argument = opcode >> 8;
  if (operation == 3) return layout_at(ffi, argument, layout, depth + 1);
  if (operation == 17) return pointee_layout_at(ffi, argument, layout, depth + 1);
  if (operation == 21 && argument < ffi.typename_indices.size())
    return pointee_layout_at(ffi, ffi.typename_indices[argument], layout, depth + 1);
  return false;
}

std::string trim_type(std::string name) {
  const size_t first = name.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const size_t last = name.find_last_not_of(" \t\r\n");
  return name.substr(first, last - first + 1);
}

bool layout_name(const FfiState& ffi, std::string name, CTypeLayout& layout,
                 unsigned depth) {
  if (depth > 32) return false;
  name = trim_type(std::move(name));
  if (name.empty()) return false;
  if (name.compare(0, 6, "const ") == 0)
    return layout_name(ffi, name.substr(6), layout, depth + 1);
  if (name.back() == ']') {
    const size_t open = name.rfind('[');
    if (open == std::string::npos || open + 1 == name.size() - 1) return false;
    size_t length = 0;
    const char* begin = name.data() + open + 1;
    const char* end = name.data() + name.size() - 1;
    const auto parsed = std::from_chars(begin, end, length);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        !layout_name(ffi, name.substr(0, open), layout, depth + 1) ||
        (layout.size != 0 && length > std::numeric_limits<size_t>::max() / layout.size))
      return false;
    layout.size *= length;
    return true;
  }
  if (name.back() == '*') {
    const std::string base = trim_type(name.substr(0, name.size() - 1));
    CTypeLayout base_layout;
    if (base != "void" && base != "const void" &&
        !layout_name(ffi, base, base_layout, depth + 1))
      return false;
    layout = {sizeof(void*), alignof(void*)};
    return true;
  }
  const auto known = ffi.typenames.find(name);
  if (known != ffi.typenames.end())
    return layout_at(ffi, known->second, layout, depth + 1);
  for (const auto& description : ffi.structs) {
    const std::string declared =
        ((description.flags & 1u) ? "union " : "struct ") + description.name;
    if (name == declared)
      return layout_at(ffi, description.type_index, layout, depth + 1);
  }
  if (name == "uintptr_t" || name == "intptr_t" || name == "size_t" ||
      name == "ssize_t") {
    layout = {sizeof(void*), alignof(void*)};
    return true;
  }
  if (name == "char" || name == "unsigned char" || name == "signed char" ||
      name == "_Bool") { layout = {1, 1}; return true; }
  if (name == "short" || name == "unsigned short" || name == "wchar_t") {
    layout = {2, 2}; return true;
  }
  if (name == "int" || name == "unsigned int" || name == "long" ||
      name == "unsigned long" || name == "float") {
    layout = {4, 4}; return true;
  }
  if (name == "long long" || name == "unsigned long long" || name == "double") {
    layout = {8, 8}; return true;
  }
  return false;
}

X3Status make_cdata(PackageState* state, X3CallContext* call, X3Runtime* runtime,
                    std::string ctype, uintptr_t address, X3Value* result);

X3Status ffi_new(X3CallContext* call, X3Runtime* runtime, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc < 2 || argc > 3)
    return package->host->raise_class_error(call, "TypeError", "ffi.new() expects a C type and optional initializer");
  auto* ffi = static_cast<FfiState*>(package->host->instance_get_native_data(args[0], kFfiType));
  std::string name;
  if (!ffi || !string_value(package, runtime, args[1], name))
    return package->host->raise_class_error(call, "TypeError", "invalid FFI type");
  name = trim_type(std::move(name));
  CTypeLayout element;
  size_t length = 1;
  bool array = false;
  if (!name.empty() && name.back() == ']') {
    array = true;
    const size_t open = name.rfind('[');
    if (open == std::string::npos || !layout_name(*ffi, name.substr(0, open), element, 0))
      return package->host->raise_class_error(call, "TypeError", "invalid C array type");
    const std::string count_text = name.substr(open + 1, name.size() - open - 2);
    if (count_text.empty()) {
      uint64_t count = 0;
      if (argc != 3 || !raw_integer_value(args[2], count) ||
          count > std::numeric_limits<size_t>::max())
        return package->host->raise_class_error(call, "TypeError", "open C array requires a length");
      length = static_cast<size_t>(count);
    } else {
      const auto parsed = std::from_chars(count_text.data(),
          count_text.data() + count_text.size(), length);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != count_text.data() + count_text.size())
        return package->host->raise_class_error(call, "TypeError", "invalid C array length");
      if (argc == 3)
        return package->host->raise_class_error(call, "NotImplementedError", "C array initializer is unavailable");
    }
  } else if (!name.empty() && name.back() == '*') {
    if (!layout_name(*ffi, name.substr(0, name.size() - 1), element, 0))
      return package->host->raise_class_error(call, "TypeError", "incomplete C pointer target");
  } else {
    const auto known = ffi->typenames.find(name);
    if (known == ffi->typenames.end() ||
        !pointee_layout_at(*ffi, known->second, element))
      return package->host->raise_class_error(call, "TypeError", "ffi.new() requires a pointer or array type");
  }
  if (element.size == 0 || length > std::numeric_limits<size_t>::max() / element.size)
    return package->host->raise_class_error(call, "OverflowError", "C allocation is too large");
  const size_t bytes = length * element.size;
  if (bytes > static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()))
    return package->host->raise_class_error(call, "OverflowError", "C allocation is too large");
  const X3Status status = make_cdata(package, call, runtime, name, 0, result);
  if (status != X3_STATUS_OK) return status;
  auto* native = static_cast<CDataState*>(
      package->host->instance_get_native_data(*result, kCDataType));
  native->owned = std::make_unique<unsigned char[]>(bytes == 0 ? 1 : bytes);
  std::memset(native->owned.get(), 0, bytes == 0 ? 1 : bytes);
  native->owned_size = bytes;
  native->element_size = element.size;
  if (array) native->element_name = trim_type(name.substr(0, name.rfind('[')));
  else if (!name.empty() && name.back() == '*')
    native->element_name = trim_type(name.substr(0, name.size() - 1));
  else native->element_name = name;
  native->ffi = std::make_shared<FfiState>(*ffi);
  native->address = reinterpret_cast<uintptr_t>(native->owned.get());
  if (!array && argc == 3) {
    uint64_t initial = 0;
    if (auto* pointer = static_cast<CDataState*>(
            package->host->instance_get_native_data(args[2], kCDataType)))
      initial = pointer->address;
    else if (!raw_integer_value(args[2], initial))
      return package->host->raise_class_error(call, "TypeError", "invalid C initializer");
    std::memcpy(native->owned.get(), &initial, std::min(bytes, sizeof(initial)));
  }
  return X3_STATUS_OK;
}

X3Status ffi_sizeof(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 2)
    return package->host->raise_class_error(call, "TypeError", "ffi.sizeof() expects a C type");
  auto* ffi = static_cast<FfiState*>(package->host->instance_get_native_data(args[0], kFfiType));
  if (!ffi)
    return package->host->raise_class_error(call, "TypeError", "invalid FFI object");
  std::string name;
  if (!string_value(package, runtime, args[1], name))
    return package->host->raise_class_error(call, "TypeError", "C type must be a string");
  CTypeLayout layout;
  if (layout_name(*ffi, name, layout, 0)) {
    *result = x3_value_uint64(layout.size);
    return X3_STATUS_OK;
  }
  return package->host->raise_class_error(call, "ValueError", "unknown or incomplete C type");
}

X3Status ffi_getwinerror(X3CallContext* call, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
#ifdef _WIN32
  if (argc < 1 || argc > 2)
    return package->host->raise_class_error(call, "TypeError", "ffi.getwinerror() expects an optional error code");
  uint64_t number = cffi_last_error;
  if (argc == 2 && !raw_integer_value(args[1], number))
    return package->host->raise_class_error(call, "TypeError", "Windows error code must be an integer");
  const DWORD code = static_cast<DWORD>(number);
  wchar_t* message_buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);
  std::wstring wide;
  if (length != 0 && message_buffer != nullptr)
    wide.assign(message_buffer, length);
  if (message_buffer != nullptr) LocalFree(message_buffer);
  while (!wide.empty() &&
         (wide.back() == L'\r' || wide.back() == L'\n' ||
          wide.back() == L' ' || wide.back() == L'.')) wide.pop_back();
  if (wide.empty()) wide = L"Windows Error";
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
      static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0)
    return package->host->raise_class_error(call, "OSError", "cannot format Windows error");
  std::string message(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      message.data(), bytes, nullptr, nullptr);
  X3Value items = package->host->value_list(runtime);
  if (items.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  X3Value message_value = package->host->value_string_utf8(
      runtime, message.data(), static_cast<uint64_t>(message.size()));
  if (message_value.tag == X3_TAG_INVALID ||
      package->host->list_append(runtime, items, x3_value_uint64(code)) != X3_STATUS_OK ||
      package->host->list_append(runtime, items, message_value) != X3_STATUS_OK) {
    if (message_value.tag != X3_TAG_INVALID) package->host->value_release(message_value);
    package->host->value_release(items);
    return X3_STATUS_ERROR;
  }
  package->host->value_release(message_value);
  X3Value tuple_class = x3_value_invalid();
  if (package->host->builtin_value(package->host, "tuple", &tuple_class) != X3_STATUS_OK) {
    package->host->value_release(items);
    return X3_STATUS_ERROR;
  }
  const X3Status status = package->host->call(runtime, tuple_class, &items, 1, result);
  package->host->value_release(tuple_class);
  package->host->value_release(items);
  return status;
#else
  return package->host->raise_class_error(call, "AttributeError", "getwinerror is only available on Windows");
#endif
}

X3Status make_cdata(PackageState* state, X3CallContext* call, X3Runtime* runtime,
                    std::string ctype, uintptr_t address, X3Value* result) {
  auto native = std::make_unique<CDataState>();
  native->package = state;
  native->ctype = std::move(ctype);
  native->address = address;
  X3Value instance = state->host->value_instance(runtime, state->cdata_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kCDataType, native.get(),
                                            cleanup_cdata) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate C data object");
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status ffi_init_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc,
                     const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return state->host->raise_class_error(call, "TypeError", "FFI() received invalid arguments");
  auto native = std::make_unique<FfiState>();
  native->package = state;
  if (argc == 2 && !string_value(state, runtime, args[1], native->module_name))
    return state->host->raise_class_error(call, "TypeError", "FFI module name must be a string");
  std::string metadata_error;
  if (!load_type_context(state, runtime, kwargs, kwargc, *native, metadata_error))
    return state->host->raise_class_error(call, "TypeError", metadata_error.c_str());
  if (state->host->instance_set_native_data(args[0], kFfiType, native.get(),
                                            cleanup_ffi) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  native.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ffi_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  return ffi_init_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status ffi_cast(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 3)
    return state->host->raise_class_error(call, "TypeError", "ffi.cast() expects a C type and value");
  std::string ctype;
  if (!string_value(state, runtime, args[1], ctype))
    return state->host->raise_class_error(call, "TypeError", "C type must be a string");
  uintptr_t address = 0;
  if (auto* source = static_cast<CDataState*>(
          state->host->instance_get_native_data(args[2], kCDataType))) {
    address = source->address;
  } else {
    int64_t number = 0;
    if (!integer_value(args[2], number))
      return state->host->raise_class_error(call, "TypeError", "integer or C data required");
    address = static_cast<uintptr_t>(number);
  }
  return make_cdata(state, call, runtime, std::move(ctype), address, result);
}

X3Status ffi_dlopen(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2)
    return state->host->raise_class_error(call, "TypeError", "ffi.dlopen() expects one library name");
  auto* ffi = static_cast<FfiState*>(state->host->instance_get_native_data(args[0], kFfiType));
  if (!ffi)
    return state->host->raise_class_error(call, "TypeError", "invalid FFI object");
  std::string name;
  if (!string_value(state, runtime, args[1], name))
    return state->host->raise_class_error(call, "TypeError", "library name must be a string");
  auto native = std::make_unique<LibraryState>();
  native->package = state;
  native->name = name;
  native->ffi = std::make_shared<FfiState>(*ffi);
#ifdef _WIN32
  native->handle = LoadLibraryA(name.c_str());
  if (!native->handle)
    return state->host->raise_class_error(call, "OSError", "cannot load dynamic library");
#else
  return state->host->raise_class_error(call, "OSError", "dynamic CFFI loading is unavailable");
#endif
  X3Value instance = state->host->value_instance(runtime, state->library_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kLibraryType, native.get(),
                                            cleanup_library) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate library object");
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status library_getattr(X3CallContext* call, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2)
    return state->host->raise_class_error(call, "TypeError", "library attribute lookup expects a name");
  auto* library = static_cast<LibraryState*>(
      state->host->instance_get_native_data(args[0], kLibraryType));
  std::string name;
  if (!library || !string_value(state, runtime, args[1], name))
    return state->host->raise_class_error(call, "AttributeError", "invalid library attribute");
  const auto declared = library->ffi->globals.find(name);
  if (declared == library->ffi->globals.end() ||
      (declared->second.type_op & 0xffu) != 35u)
    return state->host->raise_class_error(call, "AttributeError", "symbol is not declared by this FFI");
#ifdef _WIN32
  FARPROC symbol = GetProcAddress(library->handle, name.c_str());
  if (!symbol)
    return state->host->raise_class_error(call, "AttributeError", "declared symbol is absent from library");
  const X3Status status = make_cdata(state, call, runtime, "function pointer",
                                    reinterpret_cast<uintptr_t>(symbol), result);
  if (status != X3_STATUS_OK) return status;
  auto* function = static_cast<CDataState*>(
      state->host->instance_get_native_data(*result, kCDataType));
  function->ffi = library->ffi;
  function->function_index = declared->second.type_op >> 8;
  function->owner_value = args[0];
  state->host->value_retain(args[0]);
  return X3_STATUS_OK;
#else
  return state->host->raise_class_error(call, "NotImplementedError", "dynamic symbol loading is unavailable");
#endif
}

#ifdef XLANG_CFFI_HAS_LIBFFI
enum class ForeignKind { Void, Signed, Unsigned, Pointer, Float, Double };

struct ForeignType {
  ffi_type* abi = nullptr;
  ForeignKind kind = ForeignKind::Void;
  unsigned bits = 0;
  std::string name;
};

bool foreign_primitive(uint32_t primitive, ForeignType& type) {
  switch (primitive) {
    case 0: type = {&ffi_type_void, ForeignKind::Void, 0, "void"}; return true;
    case 1: case 4: case 18: case 31:
      type = {&ffi_type_uint8, ForeignKind::Unsigned, 8, "unsigned char"}; return true;
    case 2: case 3: case 17: case 30:
      type = {&ffi_type_sint8, ForeignKind::Signed, 8, "char"}; return true;
    case 6: case 16: case 20: case 33: case 50:
      type = {&ffi_type_uint16, ForeignKind::Unsigned, 16, "unsigned short"}; return true;
    case 5: case 19: case 32:
      type = {&ffi_type_sint16, ForeignKind::Signed, 16, "short"}; return true;
    case 8: case 10: case 22: case 35: case 41: case 43: case 51:
      type = {&ffi_type_uint32, ForeignKind::Unsigned, 32, "unsigned int"}; return true;
    case 7: case 9: case 21: case 34: case 40: case 42:
      type = {&ffi_type_sint32, ForeignKind::Signed, 32, "int"}; return true;
    case 12: case 24: case 26: case 28: case 37: case 45: case 47:
      type = {&ffi_type_uint64, ForeignKind::Unsigned, 64, "unsigned long long"}; return true;
    case 11: case 23: case 25: case 27: case 29: case 36: case 44: case 46:
      type = {&ffi_type_sint64, ForeignKind::Signed, 64, "long long"}; return true;
    case 13: type = {&ffi_type_float, ForeignKind::Float, 32, "float"}; return true;
    case 14: case 15:
      type = {&ffi_type_double, ForeignKind::Double, 64, "double"}; return true;
    default: return false;
  }
}

bool foreign_at(const FfiState& ffi, size_t index, ForeignType& type,
                unsigned depth = 0) {
  if (depth > 64 || index >= ffi.types.size()) return false;
  const uint32_t opcode = ffi.types[index];
  const uint32_t operation = opcode & 0xffu;
  const uint32_t argument = opcode >> 8;
  if (operation == 1) return foreign_primitive(argument, type);
  if (operation == 3 || operation == 5 || operation == 7) {
    type = {&ffi_type_pointer, ForeignKind::Pointer,
            static_cast<unsigned>(sizeof(void*) * 8), "void *"};
    for (const auto& name : ffi.typenames)
      if (name.second == index) { type.name = name.first; break; }
    return true;
  }
  if (operation == 17) return foreign_at(ffi, argument, type, depth + 1);
  if (operation == 21 && argument < ffi.typename_indices.size())
    return foreign_at(ffi, ffi.typename_indices[argument], type, depth + 1);
  return false;
}

bool foreign_named(const FfiState& ffi, const std::string& name, ForeignType& type) {
  const auto known = ffi.typenames.find(name);
  if (known != ffi.typenames.end() && foreign_at(ffi, known->second, type)) {
    type.name = name;
    return true;
  }
  if (name == "int") return foreign_primitive(7, type);
  if (name == "unsigned int") return foreign_primitive(8, type);
  if (name == "char") return foreign_primitive(2, type);
  if (name == "unsigned char") return foreign_primitive(4, type);
  if (name == "void *") {
    type = {&ffi_type_pointer, ForeignKind::Pointer,
            static_cast<unsigned>(sizeof(void*) * 8), name};
    return true;
  }
  return false;
}

bool numeric_foreign_value(PackageState* package, X3CallContext* call,
                           X3Runtime* runtime, X3Value value, uint64_t& output) {
  if (raw_integer_value(value, output)) return true;
  if (auto* data = static_cast<CDataState*>(
          package->host->instance_get_native_data(value, kCDataType))) {
    output = data->address;
    return true;
  }
  X3Value int_class = x3_value_invalid();
  X3Value isinstance_fn = x3_value_invalid();
  if (package->host->builtin_value(package->host, "int", &int_class) == X3_STATUS_OK &&
      package->host->builtin_value(package->host, "isinstance", &isinstance_fn) == X3_STATUS_OK) {
    const X3Value check_args[] = {value, int_class};
    X3Value check_result = x3_value_invalid();
    const X3Status checked = package->host->call(runtime, isinstance_fn,
                                                 check_args, 2, &check_result);
    const bool is_int = checked == X3_STATUS_OK && check_result.tag == X3_TAG_BOOL &&
                        check_result.as.b != 0;
    if (check_result.tag != X3_TAG_INVALID) package->host->value_release(check_result);
    if (checked != X3_STATUS_OK) package->host->clear_exception(call);
    if (is_int) {
      X3Value converted = x3_value_invalid();
      const X3Status status = package->host->call(runtime, int_class, &value, 1, &converted);
      if (status == X3_STATUS_OK) {
        const bool valid = raw_integer_value(converted, output);
        package->host->value_release(converted);
        package->host->value_release(isinstance_fn);
        package->host->value_release(int_class);
        return valid;
      }
      package->host->clear_exception(call);
    }
  }
  if (isinstance_fn.tag != X3_TAG_INVALID) package->host->value_release(isinstance_fn);
  if (int_class.tag != X3_TAG_INVALID) package->host->value_release(int_class);
  X3Value index_method = x3_value_invalid();
  if (package->host->get_attr(runtime, value, "__index__", &index_method) != X3_STATUS_OK) {
    package->host->clear_exception(call);
    return false;
  }
  X3Value converted = x3_value_invalid();
  const X3Status status = package->host->call(runtime, index_method, nullptr, 0, &converted);
  package->host->value_release(index_method);
  if (status != X3_STATUS_OK) {
    package->host->clear_exception(call);
    return false;
  }
  const bool valid = raw_integer_value(converted, output);
  package->host->value_release(converted);
  return valid;
}

bool convert_foreign_argument(PackageState* package, X3CallContext* call,
                              X3Runtime* runtime, X3Value value,
                              const ForeignType& type, uint64_t& storage) {
  if (type.kind == ForeignKind::Pointer) {
    if (value.tag == X3_TAG_NONE) { storage = 0; return true; }
    auto* pointer = static_cast<CDataState*>(
        package->host->instance_get_native_data(value, kCDataType));
    if (!pointer) return false;
    storage = pointer->address;
    return true;
  }
  if (type.kind == ForeignKind::Float || type.kind == ForeignKind::Double) {
    double number = 0;
    if (value.tag == X3_TAG_DOUBLE) number = value.as.f64;
    else {
      uint64_t integer = 0;
      if (!numeric_foreign_value(package, call, runtime, value, integer)) return false;
      number = static_cast<double>(static_cast<int64_t>(integer));
    }
    if (type.kind == ForeignKind::Float) {
      const float narrowed = static_cast<float>(number);
      std::memcpy(&storage, &narrowed, sizeof(narrowed));
    } else {
      std::memcpy(&storage, &number, sizeof(number));
    }
    return true;
  }
  return numeric_foreign_value(package, call, runtime, value, storage);
}

X3Status foreign_result(PackageState* package, X3CallContext* call,
                        X3Runtime* runtime, const ForeignType& type,
                        uint64_t storage, X3Value* result) {
  if (type.kind == ForeignKind::Void) { *result = x3_value_none(); return X3_STATUS_OK; }
  if (type.kind == ForeignKind::Pointer)
    return make_cdata(package, call, runtime, type.name,
                      static_cast<uintptr_t>(storage), result);
  if (type.kind == ForeignKind::Float) {
    float number = 0;
    std::memcpy(&number, &storage, sizeof(number));
    *result = x3_value_double(number);
    return X3_STATUS_OK;
  }
  if (type.kind == ForeignKind::Double) {
    double number = 0;
    std::memcpy(&number, &storage, sizeof(number));
    *result = x3_value_double(number);
    return X3_STATUS_OK;
  }
  if (type.bits < 64) storage &= (uint64_t{1} << type.bits) - 1;
  if (type.kind == ForeignKind::Signed) {
    if (type.bits < 64 && (storage & (uint64_t{1} << (type.bits - 1))))
      storage |= ~((uint64_t{1} << type.bits) - 1);
    *result = x3_value_int64(static_cast<int64_t>(storage));
  } else {
    *result = x3_value_uint64(storage);
  }
  return X3_STATUS_OK;
}
#endif

X3Status cdata_call(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc == 0)
    return package->host->raise_class_error(call, "TypeError", "missing C function pointer");
  auto* function = static_cast<CDataState*>(
      package->host->instance_get_native_data(args[0], kCDataType));
  if (!function || !function->ffi || function->function_index == UINT32_MAX)
    return package->host->raise_class_error(call, "TypeError", "C data is not callable");
#ifdef XLANG_CFFI_HAS_LIBFFI
  const FfiState& ffi = *function->ffi;
  const size_t index = function->function_index;
  if (index >= ffi.types.size() || (ffi.types[index] & 0xffu) != 13u)
    return package->host->raise_class_error(call, "TypeError", "invalid C function signature");
  ForeignType return_type;
  if (!foreign_at(ffi, ffi.types[index] >> 8, return_type))
    return package->host->raise_class_error(call, "NotImplementedError", "unsupported C return type");
  std::vector<ForeignType> parameters;
  uint32_t flags = 0;
  bool found_end = false;
  for (size_t cursor = index + 1; cursor < ffi.types.size(); ++cursor) {
    if ((ffi.types[cursor] & 0xffu) == 15u) {
      flags = ffi.types[cursor] >> 8;
      found_end = true;
      break;
    }
    ForeignType parameter;
    if (!foreign_at(ffi, cursor, parameter) || parameter.kind == ForeignKind::Void)
      return package->host->raise_class_error(call, "NotImplementedError", "unsupported C argument type");
    parameters.push_back(std::move(parameter));
  }
  if (!found_end || (flags & 1u))
    return package->host->raise_class_error(call, "NotImplementedError", "variadic C calls are unavailable");
  if (argc - 1 != parameters.size())
    return package->host->raise_class_error(call, "TypeError", "incorrect C function argument count");
  std::vector<ffi_type*> argument_types(parameters.size());
  std::vector<uint64_t> storage(parameters.size(), 0);
  std::vector<void*> argument_values(parameters.size());
  for (size_t i = 0; i < parameters.size(); ++i) {
    if (!convert_foreign_argument(package, call, runtime, args[i + 1], parameters[i], storage[i])) {
      const std::string message = "invalid C function argument " + std::to_string(i + 1);
      return package->host->raise_class_error(call, "TypeError", message.c_str());
    }
    argument_types[i] = parameters[i].abi;
    argument_values[i] = &storage[i];
  }
  ffi_cif cif{};
  if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, static_cast<unsigned>(parameters.size()),
                   return_type.abi, argument_types.data()) != FFI_OK)
    return package->host->raise_class_error(call, "RuntimeError", "cannot prepare C function call");
  uint64_t returned = 0;
  ffi_call(&cif, reinterpret_cast<void (*)()>(function->address),
           return_type.kind == ForeignKind::Void ? nullptr : &returned,
           argument_values.data());
#ifdef _WIN32
  cffi_last_error = GetLastError();
#endif
  return foreign_result(package, call, runtime, return_type, returned, result);
#else
  return package->host->raise_class_error(call, "NotImplementedError", "foreign function calls are unavailable");
#endif
}

X3Status cdata_getitem(X3CallContext* call, X3Runtime* runtime, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* package = static_cast<PackageState*>(user_data);
  if (argc != 2)
    return package->host->raise_class_error(call, "TypeError", "C data indexing expects one index");
  auto* data = static_cast<CDataState*>(
      package->host->instance_get_native_data(args[0], kCDataType));
  int64_t index = 0;
  if (!data || !data->owned || !integer_value(args[1], index) || index < 0 ||
      data->element_size == 0 ||
      static_cast<uint64_t>(index) >= data->owned_size / data->element_size)
    return package->host->raise_class_error(call, "IndexError", "C data index is out of range");
  const uintptr_t address = data->address +
      static_cast<size_t>(index) * data->element_size;
#ifdef XLANG_CFFI_HAS_LIBFFI
  ForeignType type;
  if (data->ffi && foreign_named(*data->ffi, data->element_name, type)) {
    uint64_t storage = 0;
    std::memcpy(&storage, reinterpret_cast<const void*>(address),
                std::min(data->element_size, sizeof(storage)));
    const X3Status status = foreign_result(package, call, runtime, type, storage, result);
    if (status != X3_STATUS_OK) return status;
    if (type.kind == ForeignKind::Pointer) {
      auto* child = static_cast<CDataState*>(
          package->host->instance_get_native_data(*result, kCDataType));
      child->owner_value = args[0];
      package->host->value_retain(args[0]);
    }
    return X3_STATUS_OK;
  }
#endif
  const X3Status status = make_cdata(package, call, runtime,
                                    data->element_name, address, result);
  if (status != X3_STATUS_OK) return status;
  auto* child = static_cast<CDataState*>(
      package->host->instance_get_native_data(*result, kCDataType));
  child->ffi = data->ffi;
  child->owner_value = args[0];
  package->host->value_retain(args[0]);
  return X3_STATUS_OK;
}

X3Status cdata_int(X3CallContext* call, X3Runtime*, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1)
    return state->host->raise_class_error(call, "TypeError", "C data integer conversion takes no arguments");
  auto* native = static_cast<CDataState*>(
      state->host->instance_get_native_data(args[0], kCDataType));
  if (!native)
    return state->host->raise_class_error(call, "TypeError", "invalid C data object");
  *result = x3_value_uint64(static_cast<uint64_t>(native->address));
  return X3_STATUS_OK;
}

X3Status cdata_bool(X3CallContext* call, X3Runtime*, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1)
    return state->host->raise_class_error(call, "TypeError", "C data truth test takes no arguments");
  auto* native = static_cast<CDataState*>(
      state->host->instance_get_native_data(args[0], kCDataType));
  if (!native)
    return state->host->raise_class_error(call, "TypeError", "invalid C data object");
  *result = x3_value_bool(native->address != 0);
  return X3_STATUS_OK;
}

X3Status cdata_equal(X3CallContext* call, X3Runtime*, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2)
    return state->host->raise_class_error(call, "TypeError", "C data comparison expects one operand");
  auto* left = static_cast<CDataState*>(
      state->host->instance_get_native_data(args[0], kCDataType));
  auto* right = static_cast<CDataState*>(
      state->host->instance_get_native_data(args[1], kCDataType));
  *result = x3_value_bool(left && right && left->address == right->address);
  return X3_STATUS_OK;
}

X3Status cdata_not_equal(X3CallContext* call, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  const X3Status status = cdata_equal(call, runtime, user_data, args, argc, result);
  if (status == X3_STATUS_OK) result->as.b = !result->as.b;
  return status;
}

void method(X3NativeFunctionDef& definition, const char* name,
            X3NativeFn callback, PackageState* state,
            X3NativeKeywordFn keywords = nullptr) {
  definition = {};
  definition.size = sizeof(definition);
  definition.name = name;
  definition.callback = callback;
  definition.keyword_callback = keywords;
  definition.user_data = state;
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "_cffi_backend", &module) != X3_STATUS_OK) return X3_STATUS_ERROR;

  if (host->create_class(host, "CType", nullptr, 0, &state->ctype_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef cdata_methods[6]{};
  method(cdata_methods[0], "__int__", cdata_int, state);
  method(cdata_methods[1], "__bool__", cdata_bool, state);
  method(cdata_methods[2], "__call__", cdata_call, state);
  method(cdata_methods[3], "__getitem__", cdata_getitem, state);
  method(cdata_methods[4], "__eq__", cdata_equal, state);
  method(cdata_methods[5], "__ne__", cdata_not_equal, state);
  if (host->create_class(host, "CData", cdata_methods, 6, &state->cdata_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef library_methods[1]{};
  method(library_methods[0], "__getattr__", library_getattr, state);
  if (host->create_class(host, "CLibrary", library_methods, 1, &state->library_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef ffi_methods[6]{};
  method(ffi_methods[0], "__init__", ffi_init, state, ffi_init_kw);
  method(ffi_methods[1], "cast", ffi_cast, state);
  method(ffi_methods[2], "dlopen", ffi_dlopen, state);
  method(ffi_methods[3], "sizeof", ffi_sizeof, state);
  method(ffi_methods[4], "new", ffi_new, state);
  method(ffi_methods[5], "getwinerror", ffi_getwinerror, state);
  if (host->module_add_class(module, "FFI", ffi_methods, 6, &state->ffi_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (host->class_add_value(state->ffi_class, "CData", state->cdata_class) != X3_STATUS_OK ||
      host->class_add_value(state->ffi_class, "CType", state->ctype_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (make_cdata(state, nullptr, host->runtime, "void *", 0, &state->null_value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (host->class_add_value(state->ffi_class, "NULL", state->null_value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  host->module_add_value(module, "__version__", host->value_string(host->runtime, "2.0.0-xlang3"));
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "_cffi_backend");
  host->package_set_metadata(host, "version", "2.0.0");
  return register_module(host);
}
