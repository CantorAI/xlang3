/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/sslerr.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

constexpr char kPackageVersion[] = "46.0.7";
constexpr char kVersionPointerType[] =
    "cryptography.hazmat.bindings._rust._openssl._VersionCString";
constexpr char kModuleName[] = "cryptography.hazmat.bindings._rust._openssl";
constexpr char kFfiName[] = "cryptography.hazmat.bindings._rust._openssl.ffi";
constexpr char kLibName[] = "cryptography.hazmat.bindings._rust._openssl.lib";

X3Status attach_child(X3PackageHost* host, X3Module* parent,
                      X3Module* child, const char* name) {
  X3Value value = x3_value_invalid();
  if (host->module_get_value(child, &value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status status = host->module_add_value(parent, name, value);
  host->value_release(value);
  return status;
}

X3Status ffi_string(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc < 1 || argc > 2)
    return state->host->raise_class_error(call, "TypeError",
                                          "string() takes 1 or 2 arguments");
  auto* data = static_cast<const char*>(state->host->instance_get_native_data(
      args[0], kVersionPointerType));
  if (data == nullptr)
    return state->host->raise_class_error(call, "TypeError",
                                          "string() requires a native char pointer");
  size_t size = std::strlen(data);
  if (argc == 2) {
    uint64_t limit = 0;
    if (args[1].tag == X3_TAG_INT64 && args[1].as.i64 >= 0)
      limit = static_cast<uint64_t>(args[1].as.i64);
    else if (args[1].tag == X3_TAG_UINT64)
      limit = args[1].as.u64;
    else
      return state->host->raise_class_error(call, "TypeError",
                                            "string() maxlen must be a non-negative integer");
    size = std::min(size, static_cast<size_t>(limit));
  }
  *result = state->host->value_bytes(runtime, data, size);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status openssl_version_num(X3CallContext* call, X3Runtime*, void* user_data,
                             const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0)
    return state->host->raise_class_error(call, "TypeError",
                                          "OpenSSL_version_num() takes no arguments");
  *result = x3_value_uint64(OpenSSL_version_num());
  return X3_STATUS_OK;
}

bool add_feature(X3PackageHost* host, X3Module* module,
                 const char* name, bool enabled) {
  return host->module_add_value(module, name, x3_value_int64(enabled ? 1 : 0)) ==
         X3_STATUS_OK;
}

}  // namespace

