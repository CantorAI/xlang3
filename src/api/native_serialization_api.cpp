/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/abi/xstream.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/runtime.h"
#include "runtime_lock.h"

namespace {
struct CValue {
  X3Value value = x3_value_invalid();
  ~CValue() { x3_value_release(value); }
};
struct Callbacks {
  X3NativeSerializerDef def{};
  bool owned = false;
  ~Callbacks() { if (owned && def.cleanup) def.cleanup(def.user_data); }
};
}
X3Status x3_register_native_serializer(X3Runtime* runtime, const X3NativeSerializerDef* def) {
  auto* rt = reinterpret_cast<xlang3::Runtime*>(runtime);
  if (!rt) return X3_STATUS_ERROR;
  try {
    xlang3::XlangRuntimeExecutionGuard guard;
    if (!def || def->size != sizeof(*def) || !def->type_id || !*def->type_id ||
        !def->native_type || !*def->native_type || !def->encode || !def->decode)
      throw std::runtime_error("invalid native serializer definition");
    auto callbacks = std::make_shared<Callbacks>();
    callbacks->def = *def;
    auto codec = std::make_shared<xlang3::NativeSerializationCodec>();
    codec->type_id = def->type_id;
    codec->native_type = def->native_type;
    codec->version = def->version;
    codec->encode = [callbacks, runtime, rt](const xlang3::Value& instance, xlang3::Value& state, std::string& error) {
      CValue object{xlang3::to_c_value(instance)}, output;
      if (callbacks->def.encode(runtime, object.value, callbacks->def.user_data, &output.value) != X3_STATUS_OK) {
        error = "native serialization failed: " + rt->last_error(); return false;
      }
      state = xlang3::from_c_value(output.value, error);
      return error.empty();
    };
    codec->decode = [callbacks, runtime, rt](xlang3::Value& instance, const xlang3::Value& state, std::string& error) {
      CValue object{xlang3::to_c_value(instance)}, input{xlang3::to_c_value(state)};
      if (callbacks->def.decode(runtime, object.value, input.value, callbacks->def.user_data) != X3_STATUS_OK) {
        error = "native deserialization failed: " + rt->last_error(); return false;
      }
      return true;
    };
    if (!rt->register_native_codec(std::move(codec))) throw std::runtime_error("native serializer already registered");
    callbacks->owned = true;
    return X3_STATUS_OK;
  } catch (const std::exception& e) { rt->set_last_error(e.what()); return X3_STATUS_ERROR; }
}
X3Status x3_runtime_collect_serialized_objects(X3Runtime* runtime, uint64_t* reclaimed) {
  auto* rt = reinterpret_cast<xlang3::Runtime*>(runtime);
  if (!rt || !reclaimed) return X3_STATUS_ERROR;
  try {
    xlang3::XlangRuntimeExecutionGuard guard;
    *reclaimed = 0;
    while (auto count = rt->collect_serialized_objects()) *reclaimed += count;
    return X3_STATUS_OK;
  } catch (const std::exception& e) { rt->set_last_error(e.what()); return X3_STATUS_ERROR; }
}
