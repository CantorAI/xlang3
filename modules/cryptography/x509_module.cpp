/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/ct.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kModuleName = "cryptography.hazmat.bindings._rust.x509";
constexpr const char* kCertificateType =
    "cryptography.hazmat.bindings._rust.x509.Certificate";
constexpr const char* kCsrType =
    "cryptography.hazmat.bindings._rust.x509.CertificateSigningRequest";
constexpr const char* kCrlType =
    "cryptography.hazmat.bindings._rust.x509.CertificateRevocationList";
constexpr const char* kRevokedType =
    "cryptography.hazmat.bindings._rust.x509.RevokedCertificate";
constexpr const char* kSctType =
    "cryptography.hazmat.bindings._rust.x509.Sct";
constexpr const char* kStoreType =
    "cryptography.hazmat.bindings._rust.x509.Store";
constexpr const char* kPolicyBuilderType =
    "cryptography.hazmat.bindings._rust.x509.PolicyBuilder";
constexpr const char* kPolicyType =
    "cryptography.hazmat.bindings._rust.x509.Policy";
constexpr const char* kServerVerifierType =
    "cryptography.hazmat.bindings._rust.x509.ServerVerifier";
constexpr const char* kClientVerifierType =
    "cryptography.hazmat.bindings._rust.x509.ClientVerifier";
constexpr const char* kVerifiedClientType =
    "cryptography.hazmat.bindings._rust.x509.VerifiedClient";
constexpr const char* kExtensionPolicyType =
    "cryptography.hazmat.bindings._rust.x509.ExtensionPolicy";
constexpr const char* kCriticalityType =
    "cryptography.hazmat.bindings._rust.x509.Criticality";

enum class Presence { Maybe, Required, Forbidden };
enum class Criticality { Agnostic, Critical, NonCritical };
struct ExtensionRule {
  const char* oid = nullptr;
  int nid = 0;
  Presence presence = Presence::Maybe;
  Criticality criticality = Criticality::Agnostic;
  bool configured = false;
};
struct ExtensionPolicyData {
  std::array<ExtensionRule, 8> rules;
};
struct CriticalityData { Criticality value; };

struct CertificateData {
  X509* certificate = nullptr;
  ~CertificateData() { X509_free(certificate); }
};

struct CsrData {
  X509_REQ* request = nullptr;
  ~CsrData() { X509_REQ_free(request); }
};

struct CrlData {
  X509_CRL* list = nullptr;
  ~CrlData() { X509_CRL_free(list); }
};

struct RevokedData {
  X509_REVOKED* entry = nullptr;
  ~RevokedData() { X509_REVOKED_free(entry); }
};

X3Status import_class(CryptographyNativeState* state, X3Runtime* runtime,
                      const char* module_path, const char* class_name,
                      X3Value* result);

struct SctData {
  SCT* sct = nullptr;
  ~SctData() { SCT_free(sct); }
};

struct StoreData {
  X509_STORE* store = nullptr;
  ~StoreData() { X509_STORE_free(store); }
};

struct PolicyBuilderData {
  X3PackageHost* host = nullptr;
  X3Value store = x3_value_invalid();
  int64_t validation_seconds = 0;
  bool has_time = false;
  uint32_t max_depth = 8;
  bool has_max_depth = false;
  X3Value ca_policy = x3_value_invalid();
  X3Value ee_policy = x3_value_invalid();
  ~PolicyBuilderData() {
    if (store.tag != X3_TAG_INVALID) host->value_release(store);
    if (ca_policy.tag != X3_TAG_INVALID) host->value_release(ca_policy);
    if (ee_policy.tag != X3_TAG_INVALID) host->value_release(ee_policy);
  }
};

struct PolicyData {
  X3PackageHost* host = nullptr;
  X3Value subject = x3_value_invalid();
  int64_t validation_seconds = 0;
  uint32_t max_depth = 8;
  bool client = false;
  X3Value ca_policy = x3_value_invalid();
  X3Value ee_policy = x3_value_invalid();
  ~PolicyData() {
    if (subject.tag != X3_TAG_INVALID) host->value_release(subject);
    if (ca_policy.tag != X3_TAG_INVALID) host->value_release(ca_policy);
    if (ee_policy.tag != X3_TAG_INVALID) host->value_release(ee_policy);
  }
};

struct ServerVerifierData {
  X3PackageHost* host = nullptr;
  X3Value store = x3_value_invalid();
  X3Value policy = x3_value_invalid();
  ~ServerVerifierData() {
    if (store.tag != X3_TAG_INVALID) host->value_release(store);
    if (policy.tag != X3_TAG_INVALID) host->value_release(policy);
  }
};

struct ClientVerifierData {
  X3PackageHost* host = nullptr;
  X3Value store = x3_value_invalid();
  X3Value policy = x3_value_invalid();
  ~ClientVerifierData() {
    if (store.tag != X3_TAG_INVALID) host->value_release(store);
    if (policy.tag != X3_TAG_INVALID) host->value_release(policy);
  }
};

struct VerifiedClientData {
  X3PackageHost* host = nullptr;
  X3Value chain = x3_value_invalid();
  X3Value subjects = x3_value_invalid();
  ~VerifiedClientData() {
    if (chain.tag != X3_TAG_INVALID) host->value_release(chain);
    if (subjects.tag != X3_TAG_INVALID) host->value_release(subjects);
  }
};

void cleanup_certificate(void* data) {
  delete static_cast<CertificateData*>(data);
}
void cleanup_csr(void* data) { delete static_cast<CsrData*>(data); }
void cleanup_crl(void* data) { delete static_cast<CrlData*>(data); }
void cleanup_revoked(void* data) { delete static_cast<RevokedData*>(data); }
void cleanup_sct(void* data) { delete static_cast<SctData*>(data); }
void cleanup_store(void* data) { delete static_cast<StoreData*>(data); }
void cleanup_policy_builder(void* data) {
  delete static_cast<PolicyBuilderData*>(data);
}
void cleanup_policy(void* data) { delete static_cast<PolicyData*>(data); }
void cleanup_server_verifier(void* data) {
  delete static_cast<ServerVerifierData*>(data);
}
void cleanup_client_verifier(void* data) {
  delete static_cast<ClientVerifierData*>(data);
}
void cleanup_verified_client(void* data) {
  delete static_cast<VerifiedClientData*>(data);
}
void cleanup_extension_policy(void* data) {
  delete static_cast<ExtensionPolicyData*>(data);
}
void cleanup_criticality(void* data) {
  delete static_cast<CriticalityData*>(data);
}

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

CertificateData* certificate_data(CryptographyNativeState* state,
                                  X3CallContext* call, X3Value object) {
  auto* data = static_cast<CertificateData*>(
      state->host->instance_get_native_data(object, kCertificateType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected X.509 certificate");
  return data;
}

X3Status certificate_init(X3CallContext* call, X3Runtime*, void* user_data,
                          const X3Value*, uint32_t, X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return fail(state, call, "TypeError",
              "cannot create 'cryptography.hazmat.bindings._rust.x509.Certificate' instances");
}

X3Status make_certificate(CryptographyNativeState* state, X3Runtime* runtime,
                          X509* certificate, X3Value* result) {
  auto data = std::make_unique<CertificateData>();
  data->certificate = certificate;
  X3Value instance = state->host->value_instance(
      runtime, state->x509_certificate_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kCertificateType, data.get(), cleanup_certificate) !=
      X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X509_NAME* encode_name(CryptographyNativeState* state, X3CallContext* call,
                       X3Runtime* runtime, X3Value name_object) {
  std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)> name(
      X509_NAME_new(), X509_NAME_free);
  if (!name) return nullptr;
  X3Value rdns = x3_value_invalid();
  if (state->host->get_attr(runtime, name_object, "rdns", &rdns) != X3_STATUS_OK)
    return nullptr;
  uint64_t rdn_count = 0;
  X3Status status = state->host->len(runtime, rdns, &rdn_count);
  for (uint64_t r = 0; status == X3_STATUS_OK && r < rdn_count; ++r) {
    X3Value rdn = x3_value_invalid();
    X3Value attributes = x3_value_invalid();
    status = state->host->get_item(runtime, rdns, x3_value_int64(r), &rdn);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, rdn, "_attributes", &attributes);
    uint64_t attr_count = 0;
    if (status == X3_STATUS_OK)
      status = state->host->len(runtime, attributes, &attr_count);
    std::vector<std::pair<std::vector<unsigned char>, X509_NAME_ENTRY*>> entries;
    for (uint64_t a = 0; status == X3_STATUS_OK && a < attr_count; ++a) {
      X3Value attribute = x3_value_invalid();
      X3Value oid_value = x3_value_invalid();
      X3Value dotted = x3_value_invalid();
      X3Value raw_value = x3_value_invalid();
      X3Value type_value = x3_value_invalid();
      X3Value tag_value = x3_value_invalid();
      X3Value encoded = x3_value_invalid();
      X3Value encoder = x3_value_invalid();
      X3Buffer* buffer = nullptr;
      status = state->host->get_item(runtime, attributes,
                                     x3_value_int64(a), &attribute);
      if (status == X3_STATUS_OK)
        status = state->host->get_attr(runtime, attribute, "oid", &oid_value);
      if (status == X3_STATUS_OK)
        status = state->host->get_attr(runtime, oid_value, "dotted_string", &dotted);
      if (status == X3_STATUS_OK)
        status = state->host->get_attr(runtime, attribute, "value", &raw_value);
      if (status == X3_STATUS_OK)
        status = state->host->get_attr(runtime, attribute, "_type", &type_value);
      if (status == X3_STATUS_OK)
        status = state->host->get_attr(runtime, type_value, "value", &tag_value);
      const int64_t tag = tag_value.tag == X3_TAG_INT64
          ? tag_value.as.i64 : -1;
      const char* codec = tag == V_ASN1_BMPSTRING ? "utf_16_be"
          : tag == V_ASN1_UNIVERSALSTRING ? "utf_32_be" : "utf8";
      if (status == X3_STATUS_OK && tag != V_ASN1_BIT_STRING) {
        status = state->host->get_attr(runtime, raw_value, "encode", &encoder);
        if (status == X3_STATUS_OK) {
          X3Value encoding = state->host->value_string(runtime, codec);
          status = state->host->call(runtime, encoder, &encoding, 1, &encoded);
          state->host->value_release(encoding);
        }
      } else if (status == X3_STATUS_OK) {
        encoded = raw_value;
        state->host->value_retain(encoded);
      }
      X3BufferInfo bytes{};
      if (status == X3_STATUS_OK)
        status = state->host->buffer_acquire(runtime, encoded, 0, &buffer,
                                             &bytes);
      if (status == X3_STATUS_OK && bytes.size <= INT_MAX) {
        const char* oid_text = state->host->value_to_cstr(runtime, dotted);
        std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> oid(
            oid_text == nullptr ? nullptr : OBJ_txt2obj(oid_text, 1),
            ASN1_OBJECT_free);
        X509_NAME_ENTRY* entry = oid ? X509_NAME_ENTRY_create_by_OBJ(
            nullptr, oid.get(), static_cast<int>(tag),
            static_cast<const unsigned char*>(bytes.data),
            static_cast<int>(bytes.size)) : nullptr;
        unsigned char* entry_der = nullptr;
        const int der_length = entry == nullptr ? -1
            : i2d_X509_NAME_ENTRY(entry, &entry_der);
        if (der_length < 0) {
          X509_NAME_ENTRY_free(entry);
          status = X3_STATUS_ERROR;
        } else {
          entries.emplace_back(
              std::vector<unsigned char>(entry_der, entry_der + der_length),
              entry);
        }
        OPENSSL_free(entry_der);
      } else if (status == X3_STATUS_OK) {
        status = X3_STATUS_ERROR;
      }
      if (buffer != nullptr) state->host->buffer_release(buffer);
      for (X3Value value : {encoder, encoded, tag_value, type_value,
                            raw_value, dotted, oid_value, attribute})
        if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
    }
    if (status == X3_STATUS_OK) {
      std::sort(entries.begin(), entries.end(),
                [](const auto& left, const auto& right) {
                  return left.first < right.first;
                });
      for (size_t a = 0; status == X3_STATUS_OK && a < entries.size(); ++a)
        if (X509_NAME_add_entry(name.get(), entries[a].second, -1,
                                a == 0 ? 1 : -1) != 1)
          status = X3_STATUS_ERROR;
    }
    for (const auto& entry : entries) X509_NAME_ENTRY_free(entry.second);
    if (attributes.tag != X3_TAG_INVALID) state->host->value_release(attributes);
    if (rdn.tag != X3_TAG_INVALID) state->host->value_release(rdn);
  }
  state->host->value_release(rdns);
  if (status != X3_STATUS_OK) {
    fail(state, call, "ValueError", "Unable to encode X.509 name");
    return nullptr;
  }
  return name.release();
}

X3Status encode_name_bytes(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "name is required");
  std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)> name(
      encode_name(state, call, runtime, args[0]), X509_NAME_free);
  if (!name) return X3_STATUS_ERROR;
  unsigned char* der = nullptr;
  const int length = i2d_X509_NAME(name.get(), &der);
  if (length < 0) return fail(state, call, "ValueError", "Unable to encode X.509 name");
  *result = state->host->value_bytes(runtime, der, static_cast<uint64_t>(length));
  OPENSSL_free(der);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

bool extension_bool(CryptographyNativeState* state, X3Runtime* runtime,
                    X3Value extension, const char* property, bool* result) {
  X3Value value = x3_value_invalid();
  if (state->host->get_attr(runtime, extension, property, &value) != X3_STATUS_OK)
    return false;
  const bool valid = value.tag == X3_TAG_BOOL;
  if (valid) *result = value.as.b != 0;
  state->host->value_release(value);
  return valid;
}

std::string python_class_name(CryptographyNativeState* state,
                              X3Runtime* runtime, X3Value object) {
  X3Value klass = x3_value_invalid();
  X3Value name = x3_value_invalid();
  std::string text;
  if (state->host->get_attr(runtime, object, "__class__", &klass) ==
          X3_STATUS_OK &&
      state->host->get_attr(runtime, klass, "__name__", &name) ==
          X3_STATUS_OK) {
    const char* raw = state->host->value_to_cstr(runtime, name);
    if (raw) text = raw;
  }
  if (name.tag != X3_TAG_INVALID) state->host->value_release(name);
  if (klass.tag != X3_TAG_INVALID) state->host->value_release(klass);
  return text;
}

GENERAL_NAMES* encode_general_names(CryptographyNativeState* state,
                                    X3CallContext* call, X3Runtime* runtime,
                                    X3Value names) {
  std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> encoded(
      sk_GENERAL_NAME_new_null(), GENERAL_NAMES_free);
  if (!encoded) return nullptr;
  uint64_t count = 0;
  if (state->host->len(runtime, names, &count) != X3_STATUS_OK)
    return nullptr;
  for (uint64_t i = 0; i < count; ++i) {
    X3Value item = x3_value_invalid();
    X3Value value = x3_value_invalid();
    X3Status status = state->host->get_item(runtime, names,
                                            x3_value_int64(i), &item);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, item, "value", &value);
    const std::string kind = status == X3_STATUS_OK
        ? python_class_name(state, runtime, item) : "";
    std::unique_ptr<GENERAL_NAME, decltype(&GENERAL_NAME_free)> general(
        GENERAL_NAME_new(), GENERAL_NAME_free);
    if (status == X3_STATUS_OK && general &&
        (kind == "DNSName" || kind == "RFC822Name")) {
      const char* text = state->host->value_to_cstr(runtime, value);
      if (text && std::strlen(text) <= INT_MAX) {
        general->type = kind == "DNSName" ? GEN_DNS : GEN_EMAIL;
        general->d.ia5 = ASN1_IA5STRING_new();
        if (!general->d.ia5 || ASN1_STRING_set(
                general->d.ia5, text, static_cast<int>(std::strlen(text))) != 1)
          status = X3_STATUS_ERROR;
      } else {
        status = X3_STATUS_ERROR;
      }
    } else if (status == X3_STATUS_OK && general && kind == "IPAddress") {
      const std::string address_kind = python_class_name(state, runtime, value);
      const bool network = address_kind == "IPv4Network" ||
                           address_kind == "IPv6Network";
      X3Value address = x3_value_invalid();
      X3Value mask = x3_value_invalid();
      X3Value address_bytes = x3_value_invalid();
      X3Value mask_bytes = x3_value_invalid();
      if (network)
        status = state->host->get_attr(runtime, value, "network_address",
                                       &address);
      else {
        address = value;
        state->host->value_retain(address);
      }
      if (status == X3_STATUS_OK)
        status = state->host->get_attr(runtime, address, "packed",
                                       &address_bytes);
      if (status == X3_STATUS_OK && network)
        status = state->host->get_attr(runtime, value, "netmask", &mask);
      if (status == X3_STATUS_OK && network)
        status = state->host->get_attr(runtime, mask, "packed", &mask_bytes);
      const void* address_data = nullptr;
      const void* mask_data = nullptr;
      uint64_t address_size = 0, mask_size = 0;
      if (status == X3_STATUS_OK)
        status = state->host->value_bytes_data(runtime, address_bytes,
                                               &address_data, &address_size);
      if (status == X3_STATUS_OK && network)
        status = state->host->value_bytes_data(runtime, mask_bytes,
                                               &mask_data, &mask_size);
      if (status == X3_STATUS_OK &&
          (address_size == 4 || address_size == 16) &&
          (!network || mask_size == address_size)) {
        std::vector<unsigned char> packed(
            static_cast<const unsigned char*>(address_data),
            static_cast<const unsigned char*>(address_data) + address_size);
        if (network)
          packed.insert(packed.end(),
              static_cast<const unsigned char*>(mask_data),
              static_cast<const unsigned char*>(mask_data) + mask_size);
        general->type = GEN_IPADD;
        general->d.ip = ASN1_OCTET_STRING_new();
        if (!general->d.ip || ASN1_OCTET_STRING_set(
                general->d.ip, packed.data(), static_cast<int>(packed.size())) != 1)
          status = X3_STATUS_ERROR;
      } else {
        status = X3_STATUS_ERROR;
      }
      for (X3Value temporary : {mask_bytes, address_bytes, mask, address})
        if (temporary.tag != X3_TAG_INVALID)
          state->host->value_release(temporary);
    } else if (status == X3_STATUS_OK) {
      status = fail(state, call, "NotImplementedError",
                    "native general-name encoding is unavailable for this type");
    }
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
    if (status != X3_STATUS_OK ||
        sk_GENERAL_NAME_push(encoded.get(), general.get()) == 0)
      return nullptr;
    general.release();
  }
  return encoded.release();
}

