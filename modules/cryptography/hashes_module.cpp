/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/evp.h>
#include <openssl/opensslv.h>

#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kOpensslName =
    "cryptography.hazmat.bindings._rust.openssl";
constexpr const char* kHashesName =
    "cryptography.hazmat.bindings._rust.openssl.hashes";
constexpr const char* kHashType =
    "cryptography.hazmat.bindings._rust.openssl.hashes.Hash";
constexpr const char* kXofHashType =
    "cryptography.hazmat.bindings._rust.openssl.hashes.XOFHash";

struct HashData {
  X3PackageHost* host = nullptr;
  X3Value algorithm = x3_value_invalid();
  EVP_MD_CTX* context = nullptr;
  bool is_xof_hash = false;
  bool finalized = false;
  bool squeezed = false;
  uint64_t bytes_remaining = 0;

  ~HashData() {
    if (algorithm.tag != X3_TAG_INVALID) host->value_release(algorithm);
    EVP_MD_CTX_free(context);
  }
};

void cleanup_hash(void* pointer) { delete static_cast<HashData*>(pointer); }

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

X3Status already_finalized(CryptographyNativeState* state,
                           X3CallContext* call, X3Runtime* runtime,
                           const char* message) {
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
  item = state->host->value_string(runtime, "AlreadyFinalized");
  if (name.tag == X3_TAG_INVALID || fromlist.tag == X3_TAG_INVALID ||
      item.tag == X3_TAG_INVALID ||
      state->host->list_append(runtime, fromlist, item) != X3_STATUS_OK)
    goto done;
  {
    const X3Value args[] = {name, x3_value_none(), x3_value_none(), fromlist};
    if (state->host->call(runtime, importer, args, 4, &module) != X3_STATUS_OK)
      goto done;
  }
  if (state->host->get_attr(runtime, module, "AlreadyFinalized", &klass) !=
      X3_STATUS_OK) goto done;
  status = state->host->raise_error(call, klass, message);
done:
  for (X3Value value : {klass, module, item, fromlist, name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

bool algorithm_digest(CryptographyNativeState* state, X3Runtime* runtime,
                      X3Value algorithm, const EVP_MD** digest,
                      bool* is_xof, uint64_t* digest_size) {
  X3Value name_value = x3_value_invalid();
  X3Value size_value = x3_value_invalid();
  if (state->host->get_attr(runtime, algorithm, "name", &name_value) !=
      X3_STATUS_OK) return false;
  const char* name_data = nullptr;
  uint64_t name_size = 0;
  if (state->host->value_string_data(runtime, name_value, &name_data,
                                     &name_size) != X3_STATUS_OK) {
    state->host->value_release(name_value);
    return false;
  }
  std::string name(name_data, static_cast<size_t>(name_size));
  state->host->value_release(name_value);
  *is_xof = name == "shake128" || name == "shake256";
  if (*is_xof || name == "blake2b" || name == "blake2s") {
    if (state->host->get_attr(runtime, algorithm, "digest_size", &size_value) !=
        X3_STATUS_OK) return false;
    if (size_value.tag == X3_TAG_INT64 && size_value.as.i64 > 0)
      *digest_size = static_cast<uint64_t>(size_value.as.i64);
    else if (size_value.tag == X3_TAG_UINT64 && size_value.as.u64 > 0)
      *digest_size = size_value.as.u64;
    else {
      state->host->value_release(size_value);
      return false;
    }
    state->host->value_release(size_value);
  }
  if (name == "blake2b" || name == "blake2s")
    name += std::to_string(*digest_size * 8);
  *digest = EVP_get_digestbyname(name.c_str());
  return *digest != nullptr;
}

HashData* require_hash(CryptographyNativeState* state, X3CallContext* call,
                       X3Value value, const char* kind) {
  auto* data = static_cast<HashData*>(
      state->host->instance_get_native_data(value, kind));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected cryptography hash context");
  return data;
}

X3Status make_hash(CryptographyNativeState* state, X3CallContext* call,
                   X3Runtime* runtime, X3Value self, X3Value algorithm,
                   bool xof_hash) {
  const EVP_MD* digest = nullptr;
  bool algorithm_is_xof = false;
  uint64_t digest_size = 0;
  if (!algorithm_digest(state, runtime, algorithm, &digest,
                        &algorithm_is_xof, &digest_size))
    return fail(state, call, "TypeError",
                "Expected instance of hashes.HashAlgorithm.");
  if (xof_hash && !algorithm_is_xof)
    return fail(state, call, "TypeError",
                "Expected instance of an extendable output function.");
  auto data = std::make_unique<HashData>();
  data->host = state->host;
  data->algorithm = algorithm;
  state->host->value_retain(algorithm);
  data->context = EVP_MD_CTX_new();
  data->is_xof_hash = xof_hash;
  data->bytes_remaining = digest_size;
  if (data->context == nullptr ||
      EVP_DigestInit_ex(data->context, digest, nullptr) != 1)
    return fail(state, call, "ValueError", "unable to initialize hash context");
  if (state->host->instance_set_native_data(
          self, xof_hash ? kXofHashType : kHashType, data.get(),
          cleanup_hash) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  return X3_STATUS_OK;
}

X3Status hash_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 2 || argc > 3)
    return fail(state, call, "TypeError", "Hash() requires an algorithm");
  const X3Status status = make_hash(state, call, runtime, args[0], args[1], false);
  if (status == X3_STATUS_OK) *result = x3_value_none();
  return status;
}

X3Status xof_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "XOFHash() requires an algorithm");
  const X3Status status = make_hash(state, call, runtime, args[0], args[1], true);
  if (status == X3_STATUS_OK) *result = x3_value_none();
  return status;
}

