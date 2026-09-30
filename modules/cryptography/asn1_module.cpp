/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "xlang3/xlang3.h"
#include "native_state.h"

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/asn1.h>
#include <openssl/x509.h>

#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kRootName = "cryptography.hazmat.bindings._rust";
constexpr const char* kAsn1Name = "cryptography.hazmat.bindings._rust.asn1";

using PackageState = CryptographyNativeState;

void cleanup_package(void* data) {
  auto* state = static_cast<PackageState*>(data);
  if (state != nullptr && state->oid_class.tag != X3_TAG_INVALID)
    state->host->value_release(state->oid_class);
  if (state != nullptr && state->reasons_class.tag != X3_TAG_INVALID)
    state->host->value_release(state->reasons_class);
  if (state != nullptr && state->hash_class.tag != X3_TAG_INVALID)
    state->host->value_release(state->hash_class);
  if (state != nullptr && state->xof_hash_class.tag != X3_TAG_INVALID)
    state->host->value_release(state->xof_hash_class);
  if (state != nullptr && state->hmac_class.tag != X3_TAG_INVALID)
    state->host->value_release(state->hmac_class);
  if (state != nullptr)
    for (X3Value value : {state->rsa_private_class, state->rsa_public_class,
                          state->rsa_private_numbers_class,
                          state->rsa_public_numbers_class,
                          state->ec_private_class, state->ec_public_class,
                          state->ec_private_numbers_class,
                          state->ec_public_numbers_class,
                          state->x509_certificate_class,
                          state->dh_parameters_class,
                          state->dh_parameter_numbers_class})
      if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  delete state;
}

