/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/ec.h>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

X3Status make_loaded_ec_key(CryptographyNativeState*, X3CallContext*,
                             X3Runtime*, EC_KEY*, bool, X3Value*);

namespace {

constexpr const char* kRsaModuleName =
    "cryptography.hazmat.bindings._rust.openssl.rsa";
constexpr const char* kKeysModuleName =
    "cryptography.hazmat.bindings._rust.openssl.keys";
constexpr const char* kPrivateType =
    "cryptography.hazmat.bindings._rust.openssl.rsa.RSAPrivateKey";
constexpr const char* kPublicType =
    "cryptography.hazmat.bindings._rust.openssl.rsa.RSAPublicKey";
constexpr const char* kPrivateNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.rsa.RSAPrivateNumbers";
constexpr const char* kPublicNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.rsa.RSAPublicNumbers";

struct RsaKeyData {
  RSA* key = nullptr;
  ~RsaKeyData() { RSA_free(key); }
};

struct RsaNumbersData {
  X3PackageHost* host = nullptr;
  std::array<X3Value, 7> values{};
  uint32_t count = 0;

  ~RsaNumbersData() {
    for (uint32_t i = 0; i < count; ++i)
      if (values[i].tag != X3_TAG_INVALID) host->value_release(values[i]);
  }
};

void cleanup_key(void* data) { delete static_cast<RsaKeyData*>(data); }
void cleanup_numbers(void* data) { delete static_cast<RsaNumbersData*>(data); }

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

RsaKeyData* key_data(CryptographyNativeState* state, X3CallContext* call,
                     X3Value object, bool private_key) {
  auto* data = static_cast<RsaKeyData*>(
      state->host->instance_get_native_data(
          object, private_key ? kPrivateType : kPublicType));
  if (data == nullptr) fail(state, call, "TypeError", "expected RSA key");
  return data;
}

RsaNumbersData* numbers_data(CryptographyNativeState* state,
                             X3CallContext* call, X3Value object,
                             bool private_numbers) {
  auto* data = static_cast<RsaNumbersData*>(
      state->host->instance_get_native_data(
          object, private_numbers ? kPrivateNumbersType : kPublicNumbersType));
  if (data == nullptr) fail(state, call, "TypeError", "expected RSA numbers");
  return data;
}

X3Status make_key(CryptographyNativeState* state, X3Runtime* runtime,
                  RSA* key, bool private_key, X3Value* result) {
  std::unique_ptr<RsaKeyData> data(new RsaKeyData{key});
  X3Value instance = state->host->value_instance(
      runtime, private_key ? state->rsa_private_class : state->rsa_public_class);
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

X3Status rsa_generate_private_key(
    X3CallContext* call, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2 || args[0].tag != X3_TAG_INT64 ||
      args[1].tag != X3_TAG_INT64)
    return fail(state, call, "TypeError",
                "generate_private_key() requires exponent and key size");
  const int64_t exponent = args[0].as.i64;
  const int64_t bits = args[1].as.i64;
  if ((exponent != 3 && exponent != 65537) ||
      bits < 512 || bits > std::numeric_limits<int>::max())
    return fail(state, call, "ValueError", "invalid RSA key parameters");
  std::unique_ptr<BIGNUM, decltype(&BN_free)> e(BN_new(), BN_free);
  std::unique_ptr<RSA, decltype(&RSA_free)> key(RSA_new(), RSA_free);
  if (!e || !key || BN_set_word(e.get(), static_cast<BN_ULONG>(exponent)) != 1 ||
      RSA_generate_key_ex(key.get(), static_cast<int>(bits), e.get(), nullptr) != 1)
    return fail(state, call, "ValueError", "RSA key generation failed");
  return make_key(state, runtime, key.release(), true, result);
}

X3Status rsa_key_size(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "key_size getter");
  auto* data = static_cast<RsaKeyData*>(
      state->host->instance_get_native_data(args[0], kPrivateType));
  if (data == nullptr)
    data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(RSA_bits(data->key));
  return X3_STATUS_OK;
}

X3Status rsa_public_key(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args,
                        uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "public_key() takes no arguments");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  RSA* public_key = RSAPublicKey_dup(data->key);
  if (public_key == nullptr)
    return fail(state, call, "ValueError", "unable to copy RSA public key");
  return make_key(state, runtime, public_key, false, result);
}

X3Status rsa_public_bytes(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError",
                "public_bytes() requires encoding and format");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::string encoding, format;
  if (!enum_name(state, runtime, args[1], encoding) ||
      !enum_name(state, runtime, args[2], format))
    return fail(state, call, "TypeError", "invalid encoding or format");
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return fail(state, call, "ValueError", "unable to serialize RSA key");
  int okay = 0;
  if (encoding == "PEM" && format == "SubjectPublicKeyInfo")
    okay = PEM_write_bio_RSA_PUBKEY(bio.get(), data->key);
  else if (encoding == "PEM" && format == "PKCS1")
    okay = PEM_write_bio_RSAPublicKey(bio.get(), data->key);
  else if (encoding == "DER" && format == "SubjectPublicKeyInfo")
    okay = i2d_RSA_PUBKEY_bio(bio.get(), data->key);
  else if (encoding == "DER" && format == "PKCS1")
    okay = i2d_RSAPublicKey_bio(bio.get(), data->key);
  else
    return fail(state, call, "ValueError",
                "Unsupported encoding or format for RSA public key");
  if (okay != 1)
    return fail(state, call, "ValueError", "unable to serialize RSA public key");
  return bio_bytes(state, runtime, bio.get(), result);
}