X3Status register_legacy_openssl_module(X3Module* root,
                                        CryptographyNativeState* state) {
  auto* host = state->host;
  X3Module* binding = nullptr;
  X3Module* ffi = nullptr;
  X3Module* lib = nullptr;
  if (host->add_module(host, kModuleName, &binding) != X3_STATUS_OK ||
      attach_child(host, root, binding, "_openssl") != X3_STATUS_OK ||
      host->add_module(host, kFfiName, &ffi) != X3_STATUS_OK ||
      attach_child(host, binding, ffi, "ffi") != X3_STATUS_OK ||
      host->add_module(host, kLibName, &lib) != X3_STATUS_OK ||
      attach_child(host, binding, lib, "lib") != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  const X3NativeFunctionDef string_function = {
      sizeof(X3NativeFunctionDef), "string", ffi_string, state, 1, 2, 0, nullptr};
  const X3NativeFunctionDef version_function = {
      sizeof(X3NativeFunctionDef), "OpenSSL_version_num", openssl_version_num,
      state, 0, 0, 0, nullptr};
  if (host->module_add_function(ffi, &string_function) != X3_STATUS_OK ||
      host->module_add_function(lib, &version_function) != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  X3Value pointer_class = x3_value_invalid();
  if (host->module_add_class(lib, "_VersionCString", nullptr, 0,
                             &pointer_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value pointer = host->value_instance(host->runtime, pointer_class);
  host->value_release(pointer_class);
  if (pointer.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (host->instance_set_native_data(pointer, kVersionPointerType,
                                     const_cast<char*>(kPackageVersion), nullptr) !=
      X3_STATUS_OK) {
    host->value_release(pointer);
    return X3_STATUS_ERROR;
  }
  const X3Status version_status =
      host->module_add_value(lib, "CRYPTOGRAPHY_PACKAGE_VERSION", pointer);
  host->value_release(pointer);
  if (version_status != X3_STATUS_OK) return version_status;

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
  constexpr bool kTls13 = true;
#else
  constexpr bool kTls13 = false;
#endif
#if defined(OPENSSL_NO_PSK)
  constexpr bool kPsk = false;
#else
  constexpr bool kPsk = true;
#endif
#if defined(OPENSSL_NO_ENGINE)
  constexpr bool kEngine = false;
#else
  constexpr bool kEngine = true;
#endif
#if defined(OPENSSL_NO_SRTP)
  constexpr bool kSrtp = false;
#else
  constexpr bool kSrtp = true;
#endif
#if defined(SSL_ST_BEFORE)
  constexpr bool kSslSt = true;
#else
  constexpr bool kSslSt = false;
#endif
#if defined(SSL_OP_NO_RENEGOTIATION)
  constexpr bool kNoRenegotiation = true;
#else
  constexpr bool kNoRenegotiation = false;
#endif
#if defined(SSL_OP_COOKIE_EXCHANGE)
  constexpr bool kSslCookie = true;
#else
  constexpr bool kSslCookie = false;
#endif
#if defined(BN_prime_checks_for_size)
  constexpr bool kPrimeChecks = true;
#else
  constexpr bool kPrimeChecks = false;
#endif
#if defined(SSL_R_UNEXPECTED_EOF_WHILE_READING)
  constexpr bool kUnexpectedEof = true;
#else
  constexpr bool kUnexpectedEof = false;
#endif
#if defined(SSL_OP_IGNORE_UNEXPECTED_EOF)
  constexpr bool kIgnoreUnexpectedEof = true;
#else
  constexpr bool kIgnoreUnexpectedEof = false;
#endif
#if defined(SSL_get_extms_support)
  constexpr bool kExtms = true;
#else
  constexpr bool kExtms = false;
#endif
#if defined(EVP_PKEY_DHX)
  constexpr bool kDhx = true;
#else
  constexpr bool kDhx = false;
#endif

  const struct {
    const char* name;
    bool available;
  } features[] = {
      {"Cryptography_HAS_SET_CERT_CB", true},
      {"Cryptography_HAS_SSL_ST", kSslSt},
      {"Cryptography_HAS_TLS_ST", true},
      {"Cryptography_HAS_SIGALGS", true},
      {"Cryptography_HAS_PSK", kPsk},
      {"Cryptography_HAS_PSK_TLSv1_3", kPsk && kTls13},
      {"Cryptography_HAS_CUSTOM_EXT", true},
      {"Cryptography_HAS_TLSv1_3_FUNCTIONS", kTls13},
      {"Cryptography_HAS_TLSv1_3_HS_FUNCTIONS", kTls13},
      {"Cryptography_HAS_SSL_VERIFY_CLIENT_POST_HANDSHAKE", kTls13},
      {"Cryptography_HAS_ENGINE", kEngine},
      {"Cryptography_HAS_VERIFIED_CHAIN", true},
      {"Cryptography_HAS_SRTP", kSrtp},
      {"Cryptography_HAS_OP_NO_RENEGOTIATION", kNoRenegotiation},
      {"Cryptography_HAS_DTLS_GET_DATA_MTU", true},
      {"Cryptography_HAS_SSL_COOKIE", kSslCookie},
      {"Cryptography_HAS_PRIME_CHECKS", kPrimeChecks},
      {"Cryptography_HAS_UNEXPECTED_EOF_WHILE_READING", kUnexpectedEof},
      {"Cryptography_HAS_SSL_OP_IGNORE_UNEXPECTED_EOF", kIgnoreUnexpectedEof},
      {"Cryptography_HAS_GET_EXTMS_SUPPORT", kExtms},
      {"Cryptography_HAS_SSL_GET0_GROUP_NAME", true},
      {"Cryptography_HAS_EVP_PKEY_DHX", kDhx},
  };
  for (const auto& feature : features)
    if (!add_feature(host, lib, feature.name, feature.available))
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
