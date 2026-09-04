/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "cypher_module.h"

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace xlang_net {
namespace {

struct BioDeleter {
  void operator()(BIO* bio) const { BIO_free(bio); }
};

struct RsaDeleter {
  void operator()(RSA* rsa) const { RSA_free(rsa); }
};

using BioPtr = std::unique_ptr<BIO, BioDeleter>;
using RsaPtr = std::unique_ptr<RSA, RsaDeleter>;

size_t max_message_size(int padding, RSA* rsa) {
  const int rsa_size = RSA_size(rsa);
  switch (padding) {
    case RSA_PKCS1_PADDING:
      if (rsa_size <= 11) throw std::runtime_error("RSA key too small for PKCS1 padding");
      return static_cast<size_t>(rsa_size - 11);
    case RSA_PKCS1_OAEP_PADDING:
      if (rsa_size <= 42) throw std::runtime_error("RSA key too small for OAEP padding");
      return static_cast<size_t>(rsa_size - 42);
    case RSA_NO_PADDING:
      return static_cast<size_t>(rsa_size);
    default:
      throw std::runtime_error("unsupported RSA padding");
  }
}

RsaPtr generate_rsa(int bits) {
  RsaPtr rsa(RSA_new());
  std::unique_ptr<BIGNUM, decltype(&BN_free)> exponent(BN_new(), BN_free);
  if (!rsa || !exponent || BN_set_word(exponent.get(), RSA_F4) != 1 ||
      RSA_generate_key_ex(rsa.get(), bits, exponent.get(), nullptr) != 1) {
    throw std::runtime_error("failed to generate RSA key pair");
  }
  return rsa;
}

std::string public_key_pem(RSA* rsa) {
  BioPtr bio(BIO_new(BIO_s_mem()));
  if (!bio || PEM_write_bio_RSA_PUBKEY(bio.get(), rsa) != 1) {
    throw std::runtime_error("failed to export RSA public key");
  }
  char* data = nullptr;
  const long size = BIO_get_mem_data(bio.get(), &data);
  return data == nullptr || size <= 0 ? std::string() : std::string(data, static_cast<size_t>(size));
}

RsaPtr rsa_from_public_key(const std::string& pem) {
  BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
  if (!bio) return {};
  RSA* raw = nullptr;
  if (PEM_read_bio_RSA_PUBKEY(bio.get(), &raw, nullptr, nullptr) == nullptr) {
    BIO_reset(bio.get());
    PEM_read_bio_RSAPublicKey(bio.get(), &raw, nullptr, nullptr);
  }
  return RsaPtr(raw);
}

std::filesystem::path private_key_path(const std::filesystem::path& store, const std::string& key_name) {
  return store / (key_name + "_private_key.pem");
}

void store_private_key(RSA* rsa, const std::filesystem::path& path) {
  BioPtr bio(BIO_new(BIO_s_mem()));
  if (!bio || PEM_write_bio_RSAPrivateKey(bio.get(), rsa, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
    throw std::runtime_error("failed to export RSA private key");
  }
  char* data = nullptr;
  const long size = BIO_get_mem_data(bio.get(), &data);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out || data == nullptr || size <= 0) {
    throw std::runtime_error("failed to create RSA private key file");
  }
  out.write(data, size);
  out.close();
#if !defined(_WIN32)
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
#endif
}

RsaPtr load_private_key(const std::filesystem::path& path) {
  BioPtr bio(BIO_new_file(path.string().c_str(), "rb"));
  if (!bio) return {};
  return RsaPtr(PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

std::vector<unsigned char> rsa_encrypt_chunks(
    int padding,
    const std::string& message,
    RSA* rsa,
    int (*encrypt)(int, const unsigned char*, unsigned char*, RSA*, int)) {
  const size_t rsa_size = static_cast<size_t>(RSA_size(rsa));
  const size_t block_size = max_message_size(padding, rsa);
  if (padding == RSA_NO_PADDING && (message.size() % rsa_size) != 0) {
    throw std::runtime_error("message size must be a multiple of RSA key size for no padding");
  }

  std::vector<unsigned char> encrypted;
  encrypted.reserve(rsa_size * ((message.size() + block_size - 1) / block_size));
  for (size_t pos = 0; pos < message.size(); pos += block_size) {
    const size_t chunk_size = std::min(block_size, message.size() - pos);
    std::vector<unsigned char> block(rsa_size);
    const int written = encrypt(
        static_cast<int>(chunk_size),
        reinterpret_cast<const unsigned char*>(message.data() + pos),
        block.data(),
        rsa,
        padding);
    if (written < 0) {
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("RSA encrypt failed");
    }
    encrypted.insert(encrypted.end(), block.begin(), block.begin() + written);
  }
  return encrypted;
}

std::string rsa_decrypt_chunks(
    int padding,
    const std::vector<unsigned char>& encrypted,
    RSA* rsa,
    int (*decrypt)(int, const unsigned char*, unsigned char*, RSA*, int)) {
  const size_t rsa_size = static_cast<size_t>(RSA_size(rsa));
  if (rsa_size == 0 || (encrypted.size() % rsa_size) != 0) {
    throw std::runtime_error("bad RSA encrypted block size");
  }
  std::string decrypted;
  decrypted.reserve(encrypted.size());
  for (size_t pos = 0; pos < encrypted.size(); pos += rsa_size) {
    std::vector<unsigned char> block(rsa_size);
    const int written = decrypt(
        static_cast<int>(rsa_size),
        encrypted.data() + pos,
        block.data(),
        rsa,
        padding);
    if (written < 0) {
      ERR_print_errors_fp(stderr);
      throw std::runtime_error("RSA decrypt failed");
    }
    decrypted.append(reinterpret_cast<const char*>(block.data()), static_cast<size_t>(written));
  }
  return decrypted;
}

} // namespace

xlang_net_cypher::xlang_net_cypher() {
  ERR_load_crypto_strings();
  OpenSSL_add_all_algorithms();
}

xlang_net_cypher::~xlang_net_cypher() {
  EVP_cleanup();
  ERR_free_strings();
}

bool xlang_net_cypher::EnsureStorePath() {
  std::filesystem::path path(store_name_);
  if (path.empty()) {
    path = "XLangStore";
  }
  if (path.is_relative()) {
    path = std::filesystem::path(std::getenv("APPDATA") == nullptr ? "." : std::getenv("APPDATA")) / path;
  }
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    return false;
  }
  store_path_ = path.string();
  return true;
}

std::string xlang_net_cypher::GenerateKeyPair(int key_size, std::string key_name) {
  try {
    if (!EnsureStorePath()) return {};
    RsaPtr rsa = generate_rsa(key_size);
    const std::string public_key = public_key_pem(rsa.get());
    store_private_key(rsa.get(), private_key_path(store_path_, key_name));
    return public_key;
  } catch (...) {
    return {};
  }
}

bool xlang_net_cypher::RemovePrivateKey(std::string key_name) {
  if (!EnsureStorePath()) return false;
  std::error_code ec;
  return std::filesystem::remove(private_key_path(store_path_, key_name), ec) && !ec;
}

std::string xlang_net_cypher::MessageToBytes(const X::Value& value) const {
  uint64_t size = 0;
  const void* data = value.BytesData(&size);
  if (data != nullptr) {
    return std::string(static_cast<const char*>(data), static_cast<size_t>(size));
  }
  return value.ToString(false);
}

X::Value xlang_net_cypher::BytesToValue(const std::string& bytes) const {
  return X::Value::Bytes(Host(), bytes.data(), bytes.size());
}

X::Value xlang_net_cypher::BytesToValue(const std::vector<unsigned char>& bytes) const {
  return X::Value::Bytes(Host(), bytes.data(), bytes.size());
}

X::Value xlang_net_cypher::EncryptWithPrivateKey(X::Value message, std::string key_name) {
  try {
    if (!EnsureStorePath()) return X::Value(false);
    RsaPtr rsa = load_private_key(private_key_path(store_path_, key_name));
    if (!rsa) return X::Value(false);
    return BytesToValue(rsa_encrypt_chunks(rsa_padding_mode_, MessageToBytes(message), rsa.get(), RSA_private_encrypt));
  } catch (...) {
    return X::Value(false);
  }
}

X::Value xlang_net_cypher::DecryptWithPrivateKey(X::Value encrypted, std::string key_name) {
  try {
    if (!EnsureStorePath()) return X::Value(false);
    RsaPtr rsa = load_private_key(private_key_path(store_path_, key_name));
    if (!rsa) return X::Value(false);
    const std::string bytes = MessageToBytes(encrypted);
    std::vector<unsigned char> encrypted_bytes(bytes.begin(), bytes.end());
    return BytesToValue(rsa_decrypt_chunks(rsa_padding_mode_, encrypted_bytes, rsa.get(), RSA_private_decrypt));
  } catch (...) {
    return X::Value(false);
  }
}

X::Value xlang_net_cypher::EncryptWithPublicKey(X::Value message, std::string public_key) {
  try {
    RsaPtr rsa = rsa_from_public_key(public_key);
    if (!rsa) return X::Value(false);
    return BytesToValue(rsa_encrypt_chunks(rsa_padding_mode_, MessageToBytes(message), rsa.get(), RSA_public_encrypt));
  } catch (...) {
    return X::Value(false);
  }
}

X::Value xlang_net_cypher::DecryptWithPublicKey(X::Value encrypted, std::string public_key) {
  try {
    RsaPtr rsa = rsa_from_public_key(public_key);
    if (!rsa) return X::Value(false);
    const std::string bytes = MessageToBytes(encrypted);
    std::vector<unsigned char> encrypted_bytes(bytes.begin(), bytes.end());
    return BytesToValue(rsa_decrypt_chunks(rsa_padding_mode_, encrypted_bytes, rsa.get(), RSA_public_decrypt));
  } catch (...) {
    return X::Value(false);
  }
}

} // namespace xlang_net
