/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kEcModuleName =
    "cryptography.hazmat.bindings._rust.openssl.ec";
constexpr const char* kPrivateType =
    "cryptography.hazmat.bindings._rust.openssl.ec.ECPrivateKey";
constexpr const char* kPublicType =
    "cryptography.hazmat.bindings._rust.openssl.ec.ECPublicKey";
constexpr const char* kPrivateNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.ec.EllipticCurvePrivateNumbers";
constexpr const char* kPublicNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.ec.EllipticCurvePublicNumbers";

struct EcKeyData {
  X3PackageHost* host = nullptr;
  EC_KEY* key = nullptr;
  X3Value curve = x3_value_invalid();
  ~EcKeyData() {
    EC_KEY_free(key);
    if (curve.tag != X3_TAG_INVALID) host->value_release(curve);
  }
};

struct EcNumbersData {
  X3PackageHost* host = nullptr;
  std::array<X3Value, 3> values{};
  uint32_t count = 0;
  ~EcNumbersData() {
    for (uint32_t i = 0; i < count; ++i)
      if (values[i].tag != X3_TAG_INVALID) host->value_release(values[i]);
  }
};

void cleanup_key(void* data) { delete static_cast<EcKeyData*>(data); }
void cleanup_numbers(void* data) { delete static_cast<EcNumbersData*>(data); }

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

EcKeyData* key_data(CryptographyNativeState* state, X3CallContext* call,
                    X3Value object, bool private_key) {
  auto* data = static_cast<EcKeyData*>(
      state->host->instance_get_native_data(
          object, private_key ? kPrivateType : kPublicType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected elliptic curve key");
  return data;
}

EcNumbersData* numbers_data(CryptographyNativeState* state,
                            X3CallContext* call, X3Value object,
                            bool private_numbers) {
  auto* data = static_cast<EcNumbersData*>(
      state->host->instance_get_native_data(
          object, private_numbers ? kPrivateNumbersType : kPublicNumbersType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected elliptic curve numbers");
  return data;
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
  for (X3Value value : {name, klass, type_function})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return result;
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

int curve_nid(CryptographyNativeState* state, X3Runtime* runtime,
              X3Value curve) {
  std::string name;
  if (!enum_name(state, runtime, curve, name)) return NID_undef;
  if (name == "secp256r1") name = "prime256v1";
  if (name == "secp192r1") name = "prime192v1";
  return OBJ_txt2nid(name.c_str());
}

X3Status make_key(CryptographyNativeState* state, X3Runtime* runtime,
                  EC_KEY* key, X3Value curve, bool private_key,
                  X3Value* result) {
  auto data = std::make_unique<EcKeyData>();
  data->host = state->host;
  data->key = key;
  data->curve = curve;
  state->host->value_retain(curve);
  X3Value instance = state->host->value_instance(
      runtime, private_key ? state->ec_private_class : state->ec_public_class);
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

X3Status bio_bytes(CryptographyNativeState* state, X3Runtime* runtime,
                   BIO* bio, X3Value* result) {
  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio, &memory);
  if (memory == nullptr) return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, memory->data, memory->length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status ec_curve_supported(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "curve_supported() takes 1 argument");
  const int nid = curve_nid(state, runtime, args[0]);
  if (nid == NID_undef) state->host->clear_exception(call);
  std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)> group(
      nid == NID_undef ? nullptr : EC_GROUP_new_by_curve_name(nid),
      EC_GROUP_free);
  *result = x3_value_bool(group != nullptr);
  return X3_STATUS_OK;
}

X3Status ec_generate_private_key(
    X3CallContext* call, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 1 || argc > 2)
    return fail(state, call, "TypeError",
                "generate_private_key() requires an elliptic curve");
  const int nid = curve_nid(state, runtime, args[0]);
  if (nid == NID_undef)
    return fail(state, call, "ValueError", "Unsupported elliptic curve");
  std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> key(
      EC_KEY_new_by_curve_name(nid), EC_KEY_free);
  if (!key || EC_KEY_generate_key(key.get()) != 1)
    return fail(state, call, "ValueError", "EC key generation failed");
  return make_key(state, runtime, key.release(), args[0], true, result);
}

X3Status ec_key_curve(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "curve getter");
  auto* data = static_cast<EcKeyData*>(
      state->host->instance_get_native_data(args[0], kPrivateType));
  if (data == nullptr) data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->curve;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status ec_key_size(X3CallContext* call, X3Runtime*, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "key_size getter");
  auto* data = static_cast<EcKeyData*>(
      state->host->instance_get_native_data(args[0], kPrivateType));
  if (data == nullptr) data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(EC_GROUP_get_degree(
      EC_KEY_get0_group(data->key)));
  return X3_STATUS_OK;
}

X3Status ec_public_key(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "public_key() takes no arguments");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  const EC_GROUP* group = EC_KEY_get0_group(data->key);
  std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> public_key(
      EC_KEY_new(), EC_KEY_free);
  if (!public_key || EC_KEY_set_group(public_key.get(), group) != 1 ||
      EC_KEY_set_public_key(public_key.get(),
                            EC_KEY_get0_public_key(data->key)) != 1)
    return fail(state, call, "ValueError", "unable to copy EC public key");
  return make_key(state, runtime, public_key.release(),
                  data->curve, false, result);
}

X3Status ec_private_bytes(X3CallContext* call, X3Runtime* runtime,
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
                "Unsupported encryption algorithm for EC private key");
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
    password.assign(static_cast<const char*>(info.data),
                    static_cast<size_t>(info.size));
    state->host->buffer_release(buffer);
    state->host->value_release(password_value);
    cipher = EVP_aes_256_cbc();
  }
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(
      BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return fail(state, call, "ValueError", "unable to serialize EC key");
  int okay = 0;
  if (encoding == "PEM" && format == "TraditionalOpenSSL")
    okay = PEM_write_bio_ECPrivateKey(
        bio.get(), data->key, cipher,
        cipher ? reinterpret_cast<unsigned char*>(password.data()) : nullptr,
        static_cast<int>(password.size()), nullptr, nullptr);
  else if (encoding == "DER" && format == "TraditionalOpenSSL" &&
           cipher == nullptr)
    okay = i2d_ECPrivateKey_bio(bio.get(), data->key);
  else if (format == "PKCS8" && (encoding == "PEM" || encoding == "DER")) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        EVP_PKEY_new(), EVP_PKEY_free);
    if (!pkey || EVP_PKEY_set1_EC_KEY(pkey.get(), data->key) != 1)
      return fail(state, call, "ValueError", "unable to serialize EC key");
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
                "Unsupported encoding or format for EC private key");
  if (okay != 1)
    return fail(state, call, "ValueError", "unable to serialize EC private key");
  return bio_bytes(state, runtime, bio.get(), result);
}

