/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <iterator>
#include <memory>
#include <string>

namespace {

constexpr const char* kExceptionsName =
    "cryptography.hazmat.bindings._rust.exceptions";
constexpr const char* kReasonsType =
    "cryptography.hazmat.bindings._rust.exceptions._Reasons";
constexpr const char* kReasonNames[] = {
    "BACKEND_MISSING_INTERFACE", "UNSUPPORTED_HASH", "UNSUPPORTED_CIPHER",
    "UNSUPPORTED_PADDING", "UNSUPPORTED_MGF", "UNSUPPORTED_PUBLIC_KEY_ALGORITHM",
    "UNSUPPORTED_ELLIPTIC_CURVE", "UNSUPPORTED_SERIALIZATION",
    "UNSUPPORTED_X509", "UNSUPPORTED_EXCHANGE_ALGORITHM",
    "UNSUPPORTED_DIFFIE_HELLMAN", "UNSUPPORTED_MAC",
};

struct ReasonData {
  std::string name;
};

void cleanup_reason(void* pointer) { delete static_cast<ReasonData*>(pointer); }

X3Status reason_init(X3CallContext* call, X3Runtime*, void* user_data,
                     const X3Value*, uint32_t, X3Value*) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  return state->host->raise_class_error(
      call, "TypeError",
      "cannot create 'cryptography.hazmat.bindings._rust.exceptions._Reasons' instances");
}

X3Status reason_repr(X3CallContext* call, X3Runtime* runtime, void* user_data,
                     const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1)
    return state->host->raise_class_error(call, "TypeError", "_Reasons.__repr__()");
  auto* reason = static_cast<ReasonData*>(
      state->host->instance_get_native_data(args[0], kReasonsType));
  if (reason == nullptr)
    return state->host->raise_class_error(call, "TypeError", "expected _Reasons");
  const std::string text = "_Reasons." + reason->name;
  *result = state->host->value_string(runtime, text.c_str());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status reason_eq(X3CallContext* call, X3Runtime*, void* user_data,
                   const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return state->host->raise_class_error(call, "TypeError", "_Reasons.__eq__()");
  auto* left = static_cast<ReasonData*>(
      state->host->instance_get_native_data(args[0], kReasonsType));
  if (left == nullptr)
    return state->host->raise_class_error(call, "TypeError", "expected _Reasons");
  auto* right = static_cast<ReasonData*>(
      state->host->instance_get_native_data(args[1], kReasonsType));
  if (right == nullptr)
    return state->host->builtin_value(state->host, "NotImplemented", result);
  *result = x3_value_bool(left->name == right->name);
  return X3_STATUS_OK;
}

}  // namespace

X3Status register_reasons_module(X3Module* root, CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  X3Module* module = nullptr;
  if (host->add_module(host, kExceptionsName, &module) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value module_value = x3_value_invalid();
  if (host->module_get_value(module, &module_value) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const X3Status attached = host->module_add_value(root, "exceptions", module_value);
  host->value_release(module_value);
  if (attached != X3_STATUS_OK) return attached;
  const X3NativeFunctionDef methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", reason_init, state, 1, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__repr__", reason_repr, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__str__", reason_repr, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__eq__", reason_eq, state, 2, 2, 0, nullptr},
  };
  if (host->module_add_class(module, "_Reasons", methods,
                             static_cast<uint32_t>(std::size(methods)),
                             &state->reasons_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  for (const char* name : kReasonNames) {
    X3Value value = host->value_instance(host->runtime, state->reasons_class);
    if (value.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
    auto data = std::make_unique<ReasonData>();
    data->name = name;
    if (host->instance_set_native_data(value, kReasonsType, data.get(),
                                       cleanup_reason) != X3_STATUS_OK) {
      host->value_release(value);
      return X3_STATUS_ERROR;
    }
    data.release();
    const X3Status status = host->class_add_value(state->reasons_class, name, value);
    host->value_release(value);
    if (status != X3_STATUS_OK) return status;
  }
  return host->class_add_value(state->reasons_class, "__hash__", x3_value_none());
}
