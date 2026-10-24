/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "native_state.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

namespace {
constexpr const char* kModule = "cryptography.hazmat.bindings._rust.openssl.aead";
constexpr const char* kType = "cryptography.hazmat.bindings._rust.openssl.aead.AESGCM";

struct Secret {
  std::vector<unsigned char> bytes;
  ~Secret() { if (!bytes.empty()) OPENSSL_cleanse(bytes.data(), bytes.size()); }
};
void cleanup(void* data) { delete static_cast<Secret*>(data); }

struct Buffer {
  X3PackageHost* host;
  X3Buffer* handle = nullptr;
  X3BufferInfo info{};
  explicit Buffer(X3PackageHost* value) : host(value) {}
  ~Buffer() { if (handle) host->buffer_release(handle); }
  bool acquire(X3Runtime* runtime, X3Value value) {
    return host->buffer_acquire(runtime, value, 0, &handle, &info) == X3_STATUS_OK;
  }
  const unsigned char* data() const { return static_cast<const unsigned char*>(info.data); }
};

X3Status invalid_tag(CryptographyNativeState* state, X3CallContext* call, X3Runtime* runtime) {
  X3Value importer = x3_value_invalid(), name = x3_value_invalid(), fromlist = x3_value_invalid();
  X3Value item = x3_value_invalid(), module = x3_value_invalid(), klass = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) == X3_STATUS_OK) {
    name = state->host->value_string(runtime, "cryptography.exceptions");
    item = state->host->value_string(runtime, "InvalidTag");
    fromlist = state->host->value_list(runtime);
    if (name.tag != X3_TAG_INVALID && item.tag != X3_TAG_INVALID && fromlist.tag != X3_TAG_INVALID &&
        state->host->list_append(runtime, fromlist, item) == X3_STATUS_OK) {
      const X3Value args[] = {name, x3_value_none(), x3_value_none(), fromlist};
      if (state->host->call(runtime, importer, args, 4, &module) == X3_STATUS_OK &&
          state->host->get_attr(runtime, module, "InvalidTag", &klass) == X3_STATUS_OK)
        status = state->host->raise_error(call, klass, "");
    }
  }
  for (X3Value value : {klass, module, item, fromlist, name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

X3Status initialize(X3CallContext* call, X3Runtime* runtime, void* context,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(context);
  if (argc != 2) return state->host->raise_class_error(call, "TypeError", "AESGCM requires a key");
  Buffer key(state->host);
  // The buffer ABI reports failure through runtime last_error, not a Python
  // exception in this call context. Convert rejected AESGCM inputs explicitly.
  if (!key.acquire(runtime, args[1]))
    return state->host->raise_class_error(call, "TypeError", "key must be bytes-like");
  if (key.info.size != 16 && key.info.size != 24 && key.info.size != 32)
    return state->host->raise_class_error(call, "ValueError", "AESGCM key must be 128, 192, or 256 bits");
  auto secret = std::make_unique<Secret>();
  secret->bytes.assign(key.data(), key.data() + key.info.size);
  if (state->host->instance_set_native_data(args[0], kType, secret.get(), cleanup) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  secret.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status crypt(X3CallContext* call, X3Runtime* runtime, void* context,
               const X3Value* args, uint32_t argc, X3Value* result, bool encrypt) {
  auto* state = static_cast<CryptographyNativeState*>(context);
  if (argc != 4) return state->host->raise_class_error(call, "TypeError", "nonce, data and associated_data are required");
  auto* key = static_cast<Secret*>(state->host->instance_get_native_data(args[0], kType));
  if (!key) return state->host->raise_class_error(call, "TypeError", "expected initialized AESGCM instance");
  Buffer nonce(state->host), input(state->host), aad(state->host);
  if (!nonce.acquire(runtime, args[1]))
    return state->host->raise_class_error(call, "TypeError", "nonce must be bytes-like");
  if (!input.acquire(runtime, args[2]))
    return state->host->raise_class_error(call, "TypeError", "data must be bytes-like");
  if (args[3].tag != X3_TAG_NONE && !aad.acquire(runtime, args[3]))
    return state->host->raise_class_error(call, "TypeError", "associated_data must be bytes-like");
  if (nonce.info.size < 8 || nonce.info.size > 128)
    return state->host->raise_class_error(call, "ValueError", "Nonce must be between 8 and 128 bytes");
  constexpr uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int>::max());
  if (input.info.size > limit || aad.info.size > limit)
    return state->host->raise_class_error(call, "OverflowError", "Data or associated data is too long");
  if (!encrypt && input.info.size < 16) return invalid_tag(state, call, runtime);
  const size_t data_size = static_cast<size_t>(input.info.size) - (encrypt ? 0 : 16);
  const EVP_CIPHER* cipher = key->bytes.size() == 16 ? EVP_aes_128_gcm() :
      key->bytes.size() == 24 ? EVP_aes_192_gcm() : EVP_aes_256_gcm();
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx || EVP_CipherInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr, encrypt ? 1 : 0) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.info.size), nullptr) != 1 ||
      EVP_CipherInit_ex(ctx.get(), nullptr, nullptr, key->bytes.data(), nonce.data(), -1) != 1)
    return state->host->raise_class_error(call, "ValueError", "Unable to initialize AESGCM");
  int count = 0;
  if (aad.info.size && EVP_CipherUpdate(ctx.get(), nullptr, &count, aad.data(), static_cast<int>(aad.info.size)) != 1)
    return state->host->raise_class_error(call, "ValueError", "Unable to authenticate associated data");
  Secret output;
  output.bytes.resize(data_size + 16);
  int written = 0;
  if (data_size && EVP_CipherUpdate(ctx.get(), output.bytes.data(), &written, input.data(), static_cast<int>(data_size)) != 1)
    return state->host->raise_class_error(call, "ValueError", "AESGCM operation failed");
  if (!encrypt && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16,
                                     const_cast<unsigned char*>(input.data() + data_size)) != 1)
    return state->host->raise_class_error(call, "ValueError", "Unable to set authentication tag");
  int final_bytes = 0;
  if (EVP_CipherFinal_ex(ctx.get(), output.bytes.data() + written, &final_bytes) != 1) {
    if (!encrypt) return invalid_tag(state, call, runtime);
    return state->host->raise_class_error(call, "ValueError", "AESGCM finalization failed");
  }
  const size_t total = static_cast<size_t>(written + final_bytes);
  if (total != data_size) return state->host->raise_class_error(call, "ValueError", "Unexpected AESGCM output size");
  if (encrypt && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, output.bytes.data() + total) != 1)
    return state->host->raise_class_error(call, "ValueError", "Unable to obtain authentication tag");
  *result = state->host->value_bytes(runtime, output.bytes.data(), total + (encrypt ? 16 : 0));
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status encrypt(X3CallContext* call, X3Runtime* runtime, void* state, const X3Value* args, uint32_t argc, X3Value* result) {
  return crypt(call, runtime, state, args, argc, result, true);
}
X3Status decrypt(X3CallContext* call, X3Runtime* runtime, void* state, const X3Value* args, uint32_t argc, X3Value* result) {
  return crypt(call, runtime, state, args, argc, result, false);
}

