/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <argon2.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kBufferType = "_argon2_cffi_bindings._ffi.CData";

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value buffer_class = x3_value_invalid();
  X3Value ffi_class = x3_value_invalid();
  X3Value lib_class = x3_value_invalid();
  X3Value ffi = x3_value_invalid();
  X3Value lib = x3_value_invalid();
};

struct BufferState {
  std::vector<unsigned char> bytes;
};

void cleanup_buffer(void* pointer) { delete static_cast<BufferState*>(pointer); }

void cleanup_package(void* pointer) {
  auto* state = static_cast<PackageState*>(pointer);
  if (!state) return;
  state->host->value_release(state->buffer_class);
  state->host->value_release(state->ffi_class);
  state->host->value_release(state->lib_class);
  state->host->value_release(state->ffi);
  state->host->value_release(state->lib);
  delete state;
}

bool argc_is(PackageState* state, X3CallContext* call, uint32_t argc,
             uint32_t expected, const char* name) {
  if (argc == expected) return true;
  const std::string message = std::string(name) + " received an invalid number of arguments";
  state->host->raise_class_error(call, "TypeError", message.c_str());
  return false;
}

bool unsigned_integer(X3Value value, uint64_t* output) {
  if (value.tag == X3_TAG_UINT64) { *output = value.as.u64; return true; }
  if (value.tag == X3_TAG_INT64 && value.as.i64 >= 0) {
    *output = static_cast<uint64_t>(value.as.i64); return true;
  }
  return false;
}

bool signed_integer(X3Value value, int64_t* output) {
  if (value.tag == X3_TAG_INT64) { *output = value.as.i64; return true; }
  if (value.tag == X3_TAG_UINT64 &&
      value.as.u64 <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    *output = static_cast<int64_t>(value.as.u64); return true;
  }
  return false;
}

bool text_data(PackageState* state, X3Runtime* runtime, X3Value value,
               std::string* output) {
  const char* data = nullptr;
  uint64_t size = 0;
  if (state->host->value_string_data(runtime, value, &data, &size) != X3_STATUS_OK)
    return false;
  output->assign(data, static_cast<size_t>(size));
  return true;
}

BufferState* native_buffer(PackageState* state, X3Value value) {
  return static_cast<BufferState*>(
      state->host->instance_get_native_data(value, kBufferType));
}

bool readonly_buffer(PackageState* state, X3Runtime* runtime, X3Value value,
                     const unsigned char** data, size_t* size) {
  if (auto* native = native_buffer(state, value)) {
    *data = native->bytes.data();
    *size = native->bytes.size();
    return true;
  }
  const void* raw = nullptr;
  uint64_t length = 0;
  if (state->host->value_bytes_data(runtime, value, &raw, &length) != X3_STATUS_OK ||
      length > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return false;
  *data = static_cast<const unsigned char*>(raw);
  *size = static_cast<size_t>(length);
  return true;
}

X3Status make_buffer(PackageState* state, X3CallContext* call, X3Runtime* runtime,
                     std::vector<unsigned char> bytes, X3Value* result) {
  auto native = std::make_unique<BufferState>();
  native->bytes = std::move(bytes);
  X3Value instance = state->host->value_instance(runtime, state->buffer_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(
          instance, kBufferType, native.get(), cleanup_buffer) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return state->host->raise_class_error(call, "MemoryError", "cannot allocate Argon2 buffer");
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status ffi_new(X3CallContext* call, X3Runtime* runtime, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call, argc, 3, "ffi.new()")) return X3_STATUS_ERROR;
  std::string declaration;
  if (!text_data(state, runtime, args[1], &declaration) ||
      (declaration != "char[]" && declaration != "uint8_t[]"))
    return state->host->raise_class_error(call, "TypeError", "unsupported C declaration");

  std::vector<unsigned char> bytes;
  uint64_t requested = 0;
  if (unsigned_integer(args[2], &requested)) {
    if (requested > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
      return state->host->raise_class_error(call, "OverflowError", "buffer is too large");
    bytes.resize(static_cast<size_t>(requested), 0);
  } else {
    const unsigned char* data = nullptr;
    size_t size = 0;
    if (!readonly_buffer(state, runtime, args[2], &data, &size))
      return state->host->raise_class_error(call, "TypeError", "initializer must be bytes or an integer");
    bytes.assign(data, data + size);
    bytes.push_back(0);
  }
  return make_buffer(state, call, runtime, std::move(bytes), result);
}

X3Status ffi_string(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call, argc, 2, "ffi.string()")) return X3_STATUS_ERROR;
  const unsigned char* data = nullptr;
  size_t size = 0;
  if (!readonly_buffer(state, runtime, args[1], &data, &size))
    return state->host->raise_class_error(call, "TypeError", "expected a C data buffer");
  const auto* end = static_cast<const unsigned char*>(std::memchr(data, 0, size));
  const size_t length = end == nullptr ? size : static_cast<size_t>(end - data);
  *result = state->host->value_bytes(runtime, data, length);
  return X3_STATUS_OK;
}

