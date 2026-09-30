/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kModuleName =
    "cryptography.hazmat.bindings._rust.openssl.ed25519";
constexpr const char* kPrivateType =
    "cryptography.hazmat.bindings._rust.openssl.ed25519.Ed25519PrivateKey";
constexpr const char* kPublicType =
    "cryptography.hazmat.bindings._rust.openssl.ed25519.Ed25519PublicKey";
constexpr size_t kKeySize = 32;

struct KeyData {
  EVP_PKEY* key = nullptr;
  ~KeyData() { EVP_PKEY_free(key); }
};

void cleanup_key(void* data) { delete static_cast<KeyData*>(data); }

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

KeyData* key_data(CryptographyNativeState* state, X3CallContext* call,
                  X3Value object, bool private_key) {
  auto* data = static_cast<KeyData*>(state->host->instance_get_native_data(
      object, private_key ? kPrivateType : kPublicType));
  if (data == nullptr) fail(state, call, "TypeError", "expected Ed25519 key");
  return data;
}

X3Status make_key(CryptographyNativeState* state, X3Runtime* runtime,
                  EVP_PKEY* key, bool private_key, X3Value* result) {
  auto data = std::make_unique<KeyData>();
  data->key = key;
  X3Value instance = state->host->value_instance(
      runtime, private_key ? state->ed25519_private_class
                           : state->ed25519_public_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, private_key ? kPrivateType : kPublicType,
          data.get(), cleanup_key) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status key_init(X3CallContext* call, X3Runtime*, void* user_data,
                  const X3Value*, uint32_t, X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return fail(state, call, "TypeError", "cannot create Ed25519 key instances");
}

X3Status generate_key(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value*,
                      uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0)
    return fail(state, call, "TypeError", "generate_key() takes no arguments");
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
      EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
  EVP_PKEY* key = nullptr;
  if (!context || EVP_PKEY_keygen_init(context.get()) != 1 ||
      EVP_PKEY_keygen(context.get(), &key) != 1)
    return fail(state, call, "ValueError", "Ed25519 key generation failed");
  return make_key(state, runtime, key, true, result);
}

X3Status from_bytes(X3CallContext* call, X3Runtime* runtime,
                    void* user_data, const X3Value* args,
                    uint32_t argc, X3Value* result, bool private_key) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "Ed25519 key bytes are required");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size != kKeySize) {
    state->host->buffer_release(buffer);
    return fail(state, call, "ValueError",
                private_key ? "An Ed25519 private key is 32 bytes long"
                            : "An Ed25519 public key is 32 bytes long");
  }
  EVP_PKEY* key = private_key
      ? EVP_PKEY_new_raw_private_key(
          EVP_PKEY_ED25519, nullptr,
          static_cast<const unsigned char*>(info.data), kKeySize)
      : EVP_PKEY_new_raw_public_key(
          EVP_PKEY_ED25519, nullptr,
          static_cast<const unsigned char*>(info.data), kKeySize);
  state->host->buffer_release(buffer);
  if (key == nullptr)
    return fail(state, call, "ValueError", "Invalid Ed25519 key bytes");
  return make_key(state, runtime, key, private_key, result);
}

X3Status from_private_bytes(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  return from_bytes(call, runtime, user_data, args, argc, result, true);
}

X3Status from_public_bytes(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  return from_bytes(call, runtime, user_data, args, argc, result, false);
}

X3Status raw_bytes(X3CallContext* call, X3Runtime* runtime,
                   void* user_data, const X3Value* args,
                   uint32_t argc, X3Value* result, bool private_key) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "raw key bytes take no arguments");
  auto* data = key_data(state, call, args[0], private_key);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::array<unsigned char, kKeySize> bytes{};
  size_t size = bytes.size();
  const int okay = private_key
      ? EVP_PKEY_get_raw_private_key(data->key, bytes.data(), &size)
      : EVP_PKEY_get_raw_public_key(data->key, bytes.data(), &size);
  if (okay != 1 || size != kKeySize) {
    if (private_key) OPENSSL_cleanse(bytes.data(), bytes.size());
    return fail(state, call, "ValueError", "Ed25519 raw key export failed");
  }
  *result = state->host->value_bytes(runtime, bytes.data(), size);
  if (private_key) OPENSSL_cleanse(bytes.data(), bytes.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status private_bytes_raw(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  return raw_bytes(call, runtime, user_data, args, argc, result, true);
}

X3Status public_bytes_raw(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  return raw_bytes(call, runtime, user_data, args, argc, result, false);
}

X3Status private_public_key(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "public_key() takes no arguments");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::array<unsigned char, kKeySize> bytes{};
  size_t size = bytes.size();
  if (EVP_PKEY_get_raw_public_key(data->key, bytes.data(), &size) != 1 ||
      size != kKeySize)
    return fail(state, call, "ValueError", "Ed25519 public key creation failed");
  EVP_PKEY* public_key = EVP_PKEY_new_raw_public_key(
      EVP_PKEY_ED25519, nullptr, bytes.data(), size);
  if (public_key == nullptr)
    return fail(state, call, "ValueError", "Ed25519 public key creation failed");
  return make_key(state, runtime, public_key, false, result);
}

