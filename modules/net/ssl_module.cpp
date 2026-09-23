/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/

#include "xlang3/xlang3.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/objects.h>
#include <openssl/opensslv.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

constexpr const char* kMemoryBIOType = "_ssl.MemoryBIO";
constexpr const char* kSSLContextType = "_ssl._SSLContext";
constexpr const char* kSSLSocketType = "_ssl._SSLSocket";
constexpr const char* kSSLSessionType = "_ssl.SSLSession";
constexpr const char* kCertificateType = "_ssl.Certificate";
constexpr const char* kDefaultCiphers =
    "@SECLEVEL=2:ECDH+AESGCM:ECDH+CHACHA20:ECDH+AES:DHE+AES:"
    "!aNULL:!eNULL:!aDSS:!SHA1:!AESCCM";

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value memory_bio_class = x3_value_invalid();
  X3Value ssl_context_class = x3_value_invalid();
  X3Value ssl_session_class = x3_value_invalid();
  X3Value ssl_socket_class = x3_value_invalid();
  X3Value certificate_class = x3_value_invalid();
  X3Value ssl_error = x3_value_invalid();
  X3Value ssl_zero_return_error = x3_value_invalid();
  X3Value ssl_want_read_error = x3_value_invalid();
  X3Value ssl_want_write_error = x3_value_invalid();
  X3Value ssl_syscall_error = x3_value_invalid();
  X3Value ssl_eof_error = x3_value_invalid();
  X3Value ssl_cert_verification_error = x3_value_invalid();
};

struct MemoryBIOState {
  BIO* bio = nullptr;
  bool write_eof = false;
};

struct SSLContextState {
  PackageState* package = nullptr;
  SSL_CTX* context = nullptr;
  int protocol = 0;
  bool check_hostname = false;
  int verify_mode = 0;
  unsigned int host_flags = 0;
  bool post_handshake_auth = false;
  std::string password;
  X3Value sni_callback = x3_value_none();
  X3Value message_callback = x3_value_none();
  X3Value psk_client_callback = x3_value_none();
  X3Value psk_server_callback = x3_value_none();
  X3Value keylog_filename = x3_value_none();
  std::FILE* keylog_file = nullptr;
  std::mutex keylog_mutex;
};

struct SSLSocketState {
  PackageState* package = nullptr;
  SSL* ssl = nullptr;
  X3Value context = x3_value_invalid();
  X3Value owner = x3_value_invalid();
  bool server_side = false;
  std::string server_hostname;
  X3CallContext* active_call_context = nullptr;
  X3Runtime* active_runtime = nullptr;
  bool callback_failed = false;
};

struct SSLSessionState {
  PackageState* package = nullptr;
  SSL_SESSION* session = nullptr;
  X3Value context = x3_value_invalid();
};

struct CertificateState {
  X509* certificate = nullptr;
};

void cleanup_memory_bio(void* pointer) {
  auto* state = static_cast<MemoryBIOState*>(pointer);
  if (state != nullptr) BIO_free(state->bio);
  delete state;
}

void cleanup_ssl_context(void* pointer) {
  auto* state = static_cast<SSLContextState*>(pointer);
  if (state != nullptr) {
    {
      std::lock_guard<std::mutex> lock(state->keylog_mutex);
      if (state->keylog_file != nullptr) {
        std::fclose(state->keylog_file);
        state->keylog_file = nullptr;
      }
    }
    SSL_CTX_free(state->context);
    if (state->package != nullptr)
      state->package->host->value_release(state->sni_callback);
    if (state->package != nullptr)
      state->package->host->value_release(state->message_callback);
    if (state->package != nullptr)
      state->package->host->value_release(state->psk_client_callback);
    if (state->package != nullptr)
      state->package->host->value_release(state->psk_server_callback);
    if (state->package != nullptr)
      state->package->host->value_release(state->keylog_filename);
  }
  delete state;
}

void cleanup_ssl_socket(void* pointer) {
  auto* state = static_cast<SSLSocketState*>(pointer);
  if (state == nullptr) return;
  SSL_free(state->ssl);
  state->package->host->value_release(state->context);
  state->package->host->value_release(state->owner);
  delete state;
}

void cleanup_ssl_session(void* pointer) {
  auto* session = static_cast<SSLSessionState*>(pointer);
  if (session == nullptr) return;
  SSL_SESSION_free(session->session);
  if (session->package != nullptr)
    session->package->host->value_release(session->context);
  delete session;
}

void cleanup_certificate(void* pointer) {
  auto* certificate = static_cast<CertificateState*>(pointer);
  if (certificate != nullptr) X509_free(certificate->certificate);
  delete certificate;
}

void cleanup_package(void* pointer) {
  auto* state = static_cast<PackageState*>(pointer);
  if (state == nullptr) return;
  for (X3Value value : {state->memory_bio_class, state->ssl_context_class,
                        state->ssl_session_class, state->ssl_socket_class,
                        state->certificate_class, state->ssl_error,
                        state->ssl_zero_return_error, state->ssl_want_read_error,
                        state->ssl_want_write_error, state->ssl_syscall_error,
                        state->ssl_eof_error, state->ssl_cert_verification_error}) {
    state->host->value_release(value);
  }
  delete state;
}

bool argc_is(PackageState* state, X3CallContext* context, uint32_t argc,
             uint32_t minimum, uint32_t maximum, const char* name) {
  if (argc >= minimum && argc <= maximum) return true;
  const std::string message = std::string(name) + " received an invalid number of arguments";
  state->host->raise_class_error(context, "TypeError", message.c_str());
  return false;
}

bool integer_value(PackageState* state, X3Runtime* runtime, X3Value value, int64_t* output) {
  if (value.tag == X3_TAG_INT64) {
    *output = value.as.i64;
    return true;
  }
  if (value.tag == X3_TAG_UINT64 && value.as.u64 <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    *output = static_cast<int64_t>(value.as.u64);
    return true;
  }
  X3Value enum_value = x3_value_invalid();
  if (state->host->get_attr(runtime, value, "value", &enum_value) == X3_STATUS_OK) {
    const bool ok = enum_value.tag == X3_TAG_INT64;
    if (ok) *output = enum_value.as.i64;
    state->host->value_release(enum_value);
    return ok;
  }
  return false;
}

X3Status raise_openssl(PackageState* state, X3CallContext* context, const char* operation) {
  const unsigned long code = ERR_get_error();
  std::string message(operation);
  if (code != 0) {
    char buffer[256]{};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    message += ": ";
    message += buffer;
  }
  state->host->raise_error(context, state->ssl_error, message.c_str());
  return X3_STATUS_ERROR;
}

MemoryBIOState* memory_bio(PackageState* state, X3CallContext* context, X3Value value) {
  auto* bio = static_cast<MemoryBIOState*>(
      state->host->instance_get_native_data(value, kMemoryBIOType));
  if (bio == nullptr) state->host->raise_class_error(context, "TypeError", "invalid MemoryBIO object");
  return bio;
}

SSLContextState* ssl_context(PackageState* state, X3CallContext* context, X3Value value) {
  auto* ssl = static_cast<SSLContextState*>(
      state->host->instance_get_native_data(value, kSSLContextType));
  if (ssl == nullptr) state->host->raise_class_error(context, "TypeError", "invalid SSLContext object");
  return ssl;
}

SSLSocketState* ssl_socket(PackageState* state, X3CallContext* context, X3Value value) {
  auto* ssl = static_cast<SSLSocketState*>(
      state->host->instance_get_native_data(value, kSSLSocketType));
  if (ssl == nullptr) state->host->raise_class_error(context, "TypeError", "invalid SSL object");
  return ssl;
}

SSLSessionState* ssl_session(PackageState* state, X3CallContext* context,
                             X3Value value) {
  auto* session = static_cast<SSLSessionState*>(
      state->host->instance_get_native_data(value, kSSLSessionType));
  if (session == nullptr)
    state->host->raise_class_error(context, "TypeError", "session must be an SSLSession");
  return session;
}

CertificateState* ssl_certificate(PackageState* state, X3CallContext* context,
                                  X3Value value) {
  auto* certificate = static_cast<CertificateState*>(
      state->host->instance_get_native_data(value, kCertificateType));
  if (certificate == nullptr)
    state->host->raise_class_error(context, "TypeError",
                                   "invalid Certificate object");
  return certificate;
}

bool same_object(X3Value left, X3Value right) {
  return left.tag == X3_TAG_OBJECT && right.tag == X3_TAG_OBJECT &&
      left.as.obj == right.as.obj;
}

X3Status apply_requested_session(PackageState* state, X3CallContext* context,
                                 X3Value context_value, X3Value session_value,
                                 bool server_side, SSL* ssl) {
  if (session_value.tag == X3_TAG_NONE) return X3_STATUS_OK;
  if (server_side) {
    state->host->raise_class_error(context, "ValueError",
                                   "session can only be specified in client mode");
    return X3_STATUS_ERROR;
  }
  auto* session = ssl_session(state, context, session_value);
  if (session == nullptr) return X3_STATUS_ERROR;
  if (!same_object(session->context, context_value)) {
    state->host->raise_class_error(context, "ValueError",
                                   "Session refers to a different SSLContext");
    return X3_STATUS_ERROR;
  }
  if (SSL_set_session(ssl, session->session) != 1)
    return raise_openssl(state, context, "cannot set SSL session");
  return X3_STATUS_OK;
}

const X3Value* keyword_value(const X3KeywordArg* kwargs, uint32_t kwargc,
                             const char* name) {
  for (uint32_t index = 0; index < kwargc; ++index)
    if (kwargs[index].name != nullptr && std::string(kwargs[index].name) == name)
      return &kwargs[index].value;
  return nullptr;
}

bool truth_value(X3Value value, bool* output) {
  if (value.tag == X3_TAG_BOOL) {
    *output = value.as.b != 0;
    return true;
  }
  if (value.tag == X3_TAG_INT64) {
    *output = value.as.i64 != 0;
    return true;
  }
  return false;
}

X3Status raise_ssl_io(PackageState* state, X3CallContext* context,
                      SSL* ssl, int return_code, const char* operation) {
  const int code = SSL_get_error(ssl, return_code);
  X3Value exception = state->ssl_error;
  const char* suffix = "SSL operation failed";
  if (code == SSL_ERROR_WANT_READ) {
    exception = state->ssl_want_read_error;
    suffix = "The operation did not complete (read)";
  } else if (code == SSL_ERROR_WANT_WRITE) {
    exception = state->ssl_want_write_error;
    suffix = "The operation did not complete (write)";
  } else if (code == SSL_ERROR_ZERO_RETURN) {
    exception = state->ssl_zero_return_error;
    suffix = "TLS/SSL connection has been closed";
  } else if (code == SSL_ERROR_SYSCALL) {
    exception = state->ssl_syscall_error;
    suffix = "Some I/O error occurred";
  } else if (code == SSL_ERROR_SSL) {
    const long verify_result = SSL_get_verify_result(ssl);
    if (verify_result != X509_V_OK) {
      exception = state->ssl_cert_verification_error;
      suffix = X509_verify_cert_error_string(verify_result);
    }
  }
  std::string message = std::string(operation) + ": " + suffix;
  const unsigned long openssl_code = ERR_get_error();
  if (openssl_code != 0) {
    char buffer[256]{};
    ERR_error_string_n(openssl_code, buffer, sizeof(buffer));
    message += ": ";
    message += buffer;
  }
  state->host->raise_error(context, exception, message.c_str());
  return X3_STATUS_ERROR;
}

X3Value make_tuple(PackageState* state, X3Runtime* runtime,
                   const std::vector<X3Value>& values) {
  X3Value list = state->host->value_list(runtime);
  for (const X3Value value : values) state->host->list_append(runtime, list, value);
  X3Value tuple_class = x3_value_invalid();
  X3Value result = x3_value_invalid();
  if (state->host->builtin_value(state->host, "tuple", &tuple_class) == X3_STATUS_OK) {
    state->host->call(runtime, tuple_class, &list, 1, &result);
  }
  state->host->value_release(tuple_class);
  state->host->value_release(list);
  return result;
}

bool dict_set_named(PackageState* state, X3Runtime* runtime, X3Value dict,
                    const char* name, X3Value value) {
  X3Value key = state->host->value_string(runtime, name);
  const bool ok = state->host->dict_set_item(runtime, dict, key, value) == X3_STATUS_OK;
  state->host->value_release(key);
  return ok;
}

X3Status ssl_context_get_ciphers(X3CallContext* context, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.get_ciphers()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  X3Value output = state->host->value_list(runtime);
  STACK_OF(SSL_CIPHER)* ciphers = SSL_CTX_get_ciphers(native->context);
  const int count = ciphers == nullptr ? 0 : sk_SSL_CIPHER_num(ciphers);
  for (int index = 0; index < count; ++index) {
    const SSL_CIPHER* cipher = sk_SSL_CIPHER_value(ciphers, index);
    if (cipher == nullptr) continue;
    int alg_bits = 0;
    const int strength_bits = SSL_CIPHER_get_bits(cipher, &alg_bits);
    char description[256]{};
    SSL_CIPHER_description(cipher, description, sizeof(description));
    std::string description_text(description);
    while (!description_text.empty() &&
           (description_text.back() == '\n' || description_text.back() == '\r'))
      description_text.pop_back();
    X3Value item = state->host->value_dict(runtime);
    X3Value id = x3_value_uint64(SSL_CIPHER_get_id(cipher));
    X3Value name = state->host->value_string(runtime, SSL_CIPHER_get_name(cipher));
    X3Value protocol = state->host->value_string(runtime, SSL_CIPHER_get_version(cipher));
    X3Value desc = state->host->value_string_utf8(runtime, description_text.data(), description_text.size());
    const bool ok = dict_set_named(state, runtime, item, "id", id) &&
        dict_set_named(state, runtime, item, "name", name) &&
        dict_set_named(state, runtime, item, "protocol", protocol) &&
        dict_set_named(state, runtime, item, "description", desc) &&
        dict_set_named(state, runtime, item, "strength_bits", x3_value_int64(strength_bits)) &&
        dict_set_named(state, runtime, item, "alg_bits", x3_value_int64(alg_bits)) &&
        dict_set_named(state, runtime, item, "aead", x3_value_bool(SSL_CIPHER_is_aead(cipher))) &&
        state->host->list_append(runtime, output, item) == X3_STATUS_OK;
    state->host->value_release(desc);
    state->host->value_release(protocol);
    state->host->value_release(name);
    state->host->value_release(item);
    if (!ok) {
      state->host->value_release(output);
      return X3_STATUS_ERROR;
    }
  }
  *result = output;
  return X3_STATUS_OK;
}

X3Status ssl_context_cert_store_stats(X3CallContext* context, X3Runtime* runtime,
                                      void* user_data, const X3Value* args,
                                      uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.cert_store_stats()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  int64_t certificates = 0;
  int64_t certificate_authorities = 0;
  int64_t crls = 0;
  STACK_OF(X509_OBJECT)* objects = X509_STORE_get0_objects(SSL_CTX_get_cert_store(native->context));
  const int count = objects == nullptr ? 0 : sk_X509_OBJECT_num(objects);
  for (int index = 0; index < count; ++index) {
    X509_OBJECT* object = sk_X509_OBJECT_value(objects, index);
    const int type = X509_OBJECT_get_type(object);
    if (type == X509_LU_X509) {
      ++certificates;
      X509* certificate = X509_OBJECT_get0_X509(object);
      if (certificate != nullptr && X509_check_ca(certificate) > 0) ++certificate_authorities;
    } else if (type == X509_LU_CRL) {
      ++crls;
    }
  }
  X3Value output = state->host->value_dict(runtime);
  if (!dict_set_named(state, runtime, output, "x509", x3_value_int64(certificates)) ||
      !dict_set_named(state, runtime, output, "crl", x3_value_int64(crls)) ||
      !dict_set_named(state, runtime, output, "x509_ca", x3_value_int64(certificate_authorities))) {
    state->host->value_release(output);
    return X3_STATUS_ERROR;
  }
  *result = output;
  return X3_STATUS_OK;
}