X3Status ffi_buffer(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call, argc, 3, "ffi.buffer()")) return X3_STATUS_ERROR;
  const unsigned char* data = nullptr;
  size_t size = 0;
  uint64_t requested = 0;
  if (!readonly_buffer(state, runtime, args[1], &data, &size) ||
      !unsigned_integer(args[2], &requested) || requested > size)
    return state->host->raise_class_error(call, "ValueError", "invalid buffer size");
  *result = state->host->value_bytes(runtime, data, requested);
  return X3_STATUS_OK;
}

bool read_u32(X3Value value, uint32_t* output) {
  uint64_t integer = 0;
  if (!unsigned_integer(value, &integer) || integer > UINT32_MAX) return false;
  *output = static_cast<uint32_t>(integer);
  return true;
}

X3Status lib_encodedlen(X3CallContext* call, X3Runtime*, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call, argc, 7, "argon2_encodedlen()")) return X3_STATUS_ERROR;
  uint32_t t = 0, m = 0, p = 0, salt = 0, hash = 0, type = 0;
  if (!read_u32(args[1], &t) || !read_u32(args[2], &m) ||
      !read_u32(args[3], &p) || !read_u32(args[4], &salt) ||
      !read_u32(args[5], &hash) || !read_u32(args[6], &type))
    return state->host->raise_class_error(call, "TypeError", "Argon2 parameters must be integers");
  *result = x3_value_uint64(argon2_encodedlen(
      t, m, p, salt, hash, static_cast<argon2_type>(type)));
  return X3_STATUS_OK;
}

X3Status lib_hash(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call, argc, 14, "argon2_hash()")) return X3_STATUS_ERROR;
  uint32_t t = 0, m = 0, p = 0, type = 0, version = 0;
  uint64_t pwd_length = 0, salt_length = 0, hash_length = 0, encoded_length = 0;
  if (!read_u32(args[1], &t) || !read_u32(args[2], &m) || !read_u32(args[3], &p) ||
      !unsigned_integer(args[5], &pwd_length) ||
      !unsigned_integer(args[7], &salt_length) ||
      !unsigned_integer(args[9], &hash_length) ||
      !unsigned_integer(args[11], &encoded_length) ||
      !read_u32(args[12], &type) || !read_u32(args[13], &version))
    return state->host->raise_class_error(call, "TypeError", "invalid Argon2 parameter");

  const unsigned char *pwd = nullptr, *salt = nullptr;
  size_t pwd_size = 0, salt_size = 0;
  if (!readonly_buffer(state, runtime, args[4], &pwd, &pwd_size) ||
      !readonly_buffer(state, runtime, args[6], &salt, &salt_size) ||
      pwd_length > pwd_size || salt_length > salt_size)
    return state->host->raise_class_error(call, "ValueError", "Argon2 input length exceeds its buffer");

  unsigned char* hash = nullptr;
  char* encoded = nullptr;
  auto* hash_buffer = args[8].tag == X3_TAG_NONE ? nullptr : native_buffer(state, args[8]);
  auto* encoded_buffer = args[10].tag == X3_TAG_NONE ? nullptr : native_buffer(state, args[10]);
  if ((args[8].tag != X3_TAG_NONE && !hash_buffer) ||
      (args[10].tag != X3_TAG_NONE && !encoded_buffer))
    return state->host->raise_class_error(call, "TypeError", "Argon2 output must be a C data buffer or NULL");
  if (hash_buffer) {
    if (hash_length > hash_buffer->bytes.size())
      return state->host->raise_class_error(call, "ValueError", "invalid Argon2 output buffer");
    hash = hash_buffer->bytes.data();
  }
  if (encoded_buffer) {
    if (encoded_length > encoded_buffer->bytes.size())
      return state->host->raise_class_error(call, "ValueError", "invalid Argon2 encoded buffer");
    encoded = reinterpret_cast<char*>(encoded_buffer->bytes.data());
  }

  const int status = argon2_hash(
      t, m, p, pwd, static_cast<size_t>(pwd_length), salt,
      static_cast<size_t>(salt_length), hash, static_cast<size_t>(hash_length),
      encoded, static_cast<size_t>(encoded_length),
      static_cast<argon2_type>(type), version);
  *result = x3_value_int64(status);
  return X3_STATUS_OK;
}