X3Status generate(X3CallContext* call, X3Runtime* runtime, void* context, const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(context);
  if (argc != 1 || (args[0].tag != X3_TAG_INT64 && args[0].tag != X3_TAG_UINT64))
    return state->host->raise_class_error(call, "TypeError", "bit_length must be an integer");
  const uint64_t bits = args[0].tag == X3_TAG_UINT64 ? args[0].as.u64 : static_cast<uint64_t>(args[0].as.i64);
  if (bits != 128 && bits != 192 && bits != 256)
    return state->host->raise_class_error(call, "ValueError", "bit_length must be 128, 192, or 256");
  std::array<unsigned char, 32> key{};
  if (RAND_bytes(key.data(), static_cast<int>(bits / 8)) != 1)
    return state->host->raise_class_error(call, "RuntimeError", "Secure key generation failed");
  *result = state->host->value_bytes(runtime, key.data(), bits / 8);
  OPENSSL_cleanse(key.data(), key.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status bind_keywords(X3CallContext* call, X3Runtime* runtime, void* context,
                       const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
                       uint32_t kwargc, X3Value* result, X3NativeFn target,
                       const char* const* names, uint32_t count, uint32_t offset) {
  auto* state = static_cast<CryptographyNativeState*>(context);
  const uint32_t total = count + offset;
  std::array<X3Value, 4> values{};
  std::array<bool, 4> present{};
  if (argc > total || argc < offset)
    return state->host->raise_class_error(call, "TypeError", "Invalid positional argument count");
  for (uint32_t i = 0; i < argc; ++i) { values[i] = args[i]; present[i] = true; }
  for (uint32_t i = 0; i < kwargc; ++i) {
    uint32_t position = count;
    for (uint32_t j = 0; j < count; ++j)
      if (std::strcmp(kwargs[i].name, names[j]) == 0) { position = j; break; }
    if (position == count)
      return state->host->raise_class_error(call, "TypeError", "Unexpected keyword argument");
    position += offset;
    if (present[position])
      return state->host->raise_class_error(call, "TypeError", "Multiple values for argument");
    present[position] = true;
    values[position] = kwargs[i].value;
  }
  for (uint32_t i = 0; i < total; ++i)
    if (!present[i]) return state->host->raise_class_error(call, "TypeError", "Missing required argument");
  return target(call, runtime, context, values.data(), total, result);
}

X3Status initialize_kw(X3CallContext* call, X3Runtime* runtime, void* context,
                       const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs, uint32_t count, X3Value* result) {
  const char* names[] = {"key"};
  return bind_keywords(call, runtime, context, args, argc, kwargs, count, result, initialize, names, 1, 1);
}
X3Status encrypt_kw(X3CallContext* call, X3Runtime* runtime, void* context,
                    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs, uint32_t count, X3Value* result) {
  const char* names[] = {"nonce", "data", "associated_data"};
  return bind_keywords(call, runtime, context, args, argc, kwargs, count, result, encrypt, names, 3, 1);
}
X3Status decrypt_kw(X3CallContext* call, X3Runtime* runtime, void* context,
                    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs, uint32_t count, X3Value* result) {
  const char* names[] = {"nonce", "data", "associated_data"};
  return bind_keywords(call, runtime, context, args, argc, kwargs, count, result, decrypt, names, 3, 1);
}
X3Status generate_kw(X3CallContext* call, X3Runtime* runtime, void* context,
                     const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs, uint32_t count, X3Value* result) {
  const char* names[] = {"bit_length"};
  return bind_keywords(call, runtime, context, args, argc, kwargs, count, result, generate, names, 1, 0);
}
}  // namespace

X3Status register_openssl_aead_module(X3Module* openssl, CryptographyNativeState* state) {
  auto* host = state->host;
  X3Module* module = nullptr;
  X3Value module_value = x3_value_invalid();
  if (host->add_module(host, kModule, &module) != X3_STATUS_OK ||
      host->module_get_value(module, &module_value) != X3_STATUS_OK) return X3_STATUS_ERROR;
  const X3Status attached = host->module_add_value(openssl, "aead", module_value);
  host->value_release(module_value);
  if (attached != X3_STATUS_OK) return attached;
  const X3NativeFunctionDef methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", initialize, state, 2, 2, 0, initialize_kw},
      {sizeof(X3NativeFunctionDef), "encrypt", encrypt, state, 4, 4, 0, encrypt_kw},
      {sizeof(X3NativeFunctionDef), "decrypt", decrypt, state, 4, 4, 0, decrypt_kw}};
  if (host->module_add_class(module, "AESGCM", methods, static_cast<uint32_t>(std::size(methods)), &state->aesgcm_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef generator = {sizeof(X3NativeFunctionDef), "_aesgcm_generate_key", generate, state, 1, 1, 0, generate_kw};
  if (host->module_add_function(module, &generator) != X3_STATUS_OK) return X3_STATUS_ERROR;
  X3Value source = x3_value_invalid(), static_class = x3_value_invalid(), wrapper = x3_value_invalid();
  if (host->module_get_value(module, &module_value) != X3_STATUS_OK) return X3_STATUS_ERROR;
  X3Status status = X3_STATUS_ERROR;
  if (host->get_attr(host->runtime, module_value, "_aesgcm_generate_key", &source) == X3_STATUS_OK &&
      host->builtin_value(host, "staticmethod", &static_class) == X3_STATUS_OK &&
      host->call(host->runtime, static_class, &source, 1, &wrapper) == X3_STATUS_OK)
    status = host->class_add_value(state->aesgcm_class, "generate_key", wrapper);
  for (X3Value value : {wrapper, static_class, source, module_value})
    if (value.tag != X3_TAG_INVALID) host->value_release(value);
  return status;
}