X3Status encode_extension_value(X3CallContext* call, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "extension value is required");
  X3Value oid_object = x3_value_invalid();
  X3Value dotted = x3_value_invalid();
  X3Status status = state->host->get_attr(runtime, args[0], "oid", &oid_object);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, oid_object, "dotted_string", &dotted);
  if (status != X3_STATUS_OK) {
    if (oid_object.tag != X3_TAG_INVALID) state->host->value_release(oid_object);
    return status;
  }
  const char* text = state->host->value_to_cstr(runtime, dotted);
  const int nid = text == nullptr ? NID_undef : OBJ_txt2nid(text);
  state->host->value_release(dotted);
  state->host->value_release(oid_object);
  std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> encoded(
      nullptr, X509_EXTENSION_free);
  if (nid == NID_basic_constraints) {
    std::unique_ptr<BASIC_CONSTRAINTS, decltype(&BASIC_CONSTRAINTS_free)> basic(
        BASIC_CONSTRAINTS_new(), BASIC_CONSTRAINTS_free);
    bool ca = false;
    if (!basic || !extension_bool(state, runtime, args[0], "ca", &ca))
      return X3_STATUS_ERROR;
    basic->ca = ca ? 0xff : 0;
    X3Value path_length = x3_value_invalid();
    status = state->host->get_attr(runtime, args[0], "path_length",
                                   &path_length);
    if (status != X3_STATUS_OK) return status;
    if (path_length.tag != X3_TAG_NONE) {
      if (path_length.tag != X3_TAG_INT64 || path_length.as.i64 < 0) {
        state->host->value_release(path_length);
        return fail(state, call, "ValueError", "invalid path length");
      }
      basic->pathlen = ASN1_INTEGER_new();
      if (!basic->pathlen ||
          ASN1_INTEGER_set_int64(basic->pathlen, path_length.as.i64) != 1) {
        state->host->value_release(path_length);
        return X3_STATUS_ERROR;
      }
    }
    state->host->value_release(path_length);
    encoded.reset(X509V3_EXT_i2d(nid, 0, basic.get()));
  } else if (nid == NID_subject_key_identifier) {
    X3Value digest = x3_value_invalid();
    status = state->host->get_attr(runtime, args[0], "digest", &digest);
    if (status != X3_STATUS_OK) return status;
    const void* data = nullptr;
    uint64_t length = 0;
    status = state->host->value_bytes_data(runtime, digest, &data, &length);
    std::unique_ptr<ASN1_OCTET_STRING,
                    decltype(&ASN1_OCTET_STRING_free)> keyid(
        ASN1_OCTET_STRING_new(), ASN1_OCTET_STRING_free);
    if (status == X3_STATUS_OK && keyid && length <= INT_MAX &&
        ASN1_OCTET_STRING_set(keyid.get(),
                              static_cast<const unsigned char*>(data),
                              static_cast<int>(length)) == 1)
      encoded.reset(X509V3_EXT_i2d(nid, 0, keyid.get()));
    state->host->value_release(digest);
  } else if (nid == NID_key_usage) {
    static constexpr const char* kProperties[] = {
        "digital_signature", "content_commitment", "key_encipherment",
        "data_encipherment", "key_agreement", "key_cert_sign",
        "crl_sign", "encipher_only", "decipher_only"};
    std::unique_ptr<ASN1_BIT_STRING, decltype(&ASN1_BIT_STRING_free)> bits(
        ASN1_BIT_STRING_new(), ASN1_BIT_STRING_free);
    if (!bits) return X3_STATUS_ERROR;
    bool agreement = false;
    for (int index = 0; index < 9; ++index) {
      if (index >= 7 && !agreement) continue;
      bool enabled = false;
      if (!extension_bool(state, runtime, args[0], kProperties[index],
                          &enabled))
        return X3_STATUS_ERROR;
      if (index == 4) agreement = enabled;
      if (enabled && ASN1_BIT_STRING_set_bit(bits.get(), index, 1) != 1)
        return X3_STATUS_ERROR;
    }
    encoded.reset(X509V3_EXT_i2d(nid, 0, bits.get()));
  } else if (nid == NID_authority_key_identifier) {
    X3Value identifier = x3_value_invalid();
    X3Value issuer = x3_value_invalid();
    X3Value serial = x3_value_invalid();
    status = state->host->get_attr(runtime, args[0], "key_identifier",
                                   &identifier);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, args[0],
                                     "authority_cert_issuer", &issuer);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, args[0],
                                     "authority_cert_serial_number", &serial);
    if (status != X3_STATUS_OK || issuer.tag != X3_TAG_NONE ||
        serial.tag != X3_TAG_NONE) {
      for (X3Value value : {identifier, issuer, serial})
        if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
      return status == X3_STATUS_OK
          ? fail(state, call, "NotImplementedError",
                 "authority certificate issuer and serial encoding are unavailable")
          : status;
    }
    std::unique_ptr<AUTHORITY_KEYID, decltype(&AUTHORITY_KEYID_free)> aki(
        AUTHORITY_KEYID_new(), AUTHORITY_KEYID_free);
    if (aki && identifier.tag != X3_TAG_NONE) {
      const void* data = nullptr;
      uint64_t length = 0;
      status = state->host->value_bytes_data(runtime, identifier,
                                              &data, &length);
      if (status == X3_STATUS_OK && length <= INT_MAX) {
        aki->keyid = ASN1_OCTET_STRING_new();
        if (!aki->keyid || ASN1_OCTET_STRING_set(
                aki->keyid, static_cast<const unsigned char*>(data),
                static_cast<int>(length)) != 1)
          status = X3_STATUS_ERROR;
      } else {
        status = X3_STATUS_ERROR;
      }
    }
    for (X3Value value : {identifier, issuer, serial})
      if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
    if (status != X3_STATUS_OK || !aki) return X3_STATUS_ERROR;
    encoded.reset(X509V3_EXT_i2d(nid, 0, aki.get()));
  } else if (nid == NID_ext_key_usage) {
    std::unique_ptr<EXTENDED_KEY_USAGE,
                    decltype(&EXTENDED_KEY_USAGE_free)> usages(
        sk_ASN1_OBJECT_new_null(), EXTENDED_KEY_USAGE_free);
    if (!usages) return X3_STATUS_ERROR;
    uint64_t count = 0;
    status = state->host->len(runtime, args[0], &count);
    for (uint64_t i = 0; status == X3_STATUS_OK && i < count; ++i) {
      X3Value oid_value = x3_value_invalid();
      X3Value dotted_value = x3_value_invalid();
      status = state->host->get_item(runtime, args[0], x3_value_int64(i),
                                     &oid_value);
      if (status == X3_STATUS_OK)
        status = state->host->get_attr(runtime, oid_value,
                                       "dotted_string", &dotted_value);
      if (status == X3_STATUS_OK) {
        const char* dotted = state->host->value_to_cstr(runtime, dotted_value);
        ASN1_OBJECT* oid = dotted == nullptr ? nullptr : OBJ_txt2obj(dotted, 1);
        if (!oid || sk_ASN1_OBJECT_push(usages.get(), oid) == 0) {
          ASN1_OBJECT_free(oid);
          status = X3_STATUS_ERROR;
        }
      }
      if (dotted_value.tag != X3_TAG_INVALID)
        state->host->value_release(dotted_value);
      if (oid_value.tag != X3_TAG_INVALID)
        state->host->value_release(oid_value);
    }
    if (status != X3_STATUS_OK) return status;
    encoded.reset(X509V3_EXT_i2d(nid, 0, usages.get()));
  } else if (nid == NID_subject_alt_name) {
    std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> names(
        encode_general_names(state, call, runtime, args[0]),
        GENERAL_NAMES_free);
    if (!names) return X3_STATUS_ERROR;
    encoded.reset(X509V3_EXT_i2d(nid, 0, names.get()));
  } else {
    return fail(state, call, "NotImplementedError",
                "native X.509 extension encoding is unavailable for this OID");
  }
  if (!encoded)
    return fail(state, call, "ValueError", "Unable to encode X.509 extension");
  ASN1_OCTET_STRING* bytes = X509_EXTENSION_get_data(encoded.get());
  if (!bytes) return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, ASN1_STRING_get0_data(bytes),
                                      ASN1_STRING_length(bytes));
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status serialize_builder_key(CryptographyNativeState* state,
                               X3Runtime* runtime, X3Value key,
                               bool private_key, X3Value* result) {
  X3Value method = x3_value_invalid();
  X3Value encoding_class = x3_value_invalid();
  X3Value encoding = x3_value_invalid();
  X3Value format_class = x3_value_invalid();
  X3Value format = x3_value_invalid();
  X3Value encryption_class = x3_value_invalid();
  X3Value encryption = x3_value_invalid();
  const char* serialization = "cryptography.hazmat.primitives.serialization";
  X3Status status = state->host->get_attr(
      runtime, key, private_key ? "private_bytes" : "public_bytes", &method);
  if (status == X3_STATUS_OK)
    status = import_class(state, runtime, serialization, "Encoding",
                           &encoding_class);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, encoding_class, "DER", &encoding);
  if (status == X3_STATUS_OK)
    status = import_class(state, runtime, serialization,
        private_key ? "PrivateFormat" : "PublicFormat", &format_class);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, format_class,
        private_key ? "PKCS8" : "SubjectPublicKeyInfo", &format);
  if (status == X3_STATUS_OK && private_key)
    status = import_class(state, runtime, serialization, "NoEncryption",
                           &encryption_class);
  if (status == X3_STATUS_OK && private_key)
    status = state->host->call(runtime, encryption_class, nullptr, 0,
                               &encryption);
  if (status == X3_STATUS_OK) {
    const X3Value arguments[] = {encoding, format, encryption};
    status = state->host->call(runtime, method, arguments,
                               private_key ? 3 : 2, result);
  }
  for (X3Value value : {encryption, encryption_class, format,
                        format_class, encoding, encoding_class, method})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

X3Status certificate_time(CryptographyNativeState* state, X3Runtime* runtime,
                          X3Value datetime, ASN1_TIME* target) {
  const char* fields[] = {"year", "month", "day", "hour", "minute", "second"};
  int64_t values[6]{};
  for (int i = 0; i < 6; ++i) {
    X3Value value = x3_value_invalid();
    const X3Status status = state->host->get_attr(runtime, datetime,
                                                  fields[i], &value);
    if (status != X3_STATUS_OK) return status;
    if (value.tag != X3_TAG_INT64) {
      state->host->value_release(value);
      return X3_STATUS_ERROR;
    }
    values[i] = value.as.i64;
    state->host->value_release(value);
  }
  char utc[20]{};
  std::snprintf(utc, sizeof(utc), "%04lld%02lld%02lld%02lld%02lld%02lldZ",
      static_cast<long long>(values[0]), static_cast<long long>(values[1]),
      static_cast<long long>(values[2]), static_cast<long long>(values[3]),
      static_cast<long long>(values[4]), static_cast<long long>(values[5]));
  return ASN1_TIME_set_string_X509(target, utc) == 1
      ? X3_STATUS_OK : X3_STATUS_ERROR;
}

X3Status add_builder_extensions(CryptographyNativeState* state,
                                X3CallContext* call, X3Runtime* runtime,
                                X3Value builder, X509* certificate) {
  X3Value extensions = x3_value_invalid();
  X3Status status = state->host->get_attr(runtime, builder, "_extensions",
                                          &extensions);
  if (status != X3_STATUS_OK) return status;
  bool native_failure = false;
  uint64_t count = 0;
  status = state->host->len(runtime, extensions, &count);
  for (uint64_t i = 0; status == X3_STATUS_OK && i < count; ++i) {
    X3Value extension = x3_value_invalid();
    X3Value oid_value = x3_value_invalid();
    X3Value dotted = x3_value_invalid();
    X3Value critical = x3_value_invalid();
    X3Value value = x3_value_invalid();
    X3Value public_bytes = x3_value_invalid();
    X3Value der = x3_value_invalid();
    status = state->host->get_item(runtime, extensions,
                                   x3_value_int64(i), &extension);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, extension, "oid", &oid_value);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, oid_value, "dotted_string",
                                     &dotted);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, extension, "critical",
                                     &critical);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, extension, "value", &value);
    if (status == X3_STATUS_OK)
      status = state->host->get_attr(runtime, value, "public_bytes",
                                     &public_bytes);
    if (status == X3_STATUS_OK)
      status = state->host->call(runtime, public_bytes, nullptr, 0, &der);
    if (status == X3_STATUS_OK) {
      const char* oid_text = state->host->value_to_cstr(runtime, dotted);
      const void* bytes = nullptr;
      uint64_t length = 0;
      status = state->host->value_bytes_data(runtime, der, &bytes, &length);
      std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> oid(
          oid_text ? OBJ_txt2obj(oid_text, 1) : nullptr, ASN1_OBJECT_free);
      std::unique_ptr<ASN1_OCTET_STRING,
                      decltype(&ASN1_OCTET_STRING_free)> octets(
          ASN1_OCTET_STRING_new(), ASN1_OCTET_STRING_free);
      if (status == X3_STATUS_OK && oid && octets && length <= INT_MAX &&
          critical.tag == X3_TAG_BOOL &&
          ASN1_OCTET_STRING_set(octets.get(),
              static_cast<const unsigned char*>(bytes),
              static_cast<int>(length)) == 1) {
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> native(
            X509_EXTENSION_create_by_OBJ(nullptr, oid.get(),
                critical.as.b != 0, octets.get()), X509_EXTENSION_free);
        if (!native || X509_add_ext(certificate, native.get(), -1) != 1)
          status = X3_STATUS_ERROR, native_failure = true;
      } else {
        status = X3_STATUS_ERROR;
        native_failure = true;
      }
    }
    for (X3Value temporary : {der, public_bytes, value, critical,
                              dotted, oid_value, extension})
      if (temporary.tag != X3_TAG_INVALID)
        state->host->value_release(temporary);
  }
  state->host->value_release(extensions);
  if (status != X3_STATUS_OK && native_failure)
    return fail(state, call, "ValueError", "Unable to encode certificate extensions");
  return status;
}

