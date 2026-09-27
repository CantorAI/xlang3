/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kModule =
    "cryptography.hazmat.bindings._rust.openssl.ciphers";
constexpr const char* kCipherType =
    "cryptography.hazmat.bindings._rust.openssl.ciphers.CipherContext";
constexpr const char* kAeadEncryptType =
    "cryptography.hazmat.bindings._rust.openssl.ciphers.AEADEncryptionContext";
constexpr const char* kAeadDecryptType =
    "cryptography.hazmat.bindings._rust.openssl.ciphers.AEADDecryptionContext";

struct CipherData {
  EVP_CIPHER_CTX* context = nullptr;
  bool encrypt = false;
  bool gcm = false;
  bool finalized = false;
  bool updated = false;
  bool resettable_nonce = false;
  size_t nonce_length = 0;
  std::vector<unsigned char> tag;
  ~CipherData() { EVP_CIPHER_CTX_free(context); }
};

void cleanup(void* pointer) { delete static_cast<CipherData*>(pointer); }

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
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

bool attribute_text(CryptographyNativeState* state, X3Runtime* runtime,
                    X3Value value, const char* attribute, std::string& text) {
  X3Value field = x3_value_invalid();
  if (state->host->get_attr(runtime, value, attribute, &field) != X3_STATUS_OK)
    return false;
  const char* bytes = nullptr;
  uint64_t size = 0;
  const X3Status status = state->host->value_string_data(
      runtime, field, &bytes, &size);
  if (status == X3_STATUS_OK) text.assign(bytes, static_cast<size_t>(size));
  state->host->value_release(field);
  return status == X3_STATUS_OK;
}

bool attribute_buffer(CryptographyNativeState* state, X3Runtime* runtime,
                      X3Value value, const char* attribute,
                      std::vector<unsigned char>& bytes) {
  X3Value field = x3_value_invalid();
  if (state->host->get_attr(runtime, value, attribute, &field) != X3_STATUS_OK)
    return false;
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  const X3Status status = state->host->buffer_acquire(
      runtime, field, 0, &buffer, &info);
  if (status == X3_STATUS_OK) {
    const auto* data = static_cast<const unsigned char*>(info.data);
    bytes.assign(data, data + info.size);
    state->host->buffer_release(buffer);
  }
  state->host->value_release(field);
  return status == X3_STATUS_OK;
}

const EVP_CIPHER* select_cipher(CryptographyNativeState* state,
                                X3Runtime* runtime, X3Value algorithm,
                                X3Value mode, std::vector<unsigned char>& key,
                                std::vector<unsigned char>& iv,
                                std::vector<unsigned char>& tag,
                                bool& gcm) {
  std::string name;
  if (!attribute_text(state, runtime, algorithm, "name", name) ||
      !attribute_buffer(state, runtime, algorithm, "key", key))
    return nullptr;
  std::string mode_name;
  if (mode.tag != X3_TAG_NONE &&
      !attribute_text(state, runtime, mode, "name", mode_name))
    return nullptr;
  std::string cipher_name;
  if (name == "AES" && (key.size() == 16 || key.size() == 24 ||
                         key.size() == 32 || key.size() == 64)) {
    const bool xts = mode_name == "XTS";
    if (xts && key.size() != 32 && key.size() != 64) return nullptr;
    if (!xts && key.size() == 64) return nullptr;
    const size_t bits = xts ? key.size() * 4 : key.size() * 8;
    cipher_name = "AES-" + std::to_string(bits) + "-" + mode_name;
  } else if (name == "camellia" &&
             (key.size() == 16 || key.size() == 24 || key.size() == 32)) {
    cipher_name = "CAMELLIA-" + std::to_string(key.size() * 8) + "-" +
                  mode_name;
  } else {
    return nullptr;
  }
  if (mode_name == "CBC" || mode_name == "OFB" || mode_name == "CFB" ||
      mode_name == "CFB8" || mode_name == "GCM") {
    if (!attribute_buffer(state, runtime, mode, "initialization_vector", iv))
      return nullptr;
  } else if (mode_name == "CTR") {
    if (!attribute_buffer(state, runtime, mode, "nonce", iv)) return nullptr;
  } else if (mode_name == "XTS") {
    if (!attribute_buffer(state, runtime, mode, "tweak", iv)) return nullptr;
  } else if (mode_name != "ECB") {
    return nullptr;
  }
  if (mode_name == "CFB8") cipher_name += "";
  const EVP_CIPHER* cipher = EVP_get_cipherbyname(cipher_name.c_str());
  if (cipher == nullptr) return nullptr;
  gcm = mode_name == "GCM";
  if (!gcm && iv.size() !=
                  static_cast<size_t>(EVP_CIPHER_get_iv_length(cipher)))
    return nullptr;
  if (gcm) {
    X3Value tag_value = x3_value_invalid();
    if (state->host->get_attr(runtime, mode, "tag", &tag_value) != X3_STATUS_OK)
      return nullptr;
    if (tag_value.tag != X3_TAG_NONE) {
      X3Buffer* buffer = nullptr;
      X3BufferInfo info{};
      if (state->host->buffer_acquire(runtime, tag_value, 0, &buffer,
                                      &info) != X3_STATUS_OK) {
        state->host->value_release(tag_value);
        return nullptr;
      }
      const auto* data = static_cast<const unsigned char*>(info.data);
      tag.assign(data, data + info.size);
      state->host->buffer_release(buffer);
    }
    state->host->value_release(tag_value);
  }
  return cipher;
}