X3Status hash_algorithm(X3CallContext* call, X3Runtime*, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "hash algorithm getter");
  auto* data = static_cast<HashData*>(state->host->instance_get_native_data(
      args[0], kHashType));
  if (data == nullptr)
    data = require_hash(state, call, args[0], kXofHashType);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->algorithm;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status hash_update(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "update() takes 1 argument");
  auto* data = static_cast<HashData*>(state->host->instance_get_native_data(
      args[0], kHashType));
  if (data == nullptr)
    data = require_hash(state, call, args[0], kXofHashType);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return already_finalized(state, call, runtime,
                             "Context was already finalized.");
  if (data->squeezed)
    return already_finalized(state, call, runtime,
                             "Context was already squeezed.");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  const bool okay = EVP_DigestUpdate(data->context, info.data,
                                      static_cast<size_t>(info.size)) == 1;
  state->host->buffer_release(buffer);
  if (!okay) return fail(state, call, "ValueError", "hash update failed");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status hash_finalize(X3CallContext* call, X3Runtime* runtime, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "finalize() takes no arguments");
  auto* data = require_hash(state, call, args[0], kHashType);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return already_finalized(state, call, runtime,
                             "Context was already finalized.");
  X3Value algorithm_name = x3_value_invalid();
  if (state->host->get_attr(runtime, data->algorithm, "name",
                            &algorithm_name) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* name_data = state->host->value_to_cstr(runtime, algorithm_name);
  const std::string name = name_data == nullptr ? "" : name_data;
  state->host->value_release(algorithm_name);
  const bool xof = name == "shake128" || name == "shake256";
  std::vector<unsigned char> digest;
  if (xof) {
    if (data->bytes_remaining > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
      return fail(state, call, "ValueError", "digest size is too large");
    digest.resize(static_cast<size_t>(data->bytes_remaining));
    if (EVP_DigestFinalXOF(data->context, digest.data(), digest.size()) != 1)
      return fail(state, call, "ValueError", "hash finalize failed");
  } else {
    digest.resize(EVP_MAX_MD_SIZE);
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(data->context, digest.data(), &size) != 1)
      return fail(state, call, "ValueError", "hash finalize failed");
    digest.resize(size);
  }
  data->finalized = true;
  *result = state->host->value_bytes(runtime, digest.data(), digest.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status hash_copy(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "copy() takes no arguments");
  auto* source = static_cast<HashData*>(state->host->instance_get_native_data(
      args[0], kHashType));
  const bool xof_hash = source == nullptr;
  if (source == nullptr)
    source = require_hash(state, call, args[0], kXofHashType);
  if (source == nullptr) return X3_STATUS_ERROR;
  if (source->finalized)
    return already_finalized(state, call, runtime,
                             "Context was already finalized.");
  auto copy = std::make_unique<HashData>();
  copy->host = state->host;
  copy->algorithm = source->algorithm;
  state->host->value_retain(copy->algorithm);
  copy->context = EVP_MD_CTX_new();
  copy->is_xof_hash = xof_hash;
  copy->squeezed = source->squeezed;
  copy->bytes_remaining = source->bytes_remaining;
  if (copy->context == nullptr ||
      EVP_MD_CTX_copy_ex(copy->context, source->context) != 1)
    return fail(state, call, "ValueError", "hash copy failed");
  X3Value instance = state->host->value_instance(
      runtime, xof_hash ? state->xof_hash_class : state->hash_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, xof_hash ? kXofHashType : kHashType, copy.get(),
          cleanup_hash) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  copy.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status xof_squeeze(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "squeeze() takes 1 argument");
  auto* data = require_hash(state, call, args[0], kXofHashType);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (args[1].tag != X3_TAG_INT64 || args[1].as.i64 < 0)
    return fail(state, call, "ValueError", "invalid squeeze length");
  const uint64_t length = static_cast<uint64_t>(args[1].as.i64);
  data->squeezed = true;
  if (length > data->bytes_remaining)
    return fail(state, call, "ValueError",
                "Exceeded maximum squeeze limit specified by digest_size.");
  data->bytes_remaining -= length;
  std::vector<unsigned char> bytes(static_cast<size_t>(length));
#if OPENSSL_VERSION_NUMBER >= 0x30300000L
  if (EVP_DigestSqueeze(data->context, bytes.data(), bytes.size()) != 1)
    return fail(state, call, "ValueError", "XOF squeeze failed");
#else
  return fail(state, call, "NotImplementedError",
              "XOF squeezing requires OpenSSL 3.3 or newer");
#endif
  *result = state->host->value_bytes(runtime, bytes.data(), bytes.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status hash_supported(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args,
                        uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "hash_supported() takes 1 argument");
  const EVP_MD* digest = nullptr;
  bool xof = false;
  uint64_t digest_size = 0;
  const bool supported = algorithm_digest(state, runtime, args[0],
                                           &digest, &xof, &digest_size);
  if (!supported) state->host->clear_exception(call);
  *result = x3_value_bool(supported);
  return X3_STATUS_OK;
}

X3Status openssl_version(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0)
    return fail(state, call, "TypeError", "openssl_version() takes no arguments");
  *result = x3_value_uint64(OpenSSL_version_num());
  return X3_STATUS_OK;
}

X3Status openssl_version_text(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value*,
                              uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0)
    return fail(state, call, "TypeError",
                "openssl_version_text() takes no arguments");
  *result = state->host->value_string(runtime, OpenSSL_version(OPENSSL_VERSION));
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status is_fips_enabled(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0)
    return fail(state, call, "TypeError", "is_fips_enabled() takes no arguments");
  *result = x3_value_bool(EVP_default_properties_is_fips_enabled(nullptr) == 1);
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

X3Status register_openssl_hashes_module(X3Module* root,
                                        CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* openssl = nullptr;
  if (host->add_module(host, kOpensslName, &openssl) != X3_STATUS_OK ||
      attach_child(host, root, openssl, "openssl") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef openssl_functions[] = {
      {sizeof(X3NativeFunctionDef), "openssl_version",
       openssl_version, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "openssl_version_text",
       openssl_version_text, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "is_fips_enabled",
       is_fips_enabled, state, 0, 0, 0, nullptr},
  };
  for (const auto& function : openssl_functions)
    if (host->module_add_function(openssl, &function) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  const struct {
    const char* name;
    bool value;
  } openssl_flags[] = {
      {"CRYPTOGRAPHY_IS_LIBRESSL", false},
      {"CRYPTOGRAPHY_IS_BORINGSSL", false},
      {"CRYPTOGRAPHY_IS_AWSLC", false},
      {"CRYPTOGRAPHY_OPENSSL_300_OR_GREATER", OPENSSL_VERSION_NUMBER >= 0x30000000L},
  };
  for (const auto& flag : openssl_flags)
    if (host->module_add_value(openssl, flag.name, x3_value_bool(flag.value)) !=
        X3_STATUS_OK)
      return X3_STATUS_ERROR;
  X3Module* hashes = nullptr;
  if (host->add_module(host, kHashesName, &hashes) != X3_STATUS_OK ||
      attach_child(host, openssl, hashes, "hashes") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef hash_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", hash_init, state, 2, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "update", hash_update, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "finalize", hash_finalize, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "copy", hash_copy, state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(hashes, "Hash", hash_methods,
                             static_cast<uint32_t>(std::size(hash_methods)),
                             &state->hash_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef xof_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", xof_init, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "update", hash_update, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "squeeze", xof_squeeze, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "copy", hash_copy, state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(hashes, "XOFHash", xof_methods,
                             static_cast<uint32_t>(std::size(xof_methods)),
                             &state->xof_hash_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value hash_algorithm_property = x3_value_invalid();
  X3Value xof_algorithm_property = x3_value_invalid();
  if (host->property_create(host->runtime, "algorithm", hash_algorithm,
                            nullptr, state, &hash_algorithm_property) != X3_STATUS_OK ||
      host->property_create(host->runtime, "algorithm", hash_algorithm,
                            nullptr, state, &xof_algorithm_property) != X3_STATUS_OK) {
    if (hash_algorithm_property.tag != X3_TAG_INVALID)
      host->value_release(hash_algorithm_property);
    if (xof_algorithm_property.tag != X3_TAG_INVALID)
      host->value_release(xof_algorithm_property);
    return X3_STATUS_ERROR;
  }
  const X3Status hash_property_status = host->class_add_value(
      state->hash_class, "algorithm", hash_algorithm_property);
  const X3Status xof_property_status = host->class_add_value(
      state->xof_hash_class, "algorithm", xof_algorithm_property);
  host->value_release(hash_algorithm_property);
  host->value_release(xof_algorithm_property);
  if (hash_property_status != X3_STATUS_OK || xof_property_status != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef supported = {
      sizeof(X3NativeFunctionDef), "hash_supported", hash_supported,
      state, 1, 1, 0, nullptr};
  if (host->module_add_function(hashes, &supported) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_hmac_module(openssl, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_rsa_module(openssl, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_ec_module(openssl, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_dh_module(openssl, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_dsa_module(openssl, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_ed25519_module(openssl, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_x25519_module(openssl, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  return register_openssl_ciphers_module(openssl, state);
}