X3Status create_x509_certificate(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 5)
    return fail(state, call, "TypeError", "certificate builder arguments are required");
  if (args[3].tag != X3_TAG_NONE ||
      (args[4].tag != X3_TAG_NONE &&
       !(args[4].tag == X3_TAG_BOOL && !args[4].as.b)))
    return fail(state, call, "NotImplementedError",
                "RSA-PSS and deterministic ECDSA certificate signing are unavailable");
  std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
  if (!certificate || X509_set_version(certificate.get(), 2) != 1)
    return X3_STATUS_ERROR;
  X3Value subject = x3_value_invalid();
  X3Value issuer = x3_value_invalid();
  X3Value serial = x3_value_invalid();
  X3Value serial_text = x3_value_invalid();
  X3Value string_fn = x3_value_invalid();
  X3Value before = x3_value_invalid();
  X3Value after = x3_value_invalid();
  X3Value public_key = x3_value_invalid();
  X3Value public_der = x3_value_invalid();
  X3Value private_der = x3_value_invalid();
  X3Value algorithm_name = x3_value_invalid();
  X3Status status = state->host->get_attr(runtime, args[0], "_subject_name",
                                          &subject);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, args[0], "_issuer_name", &issuer);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, args[0], "_serial_number", &serial);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, args[0], "_not_valid_before",
                                   &before);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, args[0], "_not_valid_after",
                                   &after);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, args[0], "_public_key",
                                   &public_key);
  std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)> subject_name(
      status == X3_STATUS_OK
          ? encode_name(state, call, runtime, subject) : nullptr,
      X509_NAME_free);
  std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)> issuer_name(
      status == X3_STATUS_OK
          ? encode_name(state, call, runtime, issuer) : nullptr,
      X509_NAME_free);
  if (status == X3_STATUS_OK && (!subject_name || !issuer_name ||
      X509_set_subject_name(certificate.get(), subject_name.get()) != 1 ||
      X509_set_issuer_name(certificate.get(), issuer_name.get()) != 1))
    status = X3_STATUS_ERROR;
  if (status == X3_STATUS_OK)
    status = state->host->builtin_value(state->host, "str", &string_fn);
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, string_fn, &serial, 1, &serial_text);
  if (status == X3_STATUS_OK) {
    const char* decimal = state->host->value_to_cstr(runtime, serial_text);
    BIGNUM* raw_number = nullptr;
    if (decimal) BN_dec2bn(&raw_number, decimal);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> number(raw_number, BN_free);
    std::unique_ptr<ASN1_INTEGER, decltype(&ASN1_INTEGER_free)> asn1_serial(
        number ? BN_to_ASN1_INTEGER(number.get(), nullptr) : nullptr,
        ASN1_INTEGER_free);
    if (!asn1_serial || X509_set_serialNumber(
            certificate.get(), asn1_serial.get()) != 1)
      status = X3_STATUS_ERROR;
  }
  if (status == X3_STATUS_OK)
    status = certificate_time(state, runtime, before,
        X509_getm_notBefore(certificate.get()));
  if (status == X3_STATUS_OK)
    status = certificate_time(state, runtime, after,
        X509_getm_notAfter(certificate.get()));
  if (status == X3_STATUS_OK)
    status = serialize_builder_key(state, runtime, public_key, false,
                                    &public_der);
  if (status == X3_STATUS_OK)
    status = serialize_builder_key(state, runtime, args[1], true,
                                    &private_der);
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> subject_key(
      nullptr, EVP_PKEY_free);
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> signing_key(
      nullptr, EVP_PKEY_free);
  if (status == X3_STATUS_OK) {
    const void* public_bytes = nullptr;
    const void* private_bytes = nullptr;
    uint64_t public_size = 0, private_size = 0;
    status = state->host->value_bytes_data(runtime, public_der,
                                            &public_bytes, &public_size);
    if (status == X3_STATUS_OK)
      status = state->host->value_bytes_data(runtime, private_der,
                                              &private_bytes, &private_size);
    if (status == X3_STATUS_OK && public_size <= LONG_MAX &&
        private_size <= LONG_MAX) {
      const unsigned char* public_cursor =
          static_cast<const unsigned char*>(public_bytes);
      const unsigned char* private_cursor =
          static_cast<const unsigned char*>(private_bytes);
      subject_key.reset(d2i_PUBKEY(nullptr, &public_cursor,
                                    static_cast<long>(public_size)));
      signing_key.reset(d2i_AutoPrivateKey(nullptr, &private_cursor,
                                           static_cast<long>(private_size)));
      if (!subject_key || !signing_key ||
          X509_set_pubkey(certificate.get(), subject_key.get()) != 1)
        status = X3_STATUS_ERROR;
    } else {
      status = X3_STATUS_ERROR;
    }
  }
  if (status == X3_STATUS_OK)
    status = add_builder_extensions(state, call, runtime, args[0],
                                     certificate.get());
  const EVP_MD* digest = nullptr;
  if (status == X3_STATUS_OK && args[2].tag != X3_TAG_NONE) {
    status = state->host->get_attr(runtime, args[2], "name",
                                   &algorithm_name);
    if (status == X3_STATUS_OK) {
      const char* name = state->host->value_to_cstr(runtime, algorithm_name);
      digest = name ? EVP_get_digestbyname(name) : nullptr;
      if (!digest) status = X3_STATUS_ERROR;
    }
  }
  if (status == X3_STATUS_OK &&
      X509_sign(certificate.get(), signing_key.get(), digest) <= 0)
    status = X3_STATUS_ERROR;
  for (X3Value value : {algorithm_name, private_der, public_der, public_key,
                        after, before, string_fn, serial_text, serial,
                        issuer, subject})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  if (status != X3_STATUS_OK &&
      state->host->runtime_last_error(runtime) != nullptr)
    return status;
  if (status != X3_STATUS_OK)
    return fail(state, call, "ValueError", "Unable to create X.509 certificate");
  return make_certificate(state, runtime, certificate.release(), result);
}

X3Status load_certificate(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result, bool pem) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "certificate data is required");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(buffer);
    return fail(state, call, "ValueError", "certificate data is too large");
  }
  X509* certificate = nullptr;
  if (pem) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(
        BIO_new_mem_buf(info.data, static_cast<int>(info.size)), BIO_free);
    if (bio) certificate = PEM_read_bio_X509(
        bio.get(), nullptr, nullptr, nullptr);
  } else {
    const auto* cursor = static_cast<const unsigned char*>(info.data);
    const auto* end = cursor + info.size;
    certificate = d2i_X509(nullptr, &cursor, static_cast<long>(info.size));
    if (certificate != nullptr && cursor != end) {
      X509_free(certificate);
      certificate = nullptr;
    }
  }
  state->host->buffer_release(buffer);
  if (certificate == nullptr)
    return fail(state, call, "ValueError", "Unable to load certificate");
  return make_certificate(state, runtime, certificate, result);
}

X3Status load_pem_certificate(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  return load_certificate(call, runtime, user_data, args, argc, result, true);
}

X3Status load_der_certificate(X3CallContext* call, X3Runtime* runtime,
                              void* user_data, const X3Value* args,
                              uint32_t argc, X3Value* result) {
  return load_certificate(call, runtime, user_data, args, argc, result, false);
}

X3Status load_pem_certificates(X3CallContext* call, X3Runtime* runtime,
                               void* user_data, const X3Value* args,
                               uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "certificate data is required");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(buffer);
    return fail(state, call, "ValueError", "certificate data is too large");
  }
  const std::string encoded(static_cast<const char*>(info.data),
                            static_cast<size_t>(info.size));
  state->host->buffer_release(buffer);
  X3Value certificates = state->host->value_list(runtime);
  if (certificates.tag == X3_TAG_INVALID) {
    return X3_STATUS_ERROR;
  }
  constexpr const char* kBegin = "-----BEGIN CERTIFICATE-----";
  constexpr const char* kEnd = "-----END CERTIFICATE-----";
  uint32_t count = 0;
  size_t offset = 0;
  while (true) {
    const size_t begin = encoded.find(kBegin, offset);
    if (begin == std::string::npos) break;
    const size_t end = encoded.find(kEnd, begin);
    if (end == std::string::npos) break;
    const size_t section_size = end + std::char_traits<char>::length(kEnd) - begin;
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(
        BIO_new_mem_buf(encoded.data() + begin,
                        static_cast<int>(section_size)), BIO_free);
    X509* certificate = bio
        ? PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr) : nullptr;
    if (certificate == nullptr) {
      ERR_clear_error();
      state->host->value_release(certificates);
      return fail(state, call, "ValueError", "Unable to load PEM file");
    }
    X3Value instance = x3_value_invalid();
    if (make_certificate(state, runtime, certificate, &instance) !=
        X3_STATUS_OK) {
      state->host->value_release(certificates);
      return X3_STATUS_ERROR;
    }
    const X3Status appended = state->host->list_append(
        runtime, certificates, instance);
    state->host->value_release(instance);
    if (appended != X3_STATUS_OK) {
      state->host->value_release(certificates);
      return X3_STATUS_ERROR;
    }
    ++count;
    offset = end + std::char_traits<char>::length(kEnd);
  }
  if (count == 0) {
    state->host->value_release(certificates);
    return fail(state, call, "ValueError", "Unable to load PEM file");
  }
  *result = certificates;
  return X3_STATUS_OK;
}