CipherData* data_for(CryptographyNativeState* state, X3CallContext* call,
                     X3Value value) {
  for (const char* kind : {kCipherType, kAeadEncryptType, kAeadDecryptType}) {
    auto* data = static_cast<CipherData*>(
        state->host->instance_get_native_data(value, kind));
    if (data != nullptr) return data;
  }
  fail(state, call, "TypeError", "expected cipher context");
  return nullptr;
}

X3Status create_context(X3CallContext* call, X3Runtime* runtime,
                        void* user_data, const X3Value* args, uint32_t argc,
                        X3Value* result, bool encrypt) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "algorithm and mode are required");
  std::vector<unsigned char> key, iv, tag;
  bool gcm = false;
  const EVP_CIPHER* cipher = select_cipher(
      state, runtime, args[0], args[1], key, iv, tag, gcm);
  if (cipher == nullptr)
    return crypto_exception(state, call, runtime,
                            "UnsupportedAlgorithm", "cipher is not supported");
  auto data = std::make_unique<CipherData>();
  data->context = EVP_CIPHER_CTX_new();
  data->encrypt = encrypt;
  data->gcm = gcm;
  data->resettable_nonce = !gcm &&
      EVP_CIPHER_get_mode(cipher) == EVP_CIPH_CTR_MODE;
  data->nonce_length = iv.size();
  data->tag = std::move(tag);
  if (data->context == nullptr ||
      EVP_CipherInit_ex(data->context, cipher, nullptr, nullptr, nullptr,
                        encrypt ? 1 : 0) != 1 ||
      (gcm && EVP_CIPHER_CTX_ctrl(data->context, EVP_CTRL_GCM_SET_IVLEN,
                                  static_cast<int>(iv.size()), nullptr) != 1) ||
      EVP_CipherInit_ex(data->context, nullptr, nullptr, key.data(),
                        iv.empty() ? nullptr : iv.data(), -1) != 1 ||
      EVP_CIPHER_CTX_set_padding(data->context, 0) != 1)
    return fail(state, call, "ValueError", "unable to initialize cipher");
  if (gcm && !encrypt && !data->tag.empty() &&
      EVP_CIPHER_CTX_ctrl(data->context, EVP_CTRL_GCM_SET_TAG,
                          static_cast<int>(data->tag.size()),
                          data->tag.data()) != 1)
    return fail(state, call, "ValueError", "invalid authentication tag");
  const char* kind = !gcm ? kCipherType :
      (encrypt ? kAeadEncryptType : kAeadDecryptType);
  X3Value klass = !gcm ? state->cipher_context_class :
      (encrypt ? state->aead_encrypt_class : state->aead_decrypt_class);
  X3Value instance = state->host->value_instance(runtime, klass);
  if (instance.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  if (state->host->instance_set_native_data(instance, kind, data.get(),
                                             cleanup) != X3_STATUS_OK) {
    state->host->value_release(instance);
    return X3_STATUS_ERROR;
  }
  data.release();
  *result = instance;
  return X3_STATUS_OK;
}

X3Status encrypt_context(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  return create_context(call, runtime, user_data, args, argc, result, true);
}

X3Status decrypt_context(X3CallContext* call, X3Runtime* runtime,
                         void* user_data, const X3Value* args,
                         uint32_t argc, X3Value* result) {
  return create_context(call, runtime, user_data, args, argc, result, false);
}

X3Status context_init(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value*, uint32_t, X3Value*) {
  return fail(static_cast<CryptographyNativeState*>(user_data), call,
              "TypeError", "cipher contexts are created by Cipher");
}