X3Status ssl_context_session_stats(X3CallContext* context, X3Runtime* runtime,
                                   void* user_data, const X3Value* args,
                                   uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.session_stats()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  X3Value output = state->host->value_dict(runtime);
  const struct { const char* name; long value; } stats[] = {
      {"number", SSL_CTX_sess_number(native->context)},
      {"connect", SSL_CTX_sess_connect(native->context)},
      {"connect_good", SSL_CTX_sess_connect_good(native->context)},
      {"connect_renegotiate", SSL_CTX_sess_connect_renegotiate(native->context)},
      {"accept", SSL_CTX_sess_accept(native->context)},
      {"accept_good", SSL_CTX_sess_accept_good(native->context)},
      {"accept_renegotiate", SSL_CTX_sess_accept_renegotiate(native->context)},
      {"hits", SSL_CTX_sess_hits(native->context)},
      {"misses", SSL_CTX_sess_misses(native->context)},
      {"timeouts", SSL_CTX_sess_timeouts(native->context)},
      {"cache_full", SSL_CTX_sess_cache_full(native->context)},
  };
  for (const auto& stat : stats) {
    if (!dict_set_named(state, runtime, output, stat.name, x3_value_int64(stat.value))) {
      state->host->value_release(output);
      return X3_STATUS_ERROR;
    }
  }
  *result = output;
  return X3_STATUS_OK;
}

void add_int(PackageState* state, X3Module* module, const char* name, int64_t value) {
  state->host->module_add_value(module, name, x3_value_int64(value));
}

void add_bool(PackageState* state, X3Module* module, const char* name, bool value) {
  state->host->module_add_value(module, name, x3_value_bool(value));
}

void add_string(PackageState* state, X3Runtime* runtime, X3Module* module,
                const char* name, const char* value) {
  X3Value string = state->host->value_string(runtime, value == nullptr ? "" : value);
  state->host->module_add_value(module, name, string);
  state->host->value_release(string);
}

void add_function(PackageState* state, X3Module* module, const char* name,
                  X3NativeFn callback, X3NativeKeywordFn keyword_callback = nullptr) {
  X3NativeFunctionDef definition{};
  definition.size = sizeof(definition);
  definition.name = name;
  definition.callback = callback;
  definition.keyword_callback = keyword_callback;
  definition.user_data = state;
  state->host->module_add_function(module, &definition);
}

void define_method(X3NativeFunctionDef& definition, const char* name,
                   X3NativeFn callback, PackageState* state,
                   X3NativeKeywordFn keyword_callback = nullptr) {
  definition.size = sizeof(definition);
  definition.name = name;
  definition.callback = callback;
  definition.keyword_callback = keyword_callback;
  definition.user_data = state;
}

X3Status memory_bio_init(X3CallContext* context, X3Runtime*, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "MemoryBIO()")) return X3_STATUS_ERROR;
  auto native = std::make_unique<MemoryBIOState>();
  native->bio = BIO_new(BIO_s_mem());
  if (native->bio == nullptr) return raise_openssl(state, context, "BIO_new failed");
  BIO_set_mem_eof_return(native->bio, -1);
  if (state->host->instance_set_native_data(args[0], kMemoryBIOType, native.get(),
                                             cleanup_memory_bio) != X3_STATUS_OK) {
    state->host->raise_class_error(context, "RuntimeError", "cannot initialize MemoryBIO");
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status memory_bio_write(X3CallContext* context, X3Runtime* runtime, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "MemoryBIO.write()")) return X3_STATUS_ERROR;
  auto* native = memory_bio(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (native->write_eof) {
    state->host->raise_class_error(context, "ValueError", "cannot write() after write_eof()");
    return X3_STATUS_ERROR;
  }
  const void* data = nullptr;
  uint64_t size = 0;
  if (state->host->value_bytes_data(runtime, args[1], &data, &size) != X3_STATUS_OK) {
    state->host->raise_class_error(context, "TypeError", "a bytes-like object is required");
    return X3_STATUS_ERROR;
  }
  if (size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->raise_class_error(context, "OverflowError", "MemoryBIO input is too large");
    return X3_STATUS_ERROR;
  }
  const int written = size == 0 ? 0 : BIO_write(native->bio, data, static_cast<int>(size));
  if (written < 0) return raise_openssl(state, context, "BIO_write failed");
  *result = x3_value_int64(written);
  return X3_STATUS_OK;
}

X3Status memory_bio_read(X3CallContext* context, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 2, "MemoryBIO.read()")) return X3_STATUS_ERROR;
  auto* native = memory_bio(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  int64_t requested = -1;
  if (argc == 2) {
    if (args[1].tag != X3_TAG_INT64) {
      state->host->raise_class_error(context, "TypeError", "MemoryBIO.read() size must be an integer");
      return X3_STATUS_ERROR;
    }
    requested = args[1].as.i64;
  }
  const size_t pending = static_cast<size_t>(BIO_ctrl_pending(native->bio));
  size_t count = requested < 0 ? pending : std::min<size_t>(pending, static_cast<size_t>(requested));
  if (requested < -1) count = pending;
  std::string output(count, '\0');
  const int read = count == 0 ? 0 : BIO_read(native->bio, output.data(), static_cast<int>(count));
  if (read < 0) return raise_openssl(state, context, "BIO_read failed");
  output.resize(static_cast<size_t>(read));
  *result = state->host->value_bytes(runtime, output.data(), output.size());
  return X3_STATUS_OK;
}

X3Status memory_bio_write_eof(X3CallContext* context, X3Runtime*, void* user_data,
                              const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "MemoryBIO.write_eof()")) return X3_STATUS_ERROR;
  auto* native = memory_bio(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  native->write_eof = true;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status memory_bio_pending(X3CallContext* context, X3Runtime*, void* user_data,
                            const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "MemoryBIO.pending")) return X3_STATUS_ERROR;
  auto* native = memory_bio(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(static_cast<int64_t>(BIO_ctrl_pending(native->bio)));
  return X3_STATUS_OK;
}

X3Status memory_bio_eof(X3CallContext* context, X3Runtime*, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "MemoryBIO.eof")) return X3_STATUS_ERROR;
  auto* native = memory_bio(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(native->write_eof && BIO_ctrl_pending(native->bio) == 0);
  return X3_STATUS_OK;
}

