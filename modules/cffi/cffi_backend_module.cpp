/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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

struct FfiState {
  PackageState* package = nullptr;
  std::string module_name;
};

struct CDataState {
  PackageState* package = nullptr;
  std::string ctype;
  uintptr_t address = 0;
};

struct LibraryState {
  PackageState* package = nullptr;
#ifdef _WIN32
  HMODULE handle = nullptr;
#else
  void* handle = nullptr;
#endif
  std::string name;
};

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value ffi_class = x3_value_invalid();
  X3Value cdata_class = x3_value_invalid();
  X3Value ctype_class = x3_value_invalid();
  X3Value library_class = x3_value_invalid();
  X3Value null_value = x3_value_invalid();
};

void cleanup_ffi(void* pointer) { delete static_cast<FfiState*>(pointer); }
void cleanup_cdata(void* pointer) { delete static_cast<CDataState*>(pointer); }
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
                     const X3KeywordArg*, uint32_t, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc < 1 || argc > 2)
    return state->host->raise_class_error(call, "TypeError", "FFI() received invalid arguments");
  auto native = std::make_unique<FfiState>();
  native->package = state;
  if (argc == 2 && !string_value(state, runtime, args[1], native->module_name))
    return state->host->raise_class_error(call, "TypeError", "FFI module name must be a string");
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
  std::string name;
  if (!string_value(state, runtime, args[1], name))
    return state->host->raise_class_error(call, "TypeError", "library name must be a string");
  auto native = std::make_unique<LibraryState>();
  native->package = state;
  native->name = name;
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
  X3NativeFunctionDef cdata_methods[2]{};
  method(cdata_methods[0], "__int__", cdata_int, state);
  method(cdata_methods[1], "__bool__", cdata_bool, state);
  if (host->create_class(host, "CData", cdata_methods, 2, &state->cdata_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (host->create_class(host, "CLibrary", nullptr, 0, &state->library_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef ffi_methods[3]{};
  method(ffi_methods[0], "__init__", ffi_init, state, ffi_init_kw);
  method(ffi_methods[1], "cast", ffi_cast, state);
  method(ffi_methods[2], "dlopen", ffi_dlopen, state);
  if (host->module_add_class(module, "FFI", ffi_methods, 3, &state->ffi_class) != X3_STATUS_OK)
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