X3Status certificate_public_bytes(X3CallContext* call, X3Runtime* runtime,
                                  void* user_data, const X3Value* args,
                                  uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "public_bytes() requires encoding");
  auto* data = certificate_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "name", &name) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* encoding = state->host->value_to_cstr(runtime, name);
  const std::string encoding_text = encoding == nullptr ? "" : encoding;
  state->host->value_release(name);
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(
      BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return fail(state, call, "ValueError", "certificate encoding failed");
  int okay = 0;
  if (encoding_text == "PEM")
    okay = PEM_write_bio_X509(bio.get(), data->certificate);
  else if (encoding_text == "DER")
    okay = i2d_X509_bio(bio.get(), data->certificate);
  else
    return fail(state, call, "ValueError", "Unsupported encoding");
  if (okay != 1)
    return fail(state, call, "ValueError", "certificate encoding failed");
  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio.get(), &memory);
  if (memory == nullptr) return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, memory->data, memory->length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status certificate_fingerprint(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "fingerprint() requires algorithm");
  auto* data = certificate_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "name", &name) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* text = state->host->value_to_cstr(runtime, name);
  const EVP_MD* digest = text == nullptr ? nullptr : EVP_get_digestbyname(text);
  state->host->value_release(name);
  if (digest == nullptr)
    return fail(state, call, "TypeError", "unsupported hash algorithm");
  unsigned char output[EVP_MAX_MD_SIZE]{};
  unsigned length = 0;
  if (X509_digest(data->certificate, digest, output, &length) != 1)
    return fail(state, call, "ValueError", "certificate fingerprint failed");
  *result = state->host->value_bytes(runtime, output, length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status certificate_serial_number(X3CallContext* call, X3Runtime* runtime,
                                   void* user_data, const X3Value* args,
                                   uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "serial_number getter");
  auto* data = certificate_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::unique_ptr<BIGNUM, decltype(&BN_free)> number(
      ASN1_INTEGER_to_BN(X509_get_serialNumber(data->certificate), nullptr),
      BN_free);
  if (!number)
    return fail(state, call, "ValueError", "invalid certificate serial number");
  char* decimal = BN_bn2dec(number.get());
  if (decimal == nullptr) return X3_STATUS_ERROR;
  X3Value text = state->host->value_string(runtime, decimal);
  OPENSSL_free(decimal);
  if (text.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  X3Value int_class = x3_value_invalid();
  const bool ready = state->host->builtin_value(
      state->host, "int", &int_class) == X3_STATUS_OK;
  const X3Status status = ready
      ? state->host->call(runtime, int_class, &text, 1, result)
      : X3_STATUS_ERROR;
  if (int_class.tag != X3_TAG_INVALID) state->host->value_release(int_class);
  state->host->value_release(text);
  return status;
}

X3Status decode_certificate_extension_value(
    CryptographyNativeState* state, X3CallContext* call,
    X3Runtime* runtime, X509_EXTENSION* extension, X3Value* result) {
  const int nid = OBJ_obj2nid(X509_EXTENSION_get_object(extension));
  const char* class_name = nullptr;
  X3Value arguments[9]{};
  uint32_t argument_count = 0;
  if (nid == NID_subject_key_identifier) {
    class_name = "SubjectKeyIdentifier";
    std::unique_ptr<ASN1_OCTET_STRING,
                    decltype(&ASN1_OCTET_STRING_free)> digest(
        static_cast<ASN1_OCTET_STRING*>(X509V3_EXT_d2i(extension)),
        ASN1_OCTET_STRING_free);
    if (!digest) return X3_STATUS_ERROR;
    arguments[0] = state->host->value_bytes(runtime,
        ASN1_STRING_get0_data(digest.get()), ASN1_STRING_length(digest.get()));
    argument_count = 1;
  } else if (nid == NID_basic_constraints) {
    class_name = "BasicConstraints";
    std::unique_ptr<BASIC_CONSTRAINTS,
                    decltype(&BASIC_CONSTRAINTS_free)> basic(
        static_cast<BASIC_CONSTRAINTS*>(X509V3_EXT_d2i(extension)),
        BASIC_CONSTRAINTS_free);
    if (!basic) return X3_STATUS_ERROR;
    arguments[0] = x3_value_bool(basic->ca != 0);
    arguments[1] = basic->pathlen
        ? x3_value_int64(ASN1_INTEGER_get(basic->pathlen)) : x3_value_none();
    argument_count = 2;
  } else if (nid == NID_key_usage) {
    class_name = "KeyUsage";
    std::unique_ptr<ASN1_BIT_STRING,
                    decltype(&ASN1_BIT_STRING_free)> bits(
        static_cast<ASN1_BIT_STRING*>(X509V3_EXT_d2i(extension)),
        ASN1_BIT_STRING_free);
    if (!bits) return X3_STATUS_ERROR;
    for (int i = 0; i < 9; ++i)
      arguments[i] = x3_value_bool(ASN1_BIT_STRING_get_bit(bits.get(), i) != 0);
    argument_count = 9;
  } else if (nid == NID_authority_key_identifier) {
    class_name = "AuthorityKeyIdentifier";
    std::unique_ptr<AUTHORITY_KEYID, decltype(&AUTHORITY_KEYID_free)> aki(
        static_cast<AUTHORITY_KEYID*>(X509V3_EXT_d2i(extension)),
        AUTHORITY_KEYID_free);
    if (!aki) return X3_STATUS_ERROR;
    if (aki->issuer || aki->serial)
      return fail(state, call, "NotImplementedError",
                  "authority certificate issuer and serial decoding are unavailable");
    arguments[0] = aki->keyid
        ? state->host->value_bytes(runtime,
            ASN1_STRING_get0_data(aki->keyid),
            ASN1_STRING_length(aki->keyid))
        : x3_value_none();
    arguments[1] = x3_value_none();
    arguments[2] = x3_value_none();
    argument_count = 3;
  } else if (nid == NID_ext_key_usage) {
    class_name = "ExtendedKeyUsage";
    std::unique_ptr<EXTENDED_KEY_USAGE,
                    decltype(&EXTENDED_KEY_USAGE_free)> usages(
        static_cast<EXTENDED_KEY_USAGE*>(X509V3_EXT_d2i(extension)),
        EXTENDED_KEY_USAGE_free);
    if (!usages) return X3_STATUS_ERROR;
    X3Value list = state->host->value_list(runtime);
    if (list.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
    X3Status status = X3_STATUS_OK;
    for (int i = 0; i < sk_ASN1_OBJECT_num(usages.get()); ++i) {
      ASN1_OBJECT* native_oid = sk_ASN1_OBJECT_value(usages.get(), i);
      char oid_text[128]{};
      if (OBJ_obj2txt(oid_text, sizeof(oid_text), native_oid, 1) <= 0) {
        status = X3_STATUS_ERROR;
        break;
      }
      X3Value dotted = state->host->value_string(runtime, oid_text);
      X3Value oid = x3_value_invalid();
      status = state->host->call(runtime, state->oid_class,
                                 &dotted, 1, &oid);
      if (status == X3_STATUS_OK)
        status = state->host->list_append(runtime, list, oid);
      if (oid.tag != X3_TAG_INVALID) state->host->value_release(oid);
      state->host->value_release(dotted);
      if (status != X3_STATUS_OK) break;
    }
    if (status != X3_STATUS_OK) {
      state->host->value_release(list);
      return status;
    }
    arguments[0] = list;
    argument_count = 1;
  } else if (nid == NID_subject_alt_name) {
    class_name = "SubjectAlternativeName";
    std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> names(
        static_cast<GENERAL_NAMES*>(X509V3_EXT_d2i(extension)),
        GENERAL_NAMES_free);
    if (!names) return X3_STATUS_ERROR;
    X3Value list = state->host->value_list(runtime);
    if (list.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
    X3Status status = X3_STATUS_OK;
    for (int i = 0; i < sk_GENERAL_NAME_num(names.get()); ++i) {
      GENERAL_NAME* native = sk_GENERAL_NAME_value(names.get(), i);
      X3Value value = x3_value_invalid();
      X3Value klass = x3_value_invalid();
      X3Value item = x3_value_invalid();
      if (native->type == GEN_DNS || native->type == GEN_EMAIL) {
        ASN1_IA5STRING* raw = native->d.ia5;
        std::string name(reinterpret_cast<const char*>(
            ASN1_STRING_get0_data(raw)), ASN1_STRING_length(raw));
        value = state->host->value_string(runtime, name.c_str());
        status = import_class(state, runtime,
            "cryptography.x509.general_name",
            native->type == GEN_DNS ? "DNSName" : "RFC822Name", &klass);
      } else if (native->type == GEN_IPADD) {
        ASN1_OCTET_STRING* raw = native->d.ip;
        const unsigned char* bytes = ASN1_STRING_get0_data(raw);
        const int length = ASN1_STRING_length(raw);
        const int width = length == 4 || length == 8 ? 4
            : length == 16 || length == 32 ? 16 : 0;
        if (width == 0) {
          status = X3_STATUS_ERROR;
        } else {
          std::string address;
          char buffer[48]{};
          if (width == 4) {
            std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
                bytes[0], bytes[1], bytes[2], bytes[3]);
            address = buffer;
          } else {
            for (int j = 0; j < 16; j += 2) {
              std::snprintf(buffer, sizeof(buffer), "%x",
                  (static_cast<unsigned int>(bytes[j]) << 8) | bytes[j + 1]);
              if (j != 0) address += ':';
              address += buffer;
            }
          }
          const bool network = length == width * 2;
          if (network) {
            int prefix = 0;
            bool zero_seen = false;
            for (int j = width; j < length; ++j) {
              for (int bit = 7; bit >= 0; --bit) {
                const bool one = ((bytes[j] >> bit) & 1) != 0;
                if (!one) zero_seen = true;
                else if (zero_seen) status = X3_STATUS_ERROR;
                else ++prefix;
              }
            }
            address += "/" + std::to_string(prefix);
          }
          X3Value parser = x3_value_invalid();
          X3Value text = state->host->value_string(runtime, address.c_str());
          if (status == X3_STATUS_OK)
            status = import_class(state, runtime, "ipaddress",
                network ? "ip_network" : "ip_address", &parser);
          if (status == X3_STATUS_OK)
            status = state->host->call(runtime, parser, &text, 1, &value);
          if (parser.tag != X3_TAG_INVALID) state->host->value_release(parser);
          if (text.tag != X3_TAG_INVALID) state->host->value_release(text);
          if (status == X3_STATUS_OK)
            status = import_class(state, runtime,
                "cryptography.x509.general_name", "IPAddress", &klass);
        }
      } else {
        status = fail(state, call, "NotImplementedError",
                      "native certificate general-name decoding is unavailable");
      }
      if (status == X3_STATUS_OK)
        status = state->host->call(runtime, klass, &value, 1, &item);
      if (status == X3_STATUS_OK)
        status = state->host->list_append(runtime, list, item);
      for (X3Value temporary : {item, klass, value})
        if (temporary.tag != X3_TAG_INVALID)
          state->host->value_release(temporary);
      if (status != X3_STATUS_OK) break;
    }
    if (status != X3_STATUS_OK) {
      state->host->value_release(list);
      return status;
    }
    arguments[0] = list;
    argument_count = 1;
  } else {
    return fail(state, call, "NotImplementedError",
                "native certificate extension decoding is unavailable for this OID");
  }
  X3Value klass = x3_value_invalid();
  X3Status status = import_class(state, runtime,
      "cryptography.x509.extensions", class_name, &klass);
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, klass, arguments,
                               argument_count, result);
  if (klass.tag != X3_TAG_INVALID) state->host->value_release(klass);
  for (uint32_t i = 0; i < argument_count; ++i)
    if (arguments[i].tag == X3_TAG_OBJECT)
      state->host->value_release(arguments[i]);
  return status;
}

X3Status certificate_extensions(X3CallContext* call, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = certificate_data(state, call, args[0]);
  if (!data) return X3_STATUS_ERROR;
  X3Value items = state->host->value_list(runtime);
  if (items.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  X3Value extension_class = x3_value_invalid();
  X3Value extensions_class = x3_value_invalid();
  X3Status status = import_class(state, runtime,
      "cryptography.x509.extensions", "Extension", &extension_class);
  if (status == X3_STATUS_OK)
    status = import_class(state, runtime,
        "cryptography.x509.extensions", "Extensions", &extensions_class);
  const int count = X509_get_ext_count(data->certificate);
  for (int i = 0; status == X3_STATUS_OK && i < count; ++i) {
    X509_EXTENSION* native = X509_get_ext(data->certificate, i);
    char oid_text[128]{};
    if (OBJ_obj2txt(oid_text, sizeof(oid_text),
                    X509_EXTENSION_get_object(native), 1) <= 0) {
      status = X3_STATUS_ERROR;
      break;
    }
    X3Value dotted = state->host->value_string(runtime, oid_text);
    X3Value oid = x3_value_invalid();
    X3Value value = x3_value_invalid();
    X3Value entry = x3_value_invalid();
    status = state->host->call(runtime, state->oid_class,
                               &dotted, 1, &oid);
    if (status == X3_STATUS_OK)
      status = decode_certificate_extension_value(state, call, runtime,
                                                   native, &value);
    if (status == X3_STATUS_OK) {
      const X3Value arguments[] = {
          oid, x3_value_bool(X509_EXTENSION_get_critical(native) != 0), value};
      status = state->host->call(runtime, extension_class,
                                 arguments, 3, &entry);
    }
    if (status == X3_STATUS_OK)
      status = state->host->list_append(runtime, items, entry);
    for (X3Value temporary : {entry, value, oid, dotted})
      if (temporary.tag != X3_TAG_INVALID)
        state->host->value_release(temporary);
  }
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, extensions_class, &items, 1, result);
  for (X3Value temporary : {extensions_class, extension_class, items})
    if (temporary.tag != X3_TAG_INVALID)
      state->host->value_release(temporary);
  return status;
}

X3Status decode_certificate_name(CryptographyNativeState* state,
                                 X3CallContext* call, X3Runtime* runtime,
                                 X509_NAME* source, X3Value* result) {
  X3Value name_class = x3_value_invalid();
  X3Value rdn_class = x3_value_invalid();
  X3Value attribute_class = x3_value_invalid();
  X3Value asn1_class = x3_value_invalid();
  X3Value rdns = state->host->value_list(runtime);
  X3Value attributes = state->host->value_list(runtime);
  X3Status status = import_class(state, runtime,
      "cryptography.x509.name", "Name", &name_class);
  if (status == X3_STATUS_OK)
    status = import_class(state, runtime,
        "cryptography.x509.name", "RelativeDistinguishedName", &rdn_class);
  if (status == X3_STATUS_OK)
    status = import_class(state, runtime,
        "cryptography.x509.name", "NameAttribute", &attribute_class);
  if (status == X3_STATUS_OK)
    status = import_class(state, runtime,
        "cryptography.x509.name", "_ASN1Type", &asn1_class);
  int previous_set = -1;
  const int count = X509_NAME_entry_count(source);
  for (int i = 0; status == X3_STATUS_OK && i < count; ++i) {
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(source, i);
    const int set = X509_NAME_ENTRY_set(entry);
    if (i != 0 && set != previous_set) {
      X3Value rdn = x3_value_invalid();
      status = state->host->call(runtime, rdn_class, &attributes, 1, &rdn);
      if (status == X3_STATUS_OK)
        status = state->host->list_append(runtime, rdns, rdn);
      if (rdn.tag != X3_TAG_INVALID) state->host->value_release(rdn);
      state->host->value_release(attributes);
      attributes = state->host->value_list(runtime);
    }
    previous_set = set;
    if (status != X3_STATUS_OK) break;
    char oid_text[128]{};
    if (OBJ_obj2txt(oid_text, sizeof(oid_text),
                    X509_NAME_ENTRY_get_object(entry), 1) <= 0) {
      status = X3_STATUS_ERROR;
      break;
    }
    ASN1_STRING* raw = X509_NAME_ENTRY_get_data(entry);
    unsigned char* utf8 = nullptr;
    const int utf8_length = ASN1_STRING_to_UTF8(&utf8, raw);
    if (utf8_length < 0) {
      status = X3_STATUS_ERROR;
      break;
    }
    std::string value_text(reinterpret_cast<const char*>(utf8), utf8_length);
    OPENSSL_free(utf8);
    X3Value dotted = state->host->value_string(runtime, oid_text);
    X3Value oid = x3_value_invalid();
    X3Value value = state->host->value_string(runtime, value_text.c_str());
    X3Value tag = x3_value_int64(ASN1_STRING_type(raw));
    X3Value asn1_type = x3_value_invalid();
    X3Value attribute = x3_value_invalid();
    if (dotted.tag == X3_TAG_INVALID || value.tag == X3_TAG_INVALID)
      status = X3_STATUS_ERROR;
    if (status == X3_STATUS_OK)
      status = state->host->call(runtime, state->oid_class,
                                 &dotted, 1, &oid);
    if (status == X3_STATUS_OK)
      status = state->host->call(runtime, asn1_class, &tag, 1,
                                 &asn1_type);
    if (status == X3_STATUS_OK) {
      const X3Value arguments[] = {oid, value, asn1_type};
      status = state->host->call(runtime, attribute_class, arguments,
                                 3, &attribute);
    }
    if (status == X3_STATUS_OK)
      status = state->host->list_append(runtime, attributes, attribute);
    for (X3Value temporary : {attribute, asn1_type, value, oid, dotted})
      if (temporary.tag != X3_TAG_INVALID)
        state->host->value_release(temporary);
  }
  if (status == X3_STATUS_OK && count != 0) {
    X3Value rdn = x3_value_invalid();
    status = state->host->call(runtime, rdn_class, &attributes, 1, &rdn);
    if (status == X3_STATUS_OK)
      status = state->host->list_append(runtime, rdns, rdn);
    if (rdn.tag != X3_TAG_INVALID) state->host->value_release(rdn);
  }
  if (status == X3_STATUS_OK)
    status = state->host->call(runtime, name_class, &rdns, 1, result);
  for (X3Value temporary : {attributes, rdns, asn1_class, attribute_class,
                            rdn_class, name_class})
    if (temporary.tag != X3_TAG_INVALID)
      state->host->value_release(temporary);
  if (status != X3_STATUS_OK && state->host->runtime_last_error(runtime) == nullptr)
    return fail(state, call, "ValueError", "Unable to decode X.509 name");
  return status;
}

X3Status certificate_subject(X3CallContext* call, X3Runtime* runtime,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = certificate_data(state, call, args[0]);
  return data ? decode_certificate_name(state, call, runtime,
      X509_get_subject_name(data->certificate), result) : X3_STATUS_ERROR;
}

X3Status certificate_issuer(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = certificate_data(state, call, args[0]);
  return data ? decode_certificate_name(state, call, runtime,
      X509_get_issuer_name(data->certificate), result) : X3_STATUS_ERROR;
}

X3Status document_init(X3CallContext* call, X3Runtime*, void* user_data,
                       const X3Value*, uint32_t, X3Value*) {
  return fail(static_cast<CryptographyNativeState*>(user_data), call,
              "TypeError", "X.509 documents are created by their loader");
}

X3Status make_csr(CryptographyNativeState* state, X3Runtime* runtime,
                  X509_REQ* request, X3Value* result) {
  auto data = std::make_unique<CsrData>();
  data->request = request;
  X3Value instance = state->host->value_instance(runtime, state->x509_csr_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kCsrType, data.get(), cleanup_csr) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status make_crl(CryptographyNativeState* state, X3Runtime* runtime,
                  X509_CRL* list, X3Value* result) {
  auto data = std::make_unique<CrlData>();
  data->list = list;
  X3Value instance = state->host->value_instance(runtime, state->x509_crl_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kCrlType, data.get(), cleanup_crl) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status load_x509_document(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result,
                            bool csr, bool pem) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "X.509 document data is required");
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[0], 0, &input, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(input);
    return fail(state, call, "ValueError", "X.509 document is too large");
  }
  X509_REQ* request = nullptr;
  X509_CRL* list = nullptr;
  if (pem) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(
        BIO_new_mem_buf(info.data, static_cast<int>(info.size)), BIO_free);
    if (bio) {
      if (csr)
        request = PEM_read_bio_X509_REQ(bio.get(), nullptr, nullptr, nullptr);
      else
        list = PEM_read_bio_X509_CRL(bio.get(), nullptr, nullptr, nullptr);
    }
  } else {
    const auto* cursor = static_cast<const unsigned char*>(info.data);
    const auto* end = cursor + info.size;
    if (csr) {
      request = d2i_X509_REQ(nullptr, &cursor, static_cast<long>(info.size));
      if (request != nullptr && cursor != end) {
        X509_REQ_free(request);
        request = nullptr;
      }
    } else {
      list = d2i_X509_CRL(nullptr, &cursor, static_cast<long>(info.size));
      if (list != nullptr && cursor != end) {
        X509_CRL_free(list);
        list = nullptr;
      }
    }
  }
  state->host->buffer_release(input);
  if (csr && request == nullptr)
    return fail(state, call, "ValueError", "Unable to load certificate request");
  if (!csr && list == nullptr)
    return fail(state, call, "ValueError", "Unable to load certificate revocation list");
  return csr ? make_csr(state, runtime, request, result)
             : make_crl(state, runtime, list, result);
}

X3Status load_pem_csr(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  return load_x509_document(call, runtime, user_data, args, argc, result,
                            true, true);
}

X3Status load_der_csr(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  return load_x509_document(call, runtime, user_data, args, argc, result,
                            true, false);
}

X3Status load_pem_crl(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  return load_x509_document(call, runtime, user_data, args, argc, result,
                            false, true);
}

X3Status load_der_crl(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  return load_x509_document(call, runtime, user_data, args, argc, result,
                            false, false);
}

X3Status document_public_bytes(X3CallContext* call, X3Runtime* runtime,
                               void* user_data, const X3Value* args,
                               uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "public_bytes() requires encoding");
  auto* request = static_cast<CsrData*>(
      state->host->instance_get_native_data(args[0], kCsrType));
  auto* list = static_cast<CrlData*>(
      state->host->instance_get_native_data(args[0], kCrlType));
  if (request == nullptr && list == nullptr)
    return fail(state, call, "TypeError", "expected X.509 document");
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "name", &name) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* text = state->host->value_to_cstr(runtime, name);
  const std::string encoding = text == nullptr ? "" : text;
  state->host->value_release(name);
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!bio) return X3_STATUS_ERROR;
  int okay = 0;
  if (encoding == "PEM")
    okay = request ? PEM_write_bio_X509_REQ(bio.get(), request->request)
                   : PEM_write_bio_X509_CRL(bio.get(), list->list);
  else if (encoding == "DER")
    okay = request ? i2d_X509_REQ_bio(bio.get(), request->request)
                   : i2d_X509_CRL_bio(bio.get(), list->list);
  else
    return fail(state, call, "ValueError", "Unsupported encoding");
  if (okay != 1)
    return fail(state, call, "ValueError", "X.509 document encoding failed");
  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio.get(), &memory);
  if (memory == nullptr) return X3_STATUS_ERROR;
  *result = state->host->value_bytes(runtime, memory->data, memory->length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status csr_signature_valid(X3CallContext* call, X3Runtime*,
                             void* user_data, const X3Value* args,
                             uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "CSR getter");
  auto* data = static_cast<CsrData*>(
      state->host->instance_get_native_data(args[0], kCsrType));
  if (data == nullptr) return fail(state, call, "TypeError", "expected CSR");
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> public_key(
      X509_REQ_get_pubkey(data->request), EVP_PKEY_free);
  if (!public_key)
    return fail(state, call, "ValueError", "CSR public key is invalid");
  *result = x3_value_bool(X509_REQ_verify(data->request, public_key.get()) == 1);
  return X3_STATUS_OK;
}

X3Status crl_length(X3CallContext* call, X3Runtime*, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "CRL length");
  auto* data = static_cast<CrlData*>(
      state->host->instance_get_native_data(args[0], kCrlType));
  if (data == nullptr) return fail(state, call, "TypeError", "expected CRL");
  const auto* revoked = X509_CRL_get_REVOKED(data->list);
  *result = x3_value_int64(revoked == nullptr ? 0 : sk_X509_REVOKED_num(revoked));
  return X3_STATUS_OK;
}