int ssl_servername_callback(SSL* ssl, int* alert, void* user_data) {
  auto* context_state = static_cast<SSLContextState*>(user_data);
  auto* socket_state = static_cast<SSLSocketState*>(SSL_get_app_data(ssl));
  if (context_state == nullptr || socket_state == nullptr ||
      socket_state->active_call_context == nullptr ||
      socket_state->active_runtime == nullptr ||
      context_state->sni_callback.tag == X3_TAG_NONE)
    return SSL_TLSEXT_ERR_NOACK;
  auto* package = context_state->package;
  const char* servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  X3Value hostname = servername == nullptr
      ? x3_value_none()
      : package->host->value_string(socket_state->active_runtime, servername);
  X3Value callback_args[] = {
      socket_state->owner, hostname, socket_state->context};
  X3Value callback_result = x3_value_invalid();
  const auto status = package->host->call(
      socket_state->active_runtime, context_state->sni_callback, callback_args,
      3, &callback_result);
  package->host->value_release(hostname);
  if (status != X3_STATUS_OK) {
    socket_state->callback_failed = true;
    if (callback_result.tag != X3_TAG_INVALID)
      package->host->value_release(callback_result);
    if (alert != nullptr) *alert = SSL_AD_INTERNAL_ERROR;
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
  if (callback_result.tag == X3_TAG_NONE) {
    package->host->value_release(callback_result);
    return SSL_TLSEXT_ERR_OK;
  }
  int64_t requested_alert = 0;
  if (!integer_value(package, socket_state->active_runtime, callback_result,
                     &requested_alert) ||
      requested_alert < 0 || requested_alert > 255) {
    package->host->value_release(callback_result);
    package->host->raise_class_error(
        socket_state->active_call_context, "TypeError",
        "SNI callback must return None or a TLS alert description");
    socket_state->callback_failed = true;
    if (alert != nullptr) *alert = SSL_AD_INTERNAL_ERROR;
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
  package->host->value_release(callback_result);
  if (alert != nullptr) *alert = static_cast<int>(requested_alert);
  return SSL_TLSEXT_ERR_ALERT_FATAL;
}

void ssl_message_callback(int write_direction, int version, int content_type,
                          const void* buffer, size_t size, SSL* ssl,
                          void* user_data) {
  auto* context_state = static_cast<SSLContextState*>(user_data);
  auto* socket_state = static_cast<SSLSocketState*>(SSL_get_app_data(ssl));
  if (context_state == nullptr || socket_state == nullptr ||
      socket_state->active_call_context == nullptr ||
      socket_state->active_runtime == nullptr ||
      socket_state->callback_failed ||
      context_state->message_callback.tag == X3_TAG_NONE)
    return;
  auto* package = context_state->package;
  const int message_type = size == 0
      ? 0
      : static_cast<int>(*static_cast<const unsigned char*>(buffer));
  X3Value direction = package->host->value_string(
      socket_state->active_runtime, write_direction ? "write" : "read");
  X3Value data = package->host->value_bytes(
      socket_state->active_runtime, buffer, static_cast<uint64_t>(size));
  X3Value callback_args[] = {
      socket_state->owner,
      direction,
      x3_value_int64(version),
      x3_value_int64(content_type),
      x3_value_int64(message_type),
      data,
  };
  X3Value callback_result = x3_value_invalid();
  const auto status = package->host->call(
      socket_state->active_runtime, context_state->message_callback,
      callback_args, 6, &callback_result);
  package->host->value_release(direction);
  package->host->value_release(data);
  if (callback_result.tag != X3_TAG_INVALID)
    package->host->value_release(callback_result);
  if (status != X3_STATUS_OK) socket_state->callback_failed = true;
}

void ssl_keylog_callback(const SSL* ssl, const char* line) {
  if (ssl == nullptr || line == nullptr) return;
  auto* ssl_context = SSL_get_SSL_CTX(ssl);
  auto* state = ssl_context == nullptr
      ? nullptr
      : static_cast<SSLContextState*>(SSL_CTX_get_app_data(ssl_context));
  if (state == nullptr) return;
  std::lock_guard<std::mutex> lock(state->keylog_mutex);
  if (state->keylog_file == nullptr) return;
  std::fputs(line, state->keylog_file);
  std::fputc('\n', state->keylog_file);
  std::fflush(state->keylog_file);
}

unsigned int ssl_psk_client_callback(
    SSL* ssl, const char* hint, char* identity, unsigned int max_identity_len,
    unsigned char* psk, unsigned int max_psk_len) {
  auto* socket_state = static_cast<SSLSocketState*>(SSL_get_app_data(ssl));
  auto* ssl_context = SSL_get_SSL_CTX(ssl);
  auto* context_state = ssl_context == nullptr
      ? nullptr
      : static_cast<SSLContextState*>(SSL_CTX_get_app_data(ssl_context));
  if (socket_state == nullptr || context_state == nullptr ||
      socket_state->active_call_context == nullptr ||
      socket_state->active_runtime == nullptr ||
      context_state->psk_client_callback.tag == X3_TAG_NONE)
    return 0;
  auto* package = context_state->package;
  X3Value hint_value = hint == nullptr
      ? x3_value_none()
      : package->host->value_string(socket_state->active_runtime, hint);
  X3Value callback_result = x3_value_invalid();
  const auto status = package->host->call(
      socket_state->active_runtime, context_state->psk_client_callback,
      &hint_value, 1, &callback_result);
  package->host->value_release(hint_value);
  if (status != X3_STATUS_OK) {
    package->host->value_release(callback_result);
    socket_state->callback_failed = true;
    return 0;
  }
  X3Value identity_value = x3_value_invalid();
  X3Value psk_value = x3_value_invalid();
  const bool has_items =
      package->host->get_item(socket_state->active_runtime, callback_result,
                              x3_value_int64(0), &identity_value) == X3_STATUS_OK &&
      package->host->get_item(socket_state->active_runtime, callback_result,
                              x3_value_int64(1), &psk_value) == X3_STATUS_OK;
  const char* identity_data = nullptr;
  uint64_t identity_size = 0;
  const void* psk_data = nullptr;
  uint64_t psk_size = 0;
  const bool valid = has_items &&
      package->host->value_string_data(
          socket_state->active_runtime, identity_value, &identity_data,
          &identity_size) == X3_STATUS_OK &&
      package->host->value_bytes_data(
          socket_state->active_runtime, psk_value, &psk_data, &psk_size) ==
          X3_STATUS_OK &&
      identity_size + 1 <= max_identity_len && psk_size <= max_psk_len;
  if (!valid) {
    package->host->clear_exception(socket_state->active_call_context);
    package->host->raise_class_error(
        socket_state->active_call_context, "ValueError",
        "PSK client callback must return a valid (identity, key) pair");
    socket_state->callback_failed = true;
  } else {
    std::memcpy(identity, identity_data, static_cast<size_t>(identity_size));
    identity[identity_size] = '\0';
    std::memcpy(psk, psk_data, static_cast<size_t>(psk_size));
  }
  package->host->value_release(identity_value);
  package->host->value_release(psk_value);
  package->host->value_release(callback_result);
  return valid ? static_cast<unsigned int>(psk_size) : 0;
}

unsigned int ssl_psk_server_callback(
    SSL* ssl, const char* identity, unsigned char* psk,
    unsigned int max_psk_len) {
  auto* socket_state = static_cast<SSLSocketState*>(SSL_get_app_data(ssl));
  auto* ssl_context = SSL_get_SSL_CTX(ssl);
  auto* context_state = ssl_context == nullptr
      ? nullptr
      : static_cast<SSLContextState*>(SSL_CTX_get_app_data(ssl_context));
  if (socket_state == nullptr || context_state == nullptr ||
      socket_state->active_call_context == nullptr ||
      socket_state->active_runtime == nullptr ||
      context_state->psk_server_callback.tag == X3_TAG_NONE)
    return 0;
  auto* package = context_state->package;
  X3Value identity_value = identity == nullptr
      ? x3_value_none()
      : package->host->value_string(socket_state->active_runtime, identity);
  X3Value callback_result = x3_value_invalid();
  const auto status = package->host->call(
      socket_state->active_runtime, context_state->psk_server_callback,
      &identity_value, 1, &callback_result);
  package->host->value_release(identity_value);
  if (status != X3_STATUS_OK) {
    package->host->value_release(callback_result);
    socket_state->callback_failed = true;
    return 0;
  }
  const void* psk_data = nullptr;
  uint64_t psk_size = 0;
  const bool valid = package->host->value_bytes_data(
                         socket_state->active_runtime, callback_result,
                         &psk_data, &psk_size) == X3_STATUS_OK &&
      psk_size <= max_psk_len;
  if (!valid) {
    package->host->clear_exception(socket_state->active_call_context);
    package->host->raise_class_error(
        socket_state->active_call_context, "ValueError",
        "PSK server callback must return a valid key");
    socket_state->callback_failed = true;
  } else {
    std::memcpy(psk, psk_data, static_cast<size_t>(psk_size));
  }
  package->host->value_release(callback_result);
  return valid ? static_cast<unsigned int>(psk_size) : 0;
}

X3Status ssl_context_new(X3CallContext* context, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  int64_t protocol_value = 0;
  if (!argc_is(state, context, argc, 2, 2, "_SSLContext.__new__()") ||
      !integer_value(state, runtime, args[1], &protocol_value)) {
    if (argc == 2) state->host->raise_class_error(context, "TypeError", "protocol must be an integer");
    return X3_STATUS_ERROR;
  }
  const int protocol = static_cast<int>(protocol_value);
  const SSL_METHOD* method = nullptr;
  if (protocol == 16) method = TLS_client_method();
  else if (protocol == 17) method = TLS_server_method();
  else if (protocol == 2 || protocol == 3 || protocol == 4 || protocol == 5) method = TLS_method();
  else {
    state->host->raise_class_error(context, "ValueError", "invalid or unsupported protocol version");
    return X3_STATUS_ERROR;
  }
  auto native = std::make_unique<SSLContextState>();
  native->package = state;
  native->context = SSL_CTX_new(method);
  native->protocol = protocol;
  native->check_hostname = protocol == 16;
  native->verify_mode = protocol == 16 ? 2 : 0;
  if (native->context == nullptr) return raise_openssl(state, context, "SSL_CTX_new failed");
  SSL_CTX_set_app_data(native->context, native.get());
  if (protocol == 3) SSL_CTX_set_min_proto_version(native->context, TLS1_VERSION), SSL_CTX_set_max_proto_version(native->context, TLS1_VERSION);
  if (protocol == 4) SSL_CTX_set_min_proto_version(native->context, TLS1_1_VERSION), SSL_CTX_set_max_proto_version(native->context, TLS1_1_VERSION);
  if (protocol == 5) SSL_CTX_set_min_proto_version(native->context, TLS1_2_VERSION), SSL_CTX_set_max_proto_version(native->context, TLS1_2_VERSION);
  SSL_CTX_set_verify(native->context, native->verify_mode == 0 ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
  SSL_CTX_set_options(native->context, SSL_OP_ALL | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION |
                                       SSL_OP_CIPHER_SERVER_PREFERENCE);
  if (SSL_CTX_set_cipher_list(native->context, kDefaultCiphers) != 1) {
    return raise_openssl(state, context, "cannot select default ciphers");
  }
  X3Value instance = state->host->value_instance(runtime, args[0]);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kSSLContextType, native.get(),
                                             cleanup_ssl_context) != X3_STATUS_OK) {
    state->host->value_release(instance);
    state->host->raise_class_error(context, "RuntimeError", "cannot initialize SSLContext");
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status ssl_context_set_ciphers(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.set_ciphers()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  const char* ciphers = state->host->value_to_cstr(runtime, args[1]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (ciphers == nullptr) {
    state->host->raise_class_error(context, "TypeError", "cipher list must be a string");
    return X3_STATUS_ERROR;
  }
  if (SSL_CTX_set_cipher_list(native->context, ciphers) != 1) return raise_openssl(state, context, "No cipher can be selected");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_load_dh_params(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2,
               "SSLContext.load_dh_params()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  const char* path = state->host->value_to_cstr(runtime, args[1]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (path == nullptr) {
    return state->host->raise_class_error(
        context, "TypeError", "dhfile should be a valid filesystem path");
  }
  BIO* bio = BIO_new_file(path, "r");
  if (bio == nullptr) return raise_openssl(state, context, "cannot open DH parameters");
  EVP_PKEY* parameters = PEM_read_bio_Parameters(bio, nullptr);
  BIO_free(bio);
  if (parameters == nullptr)
    return raise_openssl(state, context, "cannot read DH parameters");
  const int status = SSL_CTX_set0_tmp_dh_pkey(native->context, parameters);
  if (status != 1) {
    EVP_PKEY_free(parameters);
    return raise_openssl(state, context, "cannot set DH parameters");
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_set_ecdh_curve(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2,
               "SSLContext.set_ecdh_curve()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  const char* curve = state->host->value_to_cstr(runtime, args[1]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (curve == nullptr) {
    return state->host->raise_class_error(
        context, "TypeError", "curve_name must be a string");
  }
  if (SSL_CTX_set1_groups_list(native->context, curve) != 1)
    return raise_openssl(state, context, "unknown elliptic curve name");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_set_psk_callback(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result, bool client) {
  auto* state = static_cast<PackageState*>(user_data);
  const char* method_name = client
      ? "SSLContext.set_psk_client_callback()"
      : "SSLContext.set_psk_server_callback()";
  if (!argc_is(state, context, argc, 2, 2, method_name))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (args[1].tag != X3_TAG_NONE) {
    X3Value callable_function = x3_value_invalid();
    X3Value callable_result = x3_value_invalid();
    const bool callable =
        state->host->builtin_value(state->host, "callable",
                                   &callable_function) == X3_STATUS_OK &&
        state->host->call(runtime, callable_function, &args[1], 1,
                          &callable_result) == X3_STATUS_OK &&
        callable_result.tag == X3_TAG_BOOL && callable_result.as.b;
    state->host->value_release(callable_function);
    state->host->value_release(callable_result);
    if (!callable) {
      state->host->clear_exception(context);
      return state->host->raise_class_error(
          context, "TypeError", "callback must be callable");
    }
  }
  state->host->value_retain(args[1]);
  X3Value& stored = client
      ? native->psk_client_callback
      : native->psk_server_callback;
  state->host->value_release(stored);
  stored = args[1];
  if (client) {
    SSL_CTX_set_psk_client_callback(
        native->context,
        args[1].tag == X3_TAG_NONE ? nullptr : ssl_psk_client_callback);
  } else {
    SSL_CTX_set_psk_server_callback(
        native->context,
        args[1].tag == X3_TAG_NONE ? nullptr : ssl_psk_server_callback);
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_set_psk_client_callback(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  return ssl_context_set_psk_callback(
      context, runtime, user_data, args, argc, result, true);
}

X3Status ssl_context_set_psk_server_callback(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  return ssl_context_set_psk_callback(
      context, runtime, user_data, args, argc, result, false);
}

X3Status ssl_context_set_default_verify_paths(X3CallContext* context, X3Runtime*, void* user_data,
                                              const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.set_default_verify_paths()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (SSL_CTX_set_default_verify_paths(native->context) != 1) return raise_openssl(state, context, "cannot load default verify paths");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

int password_callback(char* buffer, int size, int, void* user_data) {
  auto* context = static_cast<SSLContextState*>(user_data);
  if (context == nullptr || size <= 0) return 0;
  const size_t count = std::min(context->password.size(), static_cast<size_t>(size - 1));
  std::copy_n(context->password.data(), count, buffer);
  buffer[count] = '\0';
  return static_cast<int>(count);
}

X3Status ssl_context_load_cert_chain_kw(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc,
    const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 4, "SSLContext.load_cert_chain()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  X3Value certfile = argc > 1 ? args[1] : x3_value_none();
  X3Value keyfile = argc > 2 ? args[2] : x3_value_none();
  X3Value password = argc > 3 ? args[3] : x3_value_none();
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string name = kwargs[index].name == nullptr ? "" : kwargs[index].name;
    if (name == "certfile") certfile = kwargs[index].value;
    else if (name == "keyfile") keyfile = kwargs[index].value;
    else if (name == "password") password = kwargs[index].value;
    else {
      state->host->raise_class_error(context, "TypeError", "load_cert_chain() got an unexpected keyword argument");
      return X3_STATUS_ERROR;
    }
  }
  const char* cert_path = state->host->value_to_cstr(runtime, certfile);
  if (cert_path == nullptr) {
    state->host->raise_class_error(context, "TypeError", "certfile should be a valid filesystem path");
    return X3_STATUS_ERROR;
  }
  const char* key_path = cert_path;
  if (keyfile.tag != X3_TAG_NONE) {
    key_path = state->host->value_to_cstr(runtime, keyfile);
    if (key_path == nullptr) {
      state->host->raise_class_error(context, "TypeError", "keyfile should be a valid filesystem path");
      return X3_STATUS_ERROR;
    }
  }
  native->password.clear();
  if (password.tag != X3_TAG_NONE) {
    const char* text = nullptr;
    uint64_t text_size = 0;
    const void* bytes = nullptr;
    uint64_t bytes_size = 0;
    if (state->host->value_string_data(runtime, password, &text, &text_size) == X3_STATUS_OK)
      native->password.assign(text, static_cast<size_t>(text_size));
    else if (state->host->value_bytes_data(runtime, password, &bytes, &bytes_size) == X3_STATUS_OK)
      native->password.assign(static_cast<const char*>(bytes), static_cast<size_t>(bytes_size));
    else {
      state->host->raise_class_error(context, "TypeError", "password should be a string or bytes");
      return X3_STATUS_ERROR;
    }
    SSL_CTX_set_default_passwd_cb(native->context, password_callback);
    SSL_CTX_set_default_passwd_cb_userdata(native->context, native);
  }
  if (SSL_CTX_use_certificate_chain_file(native->context, cert_path) != 1)
    return raise_openssl(state, context, "cannot load certificate chain");
  if (SSL_CTX_use_PrivateKey_file(native->context, key_path, SSL_FILETYPE_PEM) != 1)
    return raise_openssl(state, context, "cannot load private key");
  if (SSL_CTX_check_private_key(native->context) != 1)
    return raise_openssl(state, context, "private key does not match certificate");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_load_cert_chain(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                     const X3Value* args, uint32_t argc, X3Value* result) {
  return ssl_context_load_cert_chain_kw(context, runtime, user_data, args, argc, nullptr, 0, result);
}

bool add_ca_certificate(SSLContextState* context, X509* certificate) {
  if (X509_STORE_add_cert(SSL_CTX_get_cert_store(context->context), certificate) == 1) return true;
  const unsigned long code = ERR_peek_last_error();
  if (ERR_GET_LIB(code) == ERR_LIB_X509 && ERR_GET_REASON(code) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
    ERR_clear_error();
    return true;
  }
  return false;
}

X3Status ssl_context_load_verify_locations_kw(
    X3CallContext* call_context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc,
    const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, call_context, argc, 1, 4, "SSLContext.load_verify_locations()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, call_context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  X3Value cafile = argc > 1 ? args[1] : x3_value_none();
  X3Value capath = argc > 2 ? args[2] : x3_value_none();
  X3Value cadata = argc > 3 ? args[3] : x3_value_none();
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string name = kwargs[index].name == nullptr ? "" : kwargs[index].name;
    if (name == "cafile") cafile = kwargs[index].value;
    else if (name == "capath") capath = kwargs[index].value;
    else if (name == "cadata") cadata = kwargs[index].value;
    else {
      state->host->raise_class_error(call_context, "TypeError", "load_verify_locations() got an unexpected keyword argument");
      return X3_STATUS_ERROR;
    }
  }
  const char* file_path = nullptr;
  const char* dir_path = nullptr;
  if (cafile.tag != X3_TAG_NONE && (file_path = state->host->value_to_cstr(runtime, cafile)) == nullptr) {
    state->host->raise_class_error(call_context, "TypeError", "cafile should be a valid filesystem path");
    return X3_STATUS_ERROR;
  }
  if (capath.tag != X3_TAG_NONE && (dir_path = state->host->value_to_cstr(runtime, capath)) == nullptr) {
    state->host->raise_class_error(call_context, "TypeError", "capath should be a valid filesystem path");
    return X3_STATUS_ERROR;
  }
  if (file_path != nullptr || dir_path != nullptr) {
    if (SSL_CTX_load_verify_locations(native->context, file_path, dir_path) != 1)
      return raise_openssl(state, call_context, "cannot load verify locations");
  }
  if (cadata.tag != X3_TAG_NONE) {
    const char* text = nullptr;
    uint64_t text_size = 0;
    const void* bytes = nullptr;
    uint64_t bytes_size = 0;
    if (state->host->value_string_data(runtime, cadata, &text, &text_size) == X3_STATUS_OK) {
      BIO* bio = BIO_new_mem_buf(text, static_cast<int>(text_size));
      if (bio == nullptr) return raise_openssl(state, call_context, "cannot read CA data");
      size_t count = 0;
      while (X509* certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) {
        const bool added = add_ca_certificate(native, certificate);
        X509_free(certificate);
        if (!added) {
          BIO_free(bio);
          return raise_openssl(state, call_context, "cannot add CA certificate");
        }
        ++count;
      }
      BIO_free(bio);
      ERR_clear_error();
      if (count == 0) {
        state->host->raise_error(call_context, state->ssl_error, "no start line: cadata does not contain a certificate");
        return X3_STATUS_ERROR;
      }
    } else if (state->host->value_bytes_data(runtime, cadata, &bytes, &bytes_size) == X3_STATUS_OK) {
      const auto* cursor = static_cast<const unsigned char*>(bytes);
      X509* certificate = d2i_X509(nullptr, &cursor, static_cast<long>(bytes_size));
      if (certificate == nullptr) return raise_openssl(state, call_context, "not enough data: cadata does not contain a certificate");
      const bool added = add_ca_certificate(native, certificate);
      X509_free(certificate);
      if (!added) return raise_openssl(state, call_context, "cannot add CA certificate");
    } else {
      state->host->raise_class_error(call_context, "TypeError", "cadata should be an ASCII string or a bytes-like object");
      return X3_STATUS_ERROR;
    }
  }
  if (file_path == nullptr && dir_path == nullptr && cadata.tag == X3_TAG_NONE) {
    state->host->raise_class_error(call_context, "TypeError", "cafile, capath and cadata cannot be all omitted");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_load_verify_locations(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                           const X3Value* args, uint32_t argc, X3Value* result) {
  return ssl_context_load_verify_locations_kw(context, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status ssl_context_set_alpn(X3CallContext* context, X3Runtime* runtime, void* user_data,
                              const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext._set_alpn_protocols()")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  const void* data = nullptr;
  uint64_t size = 0;
  if (native == nullptr) return X3_STATUS_ERROR;
  if (state->host->value_bytes_data(runtime, args[1], &data, &size) != X3_STATUS_OK ||
      size > std::numeric_limits<unsigned int>::max()) {
    state->host->raise_class_error(context, "TypeError", "protocols must be a bytes-like object");
    return X3_STATUS_ERROR;
  }
  if (SSL_CTX_set_alpn_protos(native->context, static_cast<const unsigned char*>(data),
                              static_cast<unsigned int>(size)) != 0)
    return raise_openssl(state, context, "cannot set ALPN protocols");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_protocol(X3CallContext* context, X3Runtime*, void* user_data,
                              const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.protocol")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(native->protocol);
  return X3_STATUS_OK;
}

X3Status ssl_context_check_hostname_get(X3CallContext* context, X3Runtime*, void* user_data,
                                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.check_hostname")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(native->check_hostname);
  return X3_STATUS_OK;
}

X3Status ssl_context_check_hostname_set(X3CallContext* context, X3Runtime*, void* user_data,
                                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.check_hostname")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const bool enabled = args[1].tag == X3_TAG_BOOL ? args[1].as.b != 0 :
                       args[1].tag == X3_TAG_INT64 && args[1].as.i64 != 0;
  if (enabled && native->verify_mode == 0) {
    state->host->raise_class_error(context, "ValueError", "check_hostname needs a SSL context with CERT_OPTIONAL or CERT_REQUIRED");
    return X3_STATUS_ERROR;
  }
  native->check_hostname = enabled;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_verify_mode_get(X3CallContext* context, X3Runtime*, void* user_data,
                                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.verify_mode")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(native->verify_mode);
  return X3_STATUS_OK;
}

X3Status ssl_context_verify_mode_set(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  int64_t mode_value = 0;
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.verify_mode") ||
      !integer_value(state, runtime, args[1], &mode_value)) {
    if (argc == 2) state->host->raise_class_error(context, "TypeError", "verify_mode must be CERT_NONE, CERT_OPTIONAL or CERT_REQUIRED");
    return X3_STATUS_ERROR;
  }
  auto* native = ssl_context(state, context, args[0]);
  const int mode = static_cast<int>(mode_value);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (mode < 0 || mode > 2) {
    state->host->raise_class_error(context, "ValueError", "invalid verify_mode");
    return X3_STATUS_ERROR;
  }
  if (mode == 0 && native->check_hostname) {
    state->host->raise_class_error(context, "ValueError", "Cannot set verify_mode to CERT_NONE when check_hostname is enabled");
    return X3_STATUS_ERROR;
  }
  int openssl_mode = mode == 0 ? SSL_VERIFY_NONE : SSL_VERIFY_PEER;
  if (native->protocol == 17 && mode == 2) openssl_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
  SSL_CTX_set_verify(native->context, openssl_mode, nullptr);
  native->verify_mode = mode;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_options_get(X3CallContext* context, X3Runtime*, void* user_data,
                                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.options")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_uint64(SSL_CTX_get_options(native->context));
  return X3_STATUS_OK;
}

X3Status ssl_context_options_set(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.options")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  int64_t option_value = 0;
  uint64_t requested = 0;
  if (args[1].tag == X3_TAG_UINT64) requested = args[1].as.u64;
  else if (integer_value(state, runtime, args[1], &option_value)) requested = static_cast<uint64_t>(option_value);
  else {
    state->host->raise_class_error(context, "TypeError", "options must be an integer");
    return X3_STATUS_ERROR;
  }
  const uint64_t current = SSL_CTX_get_options(native->context);
  SSL_CTX_clear_options(native->context, current & ~requested);
  SSL_CTX_set_options(native->context, requested & ~current);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_verify_flags_get(X3CallContext* context, X3Runtime*, void* user_data,
                                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.verify_flags")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(static_cast<int64_t>(
      X509_VERIFY_PARAM_get_flags(SSL_CTX_get0_param(native->context))));
  return X3_STATUS_OK;
}

X3Status ssl_context_verify_flags_set(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  int64_t flags = 0;
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.verify_flags") ||
      !integer_value(state, runtime, args[1], &flags)) {
    if (argc == 2) state->host->raise_class_error(context, "TypeError", "verify_flags must be an integer");
    return X3_STATUS_ERROR;
  }
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  X509_VERIFY_PARAM* parameters = SSL_CTX_get0_param(native->context);
  const unsigned long current = X509_VERIFY_PARAM_get_flags(parameters);
  X509_VERIFY_PARAM_clear_flags(parameters, current);
  if (X509_VERIFY_PARAM_set_flags(parameters, static_cast<unsigned long>(flags)) != 1)
    return raise_openssl(state, context, "cannot set verify flags");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_host_flags_get(X3CallContext* context, X3Runtime*, void* user_data,
                                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext._host_flags")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(native->host_flags);
  return X3_STATUS_OK;
}

X3Status ssl_context_host_flags_set(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  int64_t flags = 0;
  if (!argc_is(state, context, argc, 2, 2, "SSLContext._host_flags") ||
      !integer_value(state, runtime, args[1], &flags)) {
    if (argc == 2) state->host->raise_class_error(context, "TypeError", "host flags must be an integer");
    return X3_STATUS_ERROR;
  }
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  native->host_flags = static_cast<unsigned int>(flags);
  X509_VERIFY_PARAM_set_hostflags(SSL_CTX_get0_param(native->context), native->host_flags);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_minimum_version_get(X3CallContext* context, X3Runtime*, void* user_data,
                                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.minimum_version")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const int version = SSL_CTX_get_min_proto_version(native->context);
  *result = x3_value_int64(version == 0 ? -2 : version);
  return X3_STATUS_OK;
}

X3Status ssl_context_minimum_version_set(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  int64_t version = 0;
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.minimum_version") ||
      !integer_value(state, runtime, args[1], &version)) {
    if (argc == 2) state->host->raise_class_error(context, "TypeError", "minimum_version must be a TLSVersion");
    return X3_STATUS_ERROR;
  }
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (native->protocol != 2 && native->protocol != 16 && native->protocol != 17) {
    state->host->raise_class_error(context, "ValueError", "The context's protocol doesn't support modification of highest and lowest version");
    return X3_STATUS_ERROR;
  }
  if (version == -2) version = 0;
  if (SSL_CTX_set_min_proto_version(native->context, static_cast<int>(version)) != 1)
    return raise_openssl(state, context, "unsupported minimum TLS version");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_maximum_version_get(X3CallContext* context, X3Runtime*, void* user_data,
                                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.maximum_version")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const int version = SSL_CTX_get_max_proto_version(native->context);
  *result = x3_value_int64(version == 0 ? -1 : version);
  return X3_STATUS_OK;
}

X3Status ssl_context_maximum_version_set(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  int64_t version = 0;
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.maximum_version") ||
      !integer_value(state, runtime, args[1], &version)) {
    if (argc == 2) state->host->raise_class_error(context, "TypeError", "maximum_version must be a TLSVersion");
    return X3_STATUS_ERROR;
  }
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (native->protocol != 2 && native->protocol != 16 && native->protocol != 17) {
    state->host->raise_class_error(context, "ValueError", "The context's protocol doesn't support modification of highest and lowest version");
    return X3_STATUS_ERROR;
  }
  if (version == -1) version = 0;
  if (SSL_CTX_set_max_proto_version(native->context, static_cast<int>(version)) != 1)
    return raise_openssl(state, context, "unsupported maximum TLS version");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_security_level_get(X3CallContext* context, X3Runtime*, void* user_data,
                                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.security_level")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(SSL_CTX_get_security_level(native->context));
  return X3_STATUS_OK;
}

X3Status ssl_context_num_tickets_get(X3CallContext* context, X3Runtime*, void* user_data,
                                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.num_tickets")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(static_cast<int64_t>(SSL_CTX_get_num_tickets(native->context)));
  return X3_STATUS_OK;
}

X3Status ssl_context_num_tickets_set(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  int64_t tickets = 0;
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.num_tickets") ||
      !integer_value(state, runtime, args[1], &tickets) || tickets < 0) {
    if (argc == 2) state->host->raise_class_error(context, "ValueError", "num_tickets must be a non-negative integer");
    return X3_STATUS_ERROR;
  }
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (SSL_CTX_set_num_tickets(native->context, static_cast<size_t>(tickets)) != 1)
    return raise_openssl(state, context, "cannot set number of session tickets");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_post_handshake_auth_get(X3CallContext* context, X3Runtime*, void* user_data,
                                             const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.post_handshake_auth")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(native->post_handshake_auth);
  return X3_STATUS_OK;
}

X3Status ssl_context_post_handshake_auth_set(X3CallContext* context, X3Runtime*, void* user_data,
                                             const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.post_handshake_auth")) return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  bool enabled = false;
  if (native == nullptr) return X3_STATUS_ERROR;
  if (!truth_value(args[1], &enabled)) {
    state->host->raise_class_error(context, "TypeError", "post_handshake_auth must be bool");
    return X3_STATUS_ERROR;
  }
  native->post_handshake_auth = enabled;
  SSL_CTX_set_post_handshake_auth(native->context, enabled ? 1 : 0);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_sni_callback_get(
    X3CallContext* context, X3Runtime*, void* user_data, const X3Value* args,
    uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.sni_callback"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  state->host->value_retain(native->sni_callback);
  *result = native->sni_callback;
  return X3_STATUS_OK;
}

X3Status ssl_context_sni_callback_set(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.sni_callback"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (args[1].tag != X3_TAG_NONE) {
    X3Value callable_function = x3_value_invalid();
    X3Value callable_result = x3_value_invalid();
    const bool callable =
        state->host->builtin_value(state->host, "callable",
                                   &callable_function) == X3_STATUS_OK &&
        state->host->call(runtime, callable_function, &args[1], 1,
                          &callable_result) == X3_STATUS_OK &&
        callable_result.tag == X3_TAG_BOOL && callable_result.as.b;
    if (callable_function.tag != X3_TAG_INVALID)
      state->host->value_release(callable_function);
    if (callable_result.tag != X3_TAG_INVALID)
      state->host->value_release(callable_result);
    if (!callable) {
      state->host->clear_exception(context);
      return state->host->raise_class_error(
          context, "TypeError", "not a callable object");
    }
  }
  state->host->value_retain(args[1]);
  state->host->value_release(native->sni_callback);
  native->sni_callback = args[1];
  if (args[1].tag == X3_TAG_NONE) {
    SSL_CTX_set_tlsext_servername_callback(native->context, nullptr);
    SSL_CTX_set_tlsext_servername_arg(native->context, nullptr);
  } else {
    SSL_CTX_set_tlsext_servername_callback(
        native->context, ssl_servername_callback);
    SSL_CTX_set_tlsext_servername_arg(native->context, native);
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_message_callback_get(
    X3CallContext* context, X3Runtime*, void* user_data, const X3Value* args,
    uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext._msg_callback"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  state->host->value_retain(native->message_callback);
  *result = native->message_callback;
  return X3_STATUS_OK;
}

X3Status ssl_context_message_callback_set(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext._msg_callback"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (args[1].tag != X3_TAG_NONE) {
    X3Value callable_function = x3_value_invalid();
    X3Value callable_result = x3_value_invalid();
    const bool callable =
        state->host->builtin_value(state->host, "callable",
                                   &callable_function) == X3_STATUS_OK &&
        state->host->call(runtime, callable_function, &args[1], 1,
                          &callable_result) == X3_STATUS_OK &&
        callable_result.tag == X3_TAG_BOOL && callable_result.as.b;
    if (callable_function.tag != X3_TAG_INVALID)
      state->host->value_release(callable_function);
    if (callable_result.tag != X3_TAG_INVALID)
      state->host->value_release(callable_result);
    if (!callable) {
      state->host->clear_exception(context);
      return state->host->raise_class_error(
          context, "TypeError", "message callback is not callable");
    }
  }
  state->host->value_retain(args[1]);
  state->host->value_release(native->message_callback);
  native->message_callback = args[1];
  if (args[1].tag == X3_TAG_NONE) {
    SSL_CTX_set_msg_callback(native->context, nullptr);
    SSL_CTX_set_msg_callback_arg(native->context, nullptr);
  } else {
    SSL_CTX_set_msg_callback(native->context, ssl_message_callback);
    SSL_CTX_set_msg_callback_arg(native->context, native);
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_keylog_filename_get(
    X3CallContext* context, X3Runtime*, void* user_data, const X3Value* args,
    uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLContext.keylog_filename"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  state->host->value_retain(native->keylog_filename);
  *result = native->keylog_filename;
  return X3_STATUS_OK;
}

X3Status ssl_context_keylog_filename_set(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "SSLContext.keylog_filename"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;

  std::FILE* replacement = nullptr;
  if (args[1].tag != X3_TAG_NONE) {
    X3Value path_value = args[1];
    bool release_path_value = false;
    const char* path_data = nullptr;
    uint64_t path_size = 0;
    bool is_text = state->host->value_string_data(
        runtime, path_value, &path_data, &path_size) == X3_STATUS_OK;
    if (!is_text) state->host->clear_exception(context);
    bool is_bytes = false;
    const void* bytes_data = nullptr;
    if (!is_text &&
        state->host->value_object_kind(path_value) == X3_OBJECT_KIND_BYTES) {
      is_bytes = state->host->value_bytes_data(
          runtime, path_value, &bytes_data, &path_size) == X3_STATUS_OK;
      if (is_bytes) path_data = static_cast<const char*>(bytes_data);
    }
    if (!is_text && !is_bytes) {
      X3Value fspath = x3_value_invalid();
      X3Value converted = x3_value_invalid();
      if (state->host->get_attr(runtime, path_value, "__fspath__", &fspath) !=
              X3_STATUS_OK ||
          state->host->call(runtime, fspath, nullptr, 0, &converted) !=
              X3_STATUS_OK) {
        state->host->value_release(fspath);
        state->host->value_release(converted);
        state->host->clear_exception(context);
        return state->host->raise_class_error(
            context, "TypeError",
            "expected str, bytes or os.PathLike object");
      }
      state->host->value_release(fspath);
      path_value = converted;
      release_path_value = true;
      is_text = state->host->value_string_data(
          runtime, path_value, &path_data, &path_size) == X3_STATUS_OK;
      if (!is_text) state->host->clear_exception(context);
      if (!is_text &&
          state->host->value_object_kind(path_value) == X3_OBJECT_KIND_BYTES) {
        is_bytes = state->host->value_bytes_data(
            runtime, path_value, &bytes_data, &path_size) == X3_STATUS_OK;
        if (is_bytes) path_data = static_cast<const char*>(bytes_data);
      }
      if (!is_text && !is_bytes) {
        state->host->value_release(path_value);
        return state->host->raise_class_error(
            context, "TypeError", "__fspath__() must return str or bytes");
      }
    }
    const std::string path(path_data, static_cast<size_t>(path_size));
    if (release_path_value) state->host->value_release(path_value);
    if (path.find('\0') != std::string::npos) {
      return state->host->raise_class_error(
          context, "ValueError", "embedded null byte");
    }
#if defined(_WIN32)
    const auto filesystem_path = std::filesystem::u8path(path);
    replacement = _wfopen(filesystem_path.c_str(), L"a+b");
#else
    replacement = std::fopen(path.c_str(), "a+b");
#endif
    if (replacement == nullptr) {
      return state->host->raise_class_error(
          context, "OSError", "cannot open TLS key log file");
    }
    if (std::fseek(replacement, 0, SEEK_END) != 0) {
      std::fclose(replacement);
      return state->host->raise_class_error(
          context, "OSError", "cannot seek TLS key log file");
    }
    if (std::ftell(replacement) == 0) {
      std::fputs("# TLS secrets log file, generated by OpenSSL / Python\n",
                 replacement);
      std::fflush(replacement);
    }
  }

  state->host->value_retain(args[1]);
  {
    std::lock_guard<std::mutex> lock(native->keylog_mutex);
    if (native->keylog_file != nullptr) std::fclose(native->keylog_file);
    native->keylog_file = replacement;
  }
  state->host->value_release(native->keylog_filename);
  native->keylog_filename = args[1];
  SSL_CTX_set_keylog_callback(
      native->context,
      args[1].tag == X3_TAG_NONE ? nullptr : ssl_keylog_callback);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_context_wrap_bio_kw(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc,
    const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 3, 7, "SSLContext._wrap_bio()")) return X3_STATUS_ERROR;
  auto* context_state = ssl_context(state, context, args[0]);
  auto* incoming = memory_bio(state, context, args[1]);
  auto* outgoing = memory_bio(state, context, args[2]);
  if (context_state == nullptr || incoming == nullptr || outgoing == nullptr) return X3_STATUS_ERROR;

  X3Value server_side_value = argc > 3 ? args[3] : x3_value_bool(0);
  X3Value hostname_value = argc > 4 ? args[4] : x3_value_none();
  X3Value owner_value = argc > 5 ? args[5] : x3_value_none();
  X3Value session_value = argc > 6 ? args[6] : x3_value_none();
  const char* accepted[] = {"server_side", "server_hostname", "owner", "session"};
  for (uint32_t index = 0; index < kwargc; ++index) {
    bool known = false;
    for (const char* name : accepted) known = known || std::string(kwargs[index].name) == name;
    if (!known) {
      state->host->raise_class_error(context, "TypeError", "SSLContext._wrap_bio() got an unexpected keyword argument");
      return X3_STATUS_ERROR;
    }
  }
  if (const X3Value* value = keyword_value(kwargs, kwargc, "server_side")) server_side_value = *value;
  if (const X3Value* value = keyword_value(kwargs, kwargc, "server_hostname")) hostname_value = *value;
  if (const X3Value* value = keyword_value(kwargs, kwargc, "owner")) owner_value = *value;
  if (const X3Value* value = keyword_value(kwargs, kwargc, "session")) session_value = *value;

  bool server_side = false;
  if (!truth_value(server_side_value, &server_side)) {
    state->host->raise_class_error(context, "TypeError", "server_side must be bool");
    return X3_STATUS_ERROR;
  }
  if (server_side && hostname_value.tag != X3_TAG_NONE) {
    state->host->raise_class_error(context, "ValueError", "server_hostname can only be specified in client mode");
    return X3_STATUS_ERROR;
  }
  auto native = std::make_unique<SSLSocketState>();
  native->package = state;
  native->ssl = SSL_new(context_state->context);
  native->context = args[0];
  native->owner = owner_value;
  native->server_side = server_side;
  if (native->ssl == nullptr) return raise_openssl(state, context, "SSL_new failed");
  SSL_set_app_data(native->ssl, native.get());
  if (apply_requested_session(state, context, args[0], session_value,
                              server_side, native->ssl) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (hostname_value.tag != X3_TAG_NONE) {
    const char* hostname = state->host->value_to_cstr(runtime, hostname_value);
    if (hostname == nullptr) {
      state->host->raise_class_error(context, "TypeError", "server_hostname must be a string or None");
      return X3_STATUS_ERROR;
    }
    native->server_hostname = hostname;
    if (SSL_set_tlsext_host_name(native->ssl, hostname) != 1)
      return raise_openssl(state, context, "cannot set server hostname");
    if (context_state->check_hostname && SSL_set1_host(native->ssl, hostname) != 1)
      return raise_openssl(state, context, "cannot enable hostname verification");
  } else if (!server_side && context_state->check_hostname) {
    state->host->raise_class_error(context, "ValueError", "check_hostname requires server_hostname");
    return X3_STATUS_ERROR;
  }
  if (BIO_up_ref(incoming->bio) != 1)
    return raise_openssl(state, context, "cannot retain input memory BIO");
  if (BIO_up_ref(outgoing->bio) != 1) {
    BIO_free(incoming->bio);
    return raise_openssl(state, context, "cannot retain output memory BIO");
  }
  SSL_set_bio(native->ssl, incoming->bio, outgoing->bio);
  if (server_side) SSL_set_accept_state(native->ssl);
  else SSL_set_connect_state(native->ssl);
  state->host->value_retain(native->context);
  state->host->value_retain(native->owner);
  X3Value instance = state->host->value_instance(runtime, state->ssl_socket_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kSSLSocketType, native.get(),
                                             cleanup_ssl_socket) != X3_STATUS_OK) {
    state->host->value_release(instance);
    state->host->raise_class_error(context, "RuntimeError", "cannot initialize SSL object");
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status ssl_context_wrap_bio(X3CallContext* context, X3Runtime* runtime, void* user_data,
                              const X3Value* args, uint32_t argc, X3Value* result) {
  return ssl_context_wrap_bio_kw(context, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status ssl_context_wrap_socket_kw(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc,
    const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 3, 6, "SSLContext._wrap_socket()")) return X3_STATUS_ERROR;
  auto* context_state = ssl_context(state, context, args[0]);
  if (context_state == nullptr) return X3_STATUS_ERROR;
  X3Value socket_value = args[1];
  X3Value server_side_value = args[2];
  X3Value hostname_value = argc > 3 ? args[3] : x3_value_none();
  X3Value owner_value = argc > 4 ? args[4] : x3_value_none();
  X3Value session_value = argc > 5 ? args[5] : x3_value_none();
  const char* accepted[] = {"owner", "session"};
  for (uint32_t index = 0; index < kwargc; ++index) {
    bool known = false;
    for (const char* name : accepted)
      known = known || (kwargs[index].name != nullptr && std::string(kwargs[index].name) == name);
    if (!known) {
      state->host->raise_class_error(
          context, "TypeError", "SSLContext._wrap_socket() got an unexpected keyword argument");
      return X3_STATUS_ERROR;
    }
  }
  if (const X3Value* value = keyword_value(kwargs, kwargc, "owner")) owner_value = *value;
  if (const X3Value* value = keyword_value(kwargs, kwargc, "session")) session_value = *value;
  bool server_side = false;
  if (!truth_value(server_side_value, &server_side)) {
    state->host->raise_class_error(context, "TypeError", "server_side must be bool");
    return X3_STATUS_ERROR;
  }
  if (server_side && hostname_value.tag != X3_TAG_NONE) {
    state->host->raise_class_error(context, "ValueError", "server_hostname can only be specified in client mode");
    return X3_STATUS_ERROR;
  }
  X3Value fileno_method = x3_value_invalid();
  X3Value descriptor_value = x3_value_invalid();
  int64_t descriptor = -1;
  if (state->host->get_attr(runtime, socket_value, "fileno", &fileno_method) != X3_STATUS_OK ||
      state->host->call(runtime, fileno_method, nullptr, 0, &descriptor_value) != X3_STATUS_OK ||
      !integer_value(state, runtime, descriptor_value, &descriptor) || descriptor < 0 ||
      descriptor > std::numeric_limits<int>::max()) {
    if (fileno_method.tag != X3_TAG_INVALID) state->host->value_release(fileno_method);
    if (descriptor_value.tag != X3_TAG_INVALID) state->host->value_release(descriptor_value);
    state->host->raise_class_error(context, "ValueError", "invalid socket file descriptor");
    return X3_STATUS_ERROR;
  }
  state->host->value_release(fileno_method);
  state->host->value_release(descriptor_value);
  auto native = std::make_unique<SSLSocketState>();
  native->package = state;
  native->ssl = SSL_new(context_state->context);
  native->context = args[0];
  native->owner = owner_value;
  native->server_side = server_side;
  if (native->ssl == nullptr) return raise_openssl(state, context, "SSL_new failed");
  SSL_set_app_data(native->ssl, native.get());
  if (apply_requested_session(state, context, args[0], session_value,
                              server_side, native->ssl) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (hostname_value.tag != X3_TAG_NONE) {
    const char* hostname = state->host->value_to_cstr(runtime, hostname_value);
    if (hostname == nullptr) {
      state->host->raise_class_error(context, "TypeError", "server_hostname must be a string or None");
      return X3_STATUS_ERROR;
    }
    native->server_hostname = hostname;
    if (SSL_set_tlsext_host_name(native->ssl, hostname) != 1)
      return raise_openssl(state, context, "cannot set server hostname");
    if (context_state->check_hostname && SSL_set1_host(native->ssl, hostname) != 1)
      return raise_openssl(state, context, "cannot enable hostname verification");
  } else if (!server_side && context_state->check_hostname) {
    state->host->raise_class_error(context, "ValueError", "check_hostname requires server_hostname");
    return X3_STATUS_ERROR;
  }
  if (SSL_set_fd(native->ssl, static_cast<int>(descriptor)) != 1)
    return raise_openssl(state, context, "cannot attach SSL to socket");
  if (server_side) SSL_set_accept_state(native->ssl);
  else SSL_set_connect_state(native->ssl);
  state->host->value_retain(native->context);
  state->host->value_retain(native->owner);
  X3Value instance = state->host->value_instance(runtime, state->ssl_socket_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kSSLSocketType, native.get(),
                                             cleanup_ssl_socket) != X3_STATUS_OK) {
    state->host->value_release(instance);
    state->host->raise_class_error(context, "RuntimeError", "cannot initialize SSL socket");
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status ssl_context_wrap_socket(X3CallContext* context, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  return ssl_context_wrap_socket_kw(
      context, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status ssl_socket_handshake(X3CallContext* context, X3Runtime* runtime, void* user_data,
                              const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.do_handshake()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  native->active_call_context = context;
  native->active_runtime = runtime;
  native->callback_failed = false;
  const int status = SSL_do_handshake(native->ssl);
  native->active_call_context = nullptr;
  native->active_runtime = nullptr;
  if (native->callback_failed) return X3_STATUS_ERROR;
  if (status != 1) return raise_ssl_io(state, context, native->ssl, status, "TLS handshake");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_socket_write(X3CallContext* context, X3Runtime* runtime, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "_SSLSocket.write()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  const void* data = nullptr;
  uint64_t size = 0;
  if (native == nullptr) return X3_STATUS_ERROR;
  if (state->host->value_bytes_data(runtime, args[1], &data, &size) != X3_STATUS_OK) {
    state->host->raise_class_error(context, "TypeError", "a bytes-like object is required");
    return X3_STATUS_ERROR;
  }
  size_t written = 0;
  native->active_call_context = context;
  native->active_runtime = runtime;
  native->callback_failed = false;
  const int status = SSL_write_ex(native->ssl, data, static_cast<size_t>(size), &written);
  native->active_call_context = nullptr;
  native->active_runtime = nullptr;
  if (native->callback_failed) return X3_STATUS_ERROR;
  if (status != 1) return raise_ssl_io(state, context, native->ssl, status, "TLS write");
  *result = x3_value_int64(static_cast<int64_t>(written));
  return X3_STATUS_OK;
}

X3Status ssl_socket_read(X3CallContext* context, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 3, "_SSLSocket.read()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  int64_t requested = 1024;
  if (argc >= 2 && !integer_value(state, runtime, args[1], &requested)) {
    state->host->raise_class_error(context, "TypeError", "read length must be an integer");
    return X3_STATUS_ERROR;
  }
  if (requested < 0 || requested > std::numeric_limits<int>::max()) {
    state->host->raise_class_error(context, "ValueError", "read length must be non-negative");
    return X3_STATUS_ERROR;
  }
  const bool has_buffer = argc == 3 && args[2].tag != X3_TAG_NONE;
  void* destination = nullptr;
  uint64_t buffer_size = 0;
  std::string output;
  if (has_buffer) {
    const auto kind = state->host->value_object_kind(args[2]);
    bool writable = kind == X3_OBJECT_KIND_BYTEARRAY;
    if (kind == X3_OBJECT_KIND_MEMORYVIEW) {
      X3Value readonly = x3_value_invalid();
      if (state->host->get_attr(runtime, args[2], "readonly", &readonly) !=
          X3_STATUS_OK) {
        state->host->value_release(readonly);
        return X3_STATUS_ERROR;
      }
      writable = readonly.tag == X3_TAG_BOOL && !readonly.as.b;
      state->host->value_release(readonly);
    }
    const void* data = nullptr;
    if (!writable ||
        state->host->value_bytes_data(runtime, args[2], &data, &buffer_size) !=
            X3_STATUS_OK) {
      state->host->clear_exception(context);
      return state->host->raise_class_error(
          context, "TypeError",
          "read() argument 2 must be a writable bytes-like object");
    }
    destination = const_cast<void*>(data);
    if (requested <= 0 || static_cast<uint64_t>(requested) > buffer_size)
      requested = static_cast<int64_t>(buffer_size);
  } else {
    output.assign(static_cast<size_t>(requested), '\0');
    destination = output.data();
  }
  if (requested == 0) {
    *result = has_buffer
        ? x3_value_int64(0)
        : state->host->value_bytes(runtime, nullptr, 0);
    return X3_STATUS_OK;
  }
  size_t read = 0;
  native->active_call_context = context;
  native->active_runtime = runtime;
  native->callback_failed = false;
  const int status = SSL_read_ex(
      native->ssl, destination, static_cast<size_t>(requested), &read);
  native->active_call_context = nullptr;
  native->active_runtime = nullptr;
  if (native->callback_failed) return X3_STATUS_ERROR;
  if (status != 1) return raise_ssl_io(state, context, native->ssl, status, "TLS read");
  if (has_buffer) {
    *result = x3_value_int64(static_cast<int64_t>(read));
  } else {
    output.resize(read);
    *result = state->host->value_bytes(runtime, output.data(), output.size());
  }
  return X3_STATUS_OK;
}

X3Status ssl_socket_pending(X3CallContext* context, X3Runtime*, void* user_data,
                            const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.pending()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(SSL_pending(native->ssl));
  return X3_STATUS_OK;
}

X3Status ssl_socket_version(X3CallContext* context, X3Runtime* runtime, void* user_data,
                            const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.version()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (!SSL_is_init_finished(native->ssl)) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  const char* version = SSL_get_version(native->ssl);
  if (version == nullptr || std::string(version) == "unknown") *result = x3_value_none();
  else *result = state->host->value_string(runtime, version);
  return X3_STATUS_OK;
}

X3Status ssl_socket_cipher(X3CallContext* context, X3Runtime* runtime, void* user_data,
                           const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.cipher()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const SSL_CIPHER* cipher = SSL_get_current_cipher(native->ssl);
  if (cipher == nullptr) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  int bits = 0;
  SSL_CIPHER_get_bits(cipher, &bits);
  X3Value name = state->host->value_string(runtime, SSL_CIPHER_get_name(cipher));
  X3Value version = state->host->value_string(runtime, SSL_CIPHER_get_version(cipher));
  *result = make_tuple(state, runtime, {name, version, x3_value_int64(bits)});
  state->host->value_release(name);
  state->host->value_release(version);
  return X3_STATUS_OK;
}

X3Status ssl_socket_selected_alpn(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.selected_alpn_protocol()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const unsigned char* selected = nullptr;
  unsigned int size = 0;
  SSL_get0_alpn_selected(native->ssl, &selected, &size);
  if (size == 0) *result = x3_value_none();
  else *result = state->host->value_string_utf8(runtime, reinterpret_cast<const char*>(selected), size);
  return X3_STATUS_OK;
}

X3Status ssl_socket_compression(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.compression()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  const COMP_METHOD* compression = SSL_get_current_compression(native->ssl);
  if (compression == nullptr) *result = x3_value_none();
  else *result = state->host->value_string(runtime, SSL_COMP_get_name(compression));
  return X3_STATUS_OK;
}

X3Status ssl_socket_channel_binding(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 2,
               "_SSLSocket.get_channel_binding()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (argc == 2) {
    const char* binding_type = state->host->value_to_cstr(runtime, args[1]);
    if (binding_type == nullptr || std::string(binding_type) != "tls-unique") {
      return state->host->raise_class_error(
          context, "ValueError", "channel binding type not implemented");
    }
  }
  if (!SSL_is_init_finished(native->ssl)) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  unsigned char binding[EVP_MAX_MD_SIZE]{};
  const bool use_local_finished =
      (SSL_session_reused(native->ssl) != 0) != !native->server_side;
  const size_t size = use_local_finished
      ? SSL_get_finished(native->ssl, binding, sizeof(binding))
      : SSL_get_peer_finished(native->ssl, binding, sizeof(binding));
  if (size == 0) {
    *result = x3_value_none();
  } else {
    *result = state->host->value_bytes(runtime, binding, size);
  }
  return X3_STATUS_OK;
}

X3Status ssl_socket_shared_ciphers(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1,
               "_SSLSocket.shared_ciphers()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (!native->server_side || !SSL_is_init_finished(native->ssl)) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  std::vector<char> names(32768, '\0');
  if (SSL_get_shared_ciphers(
          native->ssl, names.data(), static_cast<int>(names.size())) == nullptr) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  X3Value output = state->host->value_list(runtime);
  if (output.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  STACK_OF(SSL_CIPHER)* ciphers = SSL_get_ciphers(native->ssl);
  std::string shared(names.data());
  size_t start = 0;
  while (start <= shared.size()) {
    const size_t end = shared.find(':', start);
    const std::string name = shared.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    const SSL_CIPHER* found = nullptr;
    const int cipher_count = ciphers == nullptr ? 0 : sk_SSL_CIPHER_num(ciphers);
    for (int index = 0; index < cipher_count; ++index) {
      const SSL_CIPHER* candidate = sk_SSL_CIPHER_value(ciphers, index);
      if (candidate != nullptr && name == SSL_CIPHER_get_name(candidate)) {
        found = candidate;
        break;
      }
    }
    if (found != nullptr) {
      int bits = 0;
      SSL_CIPHER_get_bits(found, &bits);
      X3Value cipher_name = state->host->value_string_utf8(
          runtime, name.data(), name.size());
      X3Value protocol = state->host->value_string(
          runtime, SSL_CIPHER_get_version(found));
      X3Value item = make_tuple(
          state, runtime,
          {cipher_name, protocol, x3_value_int64(bits)});
      const bool appended = item.tag != X3_TAG_INVALID &&
          state->host->list_append(runtime, output, item) == X3_STATUS_OK;
      state->host->value_release(cipher_name);
      state->host->value_release(protocol);
      state->host->value_release(item);
      if (!appended) {
        state->host->value_release(output);
        return X3_STATUS_ERROR;
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  *result = output;
  return X3_STATUS_OK;
}

X3Status ssl_socket_verify_client_post_handshake(
    X3CallContext* context, X3Runtime*, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1,
               "_SSLSocket.verify_client_post_handshake()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (SSL_verify_client_post_handshake(native->ssl) != 1)
    return raise_openssl(
        state, context, "cannot initiate post-handshake authentication");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Value x509_name_value(PackageState* state, X3Runtime* runtime, X509_NAME* name) {
  std::vector<X3Value> rdns;
  const int count = name == nullptr ? 0 : X509_NAME_entry_count(name);
  for (int index = 0; index < count; ++index) {
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, index);
    const int nid = OBJ_obj2nid(X509_NAME_ENTRY_get_object(entry));
    const char* label = OBJ_nid2ln(nid);
    unsigned char* utf8 = nullptr;
    const int length = ASN1_STRING_to_UTF8(&utf8, X509_NAME_ENTRY_get_data(entry));
    if (length < 0) continue;
    X3Value key = state->host->value_string(runtime, label == nullptr ? "unknown" : label);
    X3Value value = state->host->value_string_utf8(
        runtime, reinterpret_cast<const char*>(utf8), static_cast<uint64_t>(length));
    OPENSSL_free(utf8);
    X3Value pair = make_tuple(state, runtime, {key, value});
    X3Value rdn = make_tuple(state, runtime, {pair});
    state->host->value_release(key);
    state->host->value_release(value);
    state->host->value_release(pair);
    rdns.push_back(rdn);
  }
  X3Value output = make_tuple(
      state, runtime, static_cast<const std::vector<X3Value>&>(rdns));
  for (X3Value rdn : rdns) state->host->value_release(rdn);
  return output;
}

X3Value asn1_time_value(PackageState* state, X3Runtime* runtime, const ASN1_TIME* time) {
  BIO* bio = BIO_new(BIO_s_mem());
  if (bio == nullptr || ASN1_TIME_print(bio, time) != 1) {
    BIO_free(bio);
    return x3_value_invalid();
  }
  const char* data = nullptr;
  const long size = BIO_get_mem_data(bio, &data);
  std::string text(data, size > 0 ? static_cast<size_t>(size) : 0);
  BIO_free(bio);
  text += " GMT";
  return state->host->value_string_utf8(runtime, text.data(), text.size());
}

X3Value decoded_certificate_value(PackageState* state, X3Runtime* runtime,
                                  X509* certificate) {
  X3Value output = state->host->value_dict(runtime);
  X3Value subject = x509_name_value(state, runtime, X509_get_subject_name(certificate));
  X3Value issuer = x509_name_value(state, runtime, X509_get_issuer_name(certificate));
  X3Value not_before = asn1_time_value(state, runtime, X509_get0_notBefore(certificate));
  X3Value not_after = asn1_time_value(state, runtime, X509_get0_notAfter(certificate));
  BIGNUM* serial_number = ASN1_INTEGER_to_BN(X509_get_serialNumber(certificate), nullptr);
  char* serial_hex = serial_number == nullptr ? nullptr : BN_bn2hex(serial_number);
  X3Value serial = state->host->value_string(runtime, serial_hex == nullptr ? "" : serial_hex);
  OPENSSL_free(serial_hex);
  BN_free(serial_number);
  bool ok = output.tag != X3_TAG_INVALID && subject.tag != X3_TAG_INVALID &&
      issuer.tag != X3_TAG_INVALID && not_before.tag != X3_TAG_INVALID &&
      not_after.tag != X3_TAG_INVALID && serial.tag != X3_TAG_INVALID &&
      dict_set_named(state, runtime, output, "subject", subject) &&
      dict_set_named(state, runtime, output, "issuer", issuer) &&
      dict_set_named(state, runtime, output, "version",
                     x3_value_int64(X509_get_version(certificate) + 1)) &&
      dict_set_named(state, runtime, output, "serialNumber", serial) &&
      dict_set_named(state, runtime, output, "notBefore", not_before) &&
      dict_set_named(state, runtime, output, "notAfter", not_after);

  GENERAL_NAMES* names = static_cast<GENERAL_NAMES*>(
      X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr));
  if (ok && names != nullptr) {
    std::vector<X3Value> alternate_names;
    const int count = sk_GENERAL_NAME_num(names);
    alternate_names.reserve(count > 0 ? static_cast<size_t>(count) : 0);
    for (int index = 0; index < count; ++index) {
      const GENERAL_NAME* name = sk_GENERAL_NAME_value(names, index);
      BIO* bio = BIO_new(BIO_s_mem());
      if (bio == nullptr || GENERAL_NAME_print(bio, const_cast<GENERAL_NAME*>(name)) != 1) {
        BIO_free(bio);
        ok = false;
        break;
      }
      const char* data = nullptr;
      const long size = BIO_get_mem_data(bio, &data);
      std::string rendered(data, size > 0 ? static_cast<size_t>(size) : 0);
      BIO_free(bio);
      const size_t separator = rendered.find(':');
      std::string kind = separator == std::string::npos ? "other" : rendered.substr(0, separator);
      std::string value = separator == std::string::npos ? rendered : rendered.substr(separator + 1);
      X3Value kind_value = state->host->value_string_utf8(runtime, kind.data(), kind.size());
      X3Value name_value = state->host->value_string_utf8(runtime, value.data(), value.size());
      X3Value pair = make_tuple(state, runtime, {kind_value, name_value});
      state->host->value_release(kind_value);
      state->host->value_release(name_value);
      if (pair.tag == X3_TAG_INVALID) {
        ok = false;
        break;
      }
      alternate_names.push_back(pair);
    }
    if (ok) {
      X3Value value = make_tuple(
          state, runtime, static_cast<const std::vector<X3Value>&>(alternate_names));
      ok = value.tag != X3_TAG_INVALID &&
          dict_set_named(state, runtime, output, "subjectAltName", value);
      if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
    }
    for (X3Value value : alternate_names) state->host->value_release(value);
    GENERAL_NAMES_free(names);
  }

  state->host->value_release(subject);
  state->host->value_release(issuer);
  if (not_before.tag != X3_TAG_INVALID) state->host->value_release(not_before);
  if (not_after.tag != X3_TAG_INVALID) state->host->value_release(not_after);
  if (serial.tag != X3_TAG_INVALID) state->host->value_release(serial);
  if (!ok) {
    if (output.tag != X3_TAG_INVALID) state->host->value_release(output);
    return x3_value_invalid();
  }
  return output;
}

X3Status ssl_context_get_ca_certs(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 2, "SSLContext.get_ca_certs()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_context(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  bool binary_form = false;
  if (argc == 2 && !truth_value(args[1], &binary_form)) {
    return state->host->raise_class_error(
        context, "TypeError", "binary_form must be bool");
  }
  X3Value output = state->host->value_list(runtime);
  if (output.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  STACK_OF(X509_OBJECT)* objects = X509_STORE_get0_objects(
      SSL_CTX_get_cert_store(native->context));
  const int count = objects == nullptr ? 0 : sk_X509_OBJECT_num(objects);
  for (int index = 0; index < count; ++index) {
    X509_OBJECT* object = sk_X509_OBJECT_value(objects, index);
    X509* certificate = object == nullptr ? nullptr : X509_OBJECT_get0_X509(object);
    if (certificate == nullptr || X509_check_ca(certificate) <= 0) continue;
    X3Value value = x3_value_invalid();
    if (binary_form) {
      const int size = i2d_X509(certificate, nullptr);
      if (size >= 0) {
        std::vector<unsigned char> der(static_cast<size_t>(size));
        unsigned char* cursor = der.data();
        if (i2d_X509(certificate, &cursor) == size)
          value = state->host->value_bytes(runtime, der.data(), der.size());
      }
    } else {
      value = decoded_certificate_value(state, runtime, certificate);
    }
    const bool appended = value.tag != X3_TAG_INVALID &&
        state->host->list_append(runtime, output, value) == X3_STATUS_OK;
    state->host->value_release(value);
    if (!appended) {
      state->host->value_release(output);
      return X3_STATUS_ERROR;
    }
  }
  *result = output;
  return X3_STATUS_OK;
}

X3Value certificate_value(PackageState* state, X3Runtime* runtime,
                          X509* certificate) {
  if (certificate == nullptr || X509_up_ref(certificate) != 1)
    return x3_value_invalid();
  auto native = std::make_unique<CertificateState>();
  native->certificate = certificate;
  X3Value instance = state->host->value_instance(
      runtime, state->certificate_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(
          instance, kCertificateType, native.get(), cleanup_certificate) !=
          X3_STATUS_OK) {
    state->host->value_release(instance);
    return x3_value_invalid();
  }
  native.release();
  return instance;
}

X3Status certificate_public_bytes(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 2,
               "Certificate.public_bytes()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_certificate(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  int64_t encoding = 1;
  if (argc == 2 && !integer_value(state, runtime, args[1], &encoding))
    return state->host->raise_class_error(
        context, "TypeError", "encoding must be an integer");
  if (encoding == 2) {
    const int size = i2d_X509(native->certificate, nullptr);
    if (size < 0)
      return raise_openssl(state, context, "cannot encode certificate");
    std::vector<unsigned char> der(static_cast<size_t>(size));
    unsigned char* cursor = der.data();
    if (i2d_X509(native->certificate, &cursor) != size)
      return raise_openssl(state, context, "cannot encode certificate");
    *result = state->host->value_bytes(runtime, der.data(), der.size());
    return X3_STATUS_OK;
  }
  if (encoding != 1)
    return state->host->raise_class_error(
        context, "ValueError", "unsupported certificate encoding");
  BIO* bio = BIO_new(BIO_s_mem());
  if (bio == nullptr || PEM_write_bio_X509(bio, native->certificate) != 1) {
    BIO_free(bio);
    return raise_openssl(state, context, "cannot encode certificate");
  }
  const char* data = nullptr;
  const long size = BIO_get_mem_data(bio, &data);
  *result = state->host->value_string_utf8(
      runtime, data, size > 0 ? static_cast<uint64_t>(size) : 0);
  BIO_free(bio);
  return X3_STATUS_OK;
}

X3Status certificate_get_info(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "Certificate.get_info()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_certificate(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = decoded_certificate_value(state, runtime, native->certificate);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status ssl_socket_certificate_chain(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result,
    bool verified) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1,
               verified ? "_SSLSocket.get_verified_chain()"
                        : "_SSLSocket.get_unverified_chain()"))
    return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  STACK_OF(X509)* chain = verified
      ? SSL_get0_verified_chain(native->ssl)
      : SSL_get_peer_cert_chain(native->ssl);
  X509* peer = nullptr;
  const bool prepend_peer = !verified && native->server_side;
  if (prepend_peer) peer = SSL_get1_peer_certificate(native->ssl);
  const int chain_size = chain == nullptr ? 0 : sk_X509_num(chain);
  if (chain_size == 0 && peer == nullptr) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  X3Value output = state->host->value_list(runtime);
  bool ok = output.tag != X3_TAG_INVALID;
  auto append_certificate = [&](X509* certificate) {
    if (!ok || certificate == nullptr) return;
    X3Value item = certificate_value(state, runtime, certificate);
    ok = item.tag != X3_TAG_INVALID &&
        state->host->list_append(runtime, output, item) == X3_STATUS_OK;
    state->host->value_release(item);
  };
  if (peer != nullptr) {
    const bool duplicate = chain_size > 0 &&
        X509_cmp(peer, sk_X509_value(chain, 0)) == 0;
    if (!duplicate) append_certificate(peer);
    X509_free(peer);
  }
  for (int index = 0; index < chain_size; ++index)
    append_certificate(sk_X509_value(chain, index));
  if (!ok) {
    state->host->value_release(output);
    return X3_STATUS_ERROR;
  }
  *result = output;
  return X3_STATUS_OK;
}

X3Status ssl_socket_get_verified_chain(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  return ssl_socket_certificate_chain(
      context, runtime, user_data, args, argc, result, true);
}

X3Status ssl_socket_get_unverified_chain(
    X3CallContext* context, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  return ssl_socket_certificate_chain(
      context, runtime, user_data, args, argc, result, false);
}

X3Status test_decode_cert(X3CallContext* context, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_test_decode_cert()")) return X3_STATUS_ERROR;
  const char* path = state->host->value_to_cstr(runtime, args[0]);
  if (path == nullptr) {
    state->host->raise_class_error(context, "TypeError", "path must be a string");
    return X3_STATUS_ERROR;
  }
  BIO* bio = BIO_new_file(path, "rb");
  if (bio == nullptr) return raise_openssl(state, context, "cannot open certificate file");
  X509* certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (certificate == nullptr) return raise_openssl(state, context, "cannot decode certificate");
  *result = decoded_certificate_value(state, runtime, certificate);
  X509_free(certificate);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status ssl_socket_getpeercert(X3CallContext* context, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 2, "_SSLSocket.getpeercert()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  bool binary_form = false;
  if (argc == 2 && !truth_value(args[1], &binary_form)) {
    state->host->raise_class_error(context, "TypeError", "binary_form must be bool");
    return X3_STATUS_ERROR;
  }
  X509* certificate = SSL_get1_peer_certificate(native->ssl);
  if (certificate == nullptr) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  if (binary_form) {
    const int size = i2d_X509(certificate, nullptr);
    if (size < 0) {
      X509_free(certificate);
      return raise_openssl(state, context, "cannot encode peer certificate");
    }
    std::string der(static_cast<size_t>(size), '\0');
    unsigned char* cursor = reinterpret_cast<unsigned char*>(der.data());
    if (i2d_X509(certificate, &cursor) != size) {
      X509_free(certificate);
      return raise_openssl(state, context, "cannot encode peer certificate");
    }
    X509_free(certificate);
    *result = state->host->value_bytes(runtime, der.data(), der.size());
    return X3_STATUS_OK;
  }
  auto* context_state = static_cast<SSLContextState*>(
      state->host->instance_get_native_data(native->context, kSSLContextType));
  if (context_state != nullptr && context_state->verify_mode == 0) {
    X509_free(certificate);
    *result = state->host->value_dict(runtime);
    return X3_STATUS_OK;
  }
  X3Value output = decoded_certificate_value(state, runtime, certificate);
  X509_free(certificate);
  if (output.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  *result = output;
  return X3_STATUS_OK;
}

X3Status ssl_socket_shutdown(X3CallContext* context, X3Runtime* runtime, void* user_data,
                             const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.shutdown()")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  native->active_call_context = context;
  native->active_runtime = runtime;
  native->callback_failed = false;
  const int status = SSL_shutdown(native->ssl);
  native->active_call_context = nullptr;
  native->active_runtime = nullptr;
  if (native->callback_failed) return X3_STATUS_ERROR;
  if (status < 0) return raise_ssl_io(state, context, native->ssl, status, "TLS shutdown");
  if (status == 0) {
    state->host->raise_error(context, state->ssl_want_read_error,
                             "TLS shutdown: The operation did not complete (read)");
    return X3_STATUS_ERROR;
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_socket_context_get(X3CallContext* context, X3Runtime*, void* user_data,
                                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.context")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  state->host->value_retain(native->context);
  *result = native->context;
  return X3_STATUS_OK;
}

X3Status ssl_socket_context_set(X3CallContext* context, X3Runtime*,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "_SSLSocket.context"))
    return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  auto* context_state = ssl_context(state, context, args[1]);
  if (native == nullptr || context_state == nullptr) return X3_STATUS_ERROR;
  if (SSL_set_SSL_CTX(native->ssl, context_state->context) == nullptr)
    return raise_openssl(state, context, "cannot change SSL context");
  state->host->value_retain(args[1]);
  state->host->value_release(native->context);
  native->context = args[1];
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_socket_server_side_get(X3CallContext* context, X3Runtime*, void* user_data,
                                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.server_side")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(native->server_side);
  return X3_STATUS_OK;
}

X3Status ssl_socket_owner_get(X3CallContext* context, X3Runtime*, void* user_data,
                              const X3Value* args, uint32_t argc,
                              X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.owner"))
    return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  state->host->value_retain(native->owner);
  *result = native->owner;
  return X3_STATUS_OK;
}

X3Status ssl_socket_server_hostname_get(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.server_hostname")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (native->server_hostname.empty()) *result = x3_value_none();
  else *result = state->host->value_string(runtime, native->server_hostname.c_str());
  return X3_STATUS_OK;
}

X3Status ssl_socket_session_reused_get(X3CallContext* context, X3Runtime*, void* user_data,
                                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.session_reused")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(SSL_session_reused(native->ssl));
  return X3_STATUS_OK;
}

X3Status make_ssl_session(PackageState* state, X3Runtime* runtime,
                          SSL_SESSION* session, X3Value context_value,
                          X3Value* result) {
  auto native = std::make_unique<SSLSessionState>();
  native->package = state;
  native->session = session;
  native->context = context_value;
  state->host->value_retain(native->context);
  X3Value instance = state->host->value_instance(runtime, state->ssl_session_class);
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, kSSLSessionType, native.get(),
                                             cleanup_ssl_session) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  native.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status ssl_socket_session_get(X3CallContext* context, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "_SSLSocket.session")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  SSL_SESSION* session = SSL_get1_session(native->ssl);
  if (session == nullptr) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  const auto status = make_ssl_session(state, runtime, session, native->context, result);
  if (status != X3_STATUS_OK) SSL_SESSION_free(session);
  return status;
}

X3Status ssl_socket_session_set(X3CallContext* context, X3Runtime*, void* user_data,
                                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "_SSLSocket.session")) return X3_STATUS_ERROR;
  auto* native = ssl_socket(state, context, args[0]);
  if (native == nullptr) return X3_STATUS_ERROR;
  if (SSL_is_init_finished(native->ssl)) {
    state->host->raise_class_error(context, "ValueError",
                                   "Cannot set session after handshake");
    return X3_STATUS_ERROR;
  }
  if (apply_requested_session(state, context, native->context, args[1],
                              native->server_side, native->ssl) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status ssl_session_init(X3CallContext* context, X3Runtime*, void* user_data,
                          const X3Value*, uint32_t, X3Value*) {
  auto* state = static_cast<PackageState*>(user_data);
  state->host->raise_class_error(context, "TypeError",
                                 "cannot create SSLSession instances directly");
  return X3_STATUS_ERROR;
}

X3Status ssl_session_id(X3CallContext* context, X3Runtime* runtime, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLSession.id")) return X3_STATUS_ERROR;
  auto* session = ssl_session(state, context, args[0]);
  if (session == nullptr) return X3_STATUS_ERROR;
  unsigned int size = 0;
  const unsigned char* data = SSL_SESSION_get_id(session->session, &size);
  *result = state->host->value_bytes(runtime, data, size);
  return X3_STATUS_OK;
}

X3Status ssl_session_time(X3CallContext* context, X3Runtime*, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLSession.time")) return X3_STATUS_ERROR;
  auto* session = ssl_session(state, context, args[0]);
  if (session == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(static_cast<int64_t>(SSL_SESSION_get_time(session->session)));
  return X3_STATUS_OK;
}

X3Status ssl_session_timeout(X3CallContext* context, X3Runtime*, void* user_data,
                             const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLSession.timeout")) return X3_STATUS_ERROR;
  auto* session = ssl_session(state, context, args[0]);
  if (session == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(static_cast<int64_t>(SSL_SESSION_get_timeout(session->session)));
  return X3_STATUS_OK;
}

X3Status ssl_session_ticket_lifetime_hint(
    X3CallContext* context, X3Runtime*, void* user_data,
    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLSession.ticket_lifetime_hint"))
    return X3_STATUS_ERROR;
  auto* session = ssl_session(state, context, args[0]);
  if (session == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_int64(static_cast<int64_t>(
      SSL_SESSION_get_ticket_lifetime_hint(session->session)));
  return X3_STATUS_OK;
}

X3Status ssl_session_has_ticket(X3CallContext* context, X3Runtime*, void* user_data,
                                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "SSLSession.has_ticket")) return X3_STATUS_ERROR;
  auto* session = ssl_session(state, context, args[0]);
  if (session == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_bool(SSL_SESSION_has_ticket(session->session));
  return X3_STATUS_OK;
}

X3Status rand_status(X3CallContext* context, X3Runtime*, void* user_data,
                     const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 0, 0, "RAND_status()")) return X3_STATUS_ERROR;
  *result = x3_value_bool(RAND_status() == 1);
  return X3_STATUS_OK;
}

X3Status rand_bytes(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "RAND_bytes()") || args[0].tag != X3_TAG_INT64) {
    if (argc == 1) state->host->raise_class_error(context, "TypeError", "num must be an integer");
    return X3_STATUS_ERROR;
  }
  const int64_t requested = args[0].as.i64;
  if (requested < 0 || requested > std::numeric_limits<int>::max()) {
    state->host->raise_class_error(context, "ValueError", "num must be non-negative and fit in an int");
    return X3_STATUS_ERROR;
  }
  std::string bytes(static_cast<size_t>(requested), '\0');
  if (requested != 0 && RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(requested)) != 1)
    return raise_openssl(state, context, "RAND_bytes failed");
  *result = state->host->value_bytes(runtime, bytes.data(), bytes.size());
  return X3_STATUS_OK;
}

X3Status rand_add(X3CallContext* context, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 2, 2, "RAND_add()")) return X3_STATUS_ERROR;
  const void* data = nullptr;
  uint64_t size = 0;
  if (state->host->value_bytes_data(runtime, args[0], &data, &size) != X3_STATUS_OK) {
    state->host->raise_class_error(context, "TypeError", "RAND_add() data must be bytes-like");
    return X3_STATUS_ERROR;
  }
  double entropy = 0.0;
  if (args[1].tag == X3_TAG_DOUBLE) entropy = args[1].as.f64;
  else if (args[1].tag == X3_TAG_INT64) entropy = static_cast<double>(args[1].as.i64);
  else {
    state->host->raise_class_error(context, "TypeError", "RAND_add() entropy must be numeric");
    return X3_STATUS_ERROR;
  }
  ::RAND_add(data, static_cast<int>(std::min<uint64_t>(size, std::numeric_limits<int>::max())), entropy);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status object_to_tuple(PackageState* state, X3CallContext* context, X3Runtime* runtime,
                         int nid, X3Value* result) {
  const ASN1_OBJECT* object = OBJ_nid2obj(nid);
  if (object == nullptr || nid == NID_undef) {
    state->host->raise_class_error(context, "ValueError", "unknown object identifier");
    return X3_STATUS_ERROR;
  }
  char oid[128]{};
  OBJ_obj2txt(oid, sizeof(oid), object, 1);
  const char* short_name = OBJ_nid2sn(nid);
  const char* long_name = OBJ_nid2ln(nid);
  X3Value sn = state->host->value_string(runtime, short_name == nullptr ? "" : short_name);
  X3Value ln = state->host->value_string(runtime, long_name == nullptr ? "" : long_name);
  X3Value oid_value = state->host->value_string(runtime, oid);
  *result = make_tuple(state, runtime, {x3_value_int64(nid), sn, ln, oid_value});
  state->host->value_release(sn);
  state->host->value_release(ln);
  state->host->value_release(oid_value);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status nid2obj(X3CallContext* context, X3Runtime* runtime, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "nid2obj()") || args[0].tag != X3_TAG_INT64) {
    if (argc == 1) state->host->raise_class_error(context, "TypeError", "nid must be an integer");
    return X3_STATUS_ERROR;
  }
  return object_to_tuple(state, context, runtime, static_cast<int>(args[0].as.i64), result);
}

X3Status txt2obj(X3CallContext* context, X3Runtime* runtime, void* user_data,
                 const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 2, "txt2obj()")) return X3_STATUS_ERROR;
  const char* text = state->host->value_to_cstr(runtime, args[0]);
  if (text == nullptr) {
    state->host->raise_class_error(context, "TypeError", "object identifier must be a string");
    return X3_STATUS_ERROR;
  }
  const int nid = OBJ_txt2nid(text);
  return object_to_tuple(state, context, runtime, nid, result);
}

X3Status txt2obj_kw(X3CallContext* context, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc,
                    const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (kwargc > 1 || (kwargc == 1 && std::string(kwargs[0].name) != "name")) {
    state->host->raise_class_error(context, "TypeError", "txt2obj() got an unexpected keyword argument");
    return X3_STATUS_ERROR;
  }
  return txt2obj(context, runtime, user_data, args, argc, result);
}

X3Status get_default_verify_paths(X3CallContext* context, X3Runtime* runtime, void* user_data,
                                  const X3Value*, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 0, 0, "get_default_verify_paths()")) return X3_STATUS_ERROR;
  X3Value file_env = state->host->value_string(runtime, X509_get_default_cert_file_env());
  X3Value file = state->host->value_string(runtime, X509_get_default_cert_file());
  X3Value dir_env = state->host->value_string(runtime, X509_get_default_cert_dir_env());
  X3Value dir = state->host->value_string(runtime, X509_get_default_cert_dir());
  *result = make_tuple(state, runtime, {file_env, file, dir_env, dir});
  state->host->value_release(file_env);
  state->host->value_release(file);
  state->host->value_release(dir_env);
  state->host->value_release(dir);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

#if defined(_WIN32)
X3Value make_frozenset(PackageState* state, X3Runtime* runtime, X3Value iterable) {
  X3Value klass = x3_value_invalid();
  X3Value result = x3_value_invalid();
  if (state->host->builtin_value(state->host, "frozenset", &klass) == X3_STATUS_OK)
    state->host->call(runtime, klass, &iterable, 1, &result);
  state->host->value_release(klass);
  return result;
}

const char* certificate_encoding(DWORD encoding) {
  if ((encoding & X509_ASN_ENCODING) != 0) return "x509_asn";
  if ((encoding & PKCS_7_ASN_ENCODING) != 0) return "pkcs_7_asn";
  return nullptr;
}

X3Status enum_certificates(X3CallContext* context, X3Runtime* runtime, void* user_data,
                           const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "enum_certificates()")) return X3_STATUS_ERROR;
  const char* store_name = state->host->value_to_cstr(runtime, args[0]);
  if (store_name == nullptr) {
    state->host->raise_class_error(context, "TypeError", "store_name must be a string");
    return X3_STATUS_ERROR;
  }
  HCERTSTORE store = CertOpenSystemStoreA(0, store_name);
  if (store == nullptr) {
    state->host->raise_class_error(context, "OSError", "failed to open Windows certificate store");
    return X3_STATUS_ERROR;
  }
  X3Value output = state->host->value_list(runtime);
  PCCERT_CONTEXT certificate = nullptr;
  while ((certificate = CertEnumCertificatesInStore(store, certificate)) != nullptr) {
    const char* encoding = certificate_encoding(certificate->dwCertEncodingType);
    if (encoding == nullptr) continue;
    X3Value encoded = state->host->value_bytes(runtime, certificate->pbCertEncoded,
                                                certificate->cbCertEncoded);
    X3Value encoding_value = state->host->value_string(runtime, encoding);
    X3Value trust = x3_value_invalid();
    DWORD usage_size = 0;
    if (!CertGetEnhancedKeyUsage(certificate, 0, nullptr, &usage_size)) {
      if (GetLastError() == CRYPT_E_NOT_FOUND) trust = x3_value_bool(1);
      else {
        state->host->value_release(encoded);
        state->host->value_release(encoding_value);
        state->host->value_release(output);
        CertCloseStore(store, 0);
        state->host->raise_class_error(context, "OSError", "failed to query certificate trust");
        return X3_STATUS_ERROR;
      }
    } else {
      std::vector<unsigned char> usage_storage(usage_size);
      auto* usage = reinterpret_cast<PCERT_ENHKEY_USAGE>(usage_storage.data());
      if (!CertGetEnhancedKeyUsage(certificate, 0, usage, &usage_size)) {
        state->host->value_release(encoded);
        state->host->value_release(encoding_value);
        state->host->value_release(output);
        CertCloseStore(store, 0);
        state->host->raise_class_error(context, "OSError", "failed to read certificate trust");
        return X3_STATUS_ERROR;
      }
      X3Value oids = state->host->value_list(runtime);
      for (DWORD index = 0; index < usage->cUsageIdentifier; ++index) {
        X3Value oid = state->host->value_string(runtime, usage->rgpszUsageIdentifier[index]);
        state->host->list_append(runtime, oids, oid);
        state->host->value_release(oid);
      }
      trust = make_frozenset(state, runtime, oids);
      state->host->value_release(oids);
    }
    X3Value row = make_tuple(state, runtime, {encoded, encoding_value, trust});
    state->host->list_append(runtime, output, row);
    state->host->value_release(encoded);
    state->host->value_release(encoding_value);
    state->host->value_release(trust);
    state->host->value_release(row);
  }
  CertCloseStore(store, 0);
  *result = output;
  return X3_STATUS_OK;
}

X3Status enum_crls(X3CallContext* context, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (!argc_is(state, context, argc, 1, 1, "enum_crls()")) return X3_STATUS_ERROR;
  const char* store_name = state->host->value_to_cstr(runtime, args[0]);
  if (store_name == nullptr) {
    state->host->raise_class_error(context, "TypeError", "store_name must be a string");
    return X3_STATUS_ERROR;
  }
  HCERTSTORE store = CertOpenSystemStoreA(0, store_name);
  if (store == nullptr) {
    state->host->raise_class_error(context, "OSError", "failed to open Windows certificate store");
    return X3_STATUS_ERROR;
  }
  X3Value output = state->host->value_list(runtime);
  PCCRL_CONTEXT crl = nullptr;
  while ((crl = CertEnumCRLsInStore(store, crl)) != nullptr) {
    const char* encoding = certificate_encoding(crl->dwCertEncodingType);
    if (encoding == nullptr) continue;
    X3Value encoded = state->host->value_bytes(runtime, crl->pbCrlEncoded, crl->cbCrlEncoded);
    X3Value encoding_value = state->host->value_string(runtime, encoding);
    X3Value row = make_tuple(state, runtime, {encoded, encoding_value});
    state->host->list_append(runtime, output, row);
    state->host->value_release(encoded);
    state->host->value_release(encoding_value);
    state->host->value_release(row);
  }
  CertCloseStore(store, 0);
  *result = output;
  return X3_STATUS_OK;
}
#endif

bool add_property(PackageState* state, X3Runtime* runtime, X3Value klass, const char* name,
                  X3NativeFn getter, X3NativeFn setter = nullptr) {
  X3Value property = x3_value_invalid();
  if (state->host->property_create(runtime, name, getter, setter, state, &property) != X3_STATUS_OK) return false;
  const bool ok = state->host->class_add_value(klass, name, property) == X3_STATUS_OK;
  state->host->value_release(property);
  return ok;
}

bool add_exception(PackageState* state, X3Module* module, const char* name,
                   X3Value base, X3Value* output) {
  if (state->host->module_add_class(module, name, nullptr, 0, output) != X3_STATUS_OK) return false;
  return state->host->class_set_base(*output, base) == X3_STATUS_OK;
}

void add_constants(PackageState* state, X3Runtime* runtime, X3Module* module) {
  add_int(state, module, "OPENSSL_VERSION_NUMBER", static_cast<int64_t>(OPENSSL_VERSION_NUMBER));
  add_string(state, runtime, module, "OPENSSL_VERSION", OpenSSL_version(OPENSSL_VERSION));
  X3Value version = make_tuple(state, runtime,
      {x3_value_int64(OPENSSL_VERSION_MAJOR), x3_value_int64(OPENSSL_VERSION_MINOR),
       x3_value_int64(OPENSSL_VERSION_PATCH), x3_value_int64(0), x3_value_int64(0)});
  state->host->module_add_value(module, "OPENSSL_VERSION_INFO", version);
  state->host->module_add_value(module, "_OPENSSL_API_VERSION", version);
  state->host->value_release(version);
  add_string(state, runtime, module, "_DEFAULT_CIPHERS", kDefaultCiphers);
  add_int(state, module, "ENCODING_PEM", 1);
  add_int(state, module, "ENCODING_DER", 2);

  add_bool(state, module, "HAS_SNI", true);
  add_bool(state, module, "HAS_ECDH", true);
  add_bool(state, module, "HAS_NPN", false);
  add_bool(state, module, "HAS_ALPN", true);
  add_bool(state, module, "HAS_SSLv2", false);
  add_bool(state, module, "HAS_SSLv3", false);
  add_bool(state, module, "HAS_TLSv1", true);
  add_bool(state, module, "HAS_TLSv1_1", true);
  add_bool(state, module, "HAS_TLSv1_2", true);
  add_bool(state, module, "HAS_TLSv1_3", true);
  add_bool(state, module, "HAS_PSK", true);
  add_bool(state, module, "HAS_PHA", true);

  add_int(state, module, "PROTOCOL_SSLv23", 2);
  add_int(state, module, "PROTOCOL_TLS", 2);
  add_int(state, module, "PROTOCOL_TLS_CLIENT", 16);
  add_int(state, module, "PROTOCOL_TLS_SERVER", 17);
  add_int(state, module, "PROTOCOL_TLSv1", 3);
  add_int(state, module, "PROTOCOL_TLSv1_1", 4);
  add_int(state, module, "PROTOCOL_TLSv1_2", 5);
  add_int(state, module, "PROTO_MINIMUM_SUPPORTED", -2);
  add_int(state, module, "PROTO_MAXIMUM_SUPPORTED", -1);
  add_int(state, module, "PROTO_SSLv3", SSL3_VERSION);
  add_int(state, module, "PROTO_TLSv1", TLS1_VERSION);
  add_int(state, module, "PROTO_TLSv1_1", TLS1_1_VERSION);
  add_int(state, module, "PROTO_TLSv1_2", TLS1_2_VERSION);
  add_int(state, module, "PROTO_TLSv1_3", TLS1_3_VERSION);

  add_int(state, module, "CERT_NONE", 0);
  add_int(state, module, "CERT_OPTIONAL", 1);
  add_int(state, module, "CERT_REQUIRED", 2);
  add_int(state, module, "SSL_ERROR_SSL", 1);
  add_int(state, module, "SSL_ERROR_WANT_READ", 2);
  add_int(state, module, "SSL_ERROR_WANT_WRITE", 3);
  add_int(state, module, "SSL_ERROR_WANT_X509_LOOKUP", 4);
  add_int(state, module, "SSL_ERROR_SYSCALL", 5);
  add_int(state, module, "SSL_ERROR_ZERO_RETURN", 6);
  add_int(state, module, "SSL_ERROR_WANT_CONNECT", 7);
  add_int(state, module, "SSL_ERROR_EOF", 8);
  add_int(state, module, "SSL_ERROR_INVALID_ERROR_CODE", 10);

  add_int(state, module, "OP_ALL", static_cast<int64_t>(SSL_OP_ALL));
  add_int(state, module, "OP_NO_SSLv2", static_cast<int64_t>(SSL_OP_NO_SSLv2));
  add_int(state, module, "OP_NO_SSLv3", static_cast<int64_t>(SSL_OP_NO_SSLv3));
  add_int(state, module, "OP_NO_TLSv1", static_cast<int64_t>(SSL_OP_NO_TLSv1));
  add_int(state, module, "OP_NO_TLSv1_1", static_cast<int64_t>(SSL_OP_NO_TLSv1_1));
  add_int(state, module, "OP_NO_TLSv1_2", static_cast<int64_t>(SSL_OP_NO_TLSv1_2));
  add_int(state, module, "OP_NO_TLSv1_3", static_cast<int64_t>(SSL_OP_NO_TLSv1_3));
  add_int(state, module, "OP_NO_COMPRESSION", static_cast<int64_t>(SSL_OP_NO_COMPRESSION));
  add_int(state, module, "OP_CIPHER_SERVER_PREFERENCE", static_cast<int64_t>(SSL_OP_CIPHER_SERVER_PREFERENCE));
  add_int(state, module, "OP_SINGLE_DH_USE", static_cast<int64_t>(SSL_OP_SINGLE_DH_USE));
  add_int(state, module, "OP_SINGLE_ECDH_USE", static_cast<int64_t>(SSL_OP_SINGLE_ECDH_USE));
  add_int(state, module, "OP_NO_TICKET", static_cast<int64_t>(SSL_OP_NO_TICKET));
  add_int(state, module, "OP_ENABLE_MIDDLEBOX_COMPAT", static_cast<int64_t>(SSL_OP_ENABLE_MIDDLEBOX_COMPAT));
  add_int(state, module, "OP_NO_RENEGOTIATION", static_cast<int64_t>(SSL_OP_NO_RENEGOTIATION));
  add_int(state, module, "OP_IGNORE_UNEXPECTED_EOF", static_cast<int64_t>(SSL_OP_IGNORE_UNEXPECTED_EOF));
  add_int(state, module, "OP_ENABLE_KTLS", static_cast<int64_t>(SSL_OP_ENABLE_KTLS));
  add_int(state, module, "OP_LEGACY_SERVER_CONNECT", static_cast<int64_t>(SSL_OP_LEGACY_SERVER_CONNECT));

  add_int(state, module, "VERIFY_DEFAULT", 0);
  add_int(state, module, "VERIFY_CRL_CHECK_LEAF", X509_V_FLAG_CRL_CHECK);
  add_int(state, module, "VERIFY_CRL_CHECK_CHAIN", X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
  add_int(state, module, "VERIFY_X509_STRICT", X509_V_FLAG_X509_STRICT);
  add_int(state, module, "VERIFY_ALLOW_PROXY_CERTS", X509_V_FLAG_ALLOW_PROXY_CERTS);
  add_int(state, module, "VERIFY_X509_TRUSTED_FIRST", X509_V_FLAG_TRUSTED_FIRST);
  add_int(state, module, "VERIFY_X509_PARTIAL_CHAIN", X509_V_FLAG_PARTIAL_CHAIN);

  const struct { const char* name; int value; } alerts[] = {
      {"CLOSE_NOTIFY", 0}, {"UNEXPECTED_MESSAGE", 10}, {"BAD_RECORD_MAC", 20},
      {"RECORD_OVERFLOW", 22}, {"DECOMPRESSION_FAILURE", 30}, {"HANDSHAKE_FAILURE", 40},
      {"BAD_CERTIFICATE", 42}, {"UNSUPPORTED_CERTIFICATE", 43}, {"CERTIFICATE_REVOKED", 44},
      {"CERTIFICATE_EXPIRED", 45}, {"CERTIFICATE_UNKNOWN", 46}, {"ILLEGAL_PARAMETER", 47},
      {"UNKNOWN_CA", 48}, {"ACCESS_DENIED", 49}, {"DECODE_ERROR", 50}, {"DECRYPT_ERROR", 51},
      {"PROTOCOL_VERSION", 70}, {"INSUFFICIENT_SECURITY", 71}, {"INTERNAL_ERROR", 80},
      {"USER_CANCELLED", 90}, {"NO_RENEGOTIATION", 100}, {"UNSUPPORTED_EXTENSION", 110},
      {"CERTIFICATE_UNOBTAINABLE", 111}, {"UNRECOGNIZED_NAME", 112},
      {"BAD_CERTIFICATE_STATUS_RESPONSE", 113}, {"BAD_CERTIFICATE_HASH_VALUE", 114},
      {"UNKNOWN_PSK_IDENTITY", 115}};
  for (const auto& alert : alerts) {
    const std::string name = std::string("ALERT_DESCRIPTION_") + alert.name;
    add_int(state, module, name.c_str(), alert.value);
  }
  add_int(state, module, "HOSTFLAG_ALWAYS_CHECK_SUBJECT", 1);
  add_int(state, module, "HOSTFLAG_NO_WILDCARDS", 2);
  add_int(state, module, "HOSTFLAG_NO_PARTIAL_WILDCARDS", 4);
  add_int(state, module, "HOSTFLAG_MULTI_LABEL_WILDCARDS", 8);
  add_int(state, module, "HOSTFLAG_SINGLE_LABEL_SUBDOMAINS", 16);
  add_int(state, module, "HOSTFLAG_NEVER_CHECK_SUBJECT", 32);
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState();
  state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) {
    delete state;
    return X3_STATUS_ERROR;
  }
  X3Module* module = nullptr;
  if (host->add_module(host, "_ssl", &module) != X3_STATUS_OK) return X3_STATUS_ERROR;

  X3Value os_error = x3_value_invalid();
  if (host->builtin_value(host, "OSError", &os_error) != X3_STATUS_OK ||
      !add_exception(state, module, "SSLError", os_error, &state->ssl_error) ||
      !add_exception(state, module, "SSLZeroReturnError", state->ssl_error, &state->ssl_zero_return_error) ||
      !add_exception(state, module, "SSLWantReadError", state->ssl_error, &state->ssl_want_read_error) ||
      !add_exception(state, module, "SSLWantWriteError", state->ssl_error, &state->ssl_want_write_error) ||
      !add_exception(state, module, "SSLSyscallError", state->ssl_error, &state->ssl_syscall_error) ||
      !add_exception(state, module, "SSLEOFError", state->ssl_error, &state->ssl_eof_error) ||
      !add_exception(state, module, "SSLCertVerificationError", state->ssl_error, &state->ssl_cert_verification_error)) {
    host->value_release(os_error);
    return X3_STATUS_ERROR;
  }
  host->value_release(os_error);

  X3NativeFunctionDef bio_methods[4]{};
  define_method(bio_methods[0], "__init__", memory_bio_init, state);
  define_method(bio_methods[1], "read", memory_bio_read, state);
  define_method(bio_methods[2], "write", memory_bio_write, state);
  define_method(bio_methods[3], "write_eof", memory_bio_write_eof, state);
  if (host->module_add_class(module, "MemoryBIO", bio_methods, 4, &state->memory_bio_class) != X3_STATUS_OK ||
      !add_property(state, host->runtime, state->memory_bio_class, "pending", memory_bio_pending) ||
      !add_property(state, host->runtime, state->memory_bio_class, "eof", memory_bio_eof)) return X3_STATUS_ERROR;

  X3NativeFunctionDef certificate_methods[2]{};
  define_method(certificate_methods[0], "public_bytes",
                certificate_public_bytes, state);
  define_method(certificate_methods[1], "get_info", certificate_get_info, state);
  if (host->module_add_class(module, "Certificate", certificate_methods, 2,
                             &state->certificate_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;

  X3NativeFunctionDef socket_methods[15]{};
  define_method(socket_methods[0], "do_handshake", ssl_socket_handshake, state);
  define_method(socket_methods[1], "write", ssl_socket_write, state);
  define_method(socket_methods[2], "read", ssl_socket_read, state);
  define_method(socket_methods[3], "pending", ssl_socket_pending, state);
  define_method(socket_methods[4], "version", ssl_socket_version, state);
  define_method(socket_methods[5], "cipher", ssl_socket_cipher, state);
  define_method(socket_methods[6], "selected_alpn_protocol", ssl_socket_selected_alpn, state);
  define_method(socket_methods[7], "compression", ssl_socket_compression, state);
  define_method(socket_methods[8], "shutdown", ssl_socket_shutdown, state);
  define_method(socket_methods[9], "getpeercert", ssl_socket_getpeercert, state);
  define_method(socket_methods[10], "get_channel_binding",
                ssl_socket_channel_binding, state);
  define_method(socket_methods[11], "shared_ciphers",
                ssl_socket_shared_ciphers, state);
  define_method(socket_methods[12], "get_verified_chain",
                ssl_socket_get_verified_chain, state);
  define_method(socket_methods[13], "get_unverified_chain",
                ssl_socket_get_unverified_chain, state);
  define_method(socket_methods[14], "verify_client_post_handshake",
                ssl_socket_verify_client_post_handshake, state);
  if (host->module_add_class(module, "_SSLSocket", socket_methods, 15,
                             &state->ssl_socket_class) != X3_STATUS_OK ||
      !add_property(state, host->runtime, state->ssl_socket_class, "context",
                    ssl_socket_context_get, ssl_socket_context_set) ||
      !add_property(state, host->runtime, state->ssl_socket_class, "server_side", ssl_socket_server_side_get) ||
      !add_property(state, host->runtime, state->ssl_socket_class, "owner", ssl_socket_owner_get) ||
      !add_property(state, host->runtime, state->ssl_socket_class, "server_hostname", ssl_socket_server_hostname_get) ||
      !add_property(state, host->runtime, state->ssl_socket_class, "session_reused", ssl_socket_session_reused_get) ||
      !add_property(state, host->runtime, state->ssl_socket_class, "session",
                    ssl_socket_session_get, ssl_socket_session_set))
    return X3_STATUS_ERROR;

  X3NativeFunctionDef context_methods[16]{};
  define_method(context_methods[0], "__new__", ssl_context_new, state);
  define_method(context_methods[1], "set_ciphers", ssl_context_set_ciphers, state);
  define_method(context_methods[2], "set_default_verify_paths", ssl_context_set_default_verify_paths, state);
  define_method(context_methods[3], "_wrap_bio", ssl_context_wrap_bio, state, ssl_context_wrap_bio_kw);
  define_method(context_methods[4], "_set_alpn_protocols", ssl_context_set_alpn, state);
  define_method(context_methods[5], "load_cert_chain", ssl_context_load_cert_chain, state, ssl_context_load_cert_chain_kw);
  define_method(context_methods[6], "load_verify_locations", ssl_context_load_verify_locations, state, ssl_context_load_verify_locations_kw);
  define_method(context_methods[7], "get_ciphers", ssl_context_get_ciphers, state);
  define_method(context_methods[8], "cert_store_stats", ssl_context_cert_store_stats, state);
  define_method(context_methods[9], "session_stats", ssl_context_session_stats, state);
  define_method(context_methods[10], "_wrap_socket", ssl_context_wrap_socket, state, ssl_context_wrap_socket_kw);
  define_method(context_methods[11], "get_ca_certs", ssl_context_get_ca_certs, state);
  define_method(context_methods[12], "load_dh_params", ssl_context_load_dh_params, state);
  define_method(context_methods[13], "set_ecdh_curve", ssl_context_set_ecdh_curve, state);
  define_method(context_methods[14], "set_psk_client_callback",
                ssl_context_set_psk_client_callback, state);
  define_method(context_methods[15], "set_psk_server_callback",
                ssl_context_set_psk_server_callback, state);
  if (host->module_add_class(module, "_SSLContext", context_methods, 16, &state->ssl_context_class) != X3_STATUS_OK ||
      !add_property(state, host->runtime, state->ssl_context_class, "protocol", ssl_context_protocol) ||
      !add_property(state, host->runtime, state->ssl_context_class, "check_hostname", ssl_context_check_hostname_get, ssl_context_check_hostname_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "verify_mode", ssl_context_verify_mode_get, ssl_context_verify_mode_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "verify_flags", ssl_context_verify_flags_get, ssl_context_verify_flags_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "_host_flags", ssl_context_host_flags_get, ssl_context_host_flags_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "minimum_version", ssl_context_minimum_version_get, ssl_context_minimum_version_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "maximum_version", ssl_context_maximum_version_get, ssl_context_maximum_version_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "security_level", ssl_context_security_level_get) ||
      !add_property(state, host->runtime, state->ssl_context_class, "num_tickets", ssl_context_num_tickets_get, ssl_context_num_tickets_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "post_handshake_auth", ssl_context_post_handshake_auth_get, ssl_context_post_handshake_auth_set) ||
      !add_property(state, host->runtime, state->ssl_context_class, "options", ssl_context_options_get, ssl_context_options_set) ||
      !add_property(state, host->runtime, state->ssl_context_class,
                    "sni_callback", ssl_context_sni_callback_get,
                    ssl_context_sni_callback_set) ||
      !add_property(state, host->runtime, state->ssl_context_class,
                    "_msg_callback", ssl_context_message_callback_get,
                    ssl_context_message_callback_set) ||
      !add_property(state, host->runtime, state->ssl_context_class,
                    "keylog_filename", ssl_context_keylog_filename_get,
                    ssl_context_keylog_filename_set)) return X3_STATUS_ERROR;
  X3NativeFunctionDef session_methods[1]{};
  define_method(session_methods[0], "__init__", ssl_session_init, state);
  if (host->module_add_class(module, "SSLSession", session_methods, 1,
                             &state->ssl_session_class) != X3_STATUS_OK ||
      !add_property(state, host->runtime, state->ssl_session_class, "id", ssl_session_id) ||
      !add_property(state, host->runtime, state->ssl_session_class, "time", ssl_session_time) ||
      !add_property(state, host->runtime, state->ssl_session_class, "timeout", ssl_session_timeout) ||
      !add_property(state, host->runtime, state->ssl_session_class,
                    "ticket_lifetime_hint", ssl_session_ticket_lifetime_hint) ||
      !add_property(state, host->runtime, state->ssl_session_class,
                    "has_ticket", ssl_session_has_ticket))
    return X3_STATUS_ERROR;

  add_function(state, module, "RAND_status", rand_status);
  add_function(state, module, "RAND_bytes", rand_bytes);
  add_function(state, module, "RAND_add", rand_add);
  add_function(state, module, "txt2obj", txt2obj, txt2obj_kw);
  add_function(state, module, "nid2obj", nid2obj);
  add_function(state, module, "get_default_verify_paths", get_default_verify_paths);
  add_function(state, module, "_test_decode_cert", test_decode_cert);
#if defined(_WIN32)
  add_function(state, module, "enum_certificates", enum_certificates);
  add_function(state, module, "enum_crls", enum_crls);
#endif
  add_constants(state, host->runtime, module);
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (host == nullptr || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "xlang__ssl");
  host->package_set_metadata(host, "version", "0.1.0");
  return register_module(host);
}
