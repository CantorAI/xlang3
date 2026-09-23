/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kHashType = "_hashlib.HASH";
constexpr const char* kHmacType = "_hashlib.HMAC";

struct ConstructorSpec;
struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value hash_class = x3_value_invalid();
  X3Value hmac_class = x3_value_invalid();
  X3Value unsupported_error = x3_value_invalid();
  std::vector<std::unique_ptr<ConstructorSpec>> constructors;
};

struct ConstructorSpec {
  PackageState* package = nullptr;
  const char* algorithm = nullptr;
};

struct HashState {
  EVP_MD_CTX* context = nullptr;
  const EVP_MD* digest = nullptr;
  std::string name;
  bool xof = false;
};

struct HmacState {
  HMAC_CTX* context = nullptr;
  const EVP_MD* digest = nullptr;
  std::string name;
};

void cleanup_hash(void* pointer) {
  auto* state = static_cast<HashState*>(pointer);
  if (state) EVP_MD_CTX_free(state->context);
  delete state;
}

void cleanup_hmac(void* pointer) {
  auto* state = static_cast<HmacState*>(pointer);
  if (state) HMAC_CTX_free(state->context);
  delete state;
}

void cleanup_package(void* pointer) {
  auto* state = static_cast<PackageState*>(pointer);
  if (!state) return;
  state->host->value_release(state->hash_class);
  state->host->value_release(state->hmac_class);
  state->host->value_release(state->unsupported_error);
  delete state;
}

const X3Value* keyword(const X3KeywordArg* kwargs, uint32_t count, std::string_view name) {
  for (uint32_t index = 0; index < count; ++index)
    if (kwargs[index].name && name == kwargs[index].name) return &kwargs[index].value;
  return nullptr;
}

bool argc_ok(PackageState* state, X3CallContext* call, uint32_t argc,
             uint32_t minimum, uint32_t maximum, const char* function) {
  if (argc >= minimum && argc <= maximum) return true;
  const std::string message = std::string(function) + " received an invalid number of arguments";
  state->host->raise_class_error(call, "TypeError", message.c_str());
  return false;
}

X3Status openssl_error(PackageState* state, X3CallContext* call, const char* operation) {
  std::string message(operation);
  const unsigned long code = ERR_get_error();
  if (code) {
    char detail[256]{};
    ERR_error_string_n(code, detail, sizeof(detail));
    message += ": ";
    message += detail;
  }
  return state->host->raise_class_error(call, "ValueError", message.c_str());
}

bool bytes_data(PackageState* state, X3Runtime* runtime, X3Value value,
                const unsigned char** data, size_t* size) {
  const void* raw = nullptr;
  uint64_t length = 0;
  if (state->host->value_bytes_data(runtime, value, &raw, &length) != X3_STATUS_OK ||
      length > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false;
  *data = static_cast<const unsigned char*>(raw);
  *size = static_cast<size_t>(length);
  return true;
}

bool string_data(PackageState* state, X3Runtime* runtime, X3Value value,
                 std::string* output) {
  const char* text = nullptr;
  uint64_t size = 0;
  if (state->host->value_string_data(runtime, value, &text, &size) != X3_STATUS_OK) return false;
  output->assign(text, static_cast<size_t>(size));
  return true;
}

bool integer(X3Value value, uint64_t* output) {
  if (value.tag == X3_TAG_UINT64) { *output = value.as.u64; return true; }
  if (value.tag == X3_TAG_INT64 && value.as.i64 >= 0) {
    *output = static_cast<uint64_t>(value.as.i64); return true;
  }
  return false;
}

std::string normalize_name(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  std::replace(name.begin(), name.end(), '-', '_');
  return name;
}

const EVP_MD* find_digest(const std::string& requested, std::string* canonical) {
  std::string name = normalize_name(requested);
  std::string openssl_name = name;
  std::replace(openssl_name.begin(), openssl_name.end(), '_', '-');
  const EVP_MD* digest = EVP_get_digestbyname(openssl_name.c_str());
  if (!digest) digest = EVP_get_digestbyname(name.c_str());
  if (digest && canonical) *canonical = name;
  return digest;
}

bool digestmod_name(PackageState* state, X3Runtime* runtime, X3Value value,
                    std::string* result) {
  if (string_data(state, runtime, value, result)) return true;
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, value, "__name__", &name) != X3_STATUS_OK) return false;
  const bool ok = string_data(state, runtime, name, result);
  state->host->value_release(name);
  if (ok && result->rfind("openssl_", 0) == 0) result->erase(0, 8);
  return ok;
}

