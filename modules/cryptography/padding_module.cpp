/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "native_state.h"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr const char* kNames[] = {"PKCS7PaddingContext", "PKCS7UnpaddingContext",
                                "ANSIX923PaddingContext", "ANSIX923UnpaddingContext"};
constexpr const char* kType = "cryptography.hazmat.bindings._rust.PaddingContext";
struct PaddingData {
  size_t block = 0, remainder = 0;
  bool unpad = false, ansi = false, finalized = false;
  std::vector<unsigned char> pending;
  ~PaddingData() { if (!pending.empty()) OPENSSL_cleanse(pending.data(), pending.size()); }
};
void cleanup(void* value) { delete static_cast<PaddingData*>(value); }
X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}
X3Status finalized(CryptographyNativeState* state, X3CallContext* call) {
  auto* host=state->host;auto* runtime=host->runtime;
  X3Value importer=x3_value_invalid(),name=x3_value_invalid(),fromlist=x3_value_invalid();
  X3Value member=x3_value_invalid(),module=x3_value_invalid(),klass=x3_value_invalid();
  X3Status status=X3_STATUS_ERROR;
  if(host->builtin_value(host,"__import__",&importer)==X3_STATUS_OK) {
    name=host->value_string(runtime,"cryptography.exceptions");
    fromlist=host->value_list(runtime);member=host->value_string(runtime,"AlreadyFinalized");
    if(name.tag!=X3_TAG_INVALID && fromlist.tag!=X3_TAG_INVALID && member.tag!=X3_TAG_INVALID &&
       host->list_append(runtime,fromlist,member)==X3_STATUS_OK) {
      const X3Value arguments[]={name,x3_value_none(),x3_value_none(),fromlist};
      if(host->call(runtime,importer,arguments,4,&module)==X3_STATUS_OK &&
         host->get_attr(runtime,module,"AlreadyFinalized",&klass)==X3_STATUS_OK)
        status=host->raise_error(call,klass,"Context was already finalized.");
    }
  }
  for(X3Value value:{klass,module,member,fromlist,name,importer})
    if(value.tag!=X3_TAG_INVALID)host->value_release(value);
  return status;
}
PaddingData* context(CryptographyNativeState* state, X3CallContext* call, X3Value self) {
  auto* data = static_cast<PaddingData*>(state->host->instance_get_native_data(self, kType));
  if (!data) fail(state, call, "TypeError", "expected padding context");
  return data;
}
template<int Kind>
X3Status initialize(X3CallContext* call, X3Runtime*, void* user,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user);
  if (argc != 2 || args[1].tag != X3_TAG_INT64)
    return fail(state, call, "TypeError", "block_size must be an integer");
  const auto bits = args[1].as.i64;
  if (bits < 0 || bits > 2040 || bits % 8)
    return fail(state, call, "ValueError", "block_size must be a multiple of 8 in range(0, 2041)");
  auto data = std::make_unique<PaddingData>();
  data->block = static_cast<size_t>(bits / 8);
  data->unpad = Kind % 2 != 0; data->ansi = Kind >= 2;
  if (state->host->instance_set_native_data(args[0], kType, data.get(), cleanup) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  data.release(); *result = x3_value_none(); return X3_STATUS_OK;
}
X3Status update(X3CallContext* call, X3Runtime* runtime, void* user,
                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user);
  if (argc != 2) return fail(state, call, "TypeError", "data is required");
  auto* data = context(state, call, args[0]); if (!data) return X3_STATUS_ERROR;
  if (data->finalized) return finalized(state, call);
  if (!data->block) return fail(state, call, "ValueError", "block_size cannot be zero");
  X3Buffer* input = nullptr; X3BufferInfo info{};
  if (state->host->buffer_acquire(runtime, args[1], 0, &input, &info) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const auto* bytes = static_cast<const unsigned char*>(info.data);
  if (!data->unpad) {
    data->remainder = (data->remainder + info.size % data->block) % data->block;
    *result = state->host->value_bytes(runtime, bytes, info.size);
  } else {
    // Retain only the final complete block plus any partial next block.
    // Never retain a whole message in a streaming padding context.
    std::vector<unsigned char> combined;
    combined.reserve(data->pending.size() + static_cast<size_t>(info.size));
    combined.insert(combined.end(), data->pending.begin(), data->pending.end());
    if (info.size) combined.insert(combined.end(), bytes, bytes + info.size);
    const auto whole = combined.size() / data->block;
    const size_t count = whole > 1 ? (whole - 1) * data->block : 0;
    *result = state->host->value_bytes(runtime, combined.data(), count);
    if (!data->pending.empty()) OPENSSL_cleanse(data->pending.data(), data->pending.size());
    data->pending.assign(combined.begin() + count, combined.end());
    if (!combined.empty()) OPENSSL_cleanse(combined.data(), combined.size());
  }
  state->host->buffer_release(input);
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}
X3Status finish(X3CallContext* call, X3Runtime* runtime, void* user,
                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user);
  if (argc != 1) return fail(state, call, "TypeError", "finalize takes no arguments");
  auto* data = context(state, call, args[0]); if (!data) return X3_STATUS_ERROR;
  if (data->finalized) return finalized(state, call);
  data->finalized = true;
  if (!data->block) return fail(state, call, "ValueError", "block_size cannot be zero");
  if (!data->unpad) {
    const auto size = data->block - data->remainder;
    std::array<unsigned char,255> output{};
    if (!data->ansi) std::fill_n(output.data(), size, static_cast<unsigned char>(size));
    output[size-1] = static_cast<unsigned char>(size);
    *result = state->host->value_bytes(runtime, output.data(), size);
  } else {
    if (data->pending.size() != data->block)
      return fail(state, call, "ValueError", "Invalid padding bytes.");
    const unsigned int size = data->pending.back();
    unsigned int bad = (size == 0) | (size > data->block);
    // Check every byte of the final block. Masks depend on the untrusted
    // padding length; no early exit or padding-dependent loop length.
    for (unsigned int i=0; i<data->block; ++i) {
      const unsigned int mask = 0u - ((i-size) >> 31);
      const unsigned int expected = data->ansi && i ? 0u : size;
      bad |= (data->pending[data->block-1-i] ^ expected) & mask;
    }
    if (bad) {
      OPENSSL_cleanse(data->pending.data(), data->pending.size());data->pending.clear();
      return fail(state, call, "ValueError", "Invalid padding bytes.");
    }
    *result = state->host->value_bytes(runtime, data->pending.data(), data->block-size);
    OPENSSL_cleanse(data->pending.data(), data->pending.size());data->pending.clear();
  }
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}
}
X3Status register_padding_classes(X3Module* root, CryptographyNativeState* state) {
  constexpr X3NativeFn initializers[] = {initialize<0>,initialize<1>,initialize<2>,initialize<3>};
  for (size_t i=0; i<std::size(kNames); ++i) {
    const X3NativeFunctionDef methods[] = {
      {sizeof(X3NativeFunctionDef),"__init__",initializers[i],state,2,2,0,nullptr},
      {sizeof(X3NativeFunctionDef),"update",update,state,2,2,0,nullptr},
      {sizeof(X3NativeFunctionDef),"finalize",finish,state,1,1,0,nullptr},
    };
    if (state->host->module_add_class(root,kNames[i],methods,static_cast<uint32_t>(std::size(methods)),
                                      &state->padding_classes[i]) != X3_STATUS_OK)
      return X3_STATUS_ERROR;
  }
  return X3_STATUS_OK;
}
