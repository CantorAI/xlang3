/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once

#include "xlang3/xlang3.h"

struct CryptographyNativeState {
  X3PackageHost* host = nullptr;
  X3Value oid_class = x3_value_invalid();
  X3Value reasons_class = x3_value_invalid();
  X3Value hash_class = x3_value_invalid();
  X3Value xof_hash_class = x3_value_invalid();
  X3Value hmac_class = x3_value_invalid();
  X3Value rsa_private_class = x3_value_invalid();
  X3Value rsa_public_class = x3_value_invalid();
  X3Value rsa_private_numbers_class = x3_value_invalid();
  X3Value rsa_public_numbers_class = x3_value_invalid();
  X3Value ec_private_class = x3_value_invalid();
  X3Value ec_public_class = x3_value_invalid();
  X3Value ec_private_numbers_class = x3_value_invalid();
  X3Value ec_public_numbers_class = x3_value_invalid();
  X3Value x509_certificate_class = x3_value_invalid();
  X3Value x509_csr_class = x3_value_invalid();
  X3Value x509_crl_class = x3_value_invalid();
  X3Value x509_revoked_class = x3_value_invalid();
  X3Value x509_sct_class = x3_value_invalid();
  X3Value x509_store_class = x3_value_invalid();
  X3Value x509_verification_error_class = x3_value_invalid();
  X3Value x509_policy_builder_class = x3_value_invalid();
  X3Value x509_policy_class = x3_value_invalid();
  X3Value x509_server_verifier_class = x3_value_invalid();
  X3Value x509_client_verifier_class = x3_value_invalid();
  X3Value x509_verified_client_class = x3_value_invalid();
  X3Value x509_extension_policy_class = x3_value_invalid();
  X3Value x509_criticality_class = x3_value_invalid();
  X3Value dh_parameters_class = x3_value_invalid();
  X3Value dh_parameter_numbers_class = x3_value_invalid();
  X3Value dh_private_class = x3_value_invalid();
  X3Value dh_public_class = x3_value_invalid();
  X3Value dh_private_numbers_class = x3_value_invalid();
  X3Value dh_public_numbers_class = x3_value_invalid();
  X3Value dsa_parameters_class = x3_value_invalid();
  X3Value dsa_parameter_numbers_class = x3_value_invalid();
  X3Value dsa_private_class = x3_value_invalid();
  X3Value dsa_public_class = x3_value_invalid();
  X3Value dsa_private_numbers_class = x3_value_invalid();
  X3Value dsa_public_numbers_class = x3_value_invalid();
  X3Value ed25519_private_class = x3_value_invalid();
  X3Value ed25519_public_class = x3_value_invalid();
  X3Value x25519_private_class = x3_value_invalid();
  X3Value x25519_public_class = x3_value_invalid();
  X3Value cipher_context_class = x3_value_invalid();
  X3Value aead_encrypt_class = x3_value_invalid();
  X3Value aead_decrypt_class = x3_value_invalid();
};

X3Status register_oid_class(X3Module* root, CryptographyNativeState* state);
X3Status register_reasons_module(X3Module* root, CryptographyNativeState* state);
X3Status register_openssl_hashes_module(X3Module* root, CryptographyNativeState* state);
X3Status register_legacy_openssl_module(X3Module* root, CryptographyNativeState* state);
X3Status register_openssl_hmac_module(X3Module* openssl, CryptographyNativeState* state);
X3Status register_openssl_rsa_module(X3Module* openssl, CryptographyNativeState* state);
X3Status register_openssl_ec_module(X3Module* openssl, CryptographyNativeState* state);
X3Status register_x509_module(X3Module* root, CryptographyNativeState* state);
X3Status register_openssl_dh_module(X3Module* openssl, CryptographyNativeState* state);
X3Status register_openssl_dsa_module(X3Module* openssl, CryptographyNativeState* state);
X3Status register_openssl_ed25519_module(X3Module* openssl, CryptographyNativeState* state);
X3Status register_openssl_x25519_module(X3Module* openssl, CryptographyNativeState* state);
X3Status register_openssl_ciphers_module(X3Module* openssl, CryptographyNativeState* state);