HashState* hash_state(PackageState* state, X3CallContext* call, X3Value value) {
  auto* native = static_cast<HashState*>(state->host->instance_get_native_data(value, kHashType));
  if (!native) state->host->raise_class_error(call, "TypeError", "invalid hash object");
  return native;
}

HmacState* hmac_state(PackageState* state, X3CallContext* call, X3Value value) {
  auto* native = static_cast<HmacState*>(state->host->instance_get_native_data(value, kHmacType));
  if (!native) state->host->raise_class_error(call, "TypeError", "invalid HMAC object");
  return native;
}

X3Status make_hash(PackageState* state, X3CallContext* call, X3Runtime* runtime,
                   const std::string& requested, X3Value initial, bool has_initial,
                   X3Value* result) {
  std::string canonical;
  const EVP_MD* digest = find_digest(requested, &canonical);
  if (!digest) {
    const std::string message = "unsupported hash type " + requested;
    return state->host->raise_class_error(call, "ValueError", message.c_str());
  }
  auto native = std::make_unique<HashState>();
  native->context = EVP_MD_CTX_new();
  native->digest = digest;
  native->name = canonical;
  native->xof = (EVP_MD_get_flags(digest) & EVP_MD_FLAG_XOF) != 0;
  if (!native->context || EVP_DigestInit_ex(native->context, digest, nullptr) != 1)
    return openssl_error(state, call, "cannot initialize hash");
  if (has_initial) {
    const unsigned char* data = nullptr;
    size_t size = 0;
    if (!bytes_data(state, runtime, initial, &data, &size))
      return state->host->raise_class_error(call, "TypeError", "object supporting the buffer API required");
    if (size && EVP_DigestUpdate(native->context, data, size) != 1)
      return openssl_error(state, call, "cannot update hash");
  }
  X3Value instance = state->host->value_instance(runtime, state->hash_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kHashType, native.get(), cleanup_hash) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return state->host->raise_class_error(call, "RuntimeError", "cannot allocate hash object");
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status named_hash_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                       const X3Value* args, uint32_t argc,
                       const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* spec = static_cast<ConstructorSpec*>(user_data);
  if (!argc_ok(spec->package, call, argc, 0, 1, spec->algorithm)) return X3_STATUS_ERROR;
  for (uint32_t index = 0; index < kwargc; ++index)
    if (!kwargs[index].name || std::string_view(kwargs[index].name) != "usedforsecurity")
      return spec->package->host->raise_class_error(call, "TypeError", "unexpected keyword argument");
  return make_hash(spec->package, call, runtime, spec->algorithm,
                   argc ? args[0] : x3_value_none(), argc != 0, result);
}

X3Status named_hash(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  return named_hash_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status hash_new_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc,
                     const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 2, "new()")) return X3_STATUS_ERROR;
  for (uint32_t index = 0; index < kwargc; ++index)
    if (!kwargs[index].name || std::string_view(kwargs[index].name) != "usedforsecurity")
      return state->host->raise_class_error(call, "TypeError", "new() got an unexpected keyword argument");
  std::string name;
  if (!string_data(state, runtime, args[0], &name))
    return state->host->raise_class_error(call, "TypeError", "name must be a string");
  return make_hash(state, call, runtime, name, argc == 2 ? args[1] : x3_value_none(), argc == 2, result);
}