X3Status ec_public_bytes(X3CallContext* call, X3Runtime* runtime,
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
  if (encoding == "X962" &&
      (format == "CompressedPoint" || format == "UncompressedPoint")) {
    const EC_GROUP* group = EC_KEY_get0_group(data->key);
    const EC_POINT* point = EC_KEY_get0_public_key(data->key);
    const point_conversion_form_t conversion =
        format == "CompressedPoint"
            ? POINT_CONVERSION_COMPRESSED : POINT_CONVERSION_UNCOMPRESSED;
    const size_t size = EC_POINT_point2oct(
        group, point, conversion, nullptr, 0, nullptr);
    if (size == 0)
      return fail(state, call, "ValueError", "unable to encode EC point");
    std::vector<unsigned char> bytes(size);
    if (EC_POINT_point2oct(
            group, point, conversion, bytes.data(), size, nullptr) != size)
      return fail(state, call, "ValueError", "unable to encode EC point");
    *result = state->host->value_bytes(runtime, bytes.data(), bytes.size());
    return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
  }
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(
      BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return fail(state, call, "ValueError", "unable to serialize EC key");
  int okay = 0;
  if (encoding == "PEM" && format == "SubjectPublicKeyInfo")
    okay = PEM_write_bio_EC_PUBKEY(bio.get(), data->key);
  else if (encoding == "DER" && format == "SubjectPublicKeyInfo")
    okay = i2d_EC_PUBKEY_bio(bio.get(), data->key);
  else
    return fail(state, call, "ValueError",
                "Unsupported encoding or format for EC public key");
  if (okay != 1)
    return fail(state, call, "ValueError", "unable to serialize EC public key");
  return bio_bytes(state, runtime, bio.get(), result);
}

X3Status ec_copy(X3CallContext* call, X3Runtime* runtime, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "__copy__() takes no arguments");
  auto* data = static_cast<EcKeyData*>(
      state->host->instance_get_native_data(args[0], kPrivateType));
  const bool private_key = data != nullptr;
  if (data == nullptr) data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  EC_KEY* copy = EC_KEY_dup(data->key);
  if (copy == nullptr)
    return fail(state, call, "ValueError", "EC key copy failed");
  return make_key(state, runtime, copy, data->curve, private_key, result);
}