X3Status raise(PackageState* state, X3CallContext* call,
               const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

std::string python_type_name(PackageState* state, X3Runtime* runtime,
                             X3Value value) {
  X3Value type_function = x3_value_invalid();
  X3Value type_object = x3_value_invalid();
  X3Value name = x3_value_invalid();
  std::string result = "object";
  if (state->host->builtin_value(state->host, "type", &type_function) ==
          X3_STATUS_OK &&
      state->host->call(runtime, type_function, &value, 1, &type_object) ==
          X3_STATUS_OK &&
      state->host->get_attr(runtime, type_object, "__name__", &name) ==
          X3_STATUS_OK) {
    const char* text = state->host->value_to_cstr(runtime, name);
    if (text != nullptr) result = text;
  }
  if (name.tag != X3_TAG_INVALID) state->host->value_release(name);
  if (type_object.tag != X3_TAG_INVALID) state->host->value_release(type_object);
  if (type_function.tag != X3_TAG_INVALID) state->host->value_release(type_function);
  return result;
}

bool python_int_to_bn(PackageState* state, X3CallContext* call,
                      X3Runtime* runtime, X3Value input,
                      const char* argument_name, BIGNUM** result) {
  X3Value int_class = x3_value_invalid();
  X3Value isinstance_fn = x3_value_invalid();
  X3Value str_class = x3_value_invalid();
  X3Value checked = x3_value_invalid();
  X3Value normalized = x3_value_invalid();
  X3Value decimal = x3_value_invalid();
  bool valid = false;
  if (state->host->builtin_value(state->host, "int", &int_class) != X3_STATUS_OK ||
      state->host->builtin_value(state->host, "isinstance", &isinstance_fn) != X3_STATUS_OK ||
      state->host->builtin_value(state->host, "str", &str_class) != X3_STATUS_OK)
    goto done;
  {
    const X3Value arguments[] = {input, int_class};
    if (state->host->call(runtime, isinstance_fn, arguments, 2, &checked) !=
        X3_STATUS_OK) goto done;
  }
  if (checked.tag != X3_TAG_BOOL || !checked.as.b) {
    const std::string type_name = python_type_name(state, runtime, input);
    const std::string message = std::string("argument '") + argument_name +
        "': '" + type_name + "' object cannot be converted to 'PyInt'";
    raise(state, call, "TypeError", message.c_str());
    goto done;
  }
  if (state->host->call(runtime, int_class, &input, 1, &normalized) != X3_STATUS_OK)
    goto done;
  if (state->host->call(runtime, str_class, &normalized, 1, &decimal) != X3_STATUS_OK)
    goto done;
  {
    const char* text = state->host->value_to_cstr(runtime, decimal);
    if (text == nullptr || BN_dec2bn(result, text) == 0) {
      raise(state, call, "ValueError", "invalid integer");
      goto done;
    }
  }
  if (BN_is_negative(*result)) {
    raise(state, call, "ValueError", "Negative integers are not supported");
    goto done;
  }
  valid = true;
done:
  if (decimal.tag != X3_TAG_INVALID) state->host->value_release(decimal);
  if (normalized.tag != X3_TAG_INVALID) state->host->value_release(normalized);
  if (checked.tag != X3_TAG_INVALID) state->host->value_release(checked);
  if (str_class.tag != X3_TAG_INVALID) state->host->value_release(str_class);
  if (isinstance_fn.tag != X3_TAG_INVALID) state->host->value_release(isinstance_fn);
  if (int_class.tag != X3_TAG_INVALID) state->host->value_release(int_class);
  return valid;
}

bool bn_to_python_int(PackageState* state, X3Runtime* runtime,
                      const BIGNUM* number, X3Value* result) {
  char* decimal_text = BN_bn2dec(number);
  if (decimal_text == nullptr) return false;
  X3Value decimal = state->host->value_string(runtime, decimal_text);
  OPENSSL_free(decimal_text);
  if (decimal.tag == X3_TAG_INVALID) return false;
  X3Value int_class = x3_value_invalid();
  const bool ready = state->host->builtin_value(state->host, "int", &int_class) ==
      X3_STATUS_OK;
  const bool converted = ready &&
      state->host->call(runtime, int_class, &decimal, 1, result) == X3_STATUS_OK;
  if (int_class.tag != X3_TAG_INVALID) state->host->value_release(int_class);
  state->host->value_release(decimal);
  return converted;
}

X3Status encode_dss_signature(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return raise(state, call, "TypeError",
                              "encode_dss_signature() takes 2 arguments");
  BIGNUM* r = nullptr;
  BIGNUM* s = nullptr;
  if (!python_int_to_bn(state, call, runtime, args[0], "r", &r) ||
      !python_int_to_bn(state, call, runtime, args[1], "s", &s)) {
    BN_free(r);
    BN_free(s);
    return X3_STATUS_ERROR;
  }
  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(
      ECDSA_SIG_new(), ECDSA_SIG_free);
  if (!signature || ECDSA_SIG_set0(signature.get(), r, s) != 1) {
    BN_free(r);
    BN_free(s);
    return raise(state, call, "ValueError", "unable to encode DSS signature");
  }
  const int size = i2d_ECDSA_SIG(signature.get(), nullptr);
  if (size <= 0) return raise(state, call, "ValueError", "unable to encode DSS signature");
  std::vector<unsigned char> der(static_cast<size_t>(size));
  unsigned char* cursor = der.data();
  if (i2d_ECDSA_SIG(signature.get(), &cursor) != size)
    return raise(state, call, "ValueError", "unable to encode DSS signature");
  *result = state->host->value_bytes(runtime, der.data(), der.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status decode_dss_signature(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return raise(state, call, "TypeError",
                              "decode_dss_signature() takes 1 argument");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<long>::max())) {
    state->host->buffer_release(buffer);
    return raise(state, call, "ValueError", "ASN.1 value is too large");
  }
  const auto* data = static_cast<const unsigned char*>(info.data);
  const unsigned char* cursor = data;
  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(
      d2i_ECDSA_SIG(nullptr, &cursor, static_cast<long>(info.size)),
      ECDSA_SIG_free);
  bool canonical = signature && cursor == data + info.size;
  if (canonical) {
    const int size = i2d_ECDSA_SIG(signature.get(), nullptr);
    canonical = size > 0 && static_cast<uint64_t>(size) == info.size;
    if (canonical) {
      std::vector<unsigned char> reencoded(static_cast<size_t>(size));
      unsigned char* output = reencoded.data();
      canonical = i2d_ECDSA_SIG(signature.get(), &output) == size &&
          std::memcmp(data, reencoded.data(), reencoded.size()) == 0;
    }
  }
  state->host->buffer_release(buffer);
  if (!canonical) return raise(state, call, "ValueError", "invalid DSS signature encoding");
  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;
  ECDSA_SIG_get0(signature.get(), &r, &s);
  X3Value first = x3_value_invalid();
  X3Value second = x3_value_invalid();
  if (!bn_to_python_int(state, runtime, r, &first) ||
      !bn_to_python_int(state, runtime, s, &second)) {
    if (first.tag != X3_TAG_INVALID) state->host->value_release(first);
    if (second.tag != X3_TAG_INVALID) state->host->value_release(second);
    return X3_STATUS_ERROR;
  }
  X3Value values = state->host->value_list(runtime);
  if (values.tag == X3_TAG_INVALID ||
      state->host->list_append(runtime, values, first) != X3_STATUS_OK ||
      state->host->list_append(runtime, values, second) != X3_STATUS_OK) {
    if (values.tag != X3_TAG_INVALID) state->host->value_release(values);
    state->host->value_release(first);
    state->host->value_release(second);
    return X3_STATUS_ERROR;
  }
  state->host->value_release(first);
  state->host->value_release(second);
  X3Value tuple_class = x3_value_invalid();
  const bool ready = state->host->builtin_value(state->host, "tuple", &tuple_class) ==
      X3_STATUS_OK;
  const X3Status status = ready ?
      state->host->call(runtime, tuple_class, &values, 1, result) :
      X3_STATUS_ERROR;
  if (tuple_class.tag != X3_TAG_INVALID) state->host->value_release(tuple_class);
  state->host->value_release(values);
  return status;
}

X3Status parse_spki_for_data(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return raise(state, call, "TypeError",
                              "parse_spki_for_data() takes 1 argument");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<long>::max())) {
    state->host->buffer_release(buffer);
    return raise(state, call, "ValueError", "invalid SubjectPublicKeyInfo");
  }
  const auto* begin = static_cast<const unsigned char*>(info.data);
  const auto* cursor = begin;
  std::unique_ptr<X509_PUBKEY, decltype(&X509_PUBKEY_free)> public_key(
      d2i_X509_PUBKEY(nullptr, &cursor, static_cast<long>(info.size)),
      X509_PUBKEY_free);
  if (!public_key || cursor != begin + info.size) {
    state->host->buffer_release(buffer);
    return raise(state, call, "ValueError", "invalid SubjectPublicKeyInfo");
  }
  // OpenSSL validates the SPKI structure. Inspect the DER BIT STRING header
  // separately because X509_PUBKEY_get0_param does not expose its unused-bit
  // count, while cryptography rejects subjectPublicKey with any padding bits.
  cursor = begin;
  long outer_length = 0;
  int tag = 0;
  int klass = 0;
  const int outer_flags = ASN1_get_object(&cursor, &outer_length, &tag, &klass,
                                           static_cast<long>(info.size));
  if ((outer_flags & 0x80) != 0 || tag != V_ASN1_SEQUENCE ||
      klass != V_ASN1_UNIVERSAL || outer_length < 0 ||
      outer_length > (begin + info.size) - cursor ||
      cursor + outer_length != begin + info.size) {
    state->host->buffer_release(buffer);
    return raise(state, call, "ValueError", "invalid SubjectPublicKeyInfo");
  }
  const auto* outer_end = cursor + outer_length;
  long algorithm_length = 0;
  const int algorithm_flags = ASN1_get_object(
      &cursor, &algorithm_length, &tag, &klass,
      static_cast<long>(outer_end - cursor));
  if ((algorithm_flags & 0x80) != 0 || tag != V_ASN1_SEQUENCE ||
      klass != V_ASN1_UNIVERSAL || algorithm_length < 0 ||
      algorithm_length > outer_end - cursor) {
    state->host->buffer_release(buffer);
    return raise(state, call, "ValueError", "invalid SubjectPublicKeyInfo");
  }
  cursor += algorithm_length;
  long bit_string_length = 0;
  const int bit_string_flags = ASN1_get_object(
      &cursor, &bit_string_length, &tag, &klass,
      static_cast<long>(outer_end - cursor));
  if ((bit_string_flags & 0x80) != 0 || tag != V_ASN1_BIT_STRING ||
      klass != V_ASN1_UNIVERSAL || bit_string_length < 1 ||
      cursor + bit_string_length != outer_end) {
    state->host->buffer_release(buffer);
    return raise(state, call, "ValueError", "invalid SubjectPublicKeyInfo");
  }
  if (cursor[0] != 0) {
    state->host->buffer_release(buffer);
    return raise(state, call, "ValueError", "Invalid public key encoding");
  }
  *result = state->host->value_bytes(runtime, cursor + 1,
                                      static_cast<uint64_t>(bit_string_length - 1));
  state->host->buffer_release(buffer);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status register_modules(X3PackageHost* host, PackageState* state) {
  X3Module* root = nullptr;
  if (host->add_module(host, kRootName, &root) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_oid_class(root, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_reasons_module(root, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_openssl_hashes_module(root, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_legacy_openssl_module(root, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (register_x509_module(root, state) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Module* asn1 = nullptr;
  if (host->add_module(host, kAsn1Name, &asn1) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value asn1_value = x3_value_invalid();
  if (host->module_get_value(asn1, &asn1_value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status attach_status = host->module_add_value(root, "asn1", asn1_value);
  host->value_release(asn1_value);
  if (attach_status != X3_STATUS_OK) return attach_status;
  const X3NativeFunctionDef methods[] = {
      {sizeof(X3NativeFunctionDef), "encode_dss_signature",
       encode_dss_signature, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "decode_dss_signature",
       decode_dss_signature, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "parse_spki_for_data",
       parse_spki_for_data, state, 1, 1, 0, nullptr},
  };
  for (const auto& method : methods)
    if (host->module_add_function(asn1, &method) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version =
    X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (host == nullptr || host->abi_version != X3_ABI_VERSION)
    return X3_STATUS_ERROR;
  auto* state = new PackageState{host};
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  host->package_set_metadata(host, "package", kRootName);
  host->package_set_metadata(host, "version", "46.0.7+xlang3");
  return register_modules(host, state);
}