X3Status hash_new(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  return hash_new_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status hash_update(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 2, 2, "update()")) return X3_STATUS_ERROR;
  auto* native = hash_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  const unsigned char* data = nullptr;
  size_t size = 0;
  if (!bytes_data(state, runtime, args[1], &data, &size))
    return state->host->raise_class_error(call, "TypeError", "object supporting the buffer API required");
  if (size && EVP_DigestUpdate(native->context, data, size) != 1)
    return openssl_error(state, call, "cannot update hash");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status hash_output(PackageState* state, X3CallContext* call, X3Runtime* runtime,
                     const X3Value* args, uint32_t argc, bool hexadecimal, X3Value* result) {
  if (!argc_ok(state, call, argc, 1, 2, hexadecimal ? "hexdigest()" : "digest()"))
    return X3_STATUS_ERROR;
  auto* native = hash_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  uint64_t requested = 0;
  if (native->xof) {
    if (argc != 2 || !integer(args[1], &requested))
      return state->host->raise_class_error(call, "TypeError", "a non-negative output length is required");
  } else {
    if (argc != 1)
      return state->host->raise_class_error(call, "TypeError", "digest() takes no arguments");
    requested = static_cast<uint64_t>(EVP_MD_get_size(native->digest));
  }
  if (requested > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return state->host->raise_class_error(call, "OverflowError", "output length is too large");
  EVP_MD_CTX* copy = EVP_MD_CTX_new();
  std::vector<unsigned char> bytes(static_cast<size_t>(requested));
  unsigned int length = 0;
  bool ok = copy && EVP_MD_CTX_copy_ex(copy, native->context) == 1;
  if (ok && native->xof) ok = EVP_DigestFinalXOF(copy, bytes.data(), bytes.size()) == 1;
  else if (ok) ok = EVP_DigestFinal_ex(copy, bytes.data(), &length) == 1;
  EVP_MD_CTX_free(copy);
  if (!ok) return openssl_error(state, call, "cannot finalize hash");
  if (!native->xof) bytes.resize(length);
  if (!hexadecimal) {
    *result = state->host->value_bytes(runtime, bytes.data(), bytes.size());
    return X3_STATUS_OK;
  }
  static constexpr char digits[] = "0123456789abcdef";
  std::string text(bytes.size() * 2, '0');
  for (size_t index = 0; index < bytes.size(); ++index) {
    text[index * 2] = digits[bytes[index] >> 4];
    text[index * 2 + 1] = digits[bytes[index] & 15];
  }
  *result = state->host->value_string_utf8(runtime, text.data(), text.size());
  return X3_STATUS_OK;
}

X3Status hash_digest(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  return hash_output(static_cast<PackageState*>(user_data), call, runtime, args, argc, false, result);
}

X3Status hash_hexdigest(X3CallContext* call, X3Runtime* runtime, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  return hash_output(static_cast<PackageState*>(user_data), call, runtime, args, argc, true, result);
}

X3Status hash_copy(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "copy()")) return X3_STATUS_ERROR;
  auto* source = hash_state(state, call, args[0]);
  if (!source) return X3_STATUS_ERROR;
  auto native = std::make_unique<HashState>();
  native->context = EVP_MD_CTX_new();
  native->digest = source->digest;
  native->name = source->name;
  native->xof = source->xof;
  if (!native->context || EVP_MD_CTX_copy_ex(native->context, source->context) != 1)
    return openssl_error(state, call, "cannot copy hash");
  X3Value instance = state->host->value_instance(runtime, state->hash_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kHashType, native.get(), cleanup_hash) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return state->host->raise_class_error(call, "RuntimeError", "cannot allocate hash object");
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status hash_name(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "name")) return X3_STATUS_ERROR;
  auto* native = hash_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = state->host->value_string_utf8(runtime, native->name.data(), native->name.size());
  return X3_STATUS_OK;
}

X3Status hash_digest_size(X3CallContext* call, X3Runtime*, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "digest_size")) return X3_STATUS_ERROR;
  auto* native = hash_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = x3_value_int64(native->xof ? 0 : EVP_MD_get_size(native->digest));
  return X3_STATUS_OK;
}

X3Status hash_block_size(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "block_size")) return X3_STATUS_ERROR;
  auto* native = hash_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = x3_value_int64(EVP_MD_get_block_size(native->digest));
  return X3_STATUS_OK;
}

X3Status hmac_output(PackageState* state, X3CallContext* call, X3Runtime* runtime,
                     HmacState* native, bool hexadecimal, X3Value* result) {
  HMAC_CTX* copy = HMAC_CTX_new();
  std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
  unsigned int size = 0;
  const bool ok = copy && HMAC_CTX_copy(copy, native->context) == 1 &&
                  HMAC_Final(copy, bytes.data(), &size) == 1;
  HMAC_CTX_free(copy);
  if (!ok) return openssl_error(state, call, "cannot finalize HMAC");
  if (!hexadecimal) {
    *result = state->host->value_bytes(runtime, bytes.data(), size);
    return X3_STATUS_OK;
  }
  static constexpr char digits[] = "0123456789abcdef";
  std::string text(size * 2, '0');
  for (unsigned int index = 0; index < size; ++index) {
    text[index * 2] = digits[bytes[index] >> 4];
    text[index * 2 + 1] = digits[bytes[index] & 15];
  }
  *result = state->host->value_string_utf8(runtime, text.data(), text.size());
  return X3_STATUS_OK;
}