X3Status copy_key(X3CallContext* call, X3Runtime* runtime,
                  void* user_data, const X3Value* args,
                  uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "__copy__() takes no arguments");
  const bool private_key = state->host->instance_get_native_data(
      args[0], kPrivateType) != nullptr;
  auto* data = key_data(state, call, args[0], private_key);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (EVP_PKEY_up_ref(data->key) != 1)
    return fail(state, call, "ValueError", "Ed25519 key copy failed");
  return make_key(state, runtime, data->key, private_key, result);
}

bool enum_name(CryptographyNativeState* state, X3Runtime* runtime,
               X3Value object, std::string& name) {
  X3Value value = x3_value_invalid();
  if (state->host->get_attr(runtime, object, "name", &value) != X3_STATUS_OK)
    return false;
  const char* text = nullptr;
  uint64_t size = 0;
  const X3Status status = state->host->value_string_data(
      runtime, value, &text, &size);
  if (status == X3_STATUS_OK) name.assign(text, static_cast<size_t>(size));
  state->host->value_release(value);
  return status == X3_STATUS_OK;
}

std::string class_name(CryptographyNativeState* state, X3Runtime* runtime,
                       X3Value object) {
  X3Value type_function = x3_value_invalid();
  X3Value klass = x3_value_invalid();
  X3Value name = x3_value_invalid();
  std::string result;
  if (state->host->builtin_value(state->host, "type", &type_function) ==
          X3_STATUS_OK &&
      state->host->call(runtime, type_function, &object, 1, &klass) ==
          X3_STATUS_OK &&
      state->host->get_attr(runtime, klass, "__name__", &name) ==
          X3_STATUS_OK) {
    const char* text = state->host->value_to_cstr(runtime, name);
    if (text != nullptr) result = text;
  }
  for (X3Value item : {name, klass, type_function})
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
  return result;
}

X3Status bio_bytes(CryptographyNativeState* state, X3Runtime* runtime,
                   BIO* bio, X3Value* result) {
  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio, &memory);
  if (memory == nullptr) return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, memory->data, memory->length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status public_bytes(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError", "public_bytes() requires encoding and format");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::string encoding, format;
  if (!enum_name(state, runtime, args[1], encoding) ||
      !enum_name(state, runtime, args[2], format))
    return fail(state, call, "TypeError", "invalid encoding or format");
  if (encoding == "Raw" && format == "Raw")
    return raw_bytes(call, runtime, user_data, args, 1, result, false);
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return fail(state, call, "ValueError", "Ed25519 public key serialization failed");
  int okay = 0;
  if (format == "SubjectPublicKeyInfo" && encoding == "PEM")
    okay = PEM_write_bio_PUBKEY(bio.get(), data->key);
  else if (format == "SubjectPublicKeyInfo" && encoding == "DER")
    okay = i2d_PUBKEY_bio(bio.get(), data->key);
  else
    return fail(state, call, "ValueError", "Unsupported encoding or format for Ed25519 public key");
  if (okay != 1)
    return fail(state, call, "ValueError", "Ed25519 public key serialization failed");
  return bio_bytes(state, runtime, bio.get(), result);
}

X3Status private_bytes(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 4)
    return fail(state, call, "TypeError",
                "private_bytes() requires encoding, format, and encryption");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::string encoding, format;
  if (!enum_name(state, runtime, args[1], encoding) ||
      !enum_name(state, runtime, args[2], format))
    return fail(state, call, "TypeError", "invalid encoding or format");
  const std::string encryption = class_name(state, runtime, args[3]);
  if (encryption != "NoEncryption" &&
      encryption != "BestAvailableEncryption")
    return fail(state, call, "ValueError",
                "Unsupported encryption algorithm for Ed25519 private key");
  if (encoding == "Raw" && format == "Raw" &&
      encryption == "NoEncryption")
    return raw_bytes(call, runtime, user_data, args, 1, result, true);
  if (format != "PKCS8" || (encoding != "PEM" && encoding != "DER"))
    return fail(state, call, "ValueError",
                "Unsupported encoding or format for Ed25519 private key");
  std::string password;
  const EVP_CIPHER* cipher = nullptr;
  if (encryption == "BestAvailableEncryption") {
    X3Value password_value = x3_value_invalid();
    if (state->host->get_attr(runtime, args[3], "password",
                              &password_value) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    X3Buffer* buffer = nullptr;
    X3BufferInfo info{};
    if (state->host->buffer_acquire(runtime, password_value, 0,
                                    &buffer, &info) != X3_STATUS_OK) {
      state->host->value_release(password_value);
      return X3_STATUS_ERROR;
    }
    if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
      state->host->buffer_release(buffer);
      state->host->value_release(password_value);
      return fail(state, call, "ValueError", "Ed25519 password is too long");
    }
    password.assign(static_cast<const char*>(info.data),
                    static_cast<size_t>(info.size));
    state->host->buffer_release(buffer);
    state->host->value_release(password_value);
    cipher = EVP_aes_256_cbc();
  }
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  const int okay = bio
      ? (encoding == "PEM"
             ? PEM_write_bio_PKCS8PrivateKey(
                   bio.get(), data->key, cipher,
                   cipher ? password.data() : nullptr,
                   static_cast<int>(password.size()), nullptr, nullptr)
             : i2d_PKCS8PrivateKey_bio(
                   bio.get(), data->key, cipher,
                   cipher ? password.data() : nullptr,
                   static_cast<int>(password.size()), nullptr, nullptr))
      : 0;
  if (!password.empty()) OPENSSL_cleanse(password.data(), password.size());
  if (okay != 1)
    return fail(state, call, "ValueError", "Ed25519 private key serialization failed");
  return bio_bytes(state, runtime, bio.get(), result);
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
    const X3Value arguments[] = {
        name, x3_value_none(), x3_value_none(), fromlist};
    if (state->host->call(runtime, importer, arguments, 4, &module) !=
        X3_STATUS_OK) goto done;
  }
  if (state->host->get_attr(runtime, module, class_name, &klass) !=
      X3_STATUS_OK) goto done;
  status = state->host->raise_error(call, klass, message);