X3Status update(X3CallContext* call, X3Runtime* runtime, void* user_data,
                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "data is required");
  CipherData* data = data_for(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(input);
    return fail(state, call, "OverflowError", "data is too long");
  }
  std::vector<unsigned char> output(static_cast<size_t>(info.size) +
                                    EVP_CIPHER_CTX_get_block_size(data->context));
  int written = 0;
  const int ok = EVP_CipherUpdate(data->context, output.data(), &written,
                                  static_cast<const unsigned char*>(info.data),
                                  static_cast<int>(info.size));
  state->host->buffer_release(input);
  if (ok != 1)
    return fail(state, call, "ValueError", "cipher update failed");
  data->updated = true;
  *result = state->host->value_bytes(runtime, output.data(), written);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status update_into(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 3) return fail(state, call, "TypeError", "data and buffer are required");
  CipherData* data = data_for(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  X3Buffer* input = nullptr;
  X3Buffer* output = nullptr;
  X3BufferInfo in{};
  X3BufferInfo out{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &in) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (state->host->buffer_acquire(runtime, args[2], 1, &output, &out) !=
      X3_STATUS_OK) {
    state->host->buffer_release(input);
    return X3_STATUS_ERROR;
  }
  const uint64_t needed = in.size +
      static_cast<uint64_t>(EVP_CIPHER_CTX_get_block_size(data->context)) - 1;
  if (out.size < needed || in.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(output);
    state->host->buffer_release(input);
    return fail(state, call, "ValueError", "buffer is too small");
  }
  int written = 0;
  const int ok = EVP_CipherUpdate(data->context,
                                  static_cast<unsigned char*>(out.data), &written,
                                  static_cast<const unsigned char*>(in.data),
                                  static_cast<int>(in.size));
  state->host->buffer_release(output);
  state->host->buffer_release(input);
  if (ok != 1) return fail(state, call, "ValueError", "cipher update failed");
  data->updated = true;
  *result = x3_value_int64(written);
  return X3_STATUS_OK;
}

X3Status finalize(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "cipher context is required");
  CipherData* data = data_for(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (data->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  if (data->gcm && !data->encrypt && data->tag.empty())
    return fail(state, call, "ValueError", "Authentication tag must be provided when decrypting.");
  unsigned char output[EVP_MAX_BLOCK_LENGTH]{};
  int written = 0;
  const int ok = EVP_CipherFinal_ex(data->context, output, &written);
  data->finalized = true;
  if (ok != 1)
    return data->gcm
        ? crypto_exception(state, call, runtime, "InvalidTag", "")
        : fail(state, call, "ValueError",
               "The length of the provided data is not a multiple of the block length.");
  if (data->gcm && data->encrypt) {
    data->tag.resize(16);
    if (EVP_CIPHER_CTX_ctrl(data->context, EVP_CTRL_GCM_GET_TAG, 16,
                            data->tag.data()) != 1)
      return fail(state, call, "ValueError", "unable to read authentication tag");
  }
  *result = state->host->value_bytes(runtime, output, written);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status authenticate_aad(X3CallContext* call, X3Runtime* runtime,
                          void* user_data, const X3Value* args,
                          uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "data is required");
  CipherData* data = data_for(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (!data->gcm || data->finalized || data->updated)
    return crypto_exception(state, call, runtime, "AlreadyUpdated",
                            "Update has been called on this context.");
  X3Buffer* input = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    state->host->buffer_release(input);
    return fail(state, call, "OverflowError", "data is too long");
  }
  int written = 0;
  const int ok = EVP_CipherUpdate(data->context, nullptr, &written,
                                  static_cast<const unsigned char*>(info.data),
                                  static_cast<int>(info.size));
  state->host->buffer_release(input);
  if (ok != 1) return fail(state, call, "ValueError", "AAD update failed");
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status finalize_with_tag(X3CallContext* call, X3Runtime* runtime,
                           void* user_data, const X3Value* args,
                           uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "tag is required");
  CipherData* data = data_for(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (!data->gcm || data->encrypt || data->finalized)
    return fail(state, call, "ValueError", "invalid decryption context");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size < 4 || info.size > 16) {
    state->host->buffer_release(buffer);
    return fail(state, call, "ValueError", "invalid authentication tag length");
  }
  data->tag.assign(static_cast<const unsigned char*>(info.data),
                   static_cast<const unsigned char*>(info.data) + info.size);
  state->host->buffer_release(buffer);
  if (EVP_CIPHER_CTX_ctrl(data->context, EVP_CTRL_GCM_SET_TAG,
                          static_cast<int>(data->tag.size()),
                          data->tag.data()) != 1)
    return fail(state, call, "ValueError", "invalid authentication tag");
  return finalize(call, runtime, user_data, args, 1, result);
}

X3Status tag_property(X3CallContext* call, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "cipher context is required");
  CipherData* data = data_for(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (!data->gcm || !data->encrypt || !data->finalized)
    return crypto_exception(state, call, runtime, "NotYetFinalized",
                            "You must finalize encryption before getting the tag.");
  *result = state->host->value_bytes(runtime, data->tag.data(), data->tag.size());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status reset_nonce(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "nonce is required");
  CipherData* data = data_for(state, call, args[0]);
  if (data == nullptr) return X3_STATUS_ERROR;
  if (!data->resettable_nonce)
    return crypto_exception(state, call, runtime, "UnsupportedAlgorithm",
                            "reset_nonce is not supported for this cipher");
  if (data->finalized)
    return crypto_exception(state, call, runtime, "AlreadyFinalized",
                            "Context was already finalized.");
  X3Buffer* buffer = nullptr;
  X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &buffer, &info) !=
      X3_STATUS_OK) return X3_STATUS_ERROR;
  if (info.size != data->nonce_length) {
    state->host->buffer_release(buffer);
    return fail(state, call, "ValueError", "Invalid nonce size");
  }
  const int ok = EVP_CipherInit_ex(
      data->context, nullptr, nullptr, nullptr,
      static_cast<const unsigned char*>(info.data), -1);
  state->host->buffer_release(buffer);
  if (ok != 1)
    return fail(state, call, "ValueError", "unable to reset nonce");
  data->updated = false;
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status supported(X3CallContext*, X3Runtime* runtime, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  std::vector<unsigned char> key, iv, tag;
  bool gcm = false;
  *result = x3_value_bool(argc == 2 && select_cipher(
      state, runtime, args[0], args[1], key, iv, tag, gcm) != nullptr);
  return X3_STATUS_OK;
}