X3Status rsa_private_bytes(X3CallContext* call, X3Runtime* runtime,
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
                "Unsupported encryption algorithm for RSA private key");
  std::string password;
  const EVP_CIPHER* cipher = nullptr;
  if (encryption == "BestAvailableEncryption") {
    X3Value password_value = x3_value_invalid();
    if (state->host->get_attr(runtime, args[3], "password",
                              &password_value) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    X3Buffer* buffer = nullptr;
    X3BufferInfo info{};
    if (state->host->buffer_acquire(
            runtime, password_value, 0, &buffer, &info) != X3_STATUS_OK) {
      state->host->value_release(password_value);
      return X3_STATUS_ERROR;
    }
    password.assign(
        static_cast<const char*>(info.data),
        static_cast<size_t>(info.size));
    state->host->buffer_release(buffer);
    state->host->value_release(password_value);
    cipher = EVP_aes_256_cbc();
  }
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return fail(state, call, "ValueError", "unable to serialize RSA key");
  int okay = 0;
  if (encoding == "PEM" && format == "TraditionalOpenSSL")
    okay = PEM_write_bio_RSAPrivateKey(
        bio.get(), data->key, cipher,
        cipher ? reinterpret_cast<unsigned char*>(password.data()) : nullptr,
        static_cast<int>(password.size()), nullptr, nullptr);
  else if (encoding == "DER" && format == "TraditionalOpenSSL" &&
           cipher == nullptr)
    okay = i2d_RSAPrivateKey_bio(bio.get(), data->key);
  else if (format == "PKCS8" && (encoding == "PEM" || encoding == "DER")) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        EVP_PKEY_new(), EVP_PKEY_free);
    if (!pkey || EVP_PKEY_set1_RSA(pkey.get(), data->key) != 1)
      return fail(state, call, "ValueError", "unable to serialize RSA key");
    okay = encoding == "PEM"
        ? PEM_write_bio_PKCS8PrivateKey(
              bio.get(), pkey.get(), cipher,
              cipher ? password.data() : nullptr,
              static_cast<int>(password.size()), nullptr, nullptr)
        : i2d_PKCS8PrivateKey_bio(
              bio.get(), pkey.get(), cipher,
              cipher ? password.data() : nullptr,
              static_cast<int>(password.size()), nullptr, nullptr);
  } else
    return fail(state, call, "ValueError",
                "Unsupported encoding or format for RSA private key");
  if (okay != 1)
    return fail(state, call, "ValueError", "unable to serialize RSA private key");
  return bio_bytes(state, runtime, bio.get(), result);
}

X3Status rsa_copy(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "__copy__() takes no arguments");
  auto* data = static_cast<RsaKeyData*>(
      state->host->instance_get_native_data(args[0], kPrivateType));
  const bool private_key = data != nullptr;
  if (data == nullptr) data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  RSA* copy = private_key ? RSAPrivateKey_dup(data->key)
                          : RSAPublicKey_dup(data->key);
  if (copy == nullptr) return fail(state, call, "ValueError", "RSA copy failed");
  return make_key(state, runtime, copy, private_key, result);
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

const EVP_MD* hash_digest(CryptographyNativeState* state, X3Runtime* runtime,
                          X3Value algorithm) {
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, algorithm, "name", &name) != X3_STATUS_OK)
    return nullptr;
  const char* text = nullptr;
  uint64_t size = 0;
  const X3Status status = state->host->value_string_data(
      runtime, name, &text, &size);
  const std::string name_text = status == X3_STATUS_OK
      ? std::string(text, static_cast<size_t>(size)) : "";
  state->host->value_release(name);
  return name_text.empty() ? nullptr : EVP_get_digestbyname(name_text.c_str());
}