X3Status hmac_new_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc,
                     const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 3, "hmac_new()")) return X3_STATUS_ERROR;
  X3Value message = argc >= 2 ? args[1] : x3_value_none();
  X3Value digestmod = argc >= 3 ? args[2] : x3_value_none();
  if (const X3Value* value = keyword(kwargs, kwargc, "digestmod")) digestmod = *value;
  for (uint32_t index = 0; index < kwargc; ++index)
    if (!kwargs[index].name || std::string_view(kwargs[index].name) != "digestmod")
      return state->host->raise_class_error(call, "TypeError", "hmac_new() got an unexpected keyword argument");
  std::string digest_name;
  if (!digestmod_name(state, runtime, digestmod, &digest_name))
    return state->host->raise_error(call, state->unsupported_error, "unsupported digest type");
  std::string canonical;
  const EVP_MD* digest = find_digest(digest_name, &canonical);
  if (!digest) return state->host->raise_error(call, state->unsupported_error, "unsupported digest type");
  const unsigned char* key_data = nullptr;
  size_t key_size = 0;
  if (!bytes_data(state, runtime, args[0], &key_data, &key_size))
    return state->host->raise_class_error(call, "TypeError", "key must be bytes-like");
  if (key_size > static_cast<size_t>(std::numeric_limits<int>::max()))
    return state->host->raise_class_error(call, "OverflowError", "key is too long");
  auto native = std::make_unique<HmacState>();
  native->context = HMAC_CTX_new();
  native->digest = digest;
  native->name = "hmac-" + canonical;
  if (!native->context || HMAC_Init_ex(native->context, key_data, static_cast<int>(key_size), digest, nullptr) != 1)
    return openssl_error(state, call, "cannot initialize HMAC");
  if (message.tag != X3_TAG_NONE) {
    const unsigned char* data = nullptr;
    size_t size = 0;
    if (!bytes_data(state, runtime, message, &data, &size))
      return state->host->raise_class_error(call, "TypeError", "message must be bytes-like");
    if (size && HMAC_Update(native->context, data, size) != 1)
      return openssl_error(state, call, "cannot update HMAC");
  }
  X3Value instance = state->host->value_instance(runtime, state->hmac_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kHmacType, native.get(), cleanup_hmac) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return state->host->raise_class_error(call, "RuntimeError", "cannot allocate HMAC object");
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status hmac_new(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  return hmac_new_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status hmac_update(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 2, 2, "update()")) return X3_STATUS_ERROR;
  auto* native = hmac_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  const unsigned char* data = nullptr;
  size_t size = 0;
  if (!bytes_data(state, runtime, args[1], &data, &size))
    return state->host->raise_class_error(call, "TypeError", "object supporting the buffer API required");
  if (size && HMAC_Update(native->context, data, size) != 1)
    return openssl_error(state, call, "cannot update HMAC");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status hmac_digest(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "digest()")) return X3_STATUS_ERROR;
  auto* native = hmac_state(state, call, args[0]);
  return native ? hmac_output(state, call, runtime, native, false, result) : X3_STATUS_ERROR;
}

X3Status hmac_hexdigest(X3CallContext* call, X3Runtime* runtime, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "hexdigest()")) return X3_STATUS_ERROR;
  auto* native = hmac_state(state, call, args[0]);
  return native ? hmac_output(state, call, runtime, native, true, result) : X3_STATUS_ERROR;
}

X3Status hmac_copy(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "copy()")) return X3_STATUS_ERROR;
  auto* source = hmac_state(state, call, args[0]);
  if (!source) return X3_STATUS_ERROR;
  auto native = std::make_unique<HmacState>();
  native->context = HMAC_CTX_new();
  native->digest = source->digest;
  native->name = source->name;
  if (!native->context || HMAC_CTX_copy(native->context, source->context) != 1)
    return openssl_error(state, call, "cannot copy HMAC");
  X3Value instance = state->host->value_instance(runtime, state->hmac_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kHmacType, native.get(), cleanup_hmac) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return state->host->raise_class_error(call, "RuntimeError", "cannot allocate HMAC object");
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status hmac_name(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "name")) return X3_STATUS_ERROR;
  auto* native = hmac_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = state->host->value_string_utf8(runtime, native->name.data(), native->name.size());
  return X3_STATUS_OK;
}

