/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */

#include "native_state.h"

#include <openssl/objects.h>

#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kOidNativeType =
    "cryptography.hazmat.bindings._rust.ObjectIdentifier";

struct OidData {
  std::string dotted;
};

void cleanup_oid(void* pointer) { delete static_cast<OidData*>(pointer); }

X3Status fail(CryptographyNativeState* state, X3CallContext* call,
              const char* type, const char* message) {
  return state->host->raise_class_error(call, type, message);
}

OidData* require_oid(CryptographyNativeState* state, X3CallContext* call,
                     X3Value value) {
  auto* oid = static_cast<OidData*>(
      state->host->instance_get_native_data(value, kOidNativeType));
  if (oid == nullptr)
    fail(state, call, "TypeError", "expected ObjectIdentifier");
  return oid;
}

X3Status oid_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2)
    return fail(state, call, "TypeError", "ObjectIdentifier() takes 1 argument");
  const char* text = nullptr;
  uint64_t size = 0;
  if (state->host->value_string_data(runtime, args[1], &text, &size) !=
      X3_STATUS_OK)
    return fail(state, call, "TypeError", "ObjectIdentifier value must be str");
  const std::string input(text, static_cast<size_t>(size));
  if (input.find('\0') != std::string::npos)
    return fail(state, call, "ValueError",
                "error parsing asn1 value: ParseError { kind: InvalidValue }");
  std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(
      OBJ_txt2obj(input.c_str(), 1), ASN1_OBJECT_free);
  if (!object)
    return fail(state, call, "ValueError",
                "error parsing asn1 value: ParseError { kind: InvalidValue }");
  const int needed = OBJ_obj2txt(nullptr, 0, object.get(), 1);
  if (needed <= 0)
    return fail(state, call, "ValueError",
                "error parsing asn1 value: ParseError { kind: InvalidValue }");
  std::vector<char> buffer(static_cast<size_t>(needed) + 1);
  if (OBJ_obj2txt(buffer.data(), static_cast<int>(buffer.size()), object.get(), 1)
      != needed)
    return fail(state, call, "ValueError",
                "error parsing asn1 value: ParseError { kind: InvalidValue }");
  auto oid = std::make_unique<OidData>();
  oid->dotted.assign(buffer.data(), static_cast<size_t>(needed));
  if (state->host->instance_set_native_data(args[0], kOidNativeType,
                                            oid.get(), cleanup_oid) !=
      X3_STATUS_OK)
    return X3_STATUS_ERROR;
  oid.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status oid_dotted(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "dotted_string getter");
  auto* oid = require_oid(state, call, args[0]);
  if (oid == nullptr) return X3_STATUS_ERROR;
  *result = state->host->value_string(runtime, oid->dotted.c_str());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status oid_name(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "_name getter");
  if (require_oid(state, call, args[0]) == nullptr) return X3_STATUS_ERROR;
  X3Value importer = x3_value_invalid();
  X3Value module_name = x3_value_invalid();
  X3Value fromlist = x3_value_invalid();
  X3Value item = x3_value_invalid();
  X3Value module = x3_value_invalid();
  X3Value names = x3_value_invalid();
  X3Value getter = x3_value_invalid();
  X3Value fallback = x3_value_invalid();
  X3Status status = X3_STATUS_ERROR;
  if (state->host->builtin_value(state->host, "__import__", &importer) !=
      X3_STATUS_OK) goto done;
  module_name = state->host->value_string(runtime, "cryptography.hazmat._oid");
  fromlist = state->host->value_list(runtime);
  item = state->host->value_string(runtime, "_OID_NAMES");
  if (module_name.tag == X3_TAG_INVALID || fromlist.tag == X3_TAG_INVALID ||
      item.tag == X3_TAG_INVALID ||
      state->host->list_append(runtime, fromlist, item) != X3_STATUS_OK)
    goto done;
  {
    const X3Value import_args[] = {
        module_name, x3_value_none(), x3_value_none(), fromlist};
    if (state->host->call(runtime, importer, import_args, 4, &module) !=
        X3_STATUS_OK) goto done;
  }
  if (state->host->get_attr(runtime, module, "_OID_NAMES", &names) !=
          X3_STATUS_OK ||
      state->host->get_attr(runtime, names, "get", &getter) != X3_STATUS_OK)
    goto done;
  fallback = state->host->value_string(runtime, "Unknown OID");
  if (fallback.tag == X3_TAG_INVALID) goto done;
  {
    const X3Value get_args[] = {args[0], fallback};
    status = state->host->call(runtime, getter, get_args, 2, result);
  }
done:
  for (X3Value value : {fallback, getter, names, module, item, fromlist,
                        module_name, importer})
    if (value.tag != X3_TAG_INVALID) state->host->value_release(value);
  return status;
}

