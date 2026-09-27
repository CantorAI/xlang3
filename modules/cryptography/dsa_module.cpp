/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/bn.h>
#include <openssl/dsa.h>
#include <openssl/evp.h>

#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kModuleName =
    "cryptography.hazmat.bindings._rust.openssl.dsa";
constexpr const char* kParametersType =
    "cryptography.hazmat.bindings._rust.openssl.dsa.DSAParameters";
constexpr const char* kNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.dsa.DSAParameterNumbers";
constexpr const char* kPrivateType =
    "cryptography.hazmat.bindings._rust.openssl.dsa.DSAPrivateKey";
constexpr const char* kPublicType =
    "cryptography.hazmat.bindings._rust.openssl.dsa.DSAPublicKey";
constexpr const char* kPrivateNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.dsa.DSAPrivateNumbers";
constexpr const char* kPublicNumbersType =
    "cryptography.hazmat.bindings._rust.openssl.dsa.DSAPublicNumbers";

struct ParametersData {
  DSA* parameters = nullptr;
  ~ParametersData() { DSA_free(parameters); }
};

struct KeyData {
  DSA* key = nullptr;
  ~KeyData() { DSA_free(key); }
};

struct NumbersData {
  X3PackageHost* host = nullptr;
  X3Value p = x3_value_invalid();
  X3Value q = x3_value_invalid();
  X3Value g = x3_value_invalid();
  ~NumbersData() {
    for (X3Value value : {p, q, g})
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
void cleanup_numbers(void* data) { delete static_cast<NumbersData*>(data); }
void cleanup_key(void* data) { delete static_cast<KeyData*>(data); }
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
  if (data == nullptr) fail(state, call, "TypeError", "expected DSA parameters");
  return data;
}

NumbersData* numbers_data(CryptographyNativeState* state,
                          X3CallContext* call, X3Value object) {
  auto* data = static_cast<NumbersData*>(
      state->host->instance_get_native_data(object, kNumbersType));
  if (data == nullptr) fail(state, call, "TypeError", "expected DSA parameter numbers");
  return data;
}

KeyData* key_data(CryptographyNativeState* state, X3CallContext* call,
                  X3Value object, bool private_key) {
  auto* data = static_cast<KeyData*>(state->host->instance_get_native_data(
      object, private_key ? kPrivateType : kPublicType));
  if (data == nullptr) fail(state, call, "TypeError", "expected DSA key");
  return data;
}

KeyNumbersData* key_numbers_data(CryptographyNativeState* state,
                                 X3CallContext* call, X3Value object,
                                 bool private_numbers) {
  auto* data = static_cast<KeyNumbersData*>(
      state->host->instance_get_native_data(
          object, private_numbers ? kPrivateNumbersType : kPublicNumbersType));
  if (data == nullptr) fail(state, call, "TypeError", "expected DSA key numbers");
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
                         DSA* parameters, X3Value* result) {
  auto data = std::make_unique<ParametersData>();
  data->parameters = parameters;
  X3Value instance = state->host->value_instance(
      runtime, state->dsa_parameters_class);
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

std::unique_ptr<DSA, decltype(&DSA_free)> clone_parameters(DSA* source) {
  std::unique_ptr<DSA, decltype(&DSA_free)> copy(DSA_new(), DSA_free);
  const BIGNUM *p = nullptr, *q = nullptr, *g = nullptr;
  DSA_get0_pqg(source, &p, &q, &g);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> p_copy(
      p == nullptr ? nullptr : BN_dup(p), BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> q_copy(
      q == nullptr ? nullptr : BN_dup(q), BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> g_copy(
      g == nullptr ? nullptr : BN_dup(g), BN_free);
  if (!copy || !p_copy || !q_copy || !g_copy ||
      DSA_set0_pqg(copy.get(), p_copy.get(), q_copy.get(), g_copy.get()) != 1) {
    copy.reset();
    return copy;
  }
  p_copy.release();
  q_copy.release();
  g_copy.release();
  return copy;
}

X3Status make_key(CryptographyNativeState* state, X3Runtime* runtime,
                  DSA* key, bool private_key, X3Value* result) {
  auto data = std::make_unique<KeyData>();
  data->key = key;
  X3Value instance = state->host->value_instance(
      runtime, private_key ? state->dsa_private_class : state->dsa_public_class);
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
  return fail(state, call, "TypeError", "cannot create DSA key instances");
}

X3Status generate_private_key(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "generate_private_key() takes no arguments");
  auto* data = parameters_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  auto key = clone_parameters(data->parameters);
  if (!key || DSA_generate_key(key.get()) != 1)
    return fail(state, call, "ValueError", "DSA key generation failed");
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
  auto public_key = clone_parameters(data->key);
  const BIGNUM* y = nullptr;
  DSA_get0_key(data->key, &y, nullptr);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> y_copy(
      y == nullptr ? nullptr : BN_dup(y), BN_free);
  if (!public_key || !y_copy ||
      DSA_set0_key(public_key.get(), y_copy.get(), nullptr) != 1)
    return fail(state, call, "ValueError", "DSA public key creation failed");
  y_copy.release();
  return make_key(state, runtime, public_key.release(), false, result);
}

X3Status key_parameters(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args,
                        uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "parameters() takes no arguments");
  const bool private_key = state->host->instance_get_native_data(
      args[0], kPrivateType) != nullptr;
  auto* data = key_data(state, call, args[0], private_key);
  if (data == nullptr) return X3_STATUS_ERROR;
  auto parameters = clone_parameters(data->key);
  if (!parameters) return X3_STATUS_ERROR;
  return make_parameters(state, runtime, parameters.release(), result);
}

X3Status key_size(X3CallContext* call, X3Runtime*, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "key_size getter");
  const bool private_key = state->host->instance_get_native_data(
      args[0], kPrivateType) != nullptr;
  auto* data = key_data(state, call, args[0], private_key);
  if (data == nullptr) return X3_STATUS_ERROR;
  const BIGNUM* p = nullptr;
  DSA_get0_pqg(data->key, &p, nullptr, nullptr);
  *result = x3_value_int64(p == nullptr ? 0 : BN_num_bits(p));
  return X3_STATUS_OK;
}

X3Status parameters_init(X3CallContext* call, X3Runtime*, void* user_data,
                         const X3Value*, uint32_t, X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return fail(state, call, "TypeError",
              "cannot create 'cryptography.hazmat.bindings._rust.openssl.dsa.DSAParameters' instances");
}

X3Status generate_parameters(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1 || args[0].tag != X3_TAG_INT64)
    return fail(state, call, "TypeError", "generate_parameters() requires key_size");
  const int64_t bits = args[0].as.i64;
  if (bits != 1024 && bits != 2048 && bits != 3072 && bits != 4096)
    return fail(state, call, "ValueError",
                "Key size must be 1024, 2048, 3072, or 4096 bits.");
  std::unique_ptr<DSA, decltype(&DSA_free)> parameters(DSA_new(), DSA_free);
  if (!parameters || DSA_generate_parameters_ex(
          parameters.get(), static_cast<int>(bits), nullptr, 0,
          nullptr, nullptr, nullptr) != 1)
    return fail(state, call, "ValueError", "DSA parameter generation failed");
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

X3Status numbers_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 4)
    return fail(state, call, "TypeError", "DSAParameterNumbers requires p, q, g");
  for (uint32_t index = 1; index < 4; ++index) {
    if (!is_python_int(state, runtime, args[index])) {
      const char* name = index == 1 ? "p" : index == 2 ? "q" : "g";
      const std::string message = std::string("argument '") + name +
          "': '" + python_class_name(state, runtime, args[index]) +
          "' object cannot be converted to 'PyInt'";
      return fail(state, call, "TypeError", message.c_str());
    }
  }
  auto data = std::make_unique<NumbersData>();
  data->host = state->host;
  data->p = args[1];
  data->q = args[2];
  data->g = args[3];
  for (X3Value value : {data->p, data->q, data->g})
    state->host->value_retain(value);
  if (state->host->instance_set_native_data(
          args[0], kNumbersType, data.get(), cleanup_numbers) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status numbers_field(CryptographyNativeState* state, X3CallContext* call,
                       X3Value self, char field, X3Value* result) {
  auto* data = numbers_data(state, call, self);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = field == 'p' ? data->p : field == 'q' ? data->q : data->g;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

#define DSA_NUMBER_GETTER(field)                                            \
  X3Status number_##field(X3CallContext* call, X3Runtime*, void* user_data,  \
                          const X3Value* args, uint32_t argc,              \
                          X3Value* result) {                                \
    auto* state = static_cast<CryptographyNativeState*>(user_data);          \
    if (argc != 1) return fail(state, call, "TypeError", #field " getter");  \
    return numbers_field(state, call, args[0], #field[0], result);           \
  }

DSA_NUMBER_GETTER(p)
DSA_NUMBER_GETTER(q)
DSA_NUMBER_GETTER(g)

X3Status make_numbers(CryptographyNativeState* state, X3Runtime* runtime,
                      const BIGNUM* p, const BIGNUM* q, const BIGNUM* g,
                      X3Value* result) {
  X3Value values[3] = {x3_value_invalid(), x3_value_invalid(), x3_value_invalid()};
  if (!bn_to_python_int(state, runtime, p, &values[0]) ||
      !bn_to_python_int(state, runtime, q, &values[1]) ||
      !bn_to_python_int(state, runtime, g, &values[2])) {
    for (X3Value value : values)
      if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
    return X3_STATUS_ERROR;
  }
  X3Value instance = state->host->value_instance(
      runtime, state->dsa_parameter_numbers_class);
  if (instance.tag == X3_TAG_INVALID) {
    for (X3Value value : values) state->host->value_release(value);
    return X3_STATUS_ERROR;
  }
  auto data = std::make_unique<NumbersData>();
  data->host = state->host;
  data->p = values[0];
  data->q = values[1];
  data->g = values[2];
  if (state->host->instance_set_native_data(
          instance, kNumbersType, data.get(), cleanup_numbers) !=
      X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
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
  DSA_get0_pqg(data->parameters, &p, &q, &g);
  return make_numbers(state, runtime, p, q, g, result);
}

X3Status numbers_parameters(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "parameters() takes no arguments");
  auto* data = numbers_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  BIGNUM *p = nullptr, *q = nullptr, *g = nullptr;
  const bool parsed = python_int_to_bn(state, runtime, data->p, &p) &&
      python_int_to_bn(state, runtime, data->q, &q) &&
      python_int_to_bn(state, runtime, data->g, &g);
  if (!parsed) {
    BN_free(p); BN_free(q); BN_free(g);
    return fail(state, call, "TypeError", "DSA parameters must be integers");
  }
  const int p_bits = BN_num_bits(p);
  if (p_bits != 1024 && p_bits != 2048 && p_bits != 3072 && p_bits != 4096) {
    BN_free(p); BN_free(q); BN_free(g);
    return fail(state, call, "ValueError",
                "p must be exactly 1024, 2048, 3072, or 4096 bits long");
  }
  if (BN_cmp(g, BN_value_one()) <= 0 || BN_cmp(g, p) >= 0) {
    BN_free(p); BN_free(q); BN_free(g);
    return fail(state, call, "ValueError", "g, p don't satisfy 1 < g < p.");
  }
  std::unique_ptr<DSA, decltype(&DSA_free)> parameters(DSA_new(), DSA_free);
  if (!parameters || DSA_set0_pqg(parameters.get(), p, q, g) != 1) {
    BN_free(p); BN_free(q); BN_free(g);
    return fail(state, call, "ValueError", "Invalid DSA parameters");
  }
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_PKEY_new(), EVP_PKEY_free);
  if (!key || EVP_PKEY_set1_DSA(key.get(), parameters.get()) != 1)
    return fail(state, call, "ValueError", "Invalid DSA parameters");
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
      EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
  if (!context || EVP_PKEY_param_check(context.get()) != 1)
    return fail(state, call, "ValueError", "Invalid DSA parameters");
  return make_parameters(state, runtime, parameters.release(), result);
}

X3Status key_numbers_init_impl(X3CallContext* call, X3Runtime* runtime,
                               void* user_data, const X3Value* args,
                               uint32_t argc, X3Value* result,
                               bool private_numbers) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError", "DSA key numbers require two arguments");
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
        "DSAPublicNumbers" : "DSAParameterNumbers";
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

X3Status public_numbers_init(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  return key_numbers_init_impl(call, runtime, user_data, args, argc,
                               result, false);
}

X3Status private_numbers_init(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  return key_numbers_init_impl(call, runtime, user_data, args, argc,
                               result, true);
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
      runtime, private_numbers ? state->dsa_private_numbers_class
                               : state->dsa_public_numbers_class);
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

#define DSA_KEY_NUMBER_GETTER(name, is_private, is_related)                 \
  X3Status key_number_##name(X3CallContext* call, X3Runtime*,               \
                              void* user_data, const X3Value* args,         \
                              uint32_t argc, X3Value* result) {            \
    auto* state = static_cast<CryptographyNativeState*>(user_data);          \
    if (argc != 1) return fail(state, call, "TypeError", #name " getter");  \
    return key_number_field(state, call, args[0], is_private, is_related,     \
                            result);                                        \
  }

DSA_KEY_NUMBER_GETTER(y, false, false)
DSA_KEY_NUMBER_GETTER(parameter_numbers, false, true)
DSA_KEY_NUMBER_GETTER(x, true, false)
DSA_KEY_NUMBER_GETTER(public_numbers, true, true)

X3Status make_public_numbers(CryptographyNativeState* state,
                             X3Runtime* runtime, DSA* key, X3Value* result) {
  const BIGNUM *p = nullptr, *q = nullptr, *g = nullptr, *y = nullptr;
  DSA_get0_pqg(key, &p, &q, &g);
  DSA_get0_key(key, &y, nullptr);
  X3Value parameters = x3_value_invalid();
  X3Value public_value = x3_value_invalid();
  if (make_numbers(state, runtime, p, q, g, &parameters) != X3_STATUS_OK ||
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
  DSA_get0_key(data->key, nullptr, &x);
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
    return fail(state, call, "TypeError", "DSA numbers key() takes at most one argument");
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
  auto key = clone_parameters(parameter_data->parameters);
  state->host->value_release(parameter_value);
  BIGNUM* y = nullptr;
  BIGNUM* x = nullptr;
  const bool parsed = key && python_int_to_bn(
      state, runtime, public_data->component, &y) &&
      (!private_numbers || python_int_to_bn(
          state, runtime, numbers->component, &x));
  if (!parsed) {
    BN_free(y); BN_clear_free(x);
    return fail(state, call, "TypeError", "DSA key numbers must be integers");
  }
  const BIGNUM *p = nullptr, *q = nullptr, *g = nullptr;
  DSA_get0_pqg(key.get(), &p, &q, &g);
  std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> context(BN_CTX_new(), BN_CTX_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> calculated(BN_new(), BN_free);
  if (!context || !calculated || BN_cmp(y, BN_value_one()) <= 0 ||
      BN_cmp(y, p) >= 0 ||
      BN_mod_exp(calculated.get(), y, q, p, context.get()) != 1 ||
      !BN_is_one(calculated.get())) {
    BN_free(y); BN_clear_free(x);
    return fail(state, call, "ValueError", "Invalid DSA public key");
  }
  if (private_numbers &&
      (BN_is_zero(x) || BN_cmp(x, q) >= 0 ||
       BN_mod_exp_mont_consttime(calculated.get(), g, x, p,
                                 context.get(), nullptr) != 1 ||
       BN_cmp(calculated.get(), y) != 0)) {
    BN_free(y); BN_clear_free(x);
    return fail(state, call, "ValueError", "Invalid DSA private key");
  }
  if (DSA_set0_key(key.get(), y, x) != 1) {
    BN_free(y); BN_clear_free(x);
    return fail(state, call, "ValueError", "Invalid DSA key numbers");
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

const EVP_MD* signature_digest(CryptographyNativeState* state,
                               X3Runtime* runtime, X3Value algorithm,
                               bool& prehashed) {
  prehashed = python_class_name(state, runtime, algorithm) == "Prehashed";
  X3Value nested = x3_value_invalid();
  X3Value name = x3_value_invalid();
  X3Value source = algorithm;
  if (prehashed) {
    if (state->host->get_attr(runtime, algorithm, "_algorithm", &nested) !=
        X3_STATUS_OK) return nullptr;
    source = nested;
  }
  const EVP_MD* digest = nullptr;
  if (state->host->get_attr(runtime, source, "name", &name) == X3_STATUS_OK) {
    const char* text = state->host->value_to_cstr(runtime, name);
    if (text != nullptr) digest = EVP_get_digestbyname(text);
  }
  if (name.tag != X3_TAG_INVALID) state->host->value_release(name);
  if (nested.tag != X3_TAG_INVALID) state->host->value_release(nested);
  return digest;
}

bool prepare_digest(CryptographyNativeState* state, X3Runtime* runtime,
                    X3Value source, X3Value algorithm,
                    std::vector<unsigned char>& digest_bytes) {
  bool prehashed = false;
  const EVP_MD* digest = signature_digest(state, runtime, algorithm, prehashed);
  if (digest == nullptr) return false;
  const int digest_size = EVP_MD_get_size(digest);
  if (digest_size <= 0) return false;
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, source, 0, &input, &info) !=
      X3_STATUS_OK) return false;
  bool okay = false;
  if (prehashed) {
    if (info.size == static_cast<uint64_t>(digest_size)) {
      const auto* bytes = static_cast<const unsigned char*>(info.data);
      digest_bytes.assign(bytes, bytes + static_cast<size_t>(info.size));
      okay = true;
    }
  } else {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    digest_bytes.resize(static_cast<size_t>(digest_size));
    unsigned int length = 0;
    okay = context &&
        EVP_DigestInit_ex(context.get(), digest, nullptr) == 1 &&
        EVP_DigestUpdate(context.get(), info.data,
                         static_cast<size_t>(info.size)) == 1 &&
        EVP_DigestFinal_ex(context.get(), digest_bytes.data(), &length) == 1;
    if (okay) digest_bytes.resize(length);
  }
  state->host->buffer_release(input);
  return okay;
}

X3Status private_sign(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError", "sign() requires data and algorithm");
  auto* data = key_data(state, call, args[0], true);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::vector<unsigned char> digest;
  if (!prepare_digest(state, runtime, args[1], args[2], digest))
    return fail(state, call, "ValueError", "unable to hash DSA input");
  std::unique_ptr<DSA_SIG, decltype(&DSA_SIG_free)> signature(
      DSA_do_sign(digest.data(), static_cast<int>(digest.size()), data->key),
      DSA_SIG_free);
  if (!signature)
    return fail(state, call, "ValueError", "unable to sign with DSA key");
  const int size = i2d_DSA_SIG(signature.get(), nullptr);
  if (size <= 0)
    return fail(state, call, "ValueError", "unable to encode DSA signature");
  std::vector<unsigned char> encoded(static_cast<size_t>(size));
  unsigned char* cursor = encoded.data();
  if (i2d_DSA_SIG(signature.get(), &cursor) != size)
    return fail(state, call, "ValueError", "unable to encode DSA signature");
  *result = state->host->value_bytes(runtime, encoded.data(), encoded.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status public_verify(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 4)
    return fail(state, call, "TypeError",
                "verify() requires signature, data, and algorithm");
  auto* data = key_data(state, call, args[0], false);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::vector<unsigned char> digest;
  if (!prepare_digest(state, runtime, args[2], args[3], digest))
    return fail(state, call, "ValueError", "unable to hash DSA input");
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  const auto* begin = static_cast<const unsigned char*>(info.data);
  const unsigned char* cursor = begin;
  std::unique_ptr<DSA_SIG, decltype(&DSA_SIG_free)> signature(
      info.size <= static_cast<uint64_t>(std::numeric_limits<long>::max())
          ? d2i_DSA_SIG(nullptr, &cursor, static_cast<long>(info.size))
          : nullptr,
      DSA_SIG_free);
  const bool strict = signature &&
      cursor == begin + static_cast<size_t>(info.size);
  state->host->buffer_release(input);
  const int verified = strict
      ? DSA_do_verify(digest.data(), static_cast<int>(digest.size()),
                      signature.get(), data->key)
      : 0;
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

X3Status register_openssl_dsa_module(X3Module* openssl,
                                     CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* dsa = nullptr;
  if (host->add_module(host, kModuleName, &dsa) != X3_STATUS_OK ||
      attach_child(host, openssl, dsa, "dsa") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef parameter_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       parameters_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "generate_private_key",
       generate_private_key, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parameter_numbers",
       parameter_numbers, state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef number_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       numbers_init, state, 4, 4, 0, nullptr},
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
      {sizeof(X3NativeFunctionDef), "private_numbers",
       private_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "sign",
       private_sign, state, 3, 3, 0, nullptr},
  };
  const X3NativeFunctionDef public_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       key_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parameters",
       key_parameters, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_numbers",
       public_numbers, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "verify",
       public_verify, state, 4, 4, 0, nullptr},
  };
  const X3NativeFunctionDef private_number_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       private_numbers_init, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "private_key",
       private_numbers_key, state, 1, 2, 0, nullptr},
  };
  const X3NativeFunctionDef public_number_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       public_numbers_init, state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_key",
       public_numbers_key, state, 1, 2, 0, nullptr},
  };
  if (host->module_add_class(
          dsa, "DSAParameters", parameter_methods,
          static_cast<uint32_t>(std::size(parameter_methods)),
          &state->dsa_parameters_class) != X3_STATUS_OK ||
      host->module_add_class(
          dsa, "DSAParameterNumbers", number_methods,
          static_cast<uint32_t>(std::size(number_methods)),
          &state->dsa_parameter_numbers_class) != X3_STATUS_OK ||
      host->module_add_class(
          dsa, "DSAPrivateKey", private_methods,
          static_cast<uint32_t>(std::size(private_methods)),
          &state->dsa_private_class) != X3_STATUS_OK ||
      host->module_add_class(
          dsa, "DSAPublicKey", public_methods,
          static_cast<uint32_t>(std::size(public_methods)),
          &state->dsa_public_class) != X3_STATUS_OK ||
      host->module_add_class(
          dsa, "DSAPrivateNumbers", private_number_methods,
          static_cast<uint32_t>(std::size(private_number_methods)),
          &state->dsa_private_numbers_class) != X3_STATUS_OK ||
      host->module_add_class(
          dsa, "DSAPublicNumbers", public_number_methods,
          static_cast<uint32_t>(std::size(public_number_methods)),
          &state->dsa_public_numbers_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto add_property = [&](const char* name, X3NativeFn getter) -> X3Status {
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, name, getter, nullptr,
                              state, &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(
        state->dsa_parameter_numbers_class, name, property);
    host->value_release(property);
    return status;
  };
  if (add_property("p", number_p) != X3_STATUS_OK ||
      add_property("q", number_q) != X3_STATUS_OK ||
      add_property("g", number_g) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto add_class_property = [&](X3Value klass, const char* name,
                                X3NativeFn getter) -> X3Status {
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, name, getter, nullptr,
                              state, &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(klass, name, property);
    host->value_release(property);
    return status;
  };
  if (add_class_property(state->dsa_private_class, "key_size",
                         key_size) != X3_STATUS_OK ||
      add_class_property(state->dsa_public_class, "key_size",
                         key_size) != X3_STATUS_OK ||
      add_class_property(state->dsa_private_numbers_class, "x",
                         key_number_x) != X3_STATUS_OK ||
      add_class_property(state->dsa_private_numbers_class, "public_numbers",
                         key_number_public_numbers) != X3_STATUS_OK ||
      add_class_property(state->dsa_public_numbers_class, "y",
                         key_number_y) != X3_STATUS_OK ||
      add_class_property(state->dsa_public_numbers_class, "parameter_numbers",
                         key_number_parameter_numbers) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef generate = {
      sizeof(X3NativeFunctionDef), "generate_parameters",
      generate_parameters, state, 1, 1, 0, nullptr};
  return host->module_add_function(dsa, &generate);
}