X3Status hmac_digest_size(X3CallContext* call, X3Runtime*, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "digest_size")) return X3_STATUS_ERROR;
  auto* native = hmac_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = x3_value_int64(EVP_MD_get_size(native->digest));
  return X3_STATUS_OK;
}

X3Status hmac_block_size(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "block_size")) return X3_STATUS_ERROR;
  auto* native = hmac_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = x3_value_int64(EVP_MD_get_block_size(native->digest));
  return X3_STATUS_OK;
}

X3Status hmac_digest_once(X3CallContext* call, X3Runtime* runtime, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 3, 3, "hmac_digest()")) return X3_STATUS_ERROR;
  std::string name;
  if (!digestmod_name(state, runtime, args[2], &name))
    return state->host->raise_error(call, state->unsupported_error, "unsupported digest type");
  const EVP_MD* digest = find_digest(name, nullptr);
  if (!digest) return state->host->raise_error(call, state->unsupported_error, "unsupported digest type");
  const unsigned char *key = nullptr, *message = nullptr;
  size_t key_size = 0, message_size = 0;
  if (!bytes_data(state, runtime, args[0], &key, &key_size) ||
      !bytes_data(state, runtime, args[1], &message, &message_size))
    return state->host->raise_class_error(call, "TypeError", "key and message must be bytes-like");
  if (key_size > static_cast<size_t>(std::numeric_limits<int>::max()))
    return state->host->raise_class_error(call, "OverflowError", "key is too long");
  std::array<unsigned char, EVP_MAX_MD_SIZE> output{};
  unsigned int size = 0;
  if (!HMAC(digest, key, static_cast<int>(key_size), message, message_size, output.data(), &size))
    return openssl_error(state, call, "cannot compute HMAC");
  *result = state->host->value_bytes(runtime, output.data(), size);
  return X3_STATUS_OK;
}

X3Status compare_digest(X3CallContext* call, X3Runtime* runtime, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 2, 2, "compare_digest()")) return X3_STATUS_ERROR;
  const unsigned char *left = nullptr, *right = nullptr;
  size_t left_size = 0, right_size = 0;
  std::string left_text, right_text;
  if (!bytes_data(state, runtime, args[0], &left, &left_size) ||
      !bytes_data(state, runtime, args[1], &right, &right_size)) {
    if (!string_data(state, runtime, args[0], &left_text) ||
        !string_data(state, runtime, args[1], &right_text))
      return state->host->raise_class_error(call, "TypeError", "both arguments must be bytes-like or ASCII strings");
    if (std::any_of(left_text.begin(), left_text.end(), [](unsigned char c) { return c > 127; }) ||
        std::any_of(right_text.begin(), right_text.end(), [](unsigned char c) { return c > 127; }))
      return state->host->raise_class_error(call, "TypeError", "comparing strings with non-ASCII characters is not supported");
    left = reinterpret_cast<const unsigned char*>(left_text.data()); left_size = left_text.size();
    right = reinterpret_cast<const unsigned char*>(right_text.data()); right_size = right_text.size();
  }
  const bool equal = left_size == right_size && CRYPTO_memcmp(left, right, left_size) == 0;
  *result = x3_value_bool(equal);
  return X3_STATUS_OK;
}

X3Status pbkdf2_hmac(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 4, 5, "pbkdf2_hmac()")) return X3_STATUS_ERROR;
  std::string name;
  if (!string_data(state, runtime, args[0], &name))
    return state->host->raise_class_error(call, "TypeError", "hash_name must be a string");
  const EVP_MD* digest = find_digest(name, nullptr);
  if (!digest) return state->host->raise_class_error(call, "ValueError", "unsupported hash type");
  const unsigned char *password = nullptr, *salt = nullptr;
  size_t password_size = 0, salt_size = 0;
  uint64_t iterations = 0;
  if (!bytes_data(state, runtime, args[1], &password, &password_size) ||
      !bytes_data(state, runtime, args[2], &salt, &salt_size))
    return state->host->raise_class_error(call, "TypeError", "password and salt must be bytes-like");
  if (!integer(args[3], &iterations) || iterations == 0 || iterations > INT_MAX)
    return state->host->raise_class_error(call, "ValueError", "iteration value must be greater than 0");
  uint64_t length = static_cast<uint64_t>(EVP_MD_get_size(digest));
  if (argc == 5 && (!integer(args[4], &length) || length == 0 || length > INT_MAX))
    return state->host->raise_class_error(call, "ValueError", "key length must be greater than 0");
  if (password_size > INT_MAX || salt_size > INT_MAX)
    return state->host->raise_class_error(call, "OverflowError", "input is too long");
  std::vector<unsigned char> output(static_cast<size_t>(length));
  if (PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password), static_cast<int>(password_size),
                        salt, static_cast<int>(salt_size), static_cast<int>(iterations), digest,
                        static_cast<int>(length), output.data()) != 1)
    return openssl_error(state, call, "cannot derive key");
  *result = state->host->value_bytes(runtime, output.data(), output.size());
  return X3_STATUS_OK;
}