X3Status lib_verify(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call, argc, 5, "argon2_verify()")) return X3_STATUS_ERROR;
  const unsigned char *encoded_data = nullptr, *password = nullptr;
  size_t encoded_size = 0, password_size = 0;
  uint64_t password_length = 0;
  uint32_t type = 0;
  if (!readonly_buffer(state, runtime, args[1], &encoded_data, &encoded_size) ||
      !readonly_buffer(state, runtime, args[2], &password, &password_size) ||
      !unsigned_integer(args[3], &password_length) || password_length > password_size ||
      !read_u32(args[4], &type))
    return state->host->raise_class_error(call, "ValueError", "invalid Argon2 verification input");
  std::string encoded(reinterpret_cast<const char*>(encoded_data), encoded_size);
  if (const auto position = encoded.find('\0'); position != std::string::npos)
    encoded.resize(position);
  const int status = argon2_verify(encoded.c_str(), password,
      static_cast<size_t>(password_length), static_cast<argon2_type>(type));
  *result = x3_value_int64(status);
  return X3_STATUS_OK;
}

X3Status lib_error_message(X3CallContext* call, X3Runtime* runtime, void* user_data,
                           const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call, argc, 2, "argon2_error_message()")) return X3_STATUS_ERROR;
  int64_t error = 0;
  if (!signed_integer(args[1], &error) || error < INT_MIN || error > INT_MAX)
    return state->host->raise_class_error(call, "TypeError", "error code must be an integer");
  const char* message = argon2_error_message(static_cast<int>(error));
  if (!message) message = "Unknown error code";
  *result = state->host->value_bytes(runtime, message, std::strlen(message));
  return X3_STATUS_OK;
}

void method(X3NativeFunctionDef* definition, const char* name,
            X3NativeFn callback, PackageState* state) {
  *definition = {};
  definition->size = sizeof(*definition);
  definition->name = name;
  definition->callback = callback;
  definition->user_data = state;
}