bool configure_signature_padding(CryptographyNativeState* state,
                                 X3Runtime* runtime, EVP_PKEY_CTX* context,
                                 X3Value padding, const EVP_MD* digest,
                                 bool signing) {
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, padding, "name", &name) != X3_STATUS_OK)
    return false;
  const char* text = state->host->value_to_cstr(runtime, name);
  const std::string kind = text == nullptr ? "" : text;
  state->host->value_release(name);
  if (kind == "EMSA-PKCS1-v1_5")
    return EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PADDING) == 1;
  if (kind != "EMSA-PSS" ||
      EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PSS_PADDING) != 1)
    return false;
  X3Value mgf = x3_value_invalid();
  X3Value mgf_algorithm = x3_value_invalid();
  X3Value salt = x3_value_invalid();
  bool okay = false;
  if (state->host->get_attr(runtime, padding, "mgf", &mgf) != X3_STATUS_OK ||
      state->host->get_attr(runtime, mgf, "_algorithm",
                            &mgf_algorithm) != X3_STATUS_OK ||
      state->host->get_attr(runtime, padding, "_salt_length",
                            &salt) != X3_STATUS_OK)
    goto done;
  {
    const EVP_MD* mgf_digest = hash_digest(state, runtime, mgf_algorithm);
    if (mgf_digest == nullptr ||
        EVP_PKEY_CTX_set_rsa_mgf1_md(context, mgf_digest) != 1)
      goto done;
  }
  {
    int salt_length = 0;
    if (salt.tag == X3_TAG_INT64) {
      if (salt.as.i64 < 0 || salt.as.i64 > std::numeric_limits<int>::max())
        goto done;
      salt_length = static_cast<int>(salt.as.i64);
    } else {
      const std::string salt_kind = class_name(state, runtime, salt);
      if (salt_kind == "_MaxLength") salt_length = RSA_PSS_SALTLEN_MAX;
      else if (salt_kind == "_DigestLength")
        salt_length = RSA_PSS_SALTLEN_DIGEST;
      else if (salt_kind == "_Auto" && !signing)
        salt_length = RSA_PSS_SALTLEN_AUTO;
      else goto done;
    }
    okay = EVP_PKEY_CTX_set_rsa_pss_saltlen(context, salt_length) == 1;
  }
done:
  for (X3Value value : {salt, mgf_algorithm, mgf})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return okay;
}

std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> wrap_rsa(RSA* key) {
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
      EVP_PKEY_new(), EVP_PKEY_free);
  if (!pkey || EVP_PKEY_set1_RSA(pkey.get(), key) != 1)
    pkey.reset();
  return pkey;
}

X3Status rsa_sign(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 4)
    return fail(state, call, "TypeError",
                "sign() requires data, padding, and algorithm");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  const EVP_MD* digest = hash_digest(state, runtime, args[3]);
  if (digest == nullptr)
    return fail(state, call, "TypeError",
                "Expected instance of hashes.HashAlgorithm.");
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  auto pkey = wrap_rsa(data->key);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  EVP_PKEY_CTX* key_context = nullptr;
  bool okay = pkey && context &&
      EVP_DigestSignInit(
          context.get(), &key_context, digest, nullptr, pkey.get()) == 1 &&
      configure_signature_padding(
          state, runtime, key_context, args[2], digest, true) &&
      EVP_DigestSignUpdate(
          context.get(), info.data, static_cast<size_t>(info.size)) == 1;
  state->host->buffer_release(input);
  if (!okay)
    return fail(state, call, "ValueError", "unable to sign with RSA key");
  size_t size = 0;
  if (EVP_DigestSignFinal(context.get(), nullptr, &size) != 1)
    return fail(state, call, "ValueError", "unable to sign with RSA key");
  std::vector<unsigned char> signature(size);
  if (EVP_DigestSignFinal(context.get(), signature.data(), &size) != 1)
    return fail(state, call, "ValueError", "unable to sign with RSA key");
  signature.resize(size);
  *result = state->host->value_bytes(
      runtime, signature.data(), signature.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status rsa_verify(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 5)
    return fail(state, call, "TypeError",
                "verify() requires signature, data, padding, and algorithm");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  const EVP_MD* digest = hash_digest(state, runtime, args[4]);
  if (digest == nullptr)
    return fail(state, call, "TypeError",
                "Expected instance of hashes.HashAlgorithm.");
  X3Buffer* signature = nullptr;
  X3Buffer* input = nullptr;
  X3BufferInfo signature_info{};
  X3BufferInfo input_info{};
  if (state->host->buffer_acquire(
          runtime, args[1], 0, &signature, &signature_info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (state->host->buffer_acquire(
          runtime, args[2], 0, &input, &input_info) != X3_STATUS_OK) {
    state->host->buffer_release(signature);
    return X3_STATUS_ERROR;
  }
  auto pkey = wrap_rsa(data->key);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  EVP_PKEY_CTX* key_context = nullptr;
  const bool initialized = pkey && context &&
      EVP_DigestVerifyInit(
          context.get(), &key_context, digest, nullptr, pkey.get()) == 1 &&
      configure_signature_padding(
          state, runtime, key_context, args[3], digest, false) &&
      EVP_DigestVerifyUpdate(
          context.get(), input_info.data,
          static_cast<size_t>(input_info.size)) == 1;
  state->host->buffer_release(input);
  const int verified = initialized
      ? EVP_DigestVerifyFinal(
            context.get(),
            static_cast<const unsigned char*>(signature_info.data),
            static_cast<size_t>(signature_info.size))
      : -1;
  state->host->buffer_release(signature);
  if (!initialized)
    return fail(state, call, "ValueError", "unable to verify RSA signature");
  if (verified != 1)
    return crypto_exception(state, call, runtime, "InvalidSignature", "");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

bool configure_encryption_padding(CryptographyNativeState* state,
                                  X3Runtime* runtime, EVP_PKEY_CTX* context,
                                  X3Value padding) {
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, padding, "name", &name) != X3_STATUS_OK)
    return false;
  const char* text = state->host->value_to_cstr(runtime, name);
  const std::string kind = text == nullptr ? "" : text;
  state->host->value_release(name);
  if (kind == "EMSA-PKCS1-v1_5")
    return EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PADDING) == 1;
  if (kind != "EME-OAEP" ||
      EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_OAEP_PADDING) != 1)
    return false;
  X3Value algorithm = x3_value_invalid();
  X3Value mgf = x3_value_invalid();
  X3Value mgf_algorithm = x3_value_invalid();
  X3Value label = x3_value_invalid();
  bool okay = false;
  if (state->host->get_attr(runtime, padding, "algorithm",
                            &algorithm) != X3_STATUS_OK ||
      state->host->get_attr(runtime, padding, "mgf", &mgf) != X3_STATUS_OK ||
      state->host->get_attr(runtime, mgf, "_algorithm",
                            &mgf_algorithm) != X3_STATUS_OK ||
      state->host->get_attr(runtime, padding, "_label", &label) != X3_STATUS_OK)
    goto done;
  {
    const EVP_MD* main_digest = hash_digest(state, runtime, algorithm);
    const EVP_MD* mgf_digest = hash_digest(state, runtime, mgf_algorithm);
    if (main_digest == nullptr || mgf_digest == nullptr ||
        EVP_PKEY_CTX_set_rsa_oaep_md(context, main_digest) != 1 ||
        EVP_PKEY_CTX_set_rsa_mgf1_md(context, mgf_digest) != 1)
      goto done;
  }
  if (label.tag != X3_TAG_NONE) {
    X3Buffer* buffer = nullptr;
    X3BufferInfo info{};
    if (state->host->buffer_acquire(runtime, label, 0, &buffer, &info) !=
        X3_STATUS_OK) goto done;
    if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
      state->host->buffer_release(buffer);
      goto done;
    }
    auto* copy = static_cast<unsigned char*>(
        OPENSSL_malloc(static_cast<size_t>(info.size)));
    if (copy == nullptr) {
      state->host->buffer_release(buffer);
      goto done;
    }
    if (info.size != 0)
      std::memcpy(copy, info.data, static_cast<size_t>(info.size));
    state->host->buffer_release(buffer);
    if (EVP_PKEY_CTX_set0_rsa_oaep_label(
            context, copy, static_cast<int>(info.size)) != 1) {
      OPENSSL_free(copy);
      goto done;
    }
  }
  okay = true;