X3Status attach(X3PackageHost* host, X3Module* parent, X3Module* child) {
  X3Value value = x3_value_invalid();
  if (host->module_get_value(child, &value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status status = host->module_add_value(parent, "ciphers", value);
  host->value_release(value);
  return status;
}

}  // namespace

X3Status register_openssl_ciphers_module(X3Module* openssl,
                                         CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* module = nullptr;
  if (host->add_module(host, kModule, &module) != X3_STATUS_OK ||
      attach(host, openssl, module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3NativeFunctionDef functions[] = {
      {sizeof(X3NativeFunctionDef), "create_encryption_ctx",
       encrypt_context, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "create_decryption_ctx",
       decrypt_context, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "cipher_supported",
       supported, state, 2, 2, 0, nullptr},
  };
  for (const auto& function : functions)
    if (host->module_add_function(module, &function) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  const X3NativeFunctionDef common[] = {
      {sizeof(X3NativeFunctionDef), "__init__", context_init,
       state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "update", update,
       state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "update_into", update_into,
       state, 3, 3, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "finalize", finalize,
       state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "reset_nonce", reset_nonce,
       state, 2, 2, 0, nullptr},
  };
  const X3NativeFunctionDef encrypt_methods[] = {
      common[0], common[1], common[2], common[3], common[4],
      {sizeof(X3NativeFunctionDef), "authenticate_additional_data",
       authenticate_aad, state, 2, 2, 0, nullptr},
  };
  const X3NativeFunctionDef decrypt_methods[] = {
      common[0], common[1], common[2], common[3], common[4],
      {sizeof(X3NativeFunctionDef), "authenticate_additional_data",
       authenticate_aad, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "finalize_with_tag",
       finalize_with_tag, state, 2, 2, 0, nullptr},
  };
  if (host->module_add_class(module, "CipherContext", common,
                             static_cast<uint32_t>(std::size(common)),
                             &state->cipher_context_class) != X3_STATUS_OK ||
      host->module_add_class(module, "AEADEncryptionContext", encrypt_methods,
                             static_cast<uint32_t>(std::size(encrypt_methods)),
                             &state->aead_encrypt_class) != X3_STATUS_OK ||
      host->module_add_class(module, "AEADDecryptionContext", decrypt_methods,
                             static_cast<uint32_t>(std::size(decrypt_methods)),
                             &state->aead_decrypt_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value property = x3_value_invalid();
  if (host->property_create(host->runtime, "tag", tag_property, nullptr,
                            state, &property) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status status = host->class_add_value(
      state->aead_encrypt_class, "tag", property);
  host->value_release(property);
  return status;
}