done:
  for (X3Value value : {klass, module, item, fromlist, name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

X3Status private_sign(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "sign() requires data");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  std::array<unsigned char, 64> signature{};
  size_t size = signature.size();
  const bool okay = context &&
      EVP_DigestSignInit(context.get(), nullptr, nullptr,
                         nullptr, data->key) == 1 &&
      EVP_DigestSign(context.get(), signature.data(), &size,
                     static_cast<const unsigned char*>(info.data),
                     static_cast<size_t>(info.size)) == 1;
  state->host->buffer_release(input);
  if (!okay || size != signature.size())
    return fail(state, call, "ValueError", "Ed25519 signing failed");
  *result = state->host->value_bytes(runtime, signature.data(), size);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status public_verify(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError", "verify() requires signature and data");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Buffer* signature = nullptr;
  X3Buffer* input = nullptr;
  X3BufferInfo signature_info{};
  X3BufferInfo input_info{};
  if (state->host->buffer_acquire(
          runtime, args[1], 0, &signature, &signature_info) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (state->host->buffer_acquire(
          runtime, args[2], 0, &input, &input_info) != X3_STATUS_OK) {
    state->host->buffer_release(signature);
    return X3_STATUS_ERROR;
  }
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  const int verified = context &&
      EVP_DigestVerifyInit(context.get(), nullptr, nullptr,
                           nullptr, data->key) == 1
      ? EVP_DigestVerify(
            context.get(),
            static_cast<const unsigned char*>(signature_info.data),
            static_cast<size_t>(signature_info.size),
            static_cast<const unsigned char*>(input_info.data),
            static_cast<size_t>(input_info.size))
      : -1;
  state->host->buffer_release(input);
  state->host->buffer_release(signature);
  if (verified != 1)
    return crypto_exception(state, call, runtime, "InvalidSignature", "");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status attach_child(X3PackageHost* host, X3Module* parent,
                      X3Module* child, const char* name) {
  X3Value value = x3_value_invalid();
  if (host->module_get_value(child, &value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status status = host->module_add_value(parent, name, value);
  host->value_release(value);
  return status;
}

}  // namespace

X3Status register_openssl_ed25519_module(X3Module* openssl,
                                         CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* ed25519 = nullptr;
  if (host->add_module(host, kModuleName, &ed25519) != X3_STATUS_OK ||
      attach_child(host, openssl, ed25519, "ed25519") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef private_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       key_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_key",
       private_public_key, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_bytes_raw",
       private_bytes_raw, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_bytes",
       private_bytes, state, 4, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "sign",
       private_sign, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__copy__",
       copy_key, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef public_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       key_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_bytes_raw",
       public_bytes_raw, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_bytes",
       public_bytes, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "verify",
       public_verify, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__copy__",
       copy_key, state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(
          ed25519, "Ed25519PrivateKey", private_methods,
          static_cast<uint32_t>(std::size(private_methods)),
          &state->ed25519_private_class) != X3_STATUS_OK ||
      host->module_add_class(
          ed25519, "Ed25519PublicKey", public_methods,
          static_cast<uint32_t>(std::size(public_methods)),
          &state->ed25519_public_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef functions[] = {
      {sizeof(X3NativeFunctionDef), "generate_key",
       generate_key, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "from_private_bytes",
       from_private_bytes, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "from_public_bytes",
       from_public_bytes, state, 1, 1, 0, nullptr},
  };
  for (const auto& function : functions)
    if (host->module_add_function(ed25519, &function) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