done:
  for (X3Value value : {label, mgf_algorithm, mgf, algorithm})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return okay;
}

X3Status rsa_crypt(X3CallContext* call, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result,
                   bool decrypt) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError",
                decrypt ? "decrypt() requires ciphertext and padding"
                        : "encrypt() requires plaintext and padding");
  auto* data = key_data(state, call, args[0], decrypt);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  auto pkey = wrap_rsa(data->key);
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
      pkey ? EVP_PKEY_CTX_new(pkey.get(), nullptr) : nullptr,
      EVP_PKEY_CTX_free);
  bool okay = context &&
      (decrypt ? EVP_PKEY_decrypt_init(context.get())
               : EVP_PKEY_encrypt_init(context.get())) == 1 &&
      configure_encryption_padding(state, runtime, context.get(), args[2]);
  size_t size = 0;
  if (okay)
    okay = (decrypt
        ? EVP_PKEY_decrypt(
              context.get(), nullptr, &size,
              static_cast<const unsigned char*>(info.data),
              static_cast<size_t>(info.size))
        : EVP_PKEY_encrypt(
              context.get(), nullptr, &size,
              static_cast<const unsigned char*>(info.data),
              static_cast<size_t>(info.size))) == 1;
  std::vector<unsigned char> output(size);
  if (okay)
    okay = (decrypt
        ? EVP_PKEY_decrypt(
              context.get(), output.data(), &size,
              static_cast<const unsigned char*>(info.data),
              static_cast<size_t>(info.size))
        : EVP_PKEY_encrypt(
              context.get(), output.data(), &size,
              static_cast<const unsigned char*>(info.data),
              static_cast<size_t>(info.size))) == 1;
  state->host->buffer_release(input);
  if (!okay)
    return fail(state, call, "ValueError",
                decrypt ? "Decryption failed" : "Encryption failed");
  output.resize(size);
  *result = state->host->value_bytes(runtime, output.data(), output.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status rsa_encrypt(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  return rsa_crypt(call, runtime, user_data, args, argc, result, false);
}

X3Status rsa_decrypt(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  return rsa_crypt(call, runtime, user_data, args, argc, result, true);
}

bool python_int_to_bn(CryptographyNativeState* state, X3Runtime* runtime,
                      X3Value value, BIGNUM** result) {
  X3Value string_function = x3_value_invalid();
  X3Value text = x3_value_invalid();
  bool valid = false;
  if (state->host->builtin_value(state->host, "str", &string_function) ==
          X3_STATUS_OK &&
      state->host->call(runtime, string_function, &value, 1, &text) ==
          X3_STATUS_OK) {
    const char* decimal = state->host->value_to_cstr(runtime, text);
    valid = decimal != nullptr && BN_dec2bn(result, decimal) != 0 &&
            !BN_is_negative(*result);
  }
  for (X3Value item : {text, string_function})
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
  return valid;
}

bool bn_to_python_int(CryptographyNativeState* state, X3Runtime* runtime,
                      const BIGNUM* number, X3Value* result) {
  if (number == nullptr) return false;
  char* decimal = BN_bn2dec(number);
  if (decimal == nullptr) return false;
  X3Value text = state->host->value_string(runtime, decimal);
  OPENSSL_free(decimal);
  if (text.tag == X3_TAG_INVALID) return false;
  X3Value int_function = x3_value_invalid();
  const bool ready = state->host->builtin_value(
      state->host, "int", &int_function) == X3_STATUS_OK;
  const bool converted = ready &&
      state->host->call(runtime, int_function, &text, 1, result) ==
          X3_STATUS_OK;
  if (int_function.tag != X3_TAG_INVALID)
    state->host->value_release(int_function);
  state->host->value_release(text);
  return converted;
}

X3Status make_numbers(CryptographyNativeState* state, X3Runtime* runtime,
                      const X3Value* values, uint32_t count,
                      bool private_numbers, X3Value* result) {
  auto data = std::make_unique<RsaNumbersData>();
  data->host = state->host;
  data->count = count;
  for (uint32_t i = 0; i < count; ++i) {
    data->values[i] = values[i];
    state->host->value_retain(values[i]);
  }
  X3Value instance = state->host->value_instance(
      runtime, private_numbers ? state->rsa_private_numbers_class
                               : state->rsa_public_numbers_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, private_numbers ? kPrivateNumbersType
                                    : kPublicNumbersType,
          data.get(), cleanup_numbers) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status init_numbers_on_self(CryptographyNativeState* state,
                              X3CallContext* call, X3Value self,
                              const X3Value* values, uint32_t count,
                              bool private_numbers) {
  auto data = std::make_unique<RsaNumbersData>();
  data->host = state->host;
  data->count = count;
  for (uint32_t i = 0; i < count; ++i) {
    data->values[i] = values[i];
    state->host->value_retain(values[i]);
  }
  if (state->host->instance_set_native_data(
          self, private_numbers ? kPrivateNumbersType
                                : kPublicNumbersType,
          data.get(), cleanup_numbers) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  return X3_STATUS_OK;
}

X3Status public_numbers_construct(X3CallContext* call, X3Runtime*,
                                  void* user_data, const X3Value* args,
                                  uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError",
                "RSAPublicNumbers() requires e and n");
  const X3Status status = init_numbers_on_self(
      state, call, args[0], args + 1, 2, false);
  if (status == X3_STATUS_OK) *result = x3_value_none();
  return status;
}

X3Status private_numbers_construct(X3CallContext* call, X3Runtime*,
                                   void* user_data, const X3Value* args,
                                   uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 8)
    return fail(state, call, "TypeError",
                "RSAPrivateNumbers() requires key factors and public numbers");
  const X3Status status = init_numbers_on_self(
      state, call, args[0], args + 1, 7, true);
  if (status == X3_STATUS_OK) *result = x3_value_none();
  return status;
}

X3Status number_value(CryptographyNativeState* state, X3CallContext* call,
                      X3Value self, bool private_numbers, uint32_t index,
                      X3Value* result) {
  auto* data = numbers_data(state, call, self, private_numbers);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->values[index];
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

#define RSA_PUBLIC_GETTER(name, index)                                     \
  X3Status public_##name(X3CallContext* call, X3Runtime*, void* user_data,  \
                         const X3Value* args, uint32_t argc,              \
                         X3Value* result) {                                \
    auto* state = static_cast<CryptographyNativeState*>(user_data);        \
    if (argc != 1) return fail(state, call, "TypeError", #name " getter");  \
    return number_value(state, call, args[0], false, index, result);       \
  }

RSA_PUBLIC_GETTER(e, 0)
RSA_PUBLIC_GETTER(n, 1)

#define RSA_PRIVATE_GETTER(name, index)                                    \
  X3Status private_##name(X3CallContext* call, X3Runtime*, void* user_data, \
                          const X3Value* args, uint32_t argc,             \
                          X3Value* result) {                               \
    auto* state = static_cast<CryptographyNativeState*>(user_data);        \
    if (argc != 1) return fail(state, call, "TypeError", #name " getter");  \
    return number_value(state, call, args[0], true, index, result);        \
  }

RSA_PRIVATE_GETTER(p, 0)
RSA_PRIVATE_GETTER(q, 1)
RSA_PRIVATE_GETTER(d, 2)
RSA_PRIVATE_GETTER(dmp1, 3)
RSA_PRIVATE_GETTER(dmq1, 4)
RSA_PRIVATE_GETTER(iqmp, 5)
RSA_PRIVATE_GETTER(public_numbers, 6)

X3Status rsa_public_numbers(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError",
                "public_numbers() takes no arguments");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  const BIGNUM* n = nullptr;
  const BIGNUM* e = nullptr;
  RSA_get0_key(data->key, &n, &e, nullptr);
  X3Value numbers[2] = {x3_value_invalid(), x3_value_invalid()};
  if (!bn_to_python_int(state, runtime, e, &numbers[0]) ||
      !bn_to_python_int(state, runtime, n, &numbers[1])) {
    for (X3Value value : numbers)
      if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
    return X3_STATUS_ERROR;
  }
  const X3Status status = make_numbers(state, runtime, numbers, 2, false, result);
  for (X3Value value : numbers) state->host->value_release(value);
  return status;
}

X3Status rsa_private_numbers(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError",
                "private_numbers() takes no arguments");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  const BIGNUM *n = nullptr, *e = nullptr, *d = nullptr;
  const BIGNUM *p = nullptr, *q = nullptr;
  const BIGNUM *dmp1 = nullptr, *dmq1 = nullptr, *iqmp = nullptr;
  RSA_get0_key(data->key, &n, &e, &d);
  RSA_get0_factors(data->key, &p, &q);
  RSA_get0_crt_params(data->key, &dmp1, &dmq1, &iqmp);
  X3Value values[7] = {};
  for (auto& value : values) value = x3_value_invalid();
  const BIGNUM* fields[] = {p, q, d, dmp1, dmq1, iqmp, e, n};
  X3Value public_values[2] = {x3_value_invalid(), x3_value_invalid()};
  bool okay = true;
  for (int i = 0; i < 6 && okay; ++i)
    okay = bn_to_python_int(state, runtime, fields[i], &values[i]);
  if (okay) okay = bn_to_python_int(state, runtime, e, &public_values[0]);
  if (okay) okay = bn_to_python_int(state, runtime, n, &public_values[1]);
  if (okay)
    okay = make_numbers(state, runtime, public_values, 2, false,
                        &values[6]) == X3_STATUS_OK;
  if (okay)
    okay = make_numbers(state, runtime, values, 7, true, result) ==
           X3_STATUS_OK;
  for (X3Value value : public_values)
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  for (X3Value value : values)
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return okay ? X3_STATUS_OK : X3_STATUS_ERROR;
}