X3Status scrypt_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc,
                   const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_ok(state, call, argc, 1, 1, "scrypt()")) return X3_STATUS_ERROR;
  const X3Value *salt_value = keyword(kwargs, kwargc, "salt"),
                *n_value = keyword(kwargs, kwargc, "n"),
                *r_value = keyword(kwargs, kwargc, "r"),
                *p_value = keyword(kwargs, kwargc, "p"),
                *maxmem_value = keyword(kwargs, kwargc, "maxmem"),
                *dklen_value = keyword(kwargs, kwargc, "dklen");
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string_view name(kwargs[index].name ? kwargs[index].name : "");
    if (name != "salt" && name != "n" && name != "r" && name != "p" && name != "maxmem" && name != "dklen")
      return state->host->raise_class_error(call, "TypeError", "scrypt() got an unexpected keyword argument");
  }
  if (!salt_value || !n_value || !r_value || !p_value)
    return state->host->raise_class_error(call, "TypeError", "salt, n, r, and p are required");
  const unsigned char *password = nullptr, *salt = nullptr;
  size_t password_size = 0, salt_size = 0;
  uint64_t n = 0, r = 0, p = 0, maxmem = 0, dklen = 64;
  if (!bytes_data(state, runtime, args[0], &password, &password_size) ||
      !bytes_data(state, runtime, *salt_value, &salt, &salt_size))
    return state->host->raise_class_error(call, "TypeError", "password and salt must be bytes-like");
  if (!integer(*n_value, &n) || !integer(*r_value, &r) || !integer(*p_value, &p) ||
      (maxmem_value && !integer(*maxmem_value, &maxmem)) ||
      (dklen_value && !integer(*dklen_value, &dklen)) || dklen == 0)
    return state->host->raise_class_error(call, "ValueError", "invalid scrypt parameters");
  std::vector<unsigned char> output(static_cast<size_t>(dklen));
  if (EVP_PBE_scrypt(reinterpret_cast<const char*>(password), password_size, salt, salt_size,
                     n, r, p, maxmem, output.data(), output.size()) != 1)
    return openssl_error(state, call, "invalid scrypt parameters");
  *result = state->host->value_bytes(runtime, output.data(), output.size());
  return X3_STATUS_OK;
}