X3Status oid_repr(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "ObjectIdentifier.__repr__()");
  auto* oid = require_oid(state, call, args[0]);
  if (oid == nullptr) return X3_STATUS_ERROR;
  X3Value name = x3_value_invalid();
  if (oid_name(call, runtime, user_data, args, 1, &name) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  const char* text = state->host->value_to_cstr(runtime, name);
  if (text == nullptr) {
    state->host->value_release(name);
    return X3_STATUS_ERROR;
  }
  const std::string output = "<ObjectIdentifier(oid=" + oid->dotted +
      ", name=" + text + ")>";
  state->host->value_release(name);
  *result = state->host->value_string(runtime, output.c_str());
  return result->tag == X3_TAG_INVALID ? X3_STATUS_ERROR : X3_STATUS_OK;
}

X3Status oid_eq(X3CallContext* call, X3Runtime*, void* user_data,
                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "ObjectIdentifier.__eq__()");
  auto* left = require_oid(state, call, args[0]);
  if (left == nullptr) return X3_STATUS_ERROR;
  auto* right = static_cast<OidData*>(
      state->host->instance_get_native_data(args[1], kOidNativeType));
  if (right == nullptr)
    return state->host->builtin_value(state->host, "NotImplemented", result);
  *result = x3_value_bool(left->dotted == right->dotted);
  return X3_STATUS_OK;
}

X3Status oid_ne(X3CallContext* call, X3Runtime*, void* user_data,
                const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "ObjectIdentifier.__ne__()");
  auto* left = require_oid(state, call, args[0]);
  if (left == nullptr) return X3_STATUS_ERROR;
  auto* right = static_cast<OidData*>(
      state->host->instance_get_native_data(args[1], kOidNativeType));
  if (right == nullptr)
    return state->host->builtin_value(state->host, "NotImplemented", result);
  *result = x3_value_bool(left->dotted != right->dotted);
  return X3_STATUS_OK;
}

X3Status oid_hash(X3CallContext* call, X3Runtime* runtime, void* user_data,
                  const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 1) return fail(state, call, "TypeError", "ObjectIdentifier.__hash__()");
  auto* oid = require_oid(state, call, args[0]);
  if (oid == nullptr) return X3_STATUS_ERROR;
  X3Value hash_function = x3_value_invalid();
  X3Value dotted = state->host->value_string(runtime, oid->dotted.c_str());
  if (dotted.tag == X3_TAG_INVALID) return X3_STATUS_ERROR;
  const bool ready = state->host->builtin_value(state->host, "hash",
                                                &hash_function) == X3_STATUS_OK;
  const X3Status status = ready ?
      state->host->call(runtime, hash_function, &dotted, 1, result) :
      X3_STATUS_ERROR;
  if (hash_function.tag != X3_TAG_INVALID)
    state->host->value_release(hash_function);
  state->host->value_release(dotted);
  return status;
}

X3Status oid_deepcopy(X3CallContext* call, X3Runtime*, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<CryptographyNativeState*>(user_data);
  if (argc != 2) return fail(state, call, "TypeError", "ObjectIdentifier.__deepcopy__()");
  if (require_oid(state, call, args[0]) == nullptr) return X3_STATUS_ERROR;
  *result = args[0];
  state->host->value_retain(*result);
  return X3_STATUS_OK;
}

}  // namespace

X3Status register_oid_class(X3Module* root, CryptographyNativeState* state) {
  X3PackageHost* host = state->host;
  const X3NativeFunctionDef methods[] = {
      {sizeof(X3NativeFunctionDef), "__init__", oid_init, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__repr__", oid_repr, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__eq__", oid_eq, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__ne__", oid_ne, state, 2, 2, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__hash__", oid_hash, state, 1, 1, 0, nullptr},
      {sizeof(X3NativeFunctionDef), "__deepcopy__", oid_deepcopy, state, 2, 2, 0, nullptr},
  };
  if (host->module_add_class(root, "ObjectIdentifier", methods,
                             static_cast<uint32_t>(std::size(methods)),
                             &state->oid_class) != X3_STATUS_OK)
    return X3_STATUS_ERROR;
  X3Value dotted_property = x3_value_invalid();
  X3Value name_property = x3_value_invalid();
  const bool ready = host->property_create(host->runtime, "dotted_string",
                                           oid_dotted, nullptr, state,
                                           &dotted_property) == X3_STATUS_OK &&
      host->property_create(host->runtime, "_name", oid_name, nullptr,
                            state, &name_property) == X3_STATUS_OK;
  if (!ready) {
    if (dotted_property.tag != X3_TAG_INVALID)
      host->value_release(dotted_property);
    if (name_property.tag != X3_TAG_INVALID)
      host->value_release(name_property);
    return X3_STATUS_ERROR;
  }
  const X3Status dotted_status = host->class_add_value(
      state->oid_class, "dotted_string", dotted_property);
  const X3Status name_status = host->class_add_value(
      state->oid_class, "_name", name_property);
  host->value_release(dotted_property);
  host->value_release(name_property);
  return dotted_status == X3_STATUS_OK ? name_status : dotted_status;
}