X3Status rsa_numbers_public_key(X3CallContext* call, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 1 || argc > 2)
    return fail(state, call, "TypeError", "public_key() takes at most 1 argument");
  auto* numbers = numbers_data(state, call, args[0], false);
  if (numbers == nullptr) return X3_STATUS_ERROR;
  BIGNUM* e = nullptr;
  BIGNUM* n = nullptr;
  if (!python_int_to_bn(state, runtime, numbers->values[0], &e) ||
      !python_int_to_bn(state, runtime, numbers->values[1], &n)) {
    BN_free(e);
    BN_free(n);
    return fail(state, call, "ValueError", "invalid RSA public numbers");
  }
  std::unique_ptr<RSA, decltype(&RSA_free)> key(RSA_new(), RSA_free);
  if (!key || RSA_set0_key(key.get(), n, e, nullptr) != 1) {
    BN_free(e);
    BN_free(n);
    return fail(state, call, "ValueError", "invalid RSA public numbers");
  }
  return make_key(state, runtime, key.release(), false, result);
}

X3Status rsa_numbers_private_key(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 1 || argc > 2)
    return fail(state, call, "TypeError", "private_key() takes at most 1 argument");
  auto* numbers = numbers_data(state, call, args[0], true);
  if (numbers == nullptr) return X3_STATUS_ERROR;
  auto* public_numbers = numbers_data(
      state, call, numbers->values[6], false);
  if (public_numbers == nullptr) return X3_STATUS_ERROR;
  BIGNUM *p = nullptr, *q = nullptr, *d = nullptr;
  BIGNUM *dmp1 = nullptr, *dmq1 = nullptr, *iqmp = nullptr;
  BIGNUM *e = nullptr, *n = nullptr;
  BIGNUM** targets[] = {&p, &q, &d, &dmp1, &dmq1, &iqmp};
  bool okay = true;
  for (int i = 0; i < 6 && okay; ++i)
    okay = python_int_to_bn(
        state, runtime, numbers->values[i], targets[i]);
  if (okay) okay = python_int_to_bn(
      state, runtime, public_numbers->values[0], &e);
  if (okay) okay = python_int_to_bn(
      state, runtime, public_numbers->values[1], &n);
  std::unique_ptr<RSA, decltype(&RSA_free)> key(RSA_new(), RSA_free);
  if (okay && key && RSA_set0_key(key.get(), n, e, d) == 1) {
    n = e = d = nullptr;
    if (RSA_set0_factors(key.get(), p, q) == 1) {
      p = q = nullptr;
      if (RSA_set0_crt_params(key.get(), dmp1, dmq1, iqmp) == 1) {
        dmp1 = dmq1 = iqmp = nullptr;
        okay = RSA_check_key_ex(key.get(), nullptr) == 1;
      } else okay = false;
    } else okay = false;
  } else okay = false;
  for (BIGNUM* value : {p, q, d, dmp1, dmq1, iqmp, e, n})
    BN_free(value);
  if (!okay)
    return fail(state, call, "ValueError", "invalid RSA private numbers");
  return make_key(state, runtime, key.release(), true, result);
}