bool python_int_to_bn(CryptographyNativeState* state, X3Runtime* runtime,
                      X3Value value, BIGNUM** result) {
  X3Value str_function = x3_value_invalid();
  X3Value text = x3_value_invalid();
  bool okay = false;
  if (state->host->builtin_value(state->host, "str", &str_function) ==
          X3_STATUS_OK &&
      state->host->call(runtime, str_function, &value, 1, &text) ==
          X3_STATUS_OK) {
    const char* decimal = state->host->value_to_cstr(runtime, text);
    okay = decimal != nullptr && BN_dec2bn(result, decimal) != 0 &&
           !BN_is_negative(*result);
  }
  for (X3Value item : {text, str_function})
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
  return okay;
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

X3Status init_numbers_on_self(CryptographyNativeState* state,
                              X3Value self, const X3Value* values,
                              uint32_t count, bool private_numbers) {
  auto data = std::make_unique<EcNumbersData>();
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

X3Status make_numbers(CryptographyNativeState* state, X3Runtime* runtime,
                      const X3Value* values, uint32_t count,
                      bool private_numbers, X3Value* result) {
  X3Value instance = state->host->value_instance(
      runtime, private_numbers ? state->ec_private_numbers_class
                               : state->ec_public_numbers_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (init_numbers_on_self(state, instance, values, count,
                           private_numbers) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  *result = instance;
  return X3_STATUS_OK;
}

X3Status public_numbers_construct(X3CallContext* call, X3Runtime*,
                                  void* user_data, const X3Value* args,
                                  uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 4)
    return fail(state, call, "TypeError",
                "EllipticCurvePublicNumbers() requires x, y, and curve");
  const X3Status status = init_numbers_on_self(
      state, args[0], args + 1, 3, false);
  if (status == X3_STATUS_OK) *result = x3_value_none();
  return status;
}

X3Status private_numbers_construct(X3CallContext* call, X3Runtime*,
                                   void* user_data, const X3Value* args,
                                   uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError",
                "EllipticCurvePrivateNumbers() requires value and public numbers");
  const X3Status status = init_numbers_on_self(
      state, args[0], args + 1, 2, true);
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

#define EC_PUBLIC_GETTER(name, index)                                       \
  X3Status public_##name(X3CallContext* call, X3Runtime*, void* user_data,    \
                         const X3Value* args, uint32_t argc,                \
                         X3Value* result) {                                  \
    auto* state = static_cast<CryptographyNativeState*>(user_data);          \
    if (argc != 1) return fail(state, call, "TypeError", #name " getter");    \
    return number_value(state, call, args[0], false, index, result);         \
  }

EC_PUBLIC_GETTER(x, 0)
EC_PUBLIC_GETTER(y, 1)
EC_PUBLIC_GETTER(curve, 2)

#define EC_PRIVATE_GETTER(name, index)                                      \
  X3Status private_##name(X3CallContext* call, X3Runtime*, void* user_data,  \
                          const X3Value* args, uint32_t argc,               \
                          X3Value* result) {                                 \
    auto* state = static_cast<CryptographyNativeState*>(user_data);          \
    if (argc != 1) return fail(state, call, "TypeError", #name " getter");    \
    return number_value(state, call, args[0], true, index, result);          \
  }

EC_PRIVATE_GETTER(private_value, 0)
EC_PRIVATE_GETTER(public_numbers, 1)

X3Status ec_public_numbers(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError",
                "public_numbers() takes no arguments");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::unique_ptr<BIGNUM, decltype(&BN_free)> x(BN_new(), BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> y(BN_new(), BN_free);
  if (!x || !y ||
      EC_POINT_get_affine_coordinates(
          EC_KEY_get0_group(data->key), EC_KEY_get0_public_key(data->key),
          x.get(), y.get(), nullptr) != 1)
    return fail(state, call, "ValueError", "invalid EC public point");
  X3Value values[3] = {x3_value_invalid(), x3_value_invalid(), data->curve};
  if (!bn_to_python_int(state, runtime, x.get(), &values[0]) ||
      !bn_to_python_int(state, runtime, y.get(), &values[1])) {
    for (uint32_t i = 0; i < 2; ++i)
      if (values[i].tag != X3_TAG_INVALID) state->host->value_release(values[i]);
    return X3_STATUS_ERROR;
  }
  const X3Status status = make_numbers(state, runtime, values, 3,
                                       false, result);
  state->host->value_release(values[0]);
  state->host->value_release(values[1]);
  return status;
}

X3Status ec_private_numbers(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError",
                "private_numbers() takes no arguments");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Value public_key = x3_value_invalid();
  X3Value public_numbers = x3_value_invalid();
  X3Value scalar = x3_value_invalid();
  bool okay = ec_public_key(call, runtime, user_data, args, 1,
                             &public_key) == X3_STATUS_OK;
  if (okay) okay = ec_public_numbers(
      call, runtime, user_data, &public_key, 1,
      &public_numbers) == X3_STATUS_OK;
  if (okay) okay = bn_to_python_int(
      state, runtime, EC_KEY_get0_private_key(data->key),
      &scalar);
  if (okay) {
    const X3Value values[] = {scalar, public_numbers};
    okay = make_numbers(state, runtime, values, 2,
                        true, result) == X3_STATUS_OK;
  }
  for (X3Value value : {scalar, public_numbers, public_key})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return okay ? X3_STATUS_OK : X3_STATUS_ERROR;
}

X3Status ec_from_public_numbers(X3CallContext* call, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError",
                "from_public_numbers() takes 1 argument");
  auto* numbers = numbers_data(state, call, args[0], false);
  if (numbers == nullptr) return X3_STATUS_ERROR;
  const int nid = curve_nid(state, runtime, numbers->values[2]);
  if (nid == NID_undef)
    return fail(state, call, "ValueError", "Unsupported elliptic curve");
  BIGNUM* x = nullptr;
  BIGNUM* y = nullptr;
  const bool parsed = python_int_to_bn(
          state, runtime, numbers->values[0], &x) &&
      python_int_to_bn(state, runtime, numbers->values[1], &y);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> bx(x, BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> by(y, BN_free);
  std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> key(
      EC_KEY_new_by_curve_name(nid), EC_KEY_free);
  if (!parsed || !key)
    return fail(state, call, "ValueError", "invalid EC public numbers");
  const EC_GROUP* group = EC_KEY_get0_group(key.get());
  std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> point(
      EC_POINT_new(group), EC_POINT_free);
  if (!point || EC_POINT_set_affine_coordinates(
          group, point.get(), bx.get(), by.get(), nullptr) != 1 ||
      EC_POINT_is_on_curve(group, point.get(), nullptr) != 1 ||
      EC_KEY_set_public_key(key.get(), point.get()) != 1)
    return fail(state, call, "ValueError", "invalid EC public numbers");
  return make_key(state, runtime, key.release(),
                  numbers->values[2], false, result);
}

X3Status ec_derive_private_key(X3CallContext* call, X3Runtime* runtime,
                               void* user_data, const X3Value* args,
                               uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError",
                "derive_private_key() requires private value and curve");
  const int nid = curve_nid(state, runtime, args[1]);
  if (nid == NID_undef)
    return fail(state, call, "ValueError", "Unsupported elliptic curve");
  BIGNUM* scalar = nullptr;
  if (!python_int_to_bn(state, runtime, args[0], &scalar))
    return fail(state, call, "ValueError", "invalid EC private value");
  std::unique_ptr<BIGNUM, decltype(&BN_free)> secret(scalar, BN_free);
  std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> key(
      EC_KEY_new_by_curve_name(nid), EC_KEY_free);
  if (!key || BN_is_zero(secret.get()) ||
      EC_KEY_set_private_key(key.get(), secret.get()) != 1)
    return fail(state, call, "ValueError", "invalid EC private value");
  const EC_GROUP* group = EC_KEY_get0_group(key.get());
  std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> point(
      EC_POINT_new(group), EC_POINT_free);
  if (!point || EC_POINT_mul(group, point.get(), secret.get(),
                             nullptr, nullptr, nullptr) != 1 ||
      EC_POINT_is_at_infinity(group, point.get()) == 1 ||
      EC_KEY_set_public_key(key.get(), point.get()) != 1 ||
      EC_KEY_check_key(key.get()) != 1)
    return fail(state, call, "ValueError", "invalid EC private value");
  return make_key(state, runtime, key.release(), args[1], true, result);
}

X3Status ec_from_private_numbers(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError",
                "from_private_numbers() takes 1 argument");
  auto* numbers = numbers_data(state, call, args[0], true);
  if (numbers == nullptr) return X3_STATUS_ERROR;
  auto* public_numbers = numbers_data(
      state, call, numbers->values[1], false);
  if (public_numbers == nullptr) return X3_STATUS_ERROR;
  const X3Value inputs[] = {
      numbers->values[0], public_numbers->values[2]};
  if (ec_derive_private_key(
          call, runtime, user_data, inputs, 2, result) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto* derived = key_data(state, call, *result, true);
  if (derived == nullptr) return X3_STATUS_ERROR;
  BIGNUM* x = nullptr;
  BIGNUM* y = nullptr;
  bool okay = python_int_to_bn(
      state, runtime, public_numbers->values[0], &x) &&
      python_int_to_bn(state, runtime, public_numbers->values[1], &y);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> bx(x, BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> by(y, BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> actual_x(BN_new(), BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> actual_y(BN_new(), BN_free);
  if (okay)
    okay = actual_x && actual_y &&
        EC_POINT_get_affine_coordinates(
            EC_KEY_get0_group(derived->key),
            EC_KEY_get0_public_key(derived->key),
            actual_x.get(), actual_y.get(), nullptr) == 1 &&
        BN_cmp(bx.get(), actual_x.get()) == 0 &&
        BN_cmp(by.get(), actual_y.get()) == 0;
  if (!okay) {
    state->host->value_release(*result);
    *result = x3_value_invalid();
    return fail(state, call, "ValueError",
                "private and public EC numbers do not match");
  }
  return X3_STATUS_OK;
}

X3Status ec_from_public_bytes(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError",
                "from_public_bytes() requires curve and data");
  const int nid = curve_nid(state, runtime, args[0]);
  if (nid == NID_undef)
    return fail(state, call, "ValueError", "Unsupported elliptic curve");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> key(
      EC_KEY_new_by_curve_name(nid), EC_KEY_free);
  const EC_GROUP* group = key ? EC_KEY_get0_group(key.get()) : nullptr;
  std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> point(
      group ? EC_POINT_new(group) : nullptr, EC_POINT_free);
  const bool valid = point &&
      EC_POINT_oct2point(
          group, point.get(),
          static_cast<const unsigned char*>(info.data),
          static_cast<size_t>(info.size), nullptr) == 1 &&
      EC_POINT_is_on_curve(group, point.get(), nullptr) == 1 &&
      EC_KEY_set_public_key(key.get(), point.get()) == 1;
  state->host->buffer_release(buffer);
  if (!valid)
    return fail(state, call, "ValueError", "Invalid EC public bytes");
  return make_key(state, runtime, key.release(), args[0], false, result);
}

X3Status numbers_public_key(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  if (argc < 1 || argc > 2)
    return fail(static_cast<CryptographyNativeState*>(user_data),
                call, "TypeError", "public_key() takes at most 1 argument");
  return ec_from_public_numbers(
      call, runtime, user_data, args, 1, result);
}

X3Status numbers_private_key(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  if (argc < 1 || argc > 2)
    return fail(static_cast<CryptographyNativeState*>(user_data),
                call, "TypeError", "private_key() takes at most 1 argument");
  return ec_from_private_numbers(
      call, runtime, user_data, args, 1, result);
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

X3Status make_loaded_ec_key(CryptographyNativeState* state,
                             X3CallContext* call, X3Runtime* runtime,
                             EC_KEY* raw_key, bool private_key,
                             X3Value* result) {
  std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> key(raw_key, EC_KEY_free);
  const int nid = EC_GROUP_get_curve_name(EC_KEY_get0_group(key.get()));
  const char* curve_name = OBJ_nid2sn(nid);
  if (curve_name == nullptr)
    return fail(state, call, "ValueError", "Unsupported elliptic curve");

  X3Value importer = x3_value_invalid();
  X3Value module_name = x3_value_invalid();
  X3Value fromlist = x3_value_invalid();
  X3Value fromitem = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value curve_types = x3_value_invalid();
  X3Value name = x3_value_invalid();
  X3Value curve = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) goto done;
  module_name = state->host->value_string(
      runtime, "cryptography.hazmat.primitives.asymmetric.ec");
  fromlist = state->host->value_list(runtime);
  fromitem = state->host->value_string(runtime, "_CURVE_TYPES");
  name = state->host->value_string(runtime, curve_name);
  if (module_name.tag == X3_TAG_INVALID || fromlist.tag == X3_TAG_INVALID ||
      fromitem.tag == X3_TAG_INVALID || name.tag == X3_TAG_INVALID ||
      state->host->list_append(runtime, fromlist, fromitem) != X3_STATUS_OK)
    goto done;
  {
    const X3Value args[] = {
        module_name, x3_value_none(), x3_value_none(), fromlist};
    if (state->host->call(runtime, importer, args, 4, &module) != X3_STATUS_OK)
      goto done;
  }
  if (state->host->get_attr(runtime, module, "_CURVE_TYPES", &curve_types) !=
          X3_STATUS_OK ||
      state->host->get_item(runtime, curve_types, name, &curve) != X3_STATUS_OK)
    goto done;
  status = make_key(state, runtime, key.release(), curve, private_key, result);
done:
  for (X3Value value : {curve, name, curve_types, module, fromitem,
                        fromlist, module_name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

X3Status register_openssl_ec_module(X3Module* openssl,
                                    CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* ec = nullptr;
  if (host->add_module(host, kEcModuleName, &ec) != X3_STATUS_OK ||
      attach_child(host, openssl, ec, "ec") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef private_methods[] = {
      {sizeof(X3NativeFunctionDef), "public_key",
       ec_public_key, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_bytes",
       ec_private_bytes, state, 4, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_numbers",
       ec_private_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__copy__",
       ec_copy, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef public_methods[] = {
      {sizeof(X3NativeFunctionDef), "public_bytes",
       ec_public_bytes, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_numbers",
       ec_public_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__copy__",
       ec_copy, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef private_numbers_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       private_numbers_construct, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_key",
       numbers_private_key, state, 1, 2, 0, nullptr},
  };
  const X3NativeFunctionDef public_numbers_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       public_numbers_construct, state, 4, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_key",
       numbers_public_key, state, 1, 2, 0, nullptr},
  };
  if (host->module_add_class(
          ec, "ECPrivateKey", private_methods,
          static_cast<uint32_t>(std::size(private_methods)),
          &state->ec_private_class) != X3_STATUS_OK ||
      host->module_add_class(
          ec, "ECPublicKey", public_methods,
          static_cast<uint32_t>(std::size(public_methods)),
          &state->ec_public_class) != X3_STATUS_OK ||
      host->module_add_class(
          ec, "EllipticCurvePrivateNumbers",
          private_numbers_methods,
          static_cast<uint32_t>(std::size(private_numbers_methods)),
          &state->ec_private_numbers_class) != X3_STATUS_OK ||
      host->module_add_class(
          ec, "EllipticCurvePublicNumbers",
          public_numbers_methods,
          static_cast<uint32_t>(std::size(public_numbers_methods)),
          &state->ec_public_numbers_class) != X3_STATUS_OK)
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
  if (add_property(state->ec_private_class, "curve",
                   ec_key_curve) != X3_STATUS_OK ||
      add_property(state->ec_private_class, "key_size",
                   ec_key_size) != X3_STATUS_OK ||
      add_property(state->ec_public_class, "curve",
                   ec_key_curve) != X3_STATUS_OK ||
      add_property(state->ec_public_class, "key_size",
                   ec_key_size) != X3_STATUS_OK ||
      add_property(state->ec_public_numbers_class, "x",
                   public_x) != X3_STATUS_OK ||
      add_property(state->ec_public_numbers_class, "y",
                   public_y) != X3_STATUS_OK ||
      add_property(state->ec_public_numbers_class, "curve",
                   public_curve) != X3_STATUS_OK ||
      add_property(state->ec_private_numbers_class, "private_value",
                   private_private_value) != X3_STATUS_OK ||
      add_property(state->ec_private_numbers_class, "public_numbers",
                   private_public_numbers) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef functions[] = {
      {sizeof(X3NativeFunctionDef), "curve_supported",
       ec_curve_supported, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "generate_private_key",
       ec_generate_private_key, state, 1, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "derive_private_key",
       ec_derive_private_key, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "from_private_numbers",
       ec_from_private_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "from_public_numbers",
       ec_from_public_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "from_public_bytes",
       ec_from_public_bytes, state, 2, 2, 0, nullptr},
  };
  for (const auto& function : functions)
    if (host->module_add_function(ec, &function) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