bool add_constant(PackageState* state, X3Value object, const char* name, int64_t value) {
  return state->host->set_attr(state->host->runtime, object, name,
                               x3_value_int64(value)) == X3_STATUS_OK;
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "_argon2_cffi_bindings._ffi", &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  if (host->create_class(host, "CData", nullptr, 0, &state->buffer_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef ffi_methods[3]{};
  method(&ffi_methods[0], "new", ffi_new, state);
  method(&ffi_methods[1], "string", ffi_string, state);
  method(&ffi_methods[2], "buffer", ffi_buffer, state);
  if (host->create_class(host, "FFI", ffi_methods, 3, &state->ffi_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3NativeFunctionDef lib_methods[4]{};
  method(&lib_methods[0], "argon2_encodedlen", lib_encodedlen, state);
  method(&lib_methods[1], "argon2_hash", lib_hash, state);
  method(&lib_methods[2], "argon2_verify", lib_verify, state);
  method(&lib_methods[3], "argon2_error_message", lib_error_message, state);
  if (host->create_class(host, "Lib", lib_methods, 4, &state->lib_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  state->ffi = host->value_instance(host->runtime, state->ffi_class);
  state->lib = host->value_instance(host->runtime, state->lib_class);
  if (state->ffi.tag == X3_TAG_INVALID || state->lib.tag == X3_TAG_INVALID ||
      host->set_attr(host->runtime, state->ffi, "NULL", x3_value_none()) != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  struct Constant { const char* name; int64_t value; };
  static constexpr Constant constants[] = {
      {"Argon2_d", Argon2_d}, {"Argon2_i", Argon2_i}, {"Argon2_id", Argon2_id},
      {"ARGON2_VERSION_10", ARGON2_VERSION_10},
      {"ARGON2_VERSION_13", ARGON2_VERSION_13},
      {"ARGON2_VERSION_NUMBER", ARGON2_VERSION_NUMBER},
      {"ARGON2_OK", ARGON2_OK}, {"ARGON2_VERIFY_MISMATCH", ARGON2_VERIFY_MISMATCH},
      {"ARGON2_FLAG_CLEAR_PASSWORD", ARGON2_FLAG_CLEAR_PASSWORD},
      {"ARGON2_FLAG_CLEAR_SECRET", ARGON2_FLAG_CLEAR_SECRET},
      {"ARGON2_DEFAULT_FLAGS", ARGON2_DEFAULT_FLAGS},
      {"ARGON2_MIN_LANES", ARGON2_MIN_LANES}, {"ARGON2_MAX_LANES", ARGON2_MAX_LANES},
      {"ARGON2_MIN_THREADS", ARGON2_MIN_THREADS}, {"ARGON2_MAX_THREADS", ARGON2_MAX_THREADS},
      {"ARGON2_SYNC_POINTS", ARGON2_SYNC_POINTS}, {"ARGON2_MIN_OUTLEN", ARGON2_MIN_OUTLEN},
      {"ARGON2_MAX_OUTLEN", ARGON2_MAX_OUTLEN}, {"ARGON2_MIN_MEMORY", ARGON2_MIN_MEMORY},
      {"ARGON2_MAX_MEMORY_BITS", ARGON2_MAX_MEMORY_BITS}, {"ARGON2_MAX_MEMORY", ARGON2_MAX_MEMORY},
      {"ARGON2_MIN_TIME", ARGON2_MIN_TIME}, {"ARGON2_MAX_TIME", ARGON2_MAX_TIME},
      {"ARGON2_MIN_PWD_LENGTH", ARGON2_MIN_PWD_LENGTH}, {"ARGON2_MAX_PWD_LENGTH", ARGON2_MAX_PWD_LENGTH},
      {"ARGON2_MIN_AD_LENGTH", ARGON2_MIN_AD_LENGTH}, {"ARGON2_MAX_AD_LENGTH", ARGON2_MAX_AD_LENGTH},
      {"ARGON2_MIN_SALT_LENGTH", ARGON2_MIN_SALT_LENGTH}, {"ARGON2_MAX_SALT_LENGTH", ARGON2_MAX_SALT_LENGTH},
      {"ARGON2_MIN_SECRET", ARGON2_MIN_SECRET}, {"ARGON2_MAX_SECRET", ARGON2_MAX_SECRET},
  };
  for (const auto& constant : constants)
    if (!add_constant(state, state->lib, constant.name, constant.value))
      return X3_STATUS_ERROR;
  if (host->module_add_value(module, "ffi", state->ffi) != X3_STATUS_OK ||
      host->module_add_value(module, "lib", state->lib) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "_argon2_cffi_bindings._ffi");
  host->package_set_metadata(host, "version", "26.1.0");
  return register_module(host);
}