int pem_password(char* output, int capacity, int, void* user_data) {
  const auto* password = static_cast<const std::string*>(user_data);
  if (password == nullptr || password->size() >
      static_cast<size_t>(capacity)) return 0;
  if (!password->empty())
    std::memcpy(output, password->data(), password->size());
  return static_cast<int>(password->size());
}

X3Status load_key(CryptographyNativeState* state, X3CallContext* call,
                  X3Runtime* runtime, const X3Value* args, uint32_t argc,
                  const X3KeywordArg* kwargs, uint32_t kwargc,
                  bool pem, bool private_key, X3Value* result) {
  X3Value data = x3_value_invalid();
  X3Value password = x3_value_invalid();
  if (argc > (private_key ? 3u : 2u))
    return fail(state, call, "TypeError", "too many key loader arguments");
  if (argc >= 1) data = args[0];
  if (private_key && argc >= 2) password = args[1];
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name == "data") {
      if (data.tag != X3_TAG_INVALID)
        return fail(state, call, "TypeError", "multiple values for data");
      data = kwargs[i].value;
    } else if (name == "password" && private_key) {
      if (password.tag != X3_TAG_INVALID)
        return fail(state, call, "TypeError", "multiple values for password");
      password = kwargs[i].value;
    } else if (name != "backend" &&
               name != "unsafe_skip_rsa_key_validation") {
      return fail(state, call, "TypeError", "unexpected key loader keyword");
    }
  }
  if (data.tag == X3_TAG_INVALID ||
      (private_key && password.tag == X3_TAG_INVALID))
    return fail(state, call, "TypeError", "missing key loader argument");
  X3Buffer* data_buffer = nullptr;
  X3BufferInfo data_info{};
  if (state->host->buffer_acquire(
          runtime, data, 0, &data_buffer, &data_info) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (data_info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(data_buffer);
    return fail(state, call, "ValueError", "key data is too large");
  }
  std::string password_text;
  if (private_key && password.tag != X3_TAG_NONE) {
    X3Buffer* password_buffer = nullptr;
    X3BufferInfo password_info{};
    if (state->host->buffer_acquire(
            runtime, password, 0, &password_buffer,
            &password_info) != X3_STATUS_OK) {
      state->host->buffer_release(data_buffer);
      return X3_STATUS_ERROR;
    }
    password_text.assign(
        static_cast<const char*>(password_info.data),
        static_cast<size_t>(password_info.size));
    state->host->buffer_release(password_buffer);
  }
  const std::string encoded(
      static_cast<const char*>(data_info.data),
      static_cast<size_t>(data_info.size));
  state->host->buffer_release(data_buffer);
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(
      BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size())),
      BIO_free);
  if (!bio) return fail(state, call, "ValueError", "unable to read key data");
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
      nullptr, EVP_PKEY_free);
  if (private_key) {
    if (pem) {
      pkey.reset(PEM_read_bio_PrivateKey(
          bio.get(), nullptr, pem_password, &password_text));
    } else {
      pkey.reset(d2i_PrivateKey_bio(bio.get(), nullptr));
      if (!pkey) {
        ERR_clear_error();
        BIO_reset(bio.get());
        pkey.reset(d2i_PKCS8PrivateKey_bio(
            bio.get(), nullptr, pem_password, &password_text));
      }
    }
  } else {
    pkey.reset(pem ? PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr)
                   : d2i_PUBKEY_bio(bio.get(), nullptr));
    if (!pkey) {
      ERR_clear_error();
      BIO_reset(bio.get());
      std::unique_ptr<RSA, decltype(&RSA_free)> raw(
          pem ? PEM_read_bio_RSAPublicKey(
                    bio.get(), nullptr, nullptr, nullptr)
              : d2i_RSAPublicKey_bio(bio.get(), nullptr),
          RSA_free);
      if (raw) pkey = wrap_rsa(raw.get());
    }
  }
  if (!pkey)
    return fail(state, call, "ValueError", "Could not deserialize key data");
  RSA* rsa = EVP_PKEY_get1_RSA(pkey.get());
  if (rsa == nullptr) {
    if (EVP_PKEY_base_id(pkey.get()) == EVP_PKEY_EC) {
      ERR_clear_error();
      EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pkey.get());
      if (ec == nullptr)
        return fail(state, call, "ValueError", "Could not deserialize EC key");
      return ::make_loaded_ec_key(state, call, runtime, ec, private_key, result);
    }
    return fail(state, call, "ValueError", "Unsupported key type");
  }
  return make_key(state, runtime, rsa, private_key, result);
}