X3Status scrypt_plain(X3CallContext* call, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  return scrypt_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

bool add_property(PackageState* state, X3Runtime* runtime, X3Value klass,
                  const char* name, X3NativeFn getter) {
  X3Value property = x3_value_invalid();
  if (state->host->property_create(runtime, name, getter, nullptr, state, &property) != X3_STATUS_OK)
    return false;
  const bool ok = state->host->class_add_value(klass, name, property) == X3_STATUS_OK;
  state->host->value_release(property);
  return ok;
}

void define_method(X3NativeFunctionDef& def, const char* name, X3NativeFn callback,
                   PackageState* state) {
  def = {}; def.size = sizeof(def); def.name = name; def.callback = callback; def.user_data = state;
}

void add_function(PackageState* state, X3Module* module, const char* name,
                  X3NativeFn callback, X3NativeKeywordFn keywords = nullptr) {
  X3NativeFunctionDef def{};
  def.size = sizeof(def); def.name = name; def.callback = callback;
  def.keyword_callback = keywords; def.user_data = state;
  state->host->module_add_function(module, &def);
}

bool add_exception(PackageState* state, X3Module* module, const char* name,
                   X3Value base, X3Value* output) {
  return state->host->module_add_class(module, name, nullptr, 0, output) == X3_STATUS_OK &&
         state->host->class_set_base(*output, base) == X3_STATUS_OK;
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state; return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "_hashlib", &module) != X3_STATUS_OK) return X3_STATUS_ERROR;

  X3Value value_error = x3_value_invalid();
  if (host->builtin_value(host, "ValueError", &value_error) != X3_STATUS_OK ||
      !add_exception(state, module, "UnsupportedDigestmodError", value_error, &state->unsupported_error)) {
    host->value_release(value_error); return X3_STATUS_ERROR;
  }
  host->value_release(value_error);

  X3NativeFunctionDef hash_methods[4]{};
  define_method(hash_methods[0], "update", hash_update, state);
  define_method(hash_methods[1], "digest", hash_digest, state);
  define_method(hash_methods[2], "hexdigest", hash_hexdigest, state);
  define_method(hash_methods[3], "copy", hash_copy, state);
  if (host->module_add_class(module, "HASH", hash_methods, 4, &state->hash_class) != X3_STATUS_OK ||
      !add_property(state, host->runtime, state->hash_class, "name", hash_name) ||
      !add_property(state, host->runtime, state->hash_class, "digest_size", hash_digest_size) ||
      !add_property(state, host->runtime, state->hash_class, "block_size", hash_block_size)) return X3_STATUS_ERROR;

  X3NativeFunctionDef hmac_methods[4]{};
  define_method(hmac_methods[0], "update", hmac_update, state);
  define_method(hmac_methods[1], "digest", hmac_digest, state);
  define_method(hmac_methods[2], "hexdigest", hmac_hexdigest, state);
  define_method(hmac_methods[3], "copy", hmac_copy, state);
  if (host->module_add_class(module, "HMAC", hmac_methods, 4, &state->hmac_class) != X3_STATUS_OK ||
      !add_property(state, host->runtime, state->hmac_class, "name", hmac_name) ||
      !add_property(state, host->runtime, state->hmac_class, "digest_size", hmac_digest_size) ||
      !add_property(state, host->runtime, state->hmac_class, "block_size", hmac_block_size)) return X3_STATUS_ERROR;

  add_function(state, module, "new", hash_new, hash_new_kw);
  add_function(state, module, "hmac_new", hmac_new, hmac_new_kw);
  add_function(state, module, "hmac_digest", hmac_digest_once);
  add_function(state, module, "compare_digest", compare_digest);
  add_function(state, module, "pbkdf2_hmac", pbkdf2_hmac);
  add_function(state, module, "scrypt", scrypt_plain, scrypt_kw);

  static constexpr const char* algorithms[] = {
      "md5", "sha1", "sha224", "sha256", "sha384", "sha512",
      "sha3_224", "sha3_256", "sha3_384", "sha3_512", "shake_128", "shake_256"};
  X3Value names = host->value_list(host->runtime);
  for (const char* algorithm : algorithms) {
    auto spec = std::make_unique<ConstructorSpec>();
    spec->package = state; spec->algorithm = algorithm;
    const std::string exported = std::string("openssl_") + algorithm;
    X3NativeFunctionDef def{};
    def.size = sizeof(def); def.name = exported.c_str(); def.callback = named_hash;
    def.keyword_callback = named_hash_kw; def.user_data = spec.get();
    if (host->module_add_function(module, &def) != X3_STATUS_OK) return X3_STATUS_ERROR;
    state->constructors.push_back(std::move(spec));
    X3Value name = host->value_string(host->runtime, algorithm);
    host->list_append(host->runtime, names, name);
    host->value_release(name);
  }
  X3Value frozenset_class = x3_value_invalid();
  X3Value names_set = x3_value_invalid();
  if (host->builtin_value(host, "frozenset", &frozenset_class) != X3_STATUS_OK ||
      host->call(host->runtime, frozenset_class, &names, 1, &names_set) != X3_STATUS_OK ||
      host->module_add_value(module, "openssl_md_meth_names", names_set) != X3_STATUS_OK) {
    host->value_release(frozenset_class); host->value_release(names); host->value_release(names_set);
    return X3_STATUS_ERROR;
  }
  host->value_release(frozenset_class); host->value_release(names); host->value_release(names_set);
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "xlang__hashlib");
  host->package_set_metadata(host, "version", "0.1.0");
  return register_module(host);
}
