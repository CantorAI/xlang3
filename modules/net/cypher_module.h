/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang3/xlang3.h"

#include <string>
#include <vector>

namespace xlang_net {

class xlang_net_cypher {
public:
  xlang_net_cypher();
  ~xlang_net_cypher();

  std::string GenerateKeyPair(int key_size, std::string key_name);
  bool RemovePrivateKey(std::string key_name);
  X::Value EncryptWithPrivateKey(X::Value message, std::string key_name);
  X::Value DecryptWithPrivateKey(X::Value encrypted, std::string key_name);
  X::Value EncryptWithPublicKey(X::Value message, std::string public_key);
  X::Value DecryptWithPublicKey(X::Value encrypted, std::string public_key);

  BEGIN_PACKAGE(xlang_net_cypher)
    APISET().AddPropWithType<std::string>("StorePath", &xlang_net_cypher::store_name_);
    APISET().AddConst("RSA_PKCS1_PADDING", 1);
    APISET().AddConst("RSA_SSLV23_PADDING", 2);
    APISET().AddConst("RSA_NO_PADDING", 3);
    APISET().AddConst("RSA_PKCS1_OAEP_PADDING", 4);
    APISET().AddConst("RSA_X931_PADDING", 5);
    APISET().AddPropWithType<int>("rsa_padding_mode", &xlang_net_cypher::rsa_padding_mode_);
    APISET().AddFunc<2>("generate_key_pair", &xlang_net_cypher::GenerateKeyPair);
    APISET().AddFunc<1>("remove_private_key", &xlang_net_cypher::RemovePrivateKey);
    APISET().AddFunc<2>("encrypt_with_private_key", &xlang_net_cypher::EncryptWithPrivateKey);
    APISET().AddFunc<2>("decrypt_with_private_key", &xlang_net_cypher::DecryptWithPrivateKey);
    APISET().AddFunc<2>("encrypt_with_public_key", &xlang_net_cypher::EncryptWithPublicKey);
    APISET().AddFunc<2>("decrypt_with_public_key", &xlang_net_cypher::DecryptWithPublicKey);
  END_PACKAGE

private:
  bool EnsureStorePath();
  std::string MessageToBytes(const X::Value& value) const;
  X::Value BytesToValue(const std::string& bytes) const;
  X::Value BytesToValue(const std::vector<unsigned char>& bytes) const;

  std::string store_name_ = "XLangStore";
  std::string store_path_;
  int rsa_padding_mode_ = 4;
};

} // namespace xlang_net