X3Status load_pem_private(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, nullptr, 0, true, true, result);
}
X3Status load_der_private(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, nullptr, 0, false, true, result);
}
X3Status load_pem_public(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, nullptr, 0, true, false, result);
}
X3Status load_der_public(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, nullptr, 0, false, false, result);
}

X3Status load_pem_private_kw(
    X3CallContext* call, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, kwargs, kwargc,
                  true, true, result);
}
X3Status load_der_private_kw(
    X3CallContext* call, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, kwargs, kwargc,
                  false, true, result);
}
X3Status load_pem_public_kw(
    X3CallContext* call, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, kwargs, kwargc,
                  true, false, result);
}
X3Status load_der_public_kw(
    X3CallContext* call, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result) {
  return load_key(static_cast<CryptographyNativeState*>(user_data),
                  call, runtime, args, argc, kwargs, kwargc,
                  false, false, result);
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

X3Status register_openssl_rsa_module(X3Module* openssl,
                                     CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* rsa = nullptr;
  if (host->add_module(host, kRsaModuleName, &rsa) != X3_STATUS_OK ||
      attach_child(host, openssl, rsa, "rsa") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef private_methods[] = {
      {sizeof(X3NativeFunctionDef), "public_key",
       rsa_public_key, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_bytes",
       rsa_private_bytes, state, 4, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_numbers",
       rsa_private_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "sign",
       rsa_sign, state, 4, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "decrypt",
       rsa_decrypt, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__copy__",
       rsa_copy, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef public_methods[] = {
      {sizeof(X3NativeFunctionDef), "public_bytes",
       rsa_public_bytes, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_numbers",
       rsa_public_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "verify",
       rsa_verify, state, 5, 5, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "encrypt",
       rsa_encrypt, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__copy__",
       rsa_copy, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef public_numbers_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       public_numbers_construct, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_key",
       rsa_numbers_public_key, state, 1, 2, 0, nullptr},
  };
  const X3NativeFunctionDef private_numbers_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       private_numbers_construct, state, 8, 8, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_key",
       rsa_numbers_private_key, state, 1, 2, 0, nullptr},
  };
  if (host->module_add_class(
          rsa, "RSAPrivateKey", private_methods,
          static_cast<uint32_t>(std::size(private_methods)),
          &state->rsa_private_class) != X3_STATUS_OK ||
      host->module_add_class(
          rsa, "RSAPublicKey", public_methods,
          static_cast<uint32_t>(std::size(public_methods)),
          &state->rsa_public_class) != X3_STATUS_OK ||
      host->module_add_class(
          rsa, "RSAPublicNumbers", public_numbers_methods,
          static_cast<uint32_t>(std::size(public_numbers_methods)),
          &state->rsa_public_numbers_class) != X3_STATUS_OK ||
      host->module_add_class(
          rsa, "RSAPrivateNumbers", private_numbers_methods,
          static_cast<uint32_t>(std::size(private_numbers_methods)),
          &state->rsa_private_numbers_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto add_property = [&](X3Value klass, const char* name,
                          X3NativeFn getter) -> X3Status {
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, name, getter, nullptr,
                              state, &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(klass, name, property);
    host->value_release(property);
    return status;
  };
  if (add_property(state->rsa_private_class, "key_size",
                   rsa_key_size) != X3_STATUS_OK ||
      add_property(state->rsa_public_class, "key_size",
                   rsa_key_size) != X3_STATUS_OK ||
      add_property(state->rsa_public_numbers_class, "e",
                   public_e) != X3_STATUS_OK ||
      add_property(state->rsa_public_numbers_class, "n",
                   public_n) != X3_STATUS_OK ||
      add_property(state->rsa_private_numbers_class, "p",
                   private_p) != X3_STATUS_OK ||
      add_property(state->rsa_private_numbers_class, "q",
                   private_q) != X3_STATUS_OK ||
      add_property(state->rsa_private_numbers_class, "d",
                   private_d) != X3_STATUS_OK ||
      add_property(state->rsa_private_numbers_class, "dmp1",
                   private_dmp1) != X3_STATUS_OK ||
      add_property(state->rsa_private_numbers_class, "dmq1",
                   private_dmq1) != X3_STATUS_OK ||
      add_property(state->rsa_private_numbers_class, "iqmp",
                   private_iqmp) != X3_STATUS_OK ||
      add_property(state->rsa_private_numbers_class, "public_numbers",
                   private_public_numbers) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef generate = {
      sizeof(X3NativeFunctionDef), "generate_private_key",
      rsa_generate_private_key, state, 2, 2, 0, nullptr};
  if (host->module_add_function(rsa, &generate) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Module* keys = nullptr;
  if (host->add_module(host, kKeysModuleName, &keys) != X3_STATUS_OK ||
      attach_child(host, openssl, keys, "keys") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef loaders[] = {
      {sizeof(X3NativeFunctionDef), "load_pem_private_key",
       load_pem_private, state, 2, 3, 0, load_pem_private_kw},
      {sizeof(X3NativeFunctionDef), "load_der_private_key",
       load_der_private, state, 2, 3, 0, load_der_private_kw},
      {sizeof(X3NativeFunctionDef), "load_pem_public_key",
       load_pem_public, state, 1, 2, 0, load_pem_public_kw},
      {sizeof(X3NativeFunctionDef), "load_der_public_key",
       load_der_public, state, 1, 2, 0, load_der_public_kw},
  };
  for (const auto& loader : loaders)
    if (host->module_add_function(keys, &loader) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
