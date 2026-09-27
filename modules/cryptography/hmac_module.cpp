/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kHmacName =
    "cryptography.hazmat.bindings._rust.openssl.hmac";
constexpr const char* kHmacType =
    "cryptography.hazmat.bindings._rust.openssl.hmac.HMAC";

struct HmacData {
  X3PackageHost* host = nullptr;
  X3Value algorithm = x3_value_invalid();
  EVP_MAC_CTX* context = nullptr;
  bool finalized = false;

  ~HmacData() {
    if (algorithm.tag != X3_TAG_INVALID) host->value_release(algorithm);
    EVP_MAC_CTX_free(context);
  }
};

void cleanup_hmac(void* pointer) { delete static_cast<HmacData*>(pointer); }

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

X3Status buffer_type_error(CryptographyNativeState* state,
                           X3CallContext* call, X3Runtime* runtime,
                           X3Value value, const char* argument,
                           bool bytes_only = false) {
  X3Value type_function = x3_value_invalid();
  X3Value repr_function = x3_value_invalid();
  X3Value type_value = x3_value_invalid();
  X3Value repr_value = x3_value_invalid();
  std::string type_repr = "<class 'object'>";
  bool is_string = false;
  if (state->host->builtin_value(state->host, "type", &type_function) ==
          X3_STATUS_OK &&
      state->host->builtin_value(state->host, "repr", &repr_function) ==
          X3_STATUS_OK &&
      state->host->call(runtime, type_function, &value, 1, &type_value) ==
          X3_STATUS_OK &&
      state->host->call(runtime, repr_function, &type_value, 1,
                        &repr_value) == X3_STATUS_OK) {
    const char* text = state->host->value_to_cstr(runtime, repr_value);
    if (text != nullptr) type_repr = text;
    is_string = type_repr == "<class 'str'>";
  }
  for (X3Value item : {repr_value, type_value, repr_function, type_function})
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
  state->host->clear_exception(call);
  std::string message;
  if (bytes_only) {
    const size_t quote = type_repr.find(static_cast<char>(39));
    const size_t end = type_repr.rfind(static_cast<char>(39));
    const std::string name = quote != std::string::npos && end > quote
        ? type_repr.substr(quote + 1, end - quote - 1) : "object";
    message = std::string("argument '") + argument + "': '" + name +
        "' object cannot be converted to 'PyBytes'";
  } else {
    message = std::string("argument '") + argument +
        "': Cannot convert \"" + type_repr +
        "\" instance to a buffer.";
    if (is_string) message += "\nDid you mean to pass a bytestring instead?";
  }
  return fail(state, call, "TypeError", message.c_str());
}

X3Status crypto_exception(CryptographyNativeState* state,
                          X3CallContext* call, X3Runtime* runtime,
                          const char* class_name, const char* message) {
  X3Value importer = x3_value_invalid();
  X3Value name = x3_value_invalid();
  X3Value fromlist = x3_value_invalid();
  X3Value item = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value klass = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) goto done;
  name = state->host->value_string(runtime, "cryptography.exceptions");
  fromlist = state->host->value_list(runtime);
  item = state->host->value_string(runtime, class_name);
  if (name.tag == X3_TAG_INVALID || fromlist.tag == X3_TAG_INVALID ||
      item.tag == X3_TAG_INVALID ||
      state->host->list_append(runtime, fromlist, item) != X3_STATUS_OK)
    goto done;
  {
    const X3Value args[] = {name, x3_value_none(), x3_value_none(), fromlist};
    if (state->host->call(runtime, importer, args, 4, &module) != X3_STATUS_OK)
      goto done;
  }
  if (state->host->get_attr(runtime, module, class_name, &klass) !=
      X3_STATUS_OK) goto done;
  status = state->host->raise_error(call, klass, message);
