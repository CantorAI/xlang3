/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/dh.h>
#include <openssl/pem.h>

#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>

namespace {

constexpr const char* kModuleName =
    "cryptography.hazmat.bindings._rust.openssl.dh";
constexpr const char* kParametersType =
    "cryptography.hazmat.bindings._rust.openssl.dh.DHParameters";
constexpr const char* kNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.dh.DHParameterNumbers";
constexpr const char* kPrivateType =
    "cryptography.hazmat.bindings._rust.openssl.dh.DHPrivateKey";
constexpr const char* kPublicType =
    "cryptography.hazmat.bindings._rust.openssl.dh.DHPublicKey";
constexpr const char* kPrivateNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.dh.DHPrivateNumbers";
constexpr const char* kPublicNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.dh.DHPublicNumbers";

struct ParametersData {
  DH* parameters = nullptr;
  ~ParametersData() { DH_free(parameters); }
};

struct KeyData {
  DH* key = nullptr;
  ~KeyData() { DH_free(key); }
};

struct NumbersData {
  X3PackageHost* host = nullptr;
  X3Value p = x3_value_invalid();
  X3Value g = x3_value_invalid();
  X3Value q = x3_value_invalid();
  ~NumbersData() {
    for (X3Value value : {p, g, q})
      if (value.tag != X3_TAG_INVALID) host->value_release(value);
  }
};

struct KeyNumbersData {
  X3PackageHost* host = nullptr;
  X3Value component = x3_value_invalid();
  X3Value related = x3_value_invalid();
  ~KeyNumbersData() {
    if (component.tag != X3_TAG_INVALID) host->value_release(component);
    if (related.tag != X3_TAG_INVALID) host->value_release(related);
  }
};

void cleanup_parameters(void* data) { delete static_cast<ParametersData*>(data); }
void cleanup_key(void* data) { delete static_cast<KeyData*>(data); }
void cleanup_numbers(void* data) { delete static_cast<NumbersData*>(data); }
void cleanup_key_numbers(void* data) {
  delete static_cast<KeyNumbersData*>(data);
}

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

ParametersData* parameters_data(CryptographyNativeState* state,
                                X3CallContext* call, X3Value object) {
  auto* data = static_cast<ParametersData*>(
      state->host->instance_get_native_data(object, kParametersType));
  if (data == nullptr) fail(state, call, "TypeError", "expected DH parameters");
  return data;
}

NumbersData* numbers_data(CryptographyNativeState* state,
                          X3CallContext* call, X3Value object) {
  auto* data = static_cast<NumbersData*>(
      state->host->instance_get_native_data(object, kNumbersType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected DH parameter numbers");
  return data;
}

KeyData* key_data(CryptographyNativeState* state, X3CallContext* call,
                  X3Value object, bool private_key) {
  auto* data = static_cast<KeyData*>(state->host->instance_get_native_data(
      object, private_key ? kPrivateType : kPublicType));
  if (data == nullptr) fail(state, call, "TypeError", "expected DH key");
  return data;
}

KeyNumbersData* key_numbers_data(CryptographyNativeState* state,
                                 X3CallContext* call, X3Value object,
                                 bool private_numbers) {
  auto* data = static_cast<KeyNumbersData*>(
      state->host->instance_get_native_data(
          object, private_numbers ? kPrivateNumbersType : kPublicNumbersType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected DH key numbers");
  return data;
}

bool python_int_to_bn(CryptographyNativeState* state, X3Runtime* runtime,
                      X3Value value, BIGNUM** result) {
  X3Value str_class = x3_value_invalid();
  X3Value text = x3_value_invalid();
  bool okay = false;
  if (state->host->builtin_value(state->host, "str", &str_class) ==
          X3_STATUS_OK &&
      state->host->call(runtime, str_class, &value, 1, &text) ==
          X3_STATUS_OK) {
    const char* decimal = state->host->value_to_cstr(runtime, text);
    okay = decimal != nullptr && BN_dec2bn(result, decimal) != 0 &&
           !BN_is_negative(*result);
  }
  for (X3Value item : {text, str_class})
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
  X3Value int_class = x3_value_invalid();
  const bool ready = state->host->builtin_value(
      state->host, "int", &int_class) == X3_STATUS_OK;
  const bool converted = ready &&
      state->host->call(runtime, int_class, &text, 1, result) ==
          X3_STATUS_OK;
  if (int_class.tag != X3_TAG_INVALID) state->host->value_release(int_class);
  state->host->value_release(text);
  return converted;
}

X3Status make_parameters(CryptographyNativeState* state, X3Runtime* runtime,
                         DH* parameters, X3Value* result) {
  auto data = std::make_unique<ParametersData>();
  data->parameters = parameters;
  X3Value instance = state->host->value_instance(
      runtime, state->dh_parameters_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kParametersType, data.get(), cleanup_parameters) !=
      X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status make_key(CryptographyNativeState* state, X3Runtime* runtime,
                  DH* key, bool private_key, X3Value* result) {
  auto data = std::make_unique<KeyData>();
  data->key = key;
  X3Value instance = state->host->value_instance(
      runtime, private_key ? state->dh_private_class : state->dh_public_class);
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

X3Status parameters_init(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value*, uint32_t, X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return fail(state, call, "TypeError",
              "cannot create 'cryptography.hazmat.bindings._rust.openssl.dh.DHParameters' instances");
}

X3Status key_init(X3CallContext* call, X3Runtime*, void* user_data,
                  const X3Value*, uint32_t, X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return fail(state, call, "TypeError", "cannot create DH key instances");
}

X3Status generate_private_key(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "generate_private_key() takes no arguments");
  auto* data = parameters_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::unique_ptr<DH, decltype(&DH_free)> key(
      DHparams_dup(data->parameters), DH_free);
  if (!key || DH_generate_key(key.get()) != 1)
    return fail(state, call, "ValueError", "DH key generation failed");
  return make_key(state, runtime, key.release(), true, result);
}

X3Status private_public_key(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "public_key() takes no arguments");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  const BIGNUM* public_number = nullptr;
  DH_get0_key(data->key, &public_number, nullptr);
  std::unique_ptr<DH, decltype(&DH_free)> public_key(
      DHparams_dup(data->key), DH_free);
  BIGNUM* copy = public_number == nullptr ? nullptr : BN_dup(public_number);
  if (!public_key || copy == nullptr ||
      DH_set0_key(public_key.get(), copy, nullptr) != 1) {
    BN_free(copy);
    return fail(state, call, "ValueError", "DH public key creation failed");
  }
  return make_key(state, runtime, public_key.release(), false, result);
}

X3Status key_parameters(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args,
                        uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "parameters() takes no arguments");
  auto* data = key_data(state, call, args[0],
                        state->host->instance_get_native_data(
                            args[0], kPrivateType) != nullptr);
  if (data == nullptr) return X3_STATUS_ERROR;
  DH* parameters = DHparams_dup(data->key);
  if (parameters == nullptr) return X3_STATUS_ERROR;
  return make_parameters(state, runtime, parameters, result);
}

X3Status key_size(X3CallContext* call, X3Runtime*,
                  void* user_data, const X3Value* args,
                  uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "key_size getter");
  auto* data = key_data(state, call, args[0],
                        state->host->instance_get_native_data(
                            args[0], kPrivateType) != nullptr);
  if (data == nullptr) return X3_STATUS_ERROR;
  const BIGNUM* p = nullptr;
  DH_get0_pqg(data->key, &p, nullptr, nullptr);
  *result = x3_value_int64(p == nullptr ? 0 : BN_num_bits(p));
  return X3_STATUS_OK;
}

X3Status private_exchange(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "exchange() requires a peer public key");
  auto* local = key_data(state, call, args[0], true);
  if (local == nullptr) return X3_STATUS_ERROR;
  auto* peer = key_data(state, call, args[1], false);
  if (peer == nullptr) return X3_STATUS_ERROR;
  const BIGNUM *local_p = nullptr, *local_g = nullptr;
  const BIGNUM *peer_p = nullptr, *peer_g = nullptr, *peer_public = nullptr;
  DH_get0_pqg(local->key, &local_p, nullptr, &local_g);
  DH_get0_pqg(peer->key, &peer_p, nullptr, &peer_g);
  DH_get0_key(peer->key, &peer_public, nullptr);
  if (local_p == nullptr || peer_p == nullptr || local_g == nullptr ||
      peer_g == nullptr || peer_public == nullptr ||
      BN_cmp(local_p, peer_p) != 0 || BN_cmp(local_g, peer_g) != 0)
    return fail(state, call, "ValueError",
                "peer_public_key and self are not using the same parameters");
  int check_codes = 0;
  if (DH_check_pub_key(local->key, peer_public, &check_codes) != 1 ||
      check_codes != 0)
    return fail(state, call, "ValueError", "Invalid DH public key");
  std::string secret(static_cast<size_t>(DH_size(local->key)), '\0');
  const int length = DH_compute_key_padded(
      reinterpret_cast<unsigned char*>(secret.data()), peer_public, local->key);
  if (length <= 0) {
    OPENSSL_cleanse(secret.data(), secret.size());
    return fail(state, call, "ValueError", "DH key exchange failed");
  }
  *result = state->host->value_bytes(runtime, secret.data(),
                                      static_cast<uint64_t>(length));
  OPENSSL_cleanse(secret.data(), secret.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status load_parameters(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result, bool pem) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 1 || argc > 2)
    return fail(state, call, "TypeError", "DH parameter data is required");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(buffer);
    return fail(state, call, "ValueError", "DH parameter data is too large");
  }
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(
      BIO_new_mem_buf(info.data, static_cast<int>(info.size)), BIO_free);
  DH* parameters = bio
      ? (pem ? PEM_read_bio_DHparams(bio.get(), nullptr, nullptr, nullptr)
             : d2i_DHparams_bio(bio.get(), nullptr))
      : nullptr;
  state->host->buffer_release(buffer);
  if (parameters == nullptr)
    return fail(state, call, "ValueError", "Could not deserialize DH parameters");
  return make_parameters(state, runtime, parameters, result);
}

X3Status from_pem_parameters(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  return load_parameters(call, runtime, user_data, args, argc, result, true);
}

X3Status from_der_parameters(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  return load_parameters(call, runtime, user_data, args, argc, result, false);
}

X3Status parameter_bytes(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError",
                "parameter_bytes() requires encoding and format");
  auto* data = parameters_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Value encoding_name = x3_value_invalid();
  X3Value format_name = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "name", &encoding_name) !=
          X3_STATUS_OK ||
      state->host->get_attr(runtime, args[2], "name", &format_name) !=
          X3_STATUS_OK) {
    for (X3Value item : {encoding_name, format_name})
      if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
    return X3_STATUS_ERROR;
  }
  const char* encoding_text = state->host->value_to_cstr(runtime, encoding_name);
  const std::string encoding = encoding_text == nullptr ? "" : encoding_text;
  const char* format_text = state->host->value_to_cstr(runtime, format_name);
  const std::string format = format_text == nullptr ? "" : format_text;
  state->host->value_release(encoding_name);
  state->host->value_release(format_name);
  if (format != "PKCS3" || (encoding != "PEM" && encoding != "DER"))
    return fail(state, call, "ValueError", "Unsupported DH parameter encoding");
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(
      BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return X3_STATUS_ERROR;
  const int okay = encoding == "PEM"
      ? PEM_write_bio_DHparams(bio.get(), data->parameters)
      : i2d_DHparams_bio(bio.get(), data->parameters);
  if (okay != 1)
    return fail(state, call, "ValueError", "DH parameter encoding failed");
  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio.get(), &memory);
  if (memory == nullptr) return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, memory->data, memory->length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status numbers_init(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 3 || argc > 4)
    return fail(state, call, "TypeError",
                "DHParameterNumbers requires p, g, and optional q");
  auto data = std::make_unique<NumbersData>();
  data->host = state->host;
  data->p = args[1];
  data->g = args[2];
  data->q = argc == 4 ? args[3] : x3_value_none();
  for (X3Value value : {data->p, data->g, data->q})
    state->host->value_retain(value);
  if (state->host->instance_set_native_data(
          args[0], kNumbersType, data.get(), cleanup_numbers) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status numbers_value(CryptographyNativeState* state, X3CallContext* call,
                       X3Value self, char field, X3Value* result) {
  auto* data = numbers_data(state, call, self);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = field == 'p' ? data->p : field == 'g' ? data->g : data->q;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

#define DH_NUMBER_GETTER(field)                                             \
  X3Status number_##field(X3CallContext* call, X3Runtime*, void* user_data,  \
                          const X3Value* args, uint32_t argc,              \
                          X3Value* result) {                                \
    auto* state = static_cast<CryptographyNativeState*>(user_data);          \
    if (argc != 1) return fail(state, call, "TypeError", #field " getter"); \
    return numbers_value(state, call, args[0], #field[0], result);          \
  }

DH_NUMBER_GETTER(p)
DH_NUMBER_GETTER(g)
DH_NUMBER_GETTER(q)

X3Status make_numbers(CryptographyNativeState* state, X3Runtime* runtime,
                      const BIGNUM* p, const BIGNUM* g, const BIGNUM* q,
                      X3Value* result) {
  X3Value values[3] = {x3_value_invalid(), x3_value_invalid(), x3_value_none()};
  if (!bn_to_python_int(state, runtime, p, &values[0]) ||
      !bn_to_python_int(state, runtime, g, &values[1]) ||
      (q != nullptr && !bn_to_python_int(state, runtime, q, &values[2]))) {
    for (X3Value value : values)
      if (value.tag != X3_TAG_INVALID && value.tag != X3_TAG_NONE)
        state->host->value_release(value);
    return X3_STATUS_ERROR;
  }
  X3Value instance = state->host->value_instance(
      runtime, state->dh_parameter_numbers_class);
  if (instance.tag == X3_TAG_INVALID) {
    for (X3Value value : values)
      if (value.tag != X3_TAG_INVALID && value.tag != X3_TAG_NONE)
        state->host->value_release(value);
    return X3_STATUS_ERROR;
  }
  X3Value init_args[4] = {instance, values[0], values[1], values[2]};
  X3Value ignored = x3_value_invalid();
  const X3Status initialized = numbers_init(
      nullptr, runtime, state, init_args, 4, &ignored);
  for (X3Value value : values)
    if (value.tag != X3_TAG_INVALID && value.tag != X3_TAG_NONE)
      state->host->value_release(value);
  if (initialized != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  *result = instance;
  return X3_STATUS_OK;
}

X3Status parameter_numbers(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "parameter_numbers() takes no arguments");
  auto* data = parameters_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  const BIGNUM *p = nullptr, *q = nullptr, *g = nullptr;
  DH_get0_pqg(data->parameters, &p, &q, &g);
  return make_numbers(state, runtime, p, g, q, result);
}

X3Status numbers_parameters(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "parameters() takes no arguments");
  auto* data = numbers_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  BIGNUM *p = nullptr, *g = nullptr, *q = nullptr;
  const bool okay = python_int_to_bn(state, runtime, data->p, &p) &&
      python_int_to_bn(state, runtime, data->g, &g) &&
      (data->q.tag == X3_TAG_NONE ||
       python_int_to_bn(state, runtime, data->q, &q));
  if (!okay) {
    BN_free(p); BN_free(g); BN_free(q);
    return fail(state, call, "TypeError", "DH parameters must be integers");
  }
  if (BN_num_bits(p) < 512) {
    BN_free(p); BN_free(g); BN_free(q);
    return fail(state, call, "ValueError",
                "p (modulus) must be at least 512-bit");
  }
  if (BN_cmp(g, BN_value_one()) <= 0) {
    BN_free(p); BN_free(g); BN_free(q);
    return fail(state, call, "ValueError",
                "DH generator must be 2 or greater");
  }
  std::unique_ptr<DH, decltype(&DH_free)> parameters(DH_new(), DH_free);
  if (!parameters || DH_set0_pqg(parameters.get(), p, q, g) != 1) {
    BN_free(p); BN_free(g); BN_free(q);
    return fail(state, call, "ValueError", "Invalid DH parameters");
  }
  int check_codes = 0;
  if (DH_check(parameters.get(), &check_codes) != 1 || check_codes != 0)
    return fail(state, call, "ValueError", "Invalid DH parameters");
  return make_parameters(state, runtime, parameters.release(), result);
}

X3Status generate_parameters(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 2 || argc > 3 || args[0].tag != X3_TAG_INT64 ||
      args[1].tag != X3_TAG_INT64)
    return fail(state, call, "TypeError",
                "generate_parameters() requires generator and key_size integers");
  const int64_t generator = args[0].as.i64;
  const int64_t bits = args[1].as.i64;
  if ((generator != 2 && generator != 5) || bits < 512 || bits > 8192)
    return fail(state, call, "ValueError", "Invalid DH generator or key size");
  std::unique_ptr<DH, decltype(&DH_free)> parameters(DH_new(), DH_free);
  if (!parameters || DH_generate_parameters_ex(
          parameters.get(), static_cast<int>(bits),
          static_cast<int>(generator), nullptr) != 1)
    return fail(state, call, "ValueError", "DH parameter generation failed");
  return make_parameters(state, runtime, parameters.release(), result);
}

std::string python_class_name(CryptographyNativeState* state,
                              X3Runtime* runtime, X3Value value) {
  X3Value type_function = x3_value_invalid();
  X3Value klass = x3_value_invalid();
  X3Value name = x3_value_invalid();
  std::string result = "object";
  if (state->host->builtin_value(state->host, "type", &type_function) ==
          X3_STATUS_OK &&
      state->host->call(runtime, type_function, &value, 1, &klass) ==
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

bool is_python_int(CryptographyNativeState* state, X3Runtime* runtime,
                   X3Value value) {
  X3Value checker = x3_value_invalid();
  X3Value int_class = x3_value_invalid();
  X3Value answer = x3_value_invalid();
  bool valid = false;
  if (state->host->builtin_value(state->host, "isinstance", &checker) ==
          X3_STATUS_OK &&
      state->host->builtin_value(state->host, "int", &int_class) ==
          X3_STATUS_OK) {
    X3Value arguments[2] = {value, int_class};
    if (state->host->call(runtime, checker, arguments, 2, &answer) ==
        X3_STATUS_OK)
      valid = answer.tag == X3_TAG_BOOL && answer.as.b;
  }
  for (X3Value item : {answer, int_class, checker})
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
  return valid;
}

X3Status key_numbers_init_impl(X3CallContext* call, X3Runtime* runtime,
                               void* user_data,
                               const X3Value* args, uint32_t argc,
                               X3Value* result, bool private_numbers) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError", "DH key numbers require two arguments");
  if (!is_python_int(state, runtime, args[1])) {
    const std::string message = std::string("argument '") +
        (private_numbers ? "x" : "y") + "': '" +
        python_class_name(state, runtime, args[1]) +
        "' object cannot be converted to 'PyInt'";
    return fail(state, call, "TypeError", message.c_str());
  }
  const char* expected = private_numbers ? kPublicNumbersType : kNumbersType;
  if (state->host->instance_get_native_data(args[2], expected) == nullptr) {
    const std::string type_name = private_numbers ?
        "DHPublicNumbers" : "DHParameterNumbers";
    const std::string message = std::string("argument '") +
        (private_numbers ? "public_numbers" : "parameter_numbers") +
        "': '" + python_class_name(state, runtime, args[2]) +
        "' object cannot be converted to '" + type_name + "'";
    return fail(state, call, "TypeError", message.c_str());
  }
  auto data = std::make_unique<KeyNumbersData>();
  data->host = state->host;
  data->component = args[1];
  data->related = args[2];
  state->host->value_retain(data->component);
  state->host->value_retain(data->related);
  if (state->host->instance_set_native_data(
          args[0], private_numbers ? kPrivateNumbersType : kPublicNumbersType,
          data.get(), cleanup_key_numbers) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status public_numbers_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                             const X3Value* args, uint32_t argc,
                             X3Value* result) {
  return key_numbers_init_impl(call, runtime, user_data, args, argc, result, false);
}

X3Status private_numbers_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                              const X3Value* args, uint32_t argc,
                              X3Value* result) {
  return key_numbers_init_impl(call, runtime, user_data, args, argc, result, true);
}

X3Status make_key_numbers(CryptographyNativeState* state, X3Runtime* runtime,
                          X3Value component, X3Value related,
                          bool private_numbers, X3Value* result) {
  auto data = std::make_unique<KeyNumbersData>();
  data->host = state->host;
  data->component = component;
  data->related = related;
  state->host->value_retain(component);
  state->host->value_retain(related);
  X3Value instance = state->host->value_instance(
      runtime, private_numbers ? state->dh_private_numbers_class
                               : state->dh_public_numbers_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, private_numbers ? kPrivateNumbersType : kPublicNumbersType,
          data.get(), cleanup_key_numbers) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status key_number_field(CryptographyNativeState* state, X3CallContext* call,
                          X3Value self, bool private_numbers, bool related,
                          X3Value* result) {
  auto* data = key_numbers_data(state, call, self, private_numbers);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = related ? data->related : data->component;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

#define DH_KEY_NUMBER_GETTER(name, is_private, is_related)                  \
  X3Status key_number_##name(X3CallContext* call, X3Runtime*,               \
                              void* user_data, const X3Value* args,         \
                              uint32_t argc, X3Value* result) {            \
    auto* state = static_cast<CryptographyNativeState*>(user_data);          \
    if (argc != 1) return fail(state, call, "TypeError", #name " getter");  \
    return key_number_field(state, call, args[0], is_private, is_related,     \
                            result);                                        \
  }

DH_KEY_NUMBER_GETTER(y, false, false)
DH_KEY_NUMBER_GETTER(parameter_numbers, false, true)
DH_KEY_NUMBER_GETTER(x, true, false)
DH_KEY_NUMBER_GETTER(public_numbers, true, true)

X3Status make_public_numbers(CryptographyNativeState* state,
                             X3Runtime* runtime, DH* key, X3Value* result) {
  const BIGNUM *p = nullptr, *q = nullptr, *g = nullptr, *y = nullptr;
  DH_get0_pqg(key, &p, &q, &g);
  DH_get0_key(key, &y, nullptr);
  X3Value parameters = x3_value_invalid();
  X3Value public_value = x3_value_invalid();
  if (make_numbers(state, runtime, p, g, q, &parameters) != X3_STATUS_OK ||
      !bn_to_python_int(state, runtime, y, &public_value)) {
    if (parameters.tag != X3_TAG_INVALID) state->host->value_release(parameters);
    if (public_value.tag != X3_TAG_INVALID) state->host->value_release(public_value);
    return X3_STATUS_ERROR;
  }
  const X3Status status = make_key_numbers(
      state, runtime, public_value, parameters, false, result);
  state->host->value_release(public_value);
  state->host->value_release(parameters);
  return status;
}

X3Status public_numbers(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args,
                        uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "public_numbers() takes no arguments");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  return make_public_numbers(state, runtime, data->key, result);
}

X3Status private_numbers(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "private_numbers() takes no arguments");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  const BIGNUM* x = nullptr;
  DH_get0_key(data->key, nullptr, &x);
  X3Value public_value = x3_value_invalid();
  X3Value private_value = x3_value_invalid();
  if (make_public_numbers(state, runtime, data->key, &public_value) !=
          X3_STATUS_OK ||
      !bn_to_python_int(state, runtime, x, &private_value)) {
    if (public_value.tag != X3_TAG_INVALID) state->host->value_release(public_value);
    if (private_value.tag != X3_TAG_INVALID) state->host->value_release(private_value);
    return X3_STATUS_ERROR;
  }
  const X3Status status = make_key_numbers(
      state, runtime, private_value, public_value, true, result);
  state->host->value_release(private_value);
  state->host->value_release(public_value);
  return status;
}

X3Status numbers_key(X3CallContext* call, X3Runtime* runtime,
                     void* user_data, const X3Value* args,
                     uint32_t argc, X3Value* result, bool private_numbers) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 1 || argc > 2)
    return fail(state, call, "TypeError", "DH numbers key() takes at most one argument");
  auto* numbers = key_numbers_data(state, call, args[0], private_numbers);
  if (numbers == nullptr) return X3_STATUS_ERROR;
  auto* public_data = private_numbers
      ? key_numbers_data(state, call, numbers->related, false) : numbers;
  if (public_data == nullptr) return X3_STATUS_ERROR;
  X3Value parameter_value = x3_value_invalid();
  X3Value parameter_arg = public_data->related;
  if (numbers_parameters(call, runtime, state, &parameter_arg, 1,
                         &parameter_value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto* parameter_data = parameters_data(state, call, parameter_value);
  if (parameter_data == nullptr) {
    state->host->value_release(parameter_value);
    return X3_STATUS_ERROR;
  }
  std::unique_ptr<DH, decltype(&DH_free)> key(
      DHparams_dup(parameter_data->parameters), DH_free);
  state->host->value_release(parameter_value);
  BIGNUM* y = nullptr;
  BIGNUM* x = nullptr;
  const bool parsed = key && python_int_to_bn(
      state, runtime, public_data->component, &y) &&
      (!private_numbers || python_int_to_bn(
          state, runtime, numbers->component, &x));
  if (!parsed) {
    BN_free(y); BN_free(x);
    return fail(state, call, "TypeError", "DH key numbers must be integers");
  }
  int check_codes = 0;
  if (DH_check_pub_key(key.get(), y, &check_codes) != 1 ||
      check_codes != 0) {
    BN_free(y); BN_free(x);
    return fail(state, call, "ValueError", "Invalid DH public key");
  }
  if (private_numbers) {
    const BIGNUM *p = nullptr, *g = nullptr;
    DH_get0_pqg(key.get(), &p, nullptr, &g);
    std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> context(BN_CTX_new(), BN_CTX_free);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> calculated(BN_new(), BN_free);
    if (!context || !calculated ||
        BN_mod_exp(calculated.get(), g, x, p, context.get()) != 1 ||
        BN_cmp(calculated.get(), y) != 0) {
      BN_free(y); BN_free(x);
      return fail(state, call, "ValueError", "Invalid DH private key");
    }
  }
  if (DH_set0_key(key.get(), y, x) != 1) {
    BN_free(y); BN_free(x);
    return fail(state, call, "ValueError", "Invalid DH key numbers");
  }
  return make_key(state, runtime, key.release(), private_numbers, result);
}

X3Status public_numbers_key(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  return numbers_key(call, runtime, user_data, args, argc, result, false);
}

X3Status private_numbers_key(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  return numbers_key(call, runtime, user_data, args, argc, result, true);
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

X3Status register_openssl_dh_module(X3Module* openssl,
                                    CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* dh = nullptr;
  if (host->add_module(host, kModuleName, &dh) != X3_STATUS_OK ||
      attach_child(host, openssl, dh, "dh") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef parameter_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       parameters_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "generate_private_key",
       generate_private_key, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parameter_bytes",
       parameter_bytes, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parameter_numbers",
       parameter_numbers, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef number_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       numbers_init, state, 3, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parameters",
       numbers_parameters, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef private_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       key_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_key",
       private_public_key, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parameters",
       key_parameters, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "exchange",
       private_exchange, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_numbers",
       private_numbers, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef public_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       key_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parameters",
       key_parameters, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_numbers",
       public_numbers, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef private_numbers_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       private_numbers_init, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_key",
       private_numbers_key, state, 1, 2, 0, nullptr},
  };
  const X3NativeFunctionDef public_numbers_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       public_numbers_init, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_key",
       public_numbers_key, state, 1, 2, 0, nullptr},
  };
  if (host->module_add_class(
          dh, "DHParameters", parameter_methods,
          static_cast<uint32_t>(std::size(parameter_methods)),
          &state->dh_parameters_class) != X3_STATUS_OK ||
      host->module_add_class(
          dh, "DHParameterNumbers", number_methods,
          static_cast<uint32_t>(std::size(number_methods)),
          &state->dh_parameter_numbers_class) != X3_STATUS_OK ||
      host->module_add_class(
          dh, "DHPrivateKey", private_methods,
          static_cast<uint32_t>(std::size(private_methods)),
          &state->dh_private_class) != X3_STATUS_OK ||
      host->module_add_class(
          dh, "DHPublicKey", public_methods,
          static_cast<uint32_t>(std::size(public_methods)),
          &state->dh_public_class) != X3_STATUS_OK ||
      host->module_add_class(
          dh, "DHPrivateNumbers", private_numbers_methods,
          static_cast<uint32_t>(std::size(private_numbers_methods)),
          &state->dh_private_numbers_class) != X3_STATUS_OK ||
      host->module_add_class(
          dh, "DHPublicNumbers", public_numbers_methods,
          static_cast<uint32_t>(std::size(public_numbers_methods)),
          &state->dh_public_numbers_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto add_property = [&](const char* name, X3NativeFn getter) -> X3Status {
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, name, getter, nullptr,
                              state, &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(
        state->dh_parameter_numbers_class, name, property);
    host->value_release(property);
    return status;
  };
  if (add_property("p", number_p) != X3_STATUS_OK ||
      add_property("g", number_g) != X3_STATUS_OK ||
      add_property("q", number_q) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  for (X3Value klass : {state->dh_private_class, state->dh_public_class}) {
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, "key_size", key_size, nullptr,
                              state, &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(klass, "key_size", property);
    host->value_release(property);
    if (status != X3_STATUS_OK) return X3_STATUS_ERROR;
  }
  auto add_key_number_property = [&](X3Value klass, const char* name,
                                     X3NativeFn getter) -> X3Status {
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, name, getter, nullptr,
                              state, &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(klass, name, property);
    host->value_release(property);
    return status;
  };
  if (add_key_number_property(state->dh_private_numbers_class, "x",
                              key_number_x) != X3_STATUS_OK ||
      add_key_number_property(state->dh_private_numbers_class, "public_numbers",
                              key_number_public_numbers) != X3_STATUS_OK ||
      add_key_number_property(state->dh_public_numbers_class, "y",
                              key_number_y) != X3_STATUS_OK ||
      add_key_number_property(state->dh_public_numbers_class, "parameter_numbers",
                              key_number_parameter_numbers) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef functions[] = {
      {sizeof(X3NativeFunctionDef), "from_pem_parameters",
       from_pem_parameters, state, 1, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "from_der_parameters",
       from_der_parameters, state, 1, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "generate_parameters",
       generate_parameters, state, 2, 3, 0, nullptr},
  };
  for (const auto& function : functions)
    if (host->module_add_function(dh, &function) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
