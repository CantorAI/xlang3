/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

struct WeakRefState {
  Value target;
  Value callback;
};

void weakref_state_cleanup(void* data) {
  delete static_cast<WeakRefState*>(data);
}

bool weakrefable_target(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return false;
  }
  if (auto* instance = value_as_instance(value)) {
    auto* klass = value_as_class(instance->klass);
    return klass == nullptr || klass->allow_weakref;
  }
  return true;
}

bool weakref_reference_call(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) {
    error = "weakref object expected no arguments";
    return false;
  }
  auto* state = static_cast<WeakRefState*>(user_data);
  if (state == nullptr || state->target.tag == ValueTag::Invalid) {
    value_set_none(out);
    return true;
  }
  value_assign_fast(out, state->target);
  return true;
}

bool weakref_ref(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "weakref.ref() expected object and optional callback";
    return false;
  }
  if (!weakrefable_target(args[0])) {
    error = "cannot create weak reference to object";
    return false;
  }
  auto* state = new WeakRefState();
  value_assign_fast(state->target, args[0]);
  if (argc == 2) {
    value_assign_fast(state->callback, args[1]);
  } else {
    value_set_none(state->callback);
  }
  out = runtime.make_native_function("weakref.ref", weakref_reference_call, state, weakref_state_cleanup);
  return true;
}

bool weakref_proxy(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "weakref.proxy() expected object and optional callback";
    return false;
  }
  if (!weakrefable_target(args[0])) {
    error = "cannot create weak reference to object";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool weakref_getweakrefcount(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.getweakrefcount() expected object";
    return false;
  }
  value_set_int64(out, 0);
  return true;
}

bool weakref_getweakrefs(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.getweakrefs() expected object";
    return false;
  }
  out = Value::list({});
  return true;
}

bool weakref_finalize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "weakref.finalize() expected object and callback";
    return false;
  }
  std::vector<Value> items;
  items.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    items.push_back(args[i]);
  }
  out = Value::tuple(std::move(items));
  return true;
}

void add_weakref_exports(NativeModuleBuilder& builder, Runtime& runtime) {
  Value ref_factory = runtime.make_native_function("weakref.ref", weakref_ref);
  Value proxy_factory = runtime.make_native_function("weakref.proxy", weakref_proxy);
  builder.value("ref", ref_factory)
      .value("ReferenceType", std::move(ref_factory))
      .value("proxy", proxy_factory)
      .value("ProxyType", proxy_factory)
      .value("CallableProxyType", std::move(proxy_factory))
      .function("getweakrefcount", weakref_getweakrefcount)
      .function("getweakrefs", weakref_getweakrefs);
}

} // namespace

void register_weakref_module(Runtime& runtime) {
  NativeModuleBuilder low_level(runtime, "_weakref");
  add_weakref_exports(low_level, runtime);
  runtime.register_module("_weakref", low_level.finish());

  NativeModuleBuilder facade(runtime, "weakref");
  add_weakref_exports(facade, runtime);
  facade.function("finalize", weakref_finalize);
  runtime.register_module("weakref", facade.finish());
}

} // namespace xlang3