RevokedData* revoked_data(CryptographyNativeState* state,
                          X3CallContext* call, X3Value object) {
  auto* data = static_cast<RevokedData*>(
      state->host->instance_get_native_data(object, kRevokedType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected RevokedCertificate");
  return data;
}

X3Status make_revoked(CryptographyNativeState* state, X3Runtime* runtime,
                      X509_REVOKED* entry, X3Value* result) {
  auto data = std::make_unique<RevokedData>();
  data->entry = X509_REVOKED_dup(entry);
  if (data->entry == nullptr) return X3_STATUS_ERROR;
  X3Value instance = state->host->value_instance(
      runtime, state->x509_revoked_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kRevokedType, data.get(), cleanup_revoked) !=
      X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status revoked_serial(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args,
                        uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = revoked_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::unique_ptr<BIGNUM, decltype(&BN_free)> number(
      ASN1_INTEGER_to_BN(X509_REVOKED_get0_serialNumber(data->entry), nullptr),
      BN_free);
  if (!number)
    return fail(state, call, "ValueError", "invalid revoked serial number");
  char* decimal = BN_bn2dec(number.get());
  if (decimal == nullptr) return X3_STATUS_ERROR;
  X3Value text_value = state->host->value_string(runtime, decimal);
  OPENSSL_free(decimal);
  if (text_value.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  X3Value int_class = x3_value_invalid();
  const X3Status status = state->host->builtin_value(
      state->host, "int", &int_class) == X3_STATUS_OK
      ? state->host->call(runtime, int_class, &text_value, 1, result)
      : X3_STATUS_ERROR;
  if (int_class.tag != X3_TAG_INVALID) state->host->value_release(int_class);
  state->host->value_release(text_value);
  return status;
}

X3Status revoked_date(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result, bool aware) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = revoked_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  std::tm utc{};
  if (ASN1_TIME_to_tm(X509_REVOKED_get0_revocationDate(data->entry),
                      &utc) != 1)
    return fail(state, call, "ValueError", "invalid revocation date");
  X3Value klass = x3_value_invalid();
  if (import_class(state, runtime, "datetime", "datetime", &klass) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Value components[] = {
      x3_value_int64(utc.tm_year + 1900), x3_value_int64(utc.tm_mon + 1),
      x3_value_int64(utc.tm_mday), x3_value_int64(utc.tm_hour),
      x3_value_int64(utc.tm_min), x3_value_int64(utc.tm_sec)};
  X3Status status = state->host->call(runtime, klass, components, 6, result);
  state->host->value_release(klass);
  if (status != X3_STATUS_OK || !aware) return status;
  X3Value timezone_class = x3_value_invalid();
  X3Value timezone_utc = x3_value_invalid();
  X3Value replace = x3_value_invalid();
  X3Value aware_value = x3_value_invalid();
  status = import_class(state, runtime, "datetime", "timezone",
                        &timezone_class);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, timezone_class, "utc",
                                   &timezone_utc);
  if (status == X3_STATUS_OK)
    status = state->host->get_attr(runtime, *result, "replace", &replace);
  if (status == X3_STATUS_OK) {
    X3KeywordArg keyword{"tzinfo", timezone_utc};
    status = state->host->call_kw(runtime, replace, nullptr, 0, &keyword, 1,
                        &aware_value);
  }
  for (X3Value value : {replace, timezone_utc, timezone_class})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  if (status != X3_STATUS_OK) {
    state->host->value_release(*result);
    *result = x3_value_invalid();
    return status;
  }
  state->host->value_release(*result);
  *result = aware_value;
  return X3_STATUS_OK;
}

X3Status revoked_date_naive(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  return revoked_date(call, runtime, user_data, args, argc, result, false);
}
X3Status revoked_date_utc(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  return revoked_date(call, runtime, user_data, args, argc, result, true);
}

X3Status revoked_extensions(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = revoked_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (X509_REVOKED_get_ext_count(data->entry) != 0)
    return fail(state, call, "NotImplementedError",
                "revoked entry extensions require native decoding");
  X3Value klass = x3_value_invalid();
  if (import_class(state, runtime, "cryptography.x509.extensions", "Extensions",
                   &klass) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value values = state->host->value_list(runtime);
  if (values.tag == X3_TAG_INVALID) {
    state->host->value_release(klass);
    return X3_STATUS_ERROR;
  }
  X3Status status = state->host->call(runtime, klass, &values, 1, result);
  state->host->value_release(values);
  state->host->value_release(klass);
  return status;
}

X3Status crl_get_index(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return X3_STATUS_ERROR;
  auto* data = static_cast<CrlData*>(
      state->host->instance_get_native_data(args[0], kCrlType));
  if (data == nullptr) return fail(state, call, "TypeError", "expected CRL");
  const auto* entries = X509_CRL_get_REVOKED(data->list);
  const int count = entries == nullptr ? 0 : sk_X509_REVOKED_num(entries);
  if (args[1].tag == X3_TAG_OBJECT) {
    X3Value slice_class = x3_value_invalid();
    X3Value isinstance_fn = x3_value_invalid();
    X3Value is_slice = x3_value_invalid();
    if (state->host->builtin_value(state->host, "slice", &slice_class) != X3_STATUS_OK ||
        state->host->builtin_value(state->host, "isinstance", &isinstance_fn) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Value check_args[] = {args[1], slice_class};
    X3Status status = state->host->call(runtime, isinstance_fn, check_args, 2,
                                        &is_slice);
    state->host->value_release(slice_class);
    state->host->value_release(isinstance_fn);
    if (status != X3_STATUS_OK) return status;
    const bool slice = is_slice.tag == X3_TAG_BOOL && is_slice.as.b;
    state->host->value_release(is_slice);
    if (slice) {
      X3Value indices_fn = x3_value_invalid();
      X3Value indices = x3_value_invalid();
      status = state->host->get_attr(runtime, args[1], "indices", &indices_fn);
      const X3Value length = x3_value_int64(count);
      if (status == X3_STATUS_OK)
        status = state->host->call(runtime, indices_fn, &length, 1, &indices);
      if (indices_fn.tag != X3_TAG_INVALID) state->host->value_release(indices_fn);
      if (status != X3_STATUS_OK) return status;
      int64_t bounds[3]{};
      for (int i = 0; i < 3; ++i) {
        X3Value part = x3_value_invalid();
        status = state->host->get_item(runtime, indices, x3_value_int64(i), &part);
        if (status == X3_STATUS_OK && part.tag == X3_TAG_INT64)
          bounds[i] = part.as.i64;
        else
          status = X3_STATUS_ERROR;
        if (part.tag != X3_TAG_INVALID) state->host->value_release(part);
        if (status != X3_STATUS_OK) break;
      }
      state->host->value_release(indices);
      if (status != X3_STATUS_OK) return status;
      X3Value list = state->host->value_list(runtime);
      if (list.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
      for (int64_t i = bounds[0];
           bounds[2] > 0 ? i < bounds[1] : i > bounds[1]; i += bounds[2]) {
        X3Value item = x3_value_invalid();
        status = make_revoked(state, runtime,
                              sk_X509_REVOKED_value(entries, static_cast<int>(i)),
                              &item);
        if (status == X3_STATUS_OK)
          status = state->host->list_append(runtime, list, item);
        if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
        if (status != X3_STATUS_OK) {
          state->host->value_release(list);
          return status;
        }
      }
      *result = list;
      return X3_STATUS_OK;
    }
  }
  if (args[1].tag != X3_TAG_INT64 && args[1].tag != X3_TAG_UINT64)
    return fail(state, call, "TypeError", "CRL indices must be integers");
  int64_t index = args[1].tag == X3_TAG_INT64
      ? args[1].as.i64 : static_cast<int64_t>(args[1].as.u64);
  if (index < 0) index += count;
  if (index < 0 || index >= count)
    return fail(state, call, "IndexError", "index out of range");
  return make_revoked(state, runtime,
                      sk_X509_REVOKED_value(entries, static_cast<int>(index)),
                      result);
}

X3Status crl_lookup_serial(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return X3_STATUS_ERROR;
  auto* data = static_cast<CrlData*>(
      state->host->instance_get_native_data(args[0], kCrlType));
  if (data == nullptr) return fail(state, call, "TypeError", "expected CRL");
  X3Value int_class = x3_value_invalid();
  X3Value isinstance_fn = x3_value_invalid();
  X3Value is_int = x3_value_invalid();
  if (state->host->builtin_value(state->host, "int", &int_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "isinstance", &isinstance_fn) != X3_STATUS_OK) {
    state->host->value_release(int_class);
    return X3_STATUS_ERROR;
  }
  const X3Value check_args[] = {args[1], int_class};
  X3Status status = state->host->call(runtime, isinstance_fn, check_args, 2,
                                      &is_int);
  state->host->value_release(isinstance_fn);
  state->host->value_release(int_class);
  if (status != X3_STATUS_OK) return status;
  const bool valid_int = is_int.tag == X3_TAG_BOOL && is_int.as.b;
  state->host->value_release(is_int);
  if (!valid_int)
    return fail(state, call, "TypeError", "serial must be an integer");
  X3Value string_fn = x3_value_invalid();
  X3Value text_value = x3_value_invalid();
  if (state->host->builtin_value(state->host, "str", &string_fn) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  status = state->host->call(runtime, string_fn, &args[1], 1,
                                      &text_value);
  state->host->value_release(string_fn);
  if (status != X3_STATUS_OK) return status;
  const char* decimal = state->host->value_to_cstr(runtime, text_value);
  BIGNUM* number = nullptr;
  if (args[1].tag == X3_TAG_BOOL)
    BN_dec2bn(&number, args[1].as.b ? "1" : "0");
  else if (decimal != nullptr)
    BN_dec2bn(&number, decimal);
  state->host->value_release(text_value);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> expected(number, BN_free);
  if (!expected)
    return fail(state, call, "TypeError", "serial_number must be int");
  const auto* entries = X509_CRL_get_REVOKED(data->list);
  const int count = entries == nullptr ? 0 : sk_X509_REVOKED_num(entries);
  for (int index = 0; index < count; ++index) {
    X509_REVOKED* entry = sk_X509_REVOKED_value(entries, index);
    std::unique_ptr<BIGNUM, decltype(&BN_free)> current(
        ASN1_INTEGER_to_BN(X509_REVOKED_get0_serialNumber(entry), nullptr),
        BN_free);
    if (current && BN_cmp(current.get(), expected.get()) == 0)
      return make_revoked(state, runtime, entry, result);
  }
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status document_signature(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "signature getter");
  auto* request = static_cast<CsrData*>(
      state->host->instance_get_native_data(args[0], kCsrType));
  auto* list = static_cast<CrlData*>(
      state->host->instance_get_native_data(args[0], kCrlType));
  if (request == nullptr && list == nullptr)
    return fail(state, call, "TypeError", "expected X.509 document");
  const ASN1_BIT_STRING* signature = nullptr;
  const X509_ALGOR* algorithm = nullptr;
  if (request)
    X509_REQ_get0_signature(request->request, &signature, &algorithm);
  else
    X509_CRL_get0_signature(list->list, &signature, &algorithm);
  if (signature == nullptr || signature->length < 0)
    return fail(state, call, "ValueError", "X.509 signature is invalid");
  *result = state->host->value_bytes(
      runtime, ASN1_STRING_get0_data(signature), signature->length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status document_tbs_bytes(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "signed-body getter");
  auto* request = static_cast<CsrData*>(
      state->host->instance_get_native_data(args[0], kCsrType));
  auto* list = static_cast<CrlData*>(
      state->host->instance_get_native_data(args[0], kCrlType));
  if (request == nullptr && list == nullptr)
    return fail(state, call, "TypeError", "expected X.509 document");
  unsigned char* encoded = nullptr;
  const int length = request
      ? i2d_re_X509_REQ_tbs(request->request, &encoded)
      : i2d_re_X509_CRL_tbs(list->list, &encoded);
  if (length <= 0 || encoded == nullptr) {
    OPENSSL_free(encoded);
    return fail(state, call, "ValueError", "X.509 signed body is invalid");
  }
  *result = state->host->value_bytes(runtime, encoded, length);
  OPENSSL_free(encoded);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status crl_fingerprint(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "fingerprint() requires algorithm");
  auto* data = static_cast<CrlData*>(
      state->host->instance_get_native_data(args[0], kCrlType));
  if (data == nullptr) return fail(state, call, "TypeError", "expected CRL");
  X3Value name = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "name", &name) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* text = state->host->value_to_cstr(runtime, name);
  const EVP_MD* digest = text == nullptr ? nullptr : EVP_get_digestbyname(text);
  state->host->value_release(name);
  if (digest == nullptr)
    return fail(state, call, "TypeError", "unsupported hash algorithm");
  unsigned char bytes[EVP_MAX_MD_SIZE]{};
  unsigned length = 0;
  if (X509_CRL_digest(data->list, digest, bytes, &length) != 1)
    return fail(state, call, "ValueError", "CRL fingerprint failed");
  *result = state->host->value_bytes(runtime, bytes, length);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

SctData* sct_data(CryptographyNativeState* state, X3CallContext* call,
                  X3Value object) {
  auto* data = static_cast<SctData*>(
      state->host->instance_get_native_data(object, kSctType));
  if (data == nullptr) fail(state, call, "TypeError", "expected SCT");
  return data;
}

X3Status sct_init(X3CallContext* call, X3Runtime*, void* user_data,
                  const X3Value*, uint32_t, X3Value*) {
  return fail(static_cast<CryptographyNativeState*>(user_data), call,
              "TypeError", "SCT instances are decoded from certificates");
}

X3Status make_sct(CryptographyNativeState* state, X3Runtime* runtime,
                  SCT* sct, X3Value* result) {
  auto data = std::make_unique<SctData>();
  data->sct = sct;
  X3Value instance = state->host->value_instance(runtime, state->x509_sct_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kSctType,
                                            data.get(), cleanup_sct) !=
      X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status certificate_scts(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "certificate is required");
  auto* data = certificate_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Value values = state->host->value_list(runtime);
  if (values.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (X509_get_ext_by_NID(data->certificate, NID_ct_precert_scts, -1) < 0) {
    *result = values;
    return X3_STATUS_OK;
  }
  auto* entries = static_cast<STACK_OF(SCT)*>(
      X509_get_ext_d2i(data->certificate, NID_ct_precert_scts, nullptr,
                       nullptr));
  if (entries == nullptr) {
    state->host->value_release(values);
    return fail(state, call, "ValueError", "invalid SCT extension");
  }
  while (sk_SCT_num(entries) > 0) {
    SCT* sct = sk_SCT_shift(entries);
    X3Value instance = x3_value_invalid();
    if (make_sct(state, runtime, sct, &instance) != X3_STATUS_OK) {
      SCT_LIST_free(entries);
      state->host->value_release(values);
      return X3_STATUS_ERROR;
    }
    const X3Status appended = state->host->list_append(
        runtime, values, instance);
    state->host->value_release(instance);
    if (appended != X3_STATUS_OK) {
      SCT_LIST_free(entries);
      state->host->value_release(values);
      return X3_STATUS_ERROR;
    }
  }
  SCT_LIST_free(entries);
  *result = values;
  return X3_STATUS_OK;
}

X3Status sct_bytes_property(X3CallContext* call, X3Runtime* runtime,
                            void* user_data, const X3Value* args,
                            uint32_t argc, X3Value* result,
                            int which) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "SCT getter");
  auto* data = sct_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  unsigned char* bytes = nullptr;
  const size_t size = which == 0
      ? SCT_get0_log_id(data->sct, &bytes)
      : which == 1 ? SCT_get0_signature(data->sct, &bytes)
                   : SCT_get0_extensions(data->sct, &bytes);
  *result = state->host->value_bytes(runtime, bytes, size);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status sct_log_id(X3CallContext* call, X3Runtime* runtime,
                    void* user_data, const X3Value* args,
                    uint32_t argc, X3Value* result) {
  return sct_bytes_property(call, runtime, user_data, args, argc, result, 0);
}

X3Status sct_signature(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  return sct_bytes_property(call, runtime, user_data, args, argc, result, 1);
}

X3Status sct_extensions(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args,
                        uint32_t argc, X3Value* result) {
  return sct_bytes_property(call, runtime, user_data, args, argc, result, 2);
}

X3Status sct_timestamp(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "SCT getter");
  auto* data = sct_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  const uint64_t milliseconds = SCT_get_timestamp(data->sct);
  const std::time_t seconds = static_cast<std::time_t>(milliseconds / 1000);
  std::tm utc{};
#ifdef _WIN32
  if (gmtime_s(&utc, &seconds) != 0)
#else
  if (gmtime_r(&seconds, &utc) == nullptr)
#endif
    return fail(state, call, "ValueError", "SCT timestamp is out of range");
  X3Value importer = x3_value_invalid();
  X3Value module_name = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value klass = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) goto done;
  module_name = state->host->value_string(runtime, "datetime");
  if (module_name.tag == X3_TAG_INVALID ||
      state->host->call(runtime, importer, &module_name, 1, &module) !=
          X3_STATUS_OK ||
      state->host->get_attr(runtime, module, "datetime", &klass) !=
          X3_STATUS_OK) goto done;
  {
    const X3Value components[] = {
        x3_value_int64(utc.tm_year + 1900),
        x3_value_int64(utc.tm_mon + 1),
        x3_value_int64(utc.tm_mday),
        x3_value_int64(utc.tm_hour),
        x3_value_int64(utc.tm_min),
        x3_value_int64(utc.tm_sec),
        x3_value_int64(static_cast<int64_t>(milliseconds % 1000) * 1000),
    };
    status = state->host->call(runtime, klass, components, 7, result);
  }
done:
  for (X3Value value : {klass, module, module_name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

X3Status sct_signature_hash_algorithm(X3CallContext* call,
                                      X3Runtime* runtime, void* user_data,
                                      const X3Value* args, uint32_t argc,
                                      X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "SCT getter");
  auto* data = sct_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  const int signature_nid = SCT_get_signature_nid(data->sct);
  if (signature_nid != NID_sha256WithRSAEncryption &&
      signature_nid != NID_ecdsa_with_SHA256)
    return fail(state, call, "ValueError", "unsupported SCT hash algorithm");
  X3Value importer = x3_value_invalid();
  X3Value name = x3_value_invalid();
  X3Value fromlist = x3_value_invalid();
  X3Value item = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value klass = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) goto done;
  name = state->host->value_string(
      runtime, "cryptography.hazmat.primitives.hashes");
  fromlist = state->host->value_list(runtime);
  item = state->host->value_string(runtime, "SHA256");
  if (name.tag == X3_TAG_INVALID || fromlist.tag == X3_TAG_INVALID ||
      item.tag == X3_TAG_INVALID ||
      state->host->list_append(runtime, fromlist, item) != X3_STATUS_OK)
    goto done;
  {
    const X3Value import_args[] = {
        name, x3_value_none(), x3_value_none(), fromlist};
    if (state->host->call(runtime, importer, import_args, 4, &module) !=
            X3_STATUS_OK ||
        state->host->get_attr(runtime, module, "SHA256", &klass) !=
            X3_STATUS_OK)
      goto done;
  }
  status = state->host->call(runtime, klass, nullptr, 0, result);
done:
  for (X3Value value : {klass, module, item, fromlist, name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

X3Status store_init(X3CallContext* call, X3Runtime* runtime,
                    void* user_data, const X3Value* args,
                    uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "Store() requires certificates");
  uint64_t count = 0;
  if (state->host->len(runtime, args[1], &count) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (count == 0)
    return fail(state, call, "ValueError", "can't create an empty store");
  auto data = std::make_unique<StoreData>();
  data->store = X509_STORE_new();
  if (data->store == nullptr)
    return fail(state, call, "ValueError", "unable to create certificate store");
  std::vector<X509*> anchors;
  anchors.reserve(static_cast<size_t>(count));
  for (uint64_t index = 0; index < count; ++index) {
    X3Value item = x3_value_invalid();
    if (state->host->get_item(runtime, args[1], x3_value_uint64(index),
                              &item) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    auto* certificate = certificate_data(state, call, item);
    state->host->value_release(item);
    if (certificate == nullptr) return X3_STATUS_ERROR;
    bool duplicate = false;
    for (X509* existing : anchors)
      if (X509_cmp(existing, certificate->certificate) == 0) {
        duplicate = true;
        break;
      }
    if (!duplicate) {
      if (X509_STORE_add_cert(data->store, certificate->certificate) != 1)
        return fail(state, call, "ValueError", "unable to add trusted certificate");
      anchors.push_back(certificate->certificate);
    }
  }
  if (state->host->instance_set_native_data(
          args[0], kStoreType, data.get(), cleanup_store) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status verification_error(CryptographyNativeState* state,
                            X3CallContext* call, const char* message) {
  return state->host->raise_error(call,
                                  state->x509_verification_error_class,
                                  message);
}

ExtensionPolicyData* extension_policy_data(CryptographyNativeState* state,
                                           X3CallContext* call, X3Value value) {
  auto* data = static_cast<ExtensionPolicyData*>(
      state->host->instance_get_native_data(value, kExtensionPolicyType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected ExtensionPolicy");
  return data;
}

X3Status make_extension_policy(CryptographyNativeState* state,
                               X3Runtime* runtime, int kind,
                               const ExtensionPolicyData* original,
                               X3Value* result) {
  auto data = std::make_unique<ExtensionPolicyData>();
  if (original != nullptr) {
    *data = *original;
  } else {
    data->rules = {{
        {"1.3.6.1.5.5.7.1.1", NID_info_access},
        {"2.5.29.35", NID_authority_key_identifier},
        {"2.5.29.14", NID_subject_key_identifier},
        {"2.5.29.15", NID_key_usage},
        {"2.5.29.17", NID_subject_alt_name},
        {"2.5.29.19", NID_basic_constraints},
        {"2.5.29.30", NID_name_constraints},
        {"2.5.29.37", NID_ext_key_usage},
    }};
    if (kind == 1) {
      for (size_t index : {size_t(0), size_t(1), size_t(2), size_t(7)})
        data->rules[index].criticality = Criticality::NonCritical;
      data->rules[3].presence = Presence::Required;
      data->rules[5].presence = Presence::Required;
      data->rules[5].criticality = Criticality::Critical;
    } else if (kind == 2) {
      for (size_t index : {size_t(0), size_t(1), size_t(7)})
        data->rules[index].criticality = Criticality::NonCritical;
      data->rules[1].presence = Presence::Required;
      data->rules[4].presence = Presence::Required;
      data->rules[6].presence = Presence::Forbidden;
    }
  }
  X3Value instance = state->host->value_instance(
      runtime, state->x509_extension_policy_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kExtensionPolicyType, data.get(),
          cleanup_extension_policy) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status extension_policy_permit_all(X3CallContext* call, X3Runtime* runtime,
                                     void* user_data, const X3Value*,
                                     uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0) return fail(state, call, "TypeError", "permit_all() takes no arguments");
  return make_extension_policy(state, runtime, 0, nullptr, result);
}

X3Status extension_policy_webpki_ca(X3CallContext* call, X3Runtime* runtime,
                                    void* user_data, const X3Value*,
                                    uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0)
    return fail(state, call, "TypeError", "webpki_defaults_ca() takes no arguments");
  return make_extension_policy(state, runtime, 1, nullptr, result);
}

X3Status extension_policy_webpki_ee(X3CallContext* call, X3Runtime* runtime,
                                    void* user_data, const X3Value*,
                                    uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 0)
    return fail(state, call, "TypeError", "webpki_defaults_ee() takes no arguments");
  return make_extension_policy(state, runtime, 2, nullptr, result);
}

X3Status configure_extension_rule(X3CallContext* call, X3Runtime* runtime,
                                  void* user_data, const X3Value* args,
                                  uint32_t argc, X3Value* result,
                                  Presence presence) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  const bool forbidden = presence == Presence::Forbidden;
  if (argc != (forbidden ? 2u : 4u))
    return fail(state, call, "TypeError", "invalid extension policy arguments");
  auto* policy = extension_policy_data(state, call, args[0]);
  if (policy == nullptr) return X3_STATUS_ERROR;
  X3Value oid = x3_value_invalid();
  X3Value dotted = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "oid", &oid) != X3_STATUS_OK ||
      state->host->get_attr(runtime, oid, "dotted_string", &dotted) !=
          X3_STATUS_OK) {
    if (oid.tag != X3_TAG_INVALID) state->host->value_release(oid);
    return X3_STATUS_ERROR;
  }
  const char* oid_text = state->host->value_to_cstr(runtime, dotted);
  const std::string selected = oid_text == nullptr ? "" : oid_text;
  state->host->value_release(dotted);
  state->host->value_release(oid);
  size_t index = policy->rules.size();
  for (size_t candidate = 0; candidate < policy->rules.size(); ++candidate)
    if (selected == policy->rules[candidate].oid) {
      index = candidate;
      break;
    }
  if (index == policy->rules.size())
    return fail(state, call, "ValueError", "Unsupported extension OID");
  if (policy->rules[index].configured)
    return fail(state, call, "ValueError",
                "ExtensionPolicy already configured for extension");
  Criticality criticality = Criticality::Agnostic;
  if (!forbidden) {
    auto* value = static_cast<CriticalityData*>(
        state->host->instance_get_native_data(args[2], kCriticalityType));
    if (value == nullptr)
      return fail(state, call, "TypeError", "expected Criticality");
    criticality = value->value;
    if (args[3].tag != X3_TAG_NONE)
      return fail(state, call, "NotImplementedError",
                  "X.509 extension policy callbacks are not implemented");
  }
  if (make_extension_policy(state, runtime, 0, policy, result) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto* copy = static_cast<ExtensionPolicyData*>(
      state->host->instance_get_native_data(*result, kExtensionPolicyType));
  copy->rules[index].presence = presence;
  copy->rules[index].criticality = criticality;
  copy->rules[index].configured = true;
  return X3_STATUS_OK;
}

X3Status extension_policy_forbid(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  return configure_extension_rule(call, runtime, user_data, args, argc,
                                  result, Presence::Forbidden);
}
X3Status extension_policy_maybe(X3CallContext* call, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  return configure_extension_rule(call, runtime, user_data, args, argc,
                                  result, Presence::Maybe);
}
X3Status extension_policy_require(X3CallContext* call, X3Runtime* runtime,
                                  void* user_data, const X3Value* args,
                                  uint32_t argc, X3Value* result) {
  return configure_extension_rule(call, runtime, user_data, args, argc,
                                  result, Presence::Required);
}

X3Status criticality_repr(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* value = static_cast<CriticalityData*>(
      state->host->instance_get_native_data(args[0], kCriticalityType));
  if (value == nullptr)
    return fail(state, call, "TypeError", "expected Criticality");
  const char* label = value->value == Criticality::Critical ? "CRITICAL"
      : value->value == Criticality::NonCritical ? "NON_CRITICAL"
      : "AGNOSTIC";
  const std::string repr = std::string("Criticality.") + label;
  *result = state->host->value_string(runtime, repr.c_str());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status verify_chain(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result, bool server) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 5 && argc != 6)
    return fail(state, call, "TypeError",
                "leaf, intermediates, hostname, and validation time are required");
  auto* store = static_cast<StoreData*>(
      state->host->instance_get_native_data(args[0], kStoreType));
  if (store == nullptr) return fail(state, call, "TypeError", "expected Store");
  auto* leaf = certificate_data(state, call, args[1]);
  if (leaf == nullptr) return X3_STATUS_ERROR;
  const char* hostname = nullptr;
  uint64_t hostname_size = 0;
  if (server) {
    if (state->host->value_string_data(runtime, args[3], &hostname,
                                        &hostname_size) != X3_STATUS_OK)
      return fail(state, call, "TypeError", "hostname must be str");
    if (hostname_size == 0 || hostname_size >
        static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        std::string(hostname, static_cast<size_t>(hostname_size)).find('\0') !=
            std::string::npos)
      return fail(state, call, "ValueError", "invalid DNS hostname");
  }
  int64_t validation_seconds = 0;
  if (args[4].tag == X3_TAG_INT64)
    validation_seconds = args[4].as.i64;
  else if (args[4].tag == X3_TAG_UINT64 &&
           args[4].as.u64 <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    validation_seconds = static_cast<int64_t>(args[4].as.u64);
  else
    return fail(state, call, "TypeError", "validation time must be int");
  uint64_t count = 0;
  if (state->host->len(runtime, args[2], &count) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  std::unique_ptr<STACK_OF(X509), void (*)(STACK_OF(X509)*)> intermediates(
      sk_X509_new_null(), [](STACK_OF(X509)* value) { sk_X509_free(value); });
  if (!intermediates) return X3_STATUS_ERROR;
  for (uint64_t index = 0; index < count; ++index) {
    X3Value item = x3_value_invalid();
    if (state->host->get_item(runtime, args[2], x3_value_uint64(index),
                              &item) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    auto* data = certificate_data(state, call, item);
    state->host->value_release(item);
    if (data == nullptr) return X3_STATUS_ERROR;
    if (sk_X509_push(intermediates.get(), data->certificate) == 0)
      return X3_STATUS_ERROR;
  }
  std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> context(
      X509_STORE_CTX_new(), X509_STORE_CTX_free);
  if (!context || X509_STORE_CTX_init(context.get(), store->store,
                                      leaf->certificate,
                                      intermediates.get()) != 1)
    return fail(state, call, "ValueError", "unable to initialize verification");
  X509_STORE_CTX_set_time(context.get(), 0,
                          static_cast<std::time_t>(validation_seconds));
  if (argc == 6) {
    if ((args[5].tag != X3_TAG_INT64 && args[5].tag != X3_TAG_UINT64) ||
        (args[5].tag == X3_TAG_INT64 && args[5].as.i64 < 0))
      return fail(state, call, "TypeError", "maximum depth must be nonnegative");
    const uint64_t depth = args[5].tag == X3_TAG_INT64
        ? static_cast<uint64_t>(args[5].as.i64) : args[5].as.u64;
    if (depth >= static_cast<uint64_t>(std::numeric_limits<int>::max()))
      return fail(state, call, "OverflowError", "maximum depth is too large");
    X509_VERIFY_PARAM_set_depth(X509_STORE_CTX_get0_param(context.get()),
                                 static_cast<int>(depth) + 1);
  }
  if (server)
    X509_VERIFY_PARAM_set_hostflags(
        X509_STORE_CTX_get0_param(context.get()),
        X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
  if (X509_STORE_CTX_set_purpose(
          context.get(), server ? X509_PURPOSE_SSL_SERVER : X509_PURPOSE_SSL_CLIENT) != 1 ||
      (server && X509_VERIFY_PARAM_set1_host(
          X509_STORE_CTX_get0_param(context.get()), hostname,
          static_cast<size_t>(hostname_size)) != 1))
    return fail(state, call, "ValueError", "unable to configure verification");
  if (X509_verify_cert(context.get()) != 1)
    return verification_error(
        state, call,
        X509_verify_cert_error_string(X509_STORE_CTX_get_error(context.get())));
  STACK_OF(X509)* chain = X509_STORE_CTX_get1_chain(context.get());
  if (chain == nullptr) return X3_STATUS_ERROR;
  X3Value values = state->host->value_list(runtime);
  if (values.tag == X3_TAG_INVALID) {
    sk_X509_pop_free(chain, X509_free);
    return X3_STATUS_ERROR;
  }
  while (sk_X509_num(chain) > 0) {
    X509* certificate = sk_X509_shift(chain);
    X3Value instance = x3_value_invalid();
    if (make_certificate(state, runtime, certificate, &instance) !=
        X3_STATUS_OK) {
      sk_X509_pop_free(chain, X509_free);
      state->host->value_release(values);
      return X3_STATUS_ERROR;
    }
    const X3Status appended = state->host->list_append(
        runtime, values, instance);
    state->host->value_release(instance);
    if (appended != X3_STATUS_OK) {
      sk_X509_pop_free(chain, X509_free);
      state->host->value_release(values);
      return X3_STATUS_ERROR;
    }
  }
  sk_X509_free(chain);
  *result = values;
  return X3_STATUS_OK;
}

X3Status store_verify_server_dns(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  return verify_chain(call, runtime, user_data, args, argc, result, true);
}

X3Status enforce_extension_rules(CryptographyNativeState* state,
                                 X3CallContext* call, X509* certificate,
                                 const ExtensionPolicyData* policy) {
  for (const ExtensionRule& rule : policy->rules) {
    const int index = X509_get_ext_by_NID(certificate, rule.nid, -1);
    if (index < 0) {
      if (rule.presence == Presence::Required)
        return verification_error(state, call,
                                  "certificate is missing required extension");
      continue;
    }
    if (rule.presence == Presence::Forbidden)
      return verification_error(state, call,
                                "certificate has forbidden extension");
    X509_EXTENSION* extension = X509_get_ext(certificate, index);
    const bool critical = X509_EXTENSION_get_critical(extension) != 0;
    if ((rule.criticality == Criticality::Critical && !critical) ||
        (rule.criticality == Criticality::NonCritical && critical))
      return verification_error(state, call,
                                "certificate extension has invalid criticality");
  }
  return X3_STATUS_OK;
}

X3Status enforce_chain_extensions(CryptographyNativeState* state,
                                  X3CallContext* call, X3Runtime* runtime,
                                  X3Value chain, PolicyData* policy) {
  auto* ee = extension_policy_data(state, call, policy->ee_policy);
  auto* ca = extension_policy_data(state, call, policy->ca_policy);
  if (ee == nullptr || ca == nullptr) return X3_STATUS_ERROR;
  uint64_t count = 0;
  if (state->host->len(runtime, chain, &count) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  for (uint64_t index = 0; index < count; ++index) {
    X3Value item = x3_value_invalid();
    if (state->host->get_item(runtime, chain, x3_value_uint64(index),
                              &item) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    auto* certificate = certificate_data(state, call, item);
    X3Status status = certificate == nullptr ? X3_STATUS_ERROR
        : enforce_extension_rules(state, call, certificate->certificate,
                                  index == 0 ? ee : ca);
    state->host->value_release(item);
    if (status != X3_STATUS_OK) return status;
  }
  return X3_STATUS_OK;
}

PolicyBuilderData* builder_data(CryptographyNativeState* state,
                                X3CallContext* call, X3Value object) {
  auto* data = static_cast<PolicyBuilderData*>(
      state->host->instance_get_native_data(object, kPolicyBuilderType));
  if (data == nullptr) fail(state, call, "TypeError", "expected PolicyBuilder");
  return data;
}

PolicyData* policy_data(CryptographyNativeState* state, X3CallContext* call,
                        X3Value object) {
  auto* data = static_cast<PolicyData*>(
      state->host->instance_get_native_data(object, kPolicyType));
  if (data == nullptr) fail(state, call, "TypeError", "expected Policy");
  return data;
}

ServerVerifierData* server_verifier_data(CryptographyNativeState* state,
                                        X3CallContext* call, X3Value object) {
  auto* data = static_cast<ServerVerifierData*>(
      state->host->instance_get_native_data(object, kServerVerifierType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected ServerVerifier");
  return data;
}

ClientVerifierData* client_verifier_data(CryptographyNativeState* state,
                                        X3CallContext* call, X3Value object) {
  auto* data = static_cast<ClientVerifierData*>(
      state->host->instance_get_native_data(object, kClientVerifierType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected ClientVerifier");
  return data;
}

VerifiedClientData* verified_client_data(CryptographyNativeState* state,
                                         X3CallContext* call, X3Value object) {
  auto* data = static_cast<VerifiedClientData*>(
      state->host->instance_get_native_data(object, kVerifiedClientType));
  if (data == nullptr)
    fail(state, call, "TypeError", "expected VerifiedClient");
  return data;
}

X3Status verifier_object_init(X3CallContext* call, X3Runtime*,
                              void* user_data, const X3Value*, uint32_t,
                              X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return fail(state, call, "TypeError",
              "verification objects must be created by PolicyBuilder");
}

X3Status make_builder(CryptographyNativeState* state, X3Runtime* runtime,
                      const PolicyBuilderData* original, X3Value* result) {
  auto data = std::make_unique<PolicyBuilderData>();
  data->host = state->host;
  if (original != nullptr) {
    data->store = original->store;
    if (data->store.tag != X3_TAG_INVALID)
      state->host->value_retain(data->store);
    data->validation_seconds = original->validation_seconds;
    data->has_time = original->has_time;
    data->max_depth = original->max_depth;
    data->has_max_depth = original->has_max_depth;
    data->ca_policy = original->ca_policy;
    data->ee_policy = original->ee_policy;
    if (data->ca_policy.tag != X3_TAG_INVALID)
      state->host->value_retain(data->ca_policy);
    if (data->ee_policy.tag != X3_TAG_INVALID)
      state->host->value_retain(data->ee_policy);
  }
  X3Value instance = state->host->value_instance(
      runtime, state->x509_policy_builder_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kPolicyBuilderType, data.get(),
          cleanup_policy_builder) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status builder_init(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "PolicyBuilder() takes no arguments");
  auto data = std::make_unique<PolicyBuilderData>();
  data->host = state->host;
  if (state->host->instance_set_native_data(
          args[0], kPolicyBuilderType, data.get(),
          cleanup_policy_builder) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status builder_store(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "store is required");
  auto* builder = builder_data(state, call, args[0]);
  if (builder == nullptr) return X3_STATUS_ERROR;
  if (builder->store.tag != X3_TAG_INVALID)
    return fail(state, call, "ValueError",
                "The trust store may only be set once.");
  if (state->host->instance_get_native_data(args[1], kStoreType) == nullptr)
    return fail(state, call, "TypeError", "expected Store");
  if (make_builder(state, runtime, builder, result) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto* copy = static_cast<PolicyBuilderData*>(
      state->host->instance_get_native_data(*result, kPolicyBuilderType));
  if (copy->store.tag != X3_TAG_INVALID)
    state->host->value_release(copy->store);
  copy->store = args[1];
  state->host->value_retain(copy->store);
  return X3_STATUS_OK;
}

X3Status builder_time(X3CallContext* call, X3Runtime* runtime,
                      void* user_data, const X3Value* args,
                      uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "time is required");
  auto* builder = builder_data(state, call, args[0]);
  if (builder == nullptr) return X3_STATUS_ERROR;
  if (builder->has_time)
    return fail(state, call, "ValueError",
                "The validation time may only be set once.");
  X3Value timezone = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "tzinfo", &timezone) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const bool naive = timezone.tag == X3_TAG_NONE;
  state->host->value_release(timezone);
  X3Value method = x3_value_invalid();
  X3Value seconds = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1],
                            naive ? "utctimetuple" : "timestamp", &method) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status called = state->host->call(runtime, method, nullptr, 0,
                                            &seconds);
  state->host->value_release(method);
  if (called != X3_STATUS_OK) return X3_STATUS_ERROR;
  if (naive) {
    X3Value importer = x3_value_invalid();
    X3Value module_name = x3_value_invalid();
    X3Value module = x3_value_invalid();
    X3Value timegm = x3_value_invalid();
    X3Value utc_seconds = x3_value_invalid();
    X3Status converted = X3_STATUS_ERROR;
    if (state->host->builtin_value(state->host, "__import__", &importer) ==
        X3_STATUS_OK) {
      module_name = state->host->value_string(runtime, "calendar");
      if (module_name.tag != X3_TAG_INVALID &&
          state->host->call(runtime, importer, &module_name, 1, &module) ==
              X3_STATUS_OK &&
          state->host->get_attr(runtime, module, "timegm", &timegm) ==
              X3_STATUS_OK)
        converted = state->host->call(runtime, timegm, &seconds, 1,
                                      &utc_seconds);
    }
    for (X3Value item : {timegm, module, module_name, importer})
      if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
    state->host->value_release(seconds);
    if (converted != X3_STATUS_OK) return X3_STATUS_ERROR;
    seconds = utc_seconds;
  }
  double value = 0;
  if (seconds.tag == X3_TAG_DOUBLE)
    value = seconds.as.f64;
  else if (seconds.tag == X3_TAG_INT64)
    value = static_cast<double>(seconds.as.i64);
  else {
    state->host->value_release(seconds);
    return fail(state, call, "TypeError", "invalid validation time");
  }
  state->host->value_release(seconds);
  if (!std::isfinite(value) ||
      value < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
      value >= static_cast<double>(std::numeric_limits<int64_t>::max()))
    return fail(state, call, "OverflowError", "validation time out of range");
  if (make_builder(state, runtime, builder, result) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto* copy = static_cast<PolicyBuilderData*>(
      state->host->instance_get_native_data(*result, kPolicyBuilderType));
  copy->validation_seconds = static_cast<int64_t>(std::floor(value));
  copy->has_time = true;
  return X3_STATUS_OK;
}

X3Status builder_max_chain_depth(X3CallContext* call, X3Runtime* runtime,
                                 void* user_data, const X3Value* args,
                                 uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "max depth is required");
  auto* builder = builder_data(state, call, args[0]);
  if (builder == nullptr) return X3_STATUS_ERROR;
  if (builder->has_max_depth)
    return fail(state, call, "ValueError",
                "The maximum chain depth may only be set once.");
  if (args[1].tag != X3_TAG_INT64 && args[1].tag != X3_TAG_UINT64)
    return fail(state, call, "TypeError", "max depth must be int");
  if (args[1].tag == X3_TAG_INT64 && args[1].as.i64 < 0)
    return fail(state, call, "OverflowError",
                "out of range integral type conversion attempted");
  const uint64_t depth = args[1].tag == X3_TAG_INT64
      ? static_cast<uint64_t>(args[1].as.i64) : args[1].as.u64;
  if (depth > static_cast<uint64_t>(std::numeric_limits<uint8_t>::max()))
    return fail(state, call, "OverflowError",
                "out of range integral type conversion attempted");
  if (make_builder(state, runtime, builder, result) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto* copy = static_cast<PolicyBuilderData*>(
      state->host->instance_get_native_data(*result, kPolicyBuilderType));
  copy->max_depth = static_cast<uint32_t>(depth);
  copy->has_max_depth = true;
  return X3_STATUS_OK;
}

X3Status builder_extension_policies_kw(
    X3CallContext* call, X3Runtime* runtime, void* user_data,
    const X3Value* args, uint32_t argc, const X3KeywordArg* kwargs,
    uint32_t kwargc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1 || kwargc != 2)
    return fail(state, call, "TypeError",
                "extension_policies requires ca_policy and ee_policy keywords");
  auto* builder = builder_data(state, call, args[0]);
  if (builder == nullptr) return X3_STATUS_ERROR;
  if (builder->ca_policy.tag != X3_TAG_INVALID)
    return fail(state, call, "ValueError",
                "The extension policies may only be set once.");
  X3Value ca = x3_value_invalid();
  X3Value ee = x3_value_invalid();
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (std::string(kwargs[index].name) == "ca_policy") ca = kwargs[index].value;
    else if (std::string(kwargs[index].name) == "ee_policy")
      ee = kwargs[index].value;
    else
      return fail(state, call, "TypeError", "unexpected policy keyword");
  }
  if (extension_policy_data(state, call, ca) == nullptr ||
      extension_policy_data(state, call, ee) == nullptr)
    return X3_STATUS_ERROR;
  if (make_builder(state, runtime, builder, result) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  auto* copy = static_cast<PolicyBuilderData*>(
      state->host->instance_get_native_data(*result, kPolicyBuilderType));
  copy->ca_policy = ca;
  copy->ee_policy = ee;
  state->host->value_retain(ca);
  state->host->value_retain(ee);
  return X3_STATUS_OK;
}

X3Status builder_extension_policies(X3CallContext* call, X3Runtime*,
                                    void* user_data, const X3Value*,
                                    uint32_t, X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return fail(state, call, "TypeError",
              "extension_policies requires ca_policy and ee_policy keywords");
}

X3Status set_policy_extensions(CryptographyNativeState* state,
                               X3CallContext* call, X3Runtime* runtime,
                               const PolicyBuilderData* builder,
                               PolicyData* policy, bool server) {
  if (builder->ca_policy.tag != X3_TAG_INVALID) {
    policy->ca_policy = builder->ca_policy;
    policy->ee_policy = builder->ee_policy;
    state->host->value_retain(policy->ca_policy);
    state->host->value_retain(policy->ee_policy);
  } else {
    if (make_extension_policy(state, runtime, 1, nullptr,
                              &policy->ca_policy) != X3_STATUS_OK ||
        make_extension_policy(state, runtime, 2, nullptr,
                              &policy->ee_policy) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  }
  auto* ca = extension_policy_data(state, call, policy->ca_policy);
  auto* ee = extension_policy_data(state, call, policy->ee_policy);
  if (ca == nullptr || ee == nullptr) return X3_STATUS_ERROR;
  if (ca->rules[5].presence != Presence::Required)
    return fail(state, call, "ValueError",
                "A CA extension policy must require the basicConstraints extension to be present.");
  if (server && ee->rules[4].presence != Presence::Required)
    return fail(state, call, "ValueError",
                "An EE extension policy used for server verification must require the subjectAltName extension to be present.");
  return X3_STATUS_OK;
}

X3Status builder_build_server_verifier(X3CallContext* call, X3Runtime* runtime,
                                       void* user_data, const X3Value* args,
                                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "subject is required");
  auto* builder = builder_data(state, call, args[0]);
  if (builder == nullptr) return X3_STATUS_ERROR;
  if (builder->store.tag == X3_TAG_INVALID)
    return fail(state, call, "ValueError",
                "A server verifier must have a trust store.");
  X3Value subject_value = x3_value_invalid();
  if (state->host->get_attr(runtime, args[1], "value", &subject_value) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* hostname = nullptr;
  uint64_t hostname_size = 0;
  const X3Status string_status = state->host->value_string_data(
      runtime, subject_value, &hostname, &hostname_size);
  state->host->value_release(subject_value);
  if (string_status != X3_STATUS_OK)
    return fail(state, call, "TypeError", "subject must be a DNSName");
  auto policy = std::make_unique<PolicyData>();
  policy->host = state->host;
  policy->subject = args[1];
  state->host->value_retain(policy->subject);
  policy->validation_seconds = builder->has_time
      ? builder->validation_seconds : static_cast<int64_t>(std::time(nullptr));
  policy->max_depth = builder->max_depth;
  if (set_policy_extensions(state, call, runtime, builder, policy.get(), true) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value policy_value = state->host->value_instance(
      runtime, state->x509_policy_class);
  if (policy_value.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          policy_value, kPolicyType, policy.get(), cleanup_policy) !=
      X3_STATUS_OK) {
    state->host->value_release(policy_value);
    return X3_STATUS_ERROR;
  }
  policy.release();
  auto verifier = std::make_unique<ServerVerifierData>();
  verifier->host = state->host;
  verifier->store = builder->store;
  state->host->value_retain(verifier->store);
  verifier->policy = policy_value;
  X3Value instance = state->host->value_instance(
      runtime, state->x509_server_verifier_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kServerVerifierType, verifier.get(),
          cleanup_server_verifier) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  verifier.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status builder_build_client_verifier(X3CallContext* call, X3Runtime* runtime,
                                       void* user_data, const X3Value* args,
                                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return fail(state, call, "TypeError", "build_client_verifier() takes no arguments");
  auto* builder = builder_data(state, call, args[0]);
  if (builder == nullptr) return X3_STATUS_ERROR;
  if (builder->store.tag == X3_TAG_INVALID)
    return fail(state, call, "ValueError",
                "A client verifier must have a trust store.");
  auto policy = std::make_unique<PolicyData>();
  policy->host = state->host;
  policy->client = true;
  policy->validation_seconds = builder->has_time
      ? builder->validation_seconds : static_cast<int64_t>(std::time(nullptr));
  policy->max_depth = builder->max_depth;
  if (set_policy_extensions(state, call, runtime, builder, policy.get(), false) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value policy_value = state->host->value_instance(
      runtime, state->x509_policy_class);
  if (policy_value.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          policy_value, kPolicyType, policy.get(), cleanup_policy) !=
      X3_STATUS_OK) {
    state->host->value_release(policy_value);
    return X3_STATUS_ERROR;
  }
  policy.release();
  auto verifier = std::make_unique<ClientVerifierData>();
  verifier->host = state->host;
  verifier->store = builder->store;
  state->host->value_retain(verifier->store);
  verifier->policy = policy_value;
  X3Value instance = state->host->value_instance(
      runtime, state->x509_client_verifier_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kClientVerifierType, verifier.get(),
          cleanup_client_verifier) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  verifier.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status server_verify(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError", "leaf and intermediates are required");
  auto* verifier = server_verifier_data(state, call, args[0]);
  if (verifier == nullptr) return X3_STATUS_ERROR;
  auto* policy = policy_data(state, call, verifier->policy);
  if (policy == nullptr) return X3_STATUS_ERROR;
  X3Value hostname = x3_value_invalid();
  if (state->host->get_attr(runtime, policy->subject, "value", &hostname) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Value verify_args[] = {
      verifier->store, args[1], args[2], hostname,
      x3_value_int64(policy->validation_seconds),
      x3_value_uint64(policy->max_depth)};
  const X3Status status = store_verify_server_dns(
      call, runtime, user_data, verify_args, 6, result);
  state->host->value_release(hostname);
  if (status != X3_STATUS_OK) return status;
  if (enforce_chain_extensions(state, call, runtime, *result, policy) !=
      X3_STATUS_OK) {
    state->host->value_release(*result);
    *result = x3_value_invalid();
    return X3_STATUS_ERROR;
  }
  return X3_STATUS_OK;
}

X3Status import_class(CryptographyNativeState* state, X3Runtime* runtime,
                      const char* module_path, const char* class_name,
                      X3Value* result) {
  X3Value importer = x3_value_invalid();
  X3Value module_name = x3_value_invalid();
  X3Value fromlist = x3_value_invalid();
  X3Value name = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) goto done;
  module_name = state->host->value_string(runtime, module_path);
  fromlist = state->host->value_list(runtime);
  name = state->host->value_string(runtime, class_name);
  if (module_name.tag == X3_TAG_INVALID || fromlist.tag == X3_TAG_INVALID ||
      name.tag == X3_TAG_INVALID ||
      state->host->list_append(runtime, fromlist, name) != X3_STATUS_OK)
    goto done;
  {
    const X3Value import_args[] = {
        module_name, x3_value_none(), x3_value_none(), fromlist};
    if (state->host->call(runtime, importer, import_args, 4, &module) !=
        X3_STATUS_OK) goto done;
  }
  status = state->host->get_attr(runtime, module, class_name, result);
done:
  for (X3Value item : {module, name, fromlist, module_name, importer})
    if (item.tag != X3_TAG_INVALID) state->host->value_release(item);
  return status;
}

X3Status client_subjects(CryptographyNativeState* state, X3CallContext* call,
                         X3Runtime* runtime, X509* certificate,
                         bool required, X3Value* result) {
  std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> names(
      static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(
          certificate, NID_subject_alt_name, nullptr, nullptr)),
      GENERAL_NAMES_free);
  if (!names) {
    if (required)
      return verification_error(state, call,
                                "Certificate is missing required subjectAltName extension");
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  X3Value subjects = state->host->value_list(runtime);
  if (subjects.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  for (int index = 0; index < sk_GENERAL_NAME_num(names.get()); ++index) {
    const GENERAL_NAME* name = sk_GENERAL_NAME_value(names.get(), index);
    const ASN1_IA5STRING* raw = nullptr;
    const char* class_name = nullptr;
    if (name->type == GEN_DNS) {
      raw = name->d.dNSName;
      class_name = "DNSName";
    } else if (name->type == GEN_EMAIL) {
      raw = name->d.rfc822Name;
      class_name = "RFC822Name";
    } else if (name->type == GEN_URI) {
      raw = name->d.uniformResourceIdentifier;
      class_name = "UniformResourceIdentifier";
    } else {
      state->host->value_release(subjects);
      return verification_error(state, call,
                                "unsupported subjectAltName type");
    }
    const unsigned char* bytes = ASN1_STRING_get0_data(raw);
    const int size = ASN1_STRING_length(raw);
    if (size <= 0 || std::string(reinterpret_cast<const char*>(bytes),
                                  static_cast<size_t>(size)).find('\0') !=
                         std::string::npos) {
      state->host->value_release(subjects);
      return verification_error(state, call, "invalid subjectAltName value");
    }
    X3Value klass = x3_value_invalid();
    if (import_class(state, runtime, "cryptography.x509.general_name",
                     class_name, &klass) != X3_STATUS_OK) {
      state->host->value_release(subjects);
      return X3_STATUS_ERROR;
    }
    const std::string value(reinterpret_cast<const char*>(bytes),
                            static_cast<size_t>(size));
    X3Value text_value = state->host->value_string(runtime, value.c_str());
    X3Value subject = x3_value_invalid();
    X3Status status = text_value.tag == X3_TAG_INVALID
        ? X3_STATUS_ERROR
        : state->host->call(runtime, klass, &text_value, 1, &subject);
    state->host->value_release(klass);
    if (text_value.tag != X3_TAG_INVALID)
      state->host->value_release(text_value);
    if (status == X3_STATUS_OK)
      status = state->host->list_append(runtime, subjects, subject);
    if (subject.tag != X3_TAG_INVALID)
      state->host->value_release(subject);
    if (status != X3_STATUS_OK) {
      state->host->value_release(subjects);
      return X3_STATUS_ERROR;
    }
  }
  *result = subjects;
  return X3_STATUS_OK;
}

X3Status client_verify(X3CallContext* call, X3Runtime* runtime,
                       void* user_data, const X3Value* args,
                       uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3)
    return fail(state, call, "TypeError", "leaf and intermediates are required");
  auto* verifier = client_verifier_data(state, call, args[0]);
  if (verifier == nullptr) return X3_STATUS_ERROR;
  auto* policy = policy_data(state, call, verifier->policy);
  if (policy == nullptr) return X3_STATUS_ERROR;
  auto* leaf = certificate_data(state, call, args[1]);
  if (leaf == nullptr) return X3_STATUS_ERROR;
  const X3Value verify_args[] = {
      verifier->store, args[1], args[2], x3_value_none(),
      x3_value_int64(policy->validation_seconds),
      x3_value_uint64(policy->max_depth)};
  X3Value chain = x3_value_invalid();
  if (verify_chain(call, runtime, user_data, verify_args, 6, &chain, false) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (enforce_chain_extensions(state, call, runtime, chain, policy) !=
      X3_STATUS_OK) {
    state->host->value_release(chain);
    return X3_STATUS_ERROR;
  }
  auto* ee_policy = extension_policy_data(state, call, policy->ee_policy);
  if (ee_policy == nullptr) {
    state->host->value_release(chain);
    return X3_STATUS_ERROR;
  }
  X3Value subjects = x3_value_invalid();
  if (client_subjects(state, call, runtime, leaf->certificate,
                      ee_policy->rules[4].presence == Presence::Required,
                      &subjects) !=
      X3_STATUS_OK) {
    state->host->value_release(chain);
    return X3_STATUS_ERROR;
  }
  auto data = std::make_unique<VerifiedClientData>();
  data->host = state->host;
  data->chain = chain;
  data->subjects = subjects;
  X3Value instance = state->host->value_instance(
      runtime, state->x509_verified_client_class);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(
          instance, kVerifiedClientType, data.get(),
          cleanup_verified_client) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status server_policy(X3CallContext* call, X3Runtime*, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = server_verifier_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->policy;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status server_store(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = server_verifier_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->store;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status client_policy(X3CallContext* call, X3Runtime*, void* user_data,
                       const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = client_verifier_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->policy;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status client_store(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = client_verifier_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->store;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status verified_client_chain(X3CallContext* call, X3Runtime*,
                               void* user_data, const X3Value* args,
                               uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = verified_client_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->chain;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status verified_client_subjects(X3CallContext* call, X3Runtime*,
                                  void* user_data, const X3Value* args,
                                  uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = verified_client_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = data->subjects;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status policy_subject(X3CallContext* call, X3Runtime*, void* user_data,
                        const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = policy_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->client) {
    *result = x3_value_none();
    return X3_STATUS_OK;
  }
  *result = data->subject;
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

X3Status policy_max_depth(X3CallContext* call, X3Runtime*, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = policy_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  *result = x3_value_uint64(data->max_depth);
  return X3_STATUS_OK;
}

X3Status policy_extended_key_usage(X3CallContext* call, X3Runtime* runtime,
                                   void* user_data, const X3Value* args,
                                   uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = policy_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  X3Value dotted = state->host->value_string(
      runtime, data->client ? "1.3.6.1.5.5.7.3.2" : "1.3.6.1.5.5.7.3.1");
  if (dotted.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  const X3Status status = state->host->call(
      runtime, state->oid_class, &dotted, 1, result);
  state->host->value_release(dotted);
  return status;
}

X3Status policy_validation_time(X3CallContext* call, X3Runtime* runtime,
                                void* user_data, const X3Value* args,
                                uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return X3_STATUS_ERROR;
  auto* data = policy_data(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  const std::time_t seconds = static_cast<std::time_t>(data->validation_seconds);
  std::tm utc{};
#ifdef _WIN32
  if (gmtime_s(&utc, &seconds) != 0)
#else
  if (gmtime_r(&seconds, &utc) == nullptr)
#endif
    return fail(state, call, "ValueError", "validation time is out of range");
  X3Value importer = x3_value_invalid();
  X3Value module_name = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value klass = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) goto done;
  module_name = state->host->value_string(runtime, "datetime");
  if (module_name.tag == X3_TAG_INVALID ||
      state->host->call(runtime, importer, &module_name, 1, &module) !=
          X3_STATUS_OK ||
      state->host->get_attr(runtime, module, "datetime", &klass) !=
          X3_STATUS_OK) goto done;
  {
    const X3Value components[] = {
        x3_value_int64(utc.tm_year + 1900),
        x3_value_int64(utc.tm_mon + 1),
        x3_value_int64(utc.tm_mday),
        x3_value_int64(utc.tm_hour),
        x3_value_int64(utc.tm_min),
        x3_value_int64(utc.tm_sec),
    };
    status = state->host->call(runtime, klass, components, 6, result);
  }
done:
  for (X3Value value : {klass, module, module_name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

}  // namespace

X3Status register_x509_module(X3Module* root, CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* x509 = nullptr;
  if (host->add_module(host, kModuleName, &x509) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value module_value = x3_value_invalid();
  if (host->module_get_value(x509, &module_value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status attached = host->module_add_value(root, "x509", module_value);
  host->value_release(module_value);
  if (attached != X3_STATUS_OK) return attached;
  const X3NativeFunctionDef certificate_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       certificate_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_bytes",
       certificate_public_bytes, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "fingerprint",
       certificate_fingerprint, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "_signed_certificate_timestamps",
       certificate_scts, state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(
          x509, "Certificate", certificate_methods,
          static_cast<uint32_t>(std::size(certificate_methods)),
          &state->x509_certificate_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value serial_property = x3_value_invalid();
  if (host->property_create(host->runtime, "serial_number",
                            certificate_serial_number, nullptr, state,
                            &serial_property) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status property_status = host->class_add_value(
      state->x509_certificate_class, "serial_number", serial_property);
  host->value_release(serial_property);
  if (property_status != X3_STATUS_OK) return property_status;
  const X3NativeFunctionDef csr_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       document_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_bytes",
       document_public_bytes, state, 2, 2, 0, nullptr},
  };
  const X3NativeFunctionDef crl_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       document_init, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "public_bytes",
       document_public_bytes, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__len__",
       crl_length, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "fingerprint",
       crl_fingerprint, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__getitem__",
       crl_get_index, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "get_revoked_certificate_by_serial_number",
       crl_lookup_serial, state, 2, 2, 0, nullptr},
  };
  const X3NativeFunctionDef revoked_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       document_init, state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(
          x509, "CertificateSigningRequest", csr_methods,
          static_cast<uint32_t>(std::size(csr_methods)),
          &state->x509_csr_class) != X3_STATUS_OK ||
      host->module_add_class(
          x509, "CertificateRevocationList", crl_methods,
          static_cast<uint32_t>(std::size(crl_methods)),
          &state->x509_crl_class) != X3_STATUS_OK ||
      host->module_add_class(
          x509, "RevokedCertificate", revoked_methods,
          static_cast<uint32_t>(std::size(revoked_methods)),
          &state->x509_revoked_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value valid_property = x3_value_invalid();
  if (host->property_create(host->runtime, "is_signature_valid",
                            csr_signature_valid, nullptr, state,
                            &valid_property) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status valid_status = host->class_add_value(
      state->x509_csr_class, "is_signature_valid", valid_property);
  host->value_release(valid_property);
  if (valid_status != X3_STATUS_OK) return valid_status;
  auto add_property = [&](X3Value klass, const char* name,
                          auto getter) -> X3Status {
    X3Value property = x3_value_invalid();
    if (host->property_create(host->runtime, name, getter, nullptr,
                              state, &property) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
    const X3Status status = host->class_add_value(klass, name, property);
    host->value_release(property);
    return status;
  };
  if (add_property(state->x509_csr_class, "signature",
                   document_signature) != X3_STATUS_OK ||
      add_property(state->x509_certificate_class, "extensions",
                   certificate_extensions) != X3_STATUS_OK ||
      add_property(state->x509_certificate_class, "subject",
                   certificate_subject) != X3_STATUS_OK ||
      add_property(state->x509_certificate_class, "issuer",
                   certificate_issuer) != X3_STATUS_OK ||
      add_property(state->x509_revoked_class, "serial_number",
                   revoked_serial) != X3_STATUS_OK ||
      add_property(state->x509_revoked_class, "revocation_date",
                   revoked_date_naive) != X3_STATUS_OK ||
      add_property(state->x509_revoked_class, "revocation_date_utc",
                   revoked_date_utc) != X3_STATUS_OK ||
      add_property(state->x509_revoked_class, "extensions",
                   revoked_extensions) != X3_STATUS_OK ||
      add_property(state->x509_csr_class, "tbs_certrequest_bytes",
                   document_tbs_bytes) != X3_STATUS_OK ||
      add_property(state->x509_crl_class, "signature",
                   document_signature) != X3_STATUS_OK ||
      add_property(state->x509_crl_class, "tbs_certlist_bytes",
                   document_tbs_bytes) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef sct_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       sct_init, state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(
          x509, "Sct", sct_methods,
          static_cast<uint32_t>(std::size(sct_methods)),
          &state->x509_sct_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (add_property(state->x509_sct_class, "log_id", sct_log_id) !=
          X3_STATUS_OK ||
      add_property(state->x509_sct_class, "signature", sct_signature) !=
          X3_STATUS_OK ||
      add_property(state->x509_sct_class, "extension_bytes", sct_extensions) !=
          X3_STATUS_OK ||
      add_property(state->x509_sct_class, "timestamp", sct_timestamp) !=
          X3_STATUS_OK ||
      add_property(state->x509_sct_class, "signature_hash_algorithm",
                   sct_signature_hash_algorithm) !=
          X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef store_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__",
       store_init, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "_verify_server_dns",
       store_verify_server_dns, state, 5, 6, 0, nullptr},
  };
  if (host->module_add_class(
          x509, "Store", store_methods,
          static_cast<uint32_t>(std::size(store_methods)),
          &state->x509_store_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef builder_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", builder_init, state,
       1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "store", builder_store, state,
       2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "time", builder_time, state,
       2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "max_chain_depth",
       builder_max_chain_depth, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "build_server_verifier",
       builder_build_server_verifier, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "build_client_verifier",
       builder_build_client_verifier, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "extension_policies",
       builder_extension_policies, state, 1, 1, 0,
       builder_extension_policies_kw},
  };
  const X3NativeFunctionDef verifier_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", verifier_object_init,
       state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "verify", server_verify, state,
       3, 3, 0, nullptr},
  };
  const X3NativeFunctionDef policy_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", verifier_object_init,
       state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef client_verifier_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", verifier_object_init,
       state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "verify", client_verify,
       state, 3, 3, 0, nullptr},
  };
  const X3NativeFunctionDef verified_client_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", verifier_object_init,
       state, 1, 1, 0, nullptr},
  };
  const X3NativeFunctionDef extension_policy_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", verifier_object_init,
       state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "require_not_present",
       extension_policy_forbid, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "may_be_present",
       extension_policy_maybe, state, 4, 4, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "require_present",
       extension_policy_require, state, 4, 4, 0, nullptr},
  };
  const X3NativeFunctionDef criticality_methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", verifier_object_init,
       state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__repr__", criticality_repr,
       state, 1, 1, 0, nullptr},
  };
  if (host->module_add_class(
          x509, "PolicyBuilder", builder_methods,
          static_cast<uint32_t>(std::size(builder_methods)),
          &state->x509_policy_builder_class) != X3_STATUS_OK ||
      host->module_add_class(x509, "Policy", policy_methods,
                             static_cast<uint32_t>(std::size(policy_methods)),
                             &state->x509_policy_class) != X3_STATUS_OK ||
      host->module_add_class(
          x509, "ServerVerifier", verifier_methods,
          static_cast<uint32_t>(std::size(verifier_methods)),
          &state->x509_server_verifier_class) != X3_STATUS_OK ||
      host->module_add_class(
          x509, "ClientVerifier", client_verifier_methods,
          static_cast<uint32_t>(std::size(client_verifier_methods)),
          &state->x509_client_verifier_class) != X3_STATUS_OK ||
      host->module_add_class(
          x509, "VerifiedClient", verified_client_methods,
          static_cast<uint32_t>(std::size(verified_client_methods)),
          &state->x509_verified_client_class) != X3_STATUS_OK ||
      host->module_add_class(
          x509, "ExtensionPolicy", extension_policy_methods,
          static_cast<uint32_t>(std::size(extension_policy_methods)),
          &state->x509_extension_policy_class) != X3_STATUS_OK ||
      host->module_add_class(
          x509, "Criticality", criticality_methods,
          static_cast<uint32_t>(std::size(criticality_methods)),
          &state->x509_criticality_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value verification_module = host->value_string(
      host->runtime, "cryptography.x509.verification");
  if (verification_module.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  const X3Status builder_module_status = host->class_add_value(
      state->x509_policy_builder_class, "__module__", verification_module);
  const X3Status policy_module_status = host->class_add_value(
      state->x509_policy_class, "__module__", verification_module);
  const X3Status extension_module_status = host->class_add_value(
      state->x509_extension_policy_class, "__module__", verification_module);
  const X3Status criticality_module_status = host->class_add_value(
      state->x509_criticality_class, "__module__", verification_module);
  host->value_release(verification_module);
  if (builder_module_status != X3_STATUS_OK ||
      policy_module_status != X3_STATUS_OK ||
      extension_module_status != X3_STATUS_OK ||
      criticality_module_status != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef policy_factories[] = {
      {sizeof(X3NativeFunctionDef), "_extension_policy_permit_all",
       extension_policy_permit_all, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "_extension_policy_webpki_ca",
       extension_policy_webpki_ca, state, 0, 0, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "_extension_policy_webpki_ee",
       extension_policy_webpki_ee, state, 0, 0, 0, nullptr},
  };
  for (const auto& factory : policy_factories)
    if (host->module_add_function(x509, &factory) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  auto add_static_factory = [&](const char* source_name,
                                const char* target_name) -> X3Status {
    X3Value module = x3_value_invalid();
    X3Value source = x3_value_invalid();
    X3Value staticmethod_class = x3_value_invalid();
    X3Value wrapper = x3_value_invalid();
    X3Status status = X3_STATUS_ERROR;
    if (host->module_get_value(x509, &module) == X3_STATUS_OK &&
        host->get_attr(host->runtime, module, source_name, &source) ==
            X3_STATUS_OK &&
        host->builtin_value(host, "staticmethod", &staticmethod_class) ==
            X3_STATUS_OK &&
        host->call(host->runtime, staticmethod_class, &source, 1, &wrapper) ==
            X3_STATUS_OK)
      status = host->class_add_value(state->x509_extension_policy_class,
                                     target_name, wrapper);
    for (X3Value item : {wrapper, staticmethod_class, source, module})
      if (item.tag != X3_TAG_INVALID) host->value_release(item);
    return status;
  };
  if (add_static_factory("_extension_policy_permit_all", "permit_all") !=
          X3_STATUS_OK ||
      add_static_factory("_extension_policy_webpki_ca",
                         "webpki_defaults_ca") != X3_STATUS_OK ||
      add_static_factory("_extension_policy_webpki_ee",
                         "webpki_defaults_ee") != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  for (const auto& entry : std::array<std::pair<const char*, Criticality>, 3>{
           {{"CRITICAL", Criticality::Critical},
            {"AGNOSTIC", Criticality::Agnostic},
            {"NON_CRITICAL", Criticality::NonCritical}}}) {
    auto data = std::make_unique<CriticalityData>();
    data->value = entry.second;
    X3Value instance = host->value_instance(
        host->runtime, state->x509_criticality_class);
    if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
    if (host->instance_set_native_data(instance, kCriticalityType,
                                        data.get(), cleanup_criticality) !=
        X3_STATUS_OK) {
      host->value_release(instance);
      return X3_STATUS_ERROR;
    }
    data.release();
    const X3Status status = host->class_add_value(
        state->x509_criticality_class, entry.first, instance);
    host->value_release(instance);
    if (status != X3_STATUS_OK) return status;
  }
  if (add_property(state->x509_policy_class, "subject", policy_subject) !=
          X3_STATUS_OK ||
      add_property(state->x509_policy_class, "max_chain_depth",
                   policy_max_depth) != X3_STATUS_OK ||
      add_property(state->x509_policy_class, "validation_time",
                   policy_validation_time) != X3_STATUS_OK ||
      add_property(state->x509_policy_class, "extended_key_usage",
                   policy_extended_key_usage) != X3_STATUS_OK ||
      add_property(state->x509_server_verifier_class, "policy",
                   server_policy) != X3_STATUS_OK ||
      add_property(state->x509_server_verifier_class, "store",
                   server_store) != X3_STATUS_OK ||
      add_property(state->x509_client_verifier_class, "policy",
                   client_policy) != X3_STATUS_OK ||
      add_property(state->x509_client_verifier_class, "store",
                   client_store) != X3_STATUS_OK ||
      add_property(state->x509_verified_client_class, "chain",
                   verified_client_chain) != X3_STATUS_OK ||
      add_property(state->x509_verified_client_class, "subjects",
                   verified_client_subjects) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  if (host->module_add_class(x509, "VerificationError", nullptr, 0,
                             &state->x509_verification_error_class) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value exception_base = x3_value_invalid();
  if (host->builtin_value(host, "Exception", &exception_base) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status base_status = host->class_set_base(
      state->x509_verification_error_class, exception_base);
  host->value_release(exception_base);
  if (base_status != X3_STATUS_OK) return base_status;
  const X3NativeFunctionDef functions[] = {
      {sizeof(X3NativeFunctionDef), "encode_name_bytes",
       encode_name_bytes, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "encode_extension_value",
       encode_extension_value, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "create_x509_certificate",
       create_x509_certificate, state, 5, 5, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "load_pem_x509_certificate",
       load_pem_certificate, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "load_der_x509_certificate",
       load_der_certificate, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "load_pem_x509_certificates",
       load_pem_certificates, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "load_pem_x509_csr",
       load_pem_csr, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "load_der_x509_csr",
       load_der_csr, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "load_pem_x509_crl",
       load_pem_crl, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "load_der_x509_crl",
       load_der_crl, state, 1, 1, 0, nullptr},
  };
  for (const auto& function : functions)
    if (host->module_add_function(x509, &function) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  return X3_STATUS_OK;
}