done:
  for (X3Value value : {klass, module, item, fromlist, name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

HmacData* require_hmac(CryptographyNativeState* state, X3CallContext* call,
                       X3Value value) {
  auto* data = static_cast<HmacData*>(
      state->host->instance_get_native_data(value, kHmacType));
  if (data == nullptr) fail(state, call, "TypeError", "expected HMAC context");
  return data;
}

bool digest_name(CryptographyNativeState* state, X3Runtime* runtime,
                 X3Value algorithm, std::string& name) {
  X3Value name_value = x3_value_invalid();
  if (state->host->get_attr(runtime, algorithm, "name", &name_value) !=
      X3_STATUS_OK) return false;
  const char* data = nullptr;
  uint64_t size = 0;
  const X3Status status = state->host->value_string_data(
      runtime, name_value, &data, &size);
  if (status == X3_STATUS_OK)
    name.assign(data, static_cast<size_t>(size));
  state->host->value_release(name_value);
  if (status != X3_STATUS_OK) return false;
  if (name == "blake2b" || name == "blake2s") {
    X3Value digest_size = x3_value_invalid();
    if (state->host->get_attr(runtime, algorithm, "digest_size",
                              &digest_size) != X3_STATUS_OK) return false;
    const int64_t bytes = digest_size.tag == X3_TAG_INT64
        ? digest_size.as.i64 : -1;
    state->host->value_release(digest_size);
    if (bytes <= 0) return false;
    name += std::to_string(static_cast<uint64_t>(bytes) * 8);
  }
  return EVP_get_digestbyname(name.c_str()) != nullptr;
}

X3Status hmac_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 3 || argc > 4)
    return fail(state, call, "TypeError", "HMAC() requires key and algorithm");
  X3Buffer* key_buffer = nullptr;
  X3BufferInfo key{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &key_buffer, &key) !=
      X3_STATUS_OK)
    return buffer_type_error(state, call, runtime, args[1], "key");
  std::string name;
  const bool supported = digest_name(state, runtime, args[2], name);
  if (!supported) {
    state->host->buffer_release(key_buffer);
    state->host->clear_exception(call);
    return fail(state, call, "TypeError",
                "Expected instance of hashes.HashAlgorithm.");
  }
  if (key.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    state->host->buffer_release(key_buffer);
    return fail(state, call, "OverflowError", "key is too large");
  }
  std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(
      EVP_MAC_fetch(nullptr, "HMAC", nullptr), EVP_MAC_free);
  auto data = std::make_unique<HmacData>();
  data->host = state->host;
  data->algorithm = args[2];
  state->host->value_retain(data->algorithm);
  data->context = mac ? EVP_MAC_CTX_new(mac.get()) : nullptr;
  std::array<OSSL_PARAM, 2> params = {
      OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, name.data(), 0),
      OSSL_PARAM_construct_end()};
  const bool okay = data->context != nullptr &&
      EVP_MAC_init(data->context,
                   static_cast<const unsigned char*>(key.data),
                   static_cast<size_t>(key.size), params.data()) == 1;
  state->host->buffer_release(key_buffer);
  if (!okay)
    return fail(state, call, "ValueError", "unable to initialize HMAC");
  if (state->host->instance_set_native_data(args[0], kHmacType, data.get(),
                                            cleanup_hmac) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status hmac_algorithm(X3CallContext* call, X3Runtime*, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "algorithm getter");
  auto* data = require_hmac(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->algorithm;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status hmac_update(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "update() takes 1 argument");
  auto* data = require_hmac(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  const bool okay = EVP_MAC_update(
      data->context, static_cast<const unsigned char*>(info.data),
      static_cast<size_t>(info.size)) == 1;
  state->host->buffer_release(buffer);
  if (!okay) return fail(state, call, "ValueError", "HMAC update failed");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

bool finish_hmac(HmacData* data, std::vector<unsigned char>& digest) {
  const size_t size = EVP_MAC_CTX_get_mac_size(data->context);
  if (size == 0) return false;
  digest.resize(size);
  size_t written = 0;
  if (EVP_MAC_final(data->context, digest.data(), &written, digest.size()) != 1)
    return false;
  digest.resize(written);
  data->finalized = true;
  return true;
}

X3Status hmac_finalize(X3CallContext* call, X3Runtime* runtime, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "finalize() takes no arguments");
  auto* data = require_hmac(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  std::vector<unsigned char> digest;
  if (!finish_hmac(data, digest))
    return fail(state, call, "ValueError", "HMAC finalize failed");
  *result = state->host->value_bytes(runtime, digest.data(), digest.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status hmac_verify(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "verify() takes 1 argument");
  auto* data = require_hmac(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  X3Value bytes_class = x3_value_invalid();
  X3Value isinstance_function = x3_value_invalid();
  X3Value is_bytes = x3_value_invalid();
  bool bytes_argument = false;
  if (state->host->builtin_value(state->host, "bytes", &bytes_class) ==
          X3_STATUS_OK &&
      state->host->builtin_value(state->host, "isinstance",
                                 &isinstance_function) == X3_STATUS_OK) {
    const X3Value check_args[] = {args[1], bytes_class};
    if (state->host->call(runtime, isinstance_function, check_args, 2,
                          &is_bytes) == X3_STATUS_OK)
      bytes_argument = is_bytes.tag == X3_TAG_BOOL && is_bytes.as.b;
  }
  for (X3Value item : {is_bytes, isinstance_function, bytes_class})
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
  if (!bytes_argument)
    return buffer_type_error(state, call, runtime, args[1], "signature", true);
  const void* signature = nullptr;
  uint64_t signature_size = 0;
  if (state->host->value_bytes_data(runtime, args[1], &signature,
                                    &signature_size) != X3_STATUS_OK)
    return buffer_type_error(state, call, runtime, args[1], "signature", true);
  std::vector<unsigned char> digest;
  const bool finished = finish_hmac(data, digest);
  const bool equal = finished && signature_size == digest.size() &&
      CRYPTO_memcmp(signature, digest.data(), digest.size()) == 0;
  if (!finished) return fail(state, call, "ValueError", "HMAC finalize failed");
  if (!equal)
    return crypto_exception(state, call, runtime, "InvalidSignature",
                            "Signature did not match digest.");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status hmac_copy(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "copy() takes no arguments");
  auto* source = require_hmac(state, call, args[0]);
  if (source == nullptr) return X3_STATUS_ERROR;
  if (source->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  auto copy = std::make_unique<HmacData>();
  copy->host = state->host;
  copy->algorithm = source->algorithm;
  state->host->value_retain(copy->algorithm);
  copy->context = EVP_MAC_CTX_dup(source->context);
  if (copy->context == nullptr)
    return fail(state, call, "ValueError", "HMAC copy failed");
  X3Value instance = state->host->value_instance(runtime, state->hmac_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kHmacType, copy.get(),
                                            cleanup_hmac) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  copy.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status attach_child(X3PackageHost* host, X3Module* parent,
                      X3Module* child, const char* attribute) {
  X3Value value = x3_value_invalid();
  if (host->module_get_value(child, &value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status status = host->module_add_value(parent, attribute, value);
  host->value_release(value);
  return status;
}

}  // namespace

X3Status register_openssl_hmac_module(X3Module* openssl,
                                      CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* hmac = nullptr;
  if (host->add_module(host, kHmacName, &hmac) != X3_STATUS_OK ||
      attach_child(host, openssl, hmac, "hmac") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", hmac_init, state, 3, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "update", hmac_update, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "finalize", hmac_finalize, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "verify", hmac_verify, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "copy", hmac_copy, state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(hmac, "HMAC", methods,
                             static_cast<uint32_t>(std::size(methods)),
                             &state->hmac_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value property = x3_value_invalid();
  if (host->property_create(host->runtime, "algorithm", hmac_algorithm,
                            nullptr, state, &property) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status status = host->class_add_value(
      state->hmac_class, "algorithm", property);
  host->value_release(property);
  return status;
}
