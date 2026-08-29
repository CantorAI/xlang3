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

static constexpr const char* kWeakrefCallbackAttr = "__xlang3_weakref_callback__";

struct WeakrefEntry {
  Value ref;
  Object* target = nullptr;
};

std::vector<WeakrefEntry>& weakref_registry() {
  static auto* refs = new std::vector<WeakrefEntry>();
  return *refs;
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

bool weakref_target_matches(const Value& ref, const Value& target) {
  Value ref_target;
  return weakref_get_target(ref, ref_target) && value_is(ref_target, target);
}

bool weakref_target_pointer(const Value& ref, Object*& out) {
  for (const auto& entry : weakref_registry()) {
    if (value_is(entry.ref, ref) && entry.target != nullptr) {
      out = entry.target;
      return true;
    }
  }
  out = nullptr;
  return false;
}

void register_weakref_instance(const Value& ref, const Value& target) {
  auto& refs = weakref_registry();
  Object* target_pointer = target.tag == ValueTag::Object ? target.as.obj : nullptr;
  for (auto& existing : refs) {
    if (value_is(existing.ref, ref)) {
      existing.target = target_pointer;
      return;
    }
  }
  refs.push_back({ref, target_pointer});
}

bool weakref_reference_call(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref object expected no arguments";
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    value_set_none(out);
    return true;
  }
  value_assign_fast(out, target);
  return true;
}

bool weakref_reference_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "weakref.ReferenceType() expected object and optional callback";
    return false;
  }
  if (!weakrefable_target(args[1])) {
    error = "cannot create weak reference to object";
    return false;
  }
  Value self = args[0];
  if (!object_set_attr(self, kWeakrefCallbackAttr, argc == 3 ? args[2] : Value::none(), error)) {
    return false;
  }
  register_weakref_instance(self, args[1]);
  value_set_none(out);
  return true;
}

Value weakref_reference_type(Runtime& runtime) {
  static Value reference_type = Value::invalid();
  if (reference_type.tag != ValueTag::Invalid) {
    return reference_type;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("weakref")});
  attrs.push_back({"__init__", runtime.make_native_function("weakref.ReferenceType.__init__", weakref_reference_init)});
  attrs.push_back({"__call__", runtime.make_native_function("weakref.ReferenceType.__call__", weakref_reference_call)});
  reference_type = Value::class_object("ReferenceType", std::move(attrs));
  return reference_type;
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
  out = make_weakref_ref(runtime, args[0]);
  if (argc == 2) {
    return object_set_attr(out, kWeakrefCallbackAttr, args[1], error);
  } else {
    return object_set_attr(out, kWeakrefCallbackAttr, Value::none(), error);
  }
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

bool weakref_getweakrefcount(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.getweakrefcount() expected object";
    return false;
  }
  int64_t count = 0;
  if (args[0].tag == ValueTag::Object) {
    for (const auto& entry : weakref_registry()) {
      if (entry.target != nullptr && entry.target == args[0].as.obj) {
        ++count;
      }
    }
  }
  value_set_int64(out, count);
  return true;
}

bool weakref_getweakrefs(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.getweakrefs() expected object";
    return false;
  }
  std::vector<Value> refs;
  if (args[0].tag == ValueTag::Object) {
    for (const auto& entry : weakref_registry()) {
      if (entry.target != nullptr && entry.target == args[0].as.obj) {
        refs.push_back(entry.ref);
      }
    }
  }
  out = Value::list(std::move(refs));
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
      .value("ReferenceType", weakref_reference_type(runtime))
      .value("proxy", proxy_factory)
      .value("ProxyType", proxy_factory)
      .value("CallableProxyType", std::move(proxy_factory))
      .function("getweakrefcount", weakref_getweakrefcount)
      .function("getweakrefs", weakref_getweakrefs);
}

} // namespace

Value make_weakref_ref(Runtime& runtime, const Value& target) {
  Value ref = Value::instance(weakref_reference_type(runtime));
  std::string ignored;
  object_set_attr(ref, kWeakrefCallbackAttr, Value::none(), ignored);
  register_weakref_instance(ref, target);
  return ref;
}

bool weakref_get_target(const Value& ref, Value& out) {
  Object* target = nullptr;
  if (!weakref_target_pointer(ref, target)) {
    return false;
  }
  Value borrowed;
  borrowed.tag = ValueTag::Object;
  borrowed.flags = kXlangValueBorrowedRefFlag;
  borrowed.as.obj = target;
  value_assign_fast(out, borrowed);
  return true;
}

void weakref_invalidate_target(Object* target) {
  if (target == nullptr) {
    return;
  }
  for (auto& entry : weakref_registry()) {
    if (entry.target == target) {
      entry.target = nullptr;
    }
  }
}

void register_weakref_module(Runtime& runtime) {
  NativeModuleBuilder low_level(runtime, "_weakref");
  add_weakref_exports(low_level, runtime);
  runtime.register_module("_weakref", low_level.finish());
}

} // namespace xlang3
