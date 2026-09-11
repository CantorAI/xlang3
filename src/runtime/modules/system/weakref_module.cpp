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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace xlang3 {

namespace {

static constexpr const char* kWeakrefCallbackAttr = "__xlang3_weakref_callback__";
static constexpr const char* kWeakrefHashAttr = "__xlang3_weakref_hash__";

struct WeakrefEntry {
  Object* ref = nullptr;
  Object* target = nullptr;
};

std::vector<WeakrefEntry>& weakref_registry() {
  static auto* refs = new std::vector<WeakrefEntry>();
  return *refs;
}

std::vector<Value>& pending_weakref_callbacks() {
  static auto* callbacks = new std::vector<Value>();
  return *callbacks;
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
    if (ref.tag == ValueTag::Object && entry.ref == ref.as.obj && entry.target != nullptr) {
      out = entry.target;
      return true;
    }
  }
  out = nullptr;
  return false;
}

void register_weakref_instance(const Value& ref, const Value& target) {
  auto& refs = weakref_registry();
  Object* ref_pointer = ref.tag == ValueTag::Object ? ref.as.obj : nullptr;
  Object* target_pointer = target.tag == ValueTag::Object ? target.as.obj : nullptr;
  for (auto& existing : refs) {
    if (existing.ref == ref_pointer) {
      existing.target = target_pointer;
      return;
    }
  }
  refs.push_back({ref_pointer, target_pointer});
}

Value weakref_reference_type(Runtime& runtime);
Value weakref_proxy_type(Runtime& runtime);
Value weakref_callable_proxy_type(Runtime& runtime);

bool weakref_reference_new(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "weakref.ReferenceType.__new__() expected a type, object, and optional callback";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* requested_class = value_as_class(args[0]);
  auto* reference_class = value_as_class(weakref_reference_type(runtime));
  if (requested_class == nullptr || reference_class == nullptr ||
      !class_is_subclass(requested_class, reference_class)) {
    error = "weakref.ReferenceType.__new__() first argument must be a subtype of ReferenceType";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!weakrefable_target(args[1])) {
    error = "cannot create weak reference to object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 2 && value_is(args[0], weakref_reference_type(runtime)) && args[1].tag == ValueTag::Object) {
    for (const auto& entry : weakref_registry()) {
      if (entry.target != args[1].as.obj || entry.ref == nullptr) continue;
      Value candidate;
      candidate.tag = ValueTag::Object;
      candidate.flags = kXlangValueBorrowedRefFlag;
      candidate.as.obj = entry.ref;
      auto* instance = value_as_instance(candidate);
      if (instance == nullptr || !value_is(instance->klass, args[0])) continue;
      Value callback;
      std::string ignored;
      if (object_get_attr(candidate, kWeakrefCallbackAttr, callback, ignored) && callback.tag == ValueTag::None) {
        value_assign_fast(out, candidate);
        return true;
      }
    }
  }
  out = Value::instance(args[0]);
  if (!object_set_attr(out, kWeakrefCallbackAttr, argc == 3 ? args[2] : Value::none(), error)) {
    return false;
  }
  register_weakref_instance(out, args[1]);
  return true;
}

bool weakref_reference_new_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  // ReferenceType's object and callback parameters are positional-only.
  error = "weakref.ReferenceType.__new__() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
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

bool weakref_reference_callback(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.ReferenceType.__callback__ getter expected a reference";
    return false;
  }
  std::string ignored;
  if (!object_get_attr(args[0], kWeakrefCallbackAttr, out, ignored)) {
    value_set_none(out);
  }
  return true;
}

bool weakref_reference_hash(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.__hash__() expected self";
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    std::string ignored;
    if (object_get_attr(args[0], kWeakrefHashAttr, out, ignored)) return true;
    error = "weak object has gone away";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t hash = 0;
  if (!value_hash_key(target, hash, error)) {
    return false;
  }
  out = Value::int64(static_cast<int64_t>(hash));
  std::string ignored;
  Value self = args[0];
  (void)object_set_attr(self, kWeakrefHashAttr, out, ignored);
  return true;
}

bool weakref_reference_compare(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const char* op) {
  if (argc != 2) {
    error = "weakref comparison expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value left_target;
  Value right_target;
  const bool left_live = weakref_get_target(args[0], left_target);
  const bool right_live = weakref_get_target(args[1], right_target);
  if (!left_live || !right_live) {
    const bool equal = value_is(args[0], args[1]);
    value_set_bool(out, std::string_view(op) == "==" ? equal : !equal);
    return true;
  }
  return runtime_value_compare(runtime, op, left_target, right_target, out, error);
}

bool weakref_reference_eq(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return weakref_reference_compare(runtime, args, argc, out, error, "==");
}

bool weakref_reference_ne(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return weakref_reference_compare(runtime, args, argc, out, error, "!=");
}

bool weakref_reference_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "weakref.ReferenceType() expected object and optional callback";
    return false;
  }
  if (!weakrefable_target(args[1])) {
    error = "cannot create weak reference to object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value existing_target;
  if (!weakref_get_target(args[0], existing_target)) {
    Value self = args[0];
    if (!object_set_attr(self, kWeakrefCallbackAttr, argc == 3 ? args[2] : Value::none(), error)) {
      return false;
    }
    register_weakref_instance(self, args[1]);
  }
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
  attrs.push_back({"__xlang3_compact_repr__", Value::boolean(true)});
  attrs.push_back({"__new__", Value::static_method(
      runtime.make_native_function(
          "weakref.ReferenceType.__new__", weakref_reference_new,
          nullptr, nullptr, nullptr, false, weakref_reference_new_kw))});
  attrs.push_back({"__init__", runtime.make_native_function("weakref.ReferenceType.__init__", weakref_reference_init)});
  attrs.push_back({"__call__", runtime.make_native_function("weakref.ReferenceType.__call__", weakref_reference_call)});
  attrs.push_back({"__callback__", Value::property(
      runtime.make_native_function("weakref.ReferenceType.__callback__", weakref_reference_callback),
      Value::none(), Value::none(), Value::none())});
  attrs.push_back({"__hash__", runtime.make_native_function("weakref.ReferenceType.__hash__", weakref_reference_hash)});
  attrs.push_back({"__eq__", runtime.make_native_function("weakref.ReferenceType.__eq__", weakref_reference_eq)});
  attrs.push_back({"__ne__", runtime.make_native_function("weakref.ReferenceType.__ne__", weakref_reference_ne)});
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 1 && args[0].tag == ValueTag::Object) {
    const Value reference_type = weakref_reference_type(runtime);
    for (const auto& entry : weakref_registry()) {
      if (entry.target != args[0].as.obj || entry.ref == nullptr) continue;
      Value candidate;
      candidate.tag = ValueTag::Object;
      candidate.flags = kXlangValueBorrowedRefFlag;
      candidate.as.obj = entry.ref;
      auto* instance = value_as_instance(candidate);
      if (instance == nullptr || !value_is(instance->klass, reference_type)) continue;
      Value callback;
      std::string ignored;
      if (object_get_attr(candidate, kWeakrefCallbackAttr, callback, ignored) && callback.tag == ValueTag::None) {
        value_assign_fast(out, candidate);
        return true;
      }
    }
  }
  out = make_weakref_ref(runtime, args[0]);
  if (argc == 2) {
    return object_set_attr(out, kWeakrefCallbackAttr, args[1], error);
  } else {
    return object_set_attr(out, kWeakrefCallbackAttr, Value::none(), error);
  }
}

bool weakref_proxy_getattr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || value_as_string(args[1]) == nullptr) {
    error = "weak proxy attribute name must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    error = "weakly-referenced object no longer exists";
    runtime.raise_class_error("ReferenceError", error);
    return false;
  }
  return object_get_attr(
      target,
      string_object_to_string(*value_as_string(args[1])),
      out,
      error);
}

bool weakref_proxy_setattr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3 || value_as_string(args[1]) == nullptr) {
    error = "weak proxy attribute name must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    error = "weakly-referenced object no longer exists";
    runtime.raise_class_error("ReferenceError", error);
    return false;
  }
  Value mutable_target = target;
  if (!object_set_attr(
          mutable_target,
          string_object_to_string(*value_as_string(args[1])),
          args[2],
          error)) {
    return false;
  }
  value_assign_fast(out, args[2]);
  return true;
}

bool weakref_proxy_delattr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || value_as_string(args[1]) == nullptr) {
    error = "weak proxy attribute name must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    error = "weakly-referenced object no longer exists";
    runtime.raise_class_error("ReferenceError", error);
    return false;
  }
  Value mutable_target = target;
  if (!object_delete_attr(
          mutable_target,
          string_object_to_string(*value_as_string(args[1])),
          error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool weakref_proxy_forward(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 1 || data == nullptr) {
    error = "weak proxy special method expected self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    error = "weakly-referenced object no longer exists";
    runtime.raise_class_error("ReferenceError", error);
    return false;
  }
  const char* method_name = static_cast<const char*>(data);
  if (std::strcmp(method_name, "__next__") == 0 && argc == 1) {
    Value iterator = target;
    bool done = false;
    if (!sequence_iter_next(iterator, done, out, error)) {
      error = "Weakref proxy referenced a non-iterator";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      error = "";
      runtime.raise_class_error("StopIteration", error);
      return false;
    }
    return true;
  }
  Value method;
  if (!object_get_attr(target, method_name, method, error)) {
    if (std::strcmp(method_name, "__bool__") != 0) return false;
    Value length_method;
    std::string ignored;
    if (!object_get_attr(target, "__len__", length_method, ignored)) {
      value_set_bool(out, true);
      return true;
    }
    Value length;
    if (!runtime_call_callable(runtime, length_method, nullptr, 0, length, error)) return false;
    bool truth = false;
    if (!runtime_truthy(runtime, length, truth, error)) return false;
    value_set_bool(out, truth);
    return true;
  }
  return runtime_call_callable(runtime, method, args + 1, argc - 1, out, error);
}

bool weakref_proxy_hash(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "unhashable type: 'weakref.ProxyType'";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool weakref_callable_proxy_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "weak callable proxy expected self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    error = "weakly-referenced object no longer exists";
    runtime.raise_class_error("ReferenceError", error);
    return false;
  }
  return runtime_call_callable(runtime, target, args + 1, argc - 1, out, error);
}

bool weakref_proxy_contains(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "weak proxy __contains__ expected self and item";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    error = "weakly-referenced object no longer exists";
    runtime.raise_class_error("ReferenceError", error);
    return false;
  }
  Value method;
  std::string ignored;
  if (object_get_attr(target, "__contains__", method, ignored)) {
    return runtime_call_callable(runtime, method, args + 1, 1, out, error);
  }
  std::vector<Value> items;
  if (!runtime_collect_iterable(runtime, target, items, error)) return false;
  for (const auto& candidate : items) {
    Value equal;
    if (!runtime_value_compare(runtime, "==", candidate, args[1], equal, error)) return false;
    bool matches = false;
    if (!runtime_truthy(runtime, equal, matches, error)) return false;
    if (matches) {
      value_set_bool(out, true);
      return true;
    }
  }
  value_set_bool(out, false);
  return true;
}

bool weakref_callable_proxy_call_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "weak callable proxy expected self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target;
  if (!weakref_get_target(args[0], target)) {
    error = "weakly-referenced object no longer exists";
    runtime.raise_class_error("ReferenceError", error);
    return false;
  }
  std::vector<std::pair<std::string, Value>> forwarded;
  forwarded.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "weak callable proxy received invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    forwarded.emplace_back(kwargs[i].name, *kwargs[i].value);
  }
  return runtime_call_callable_kw(runtime, target, args + 1, argc - 1, forwarded, out, error);
}

Value weakref_proxy_type(Runtime& runtime) {
  static Value proxy_type = Value::invalid();
  if (proxy_type.tag == ValueTag::Invalid) {
    proxy_type = Value::class_object(
        "ProxyType",
        {{"__module__", Value::string("weakref")},
         {"__getattr__", runtime.make_native_function(
             "weakref.ProxyType.__getattr__", weakref_proxy_getattr)},
         {"__setattr__", runtime.make_native_function(
             "weakref.ProxyType.__setattr__", weakref_proxy_setattr)},
         {"__delattr__", runtime.make_native_function(
             "weakref.ProxyType.__delattr__", weakref_proxy_delattr)},
         {"__bool__", runtime.make_native_function("weakref.ProxyType.__bool__", weakref_proxy_forward, const_cast<char*>("__bool__"))},
         {"__eq__", runtime.make_native_function("weakref.ProxyType.__eq__", weakref_proxy_forward, const_cast<char*>("__eq__"))},
         {"__ne__", runtime.make_native_function("weakref.ProxyType.__ne__", weakref_proxy_forward, const_cast<char*>("__ne__"))},
         {"__hash__", runtime.make_native_function("weakref.ProxyType.__hash__", weakref_proxy_hash)},
         {"__str__", runtime.make_native_function("weakref.ProxyType.__str__", weakref_proxy_forward, const_cast<char*>("__str__"))},
         {"__repr__", runtime.make_native_function("weakref.ProxyType.__repr__", weakref_proxy_forward, const_cast<char*>("__repr__"))},
         {"__bytes__", runtime.make_native_function("weakref.ProxyType.__bytes__", weakref_proxy_forward, const_cast<char*>("__bytes__"))},
         {"__int__", runtime.make_native_function("weakref.ProxyType.__int__", weakref_proxy_forward, const_cast<char*>("__int__"))},
         {"__add__", runtime.make_native_function("weakref.ProxyType.__add__", weakref_proxy_forward, const_cast<char*>("__add__"))},
         {"__sub__", runtime.make_native_function("weakref.ProxyType.__sub__", weakref_proxy_forward, const_cast<char*>("__sub__"))},
         {"__mul__", runtime.make_native_function("weakref.ProxyType.__mul__", weakref_proxy_forward, const_cast<char*>("__mul__"))},
         {"__floordiv__", runtime.make_native_function("weakref.ProxyType.__floordiv__", weakref_proxy_forward, const_cast<char*>("__floordiv__"))},
         {"__ifloordiv__", runtime.make_native_function("weakref.ProxyType.__ifloordiv__", weakref_proxy_forward, const_cast<char*>("__ifloordiv__"))},
         {"__matmul__", runtime.make_native_function("weakref.ProxyType.__matmul__", weakref_proxy_forward, const_cast<char*>("__matmul__"))},
         {"__rmatmul__", runtime.make_native_function("weakref.ProxyType.__rmatmul__", weakref_proxy_forward, const_cast<char*>("__rmatmul__"))},
         {"__imatmul__", runtime.make_native_function("weakref.ProxyType.__imatmul__", weakref_proxy_forward, const_cast<char*>("__imatmul__"))},
         {"__index__", runtime.make_native_function("weakref.ProxyType.__index__", weakref_proxy_forward, const_cast<char*>("__index__"))},
         {"__neg__", runtime.make_native_function("weakref.ProxyType.__neg__", weakref_proxy_forward, const_cast<char*>("__neg__"))},
         {"__invert__", runtime.make_native_function("weakref.ProxyType.__invert__", weakref_proxy_forward, const_cast<char*>("__invert__"))},
         {"__len__", runtime.make_native_function("weakref.ProxyType.__len__", weakref_proxy_forward, const_cast<char*>("__len__"))},
         {"__iter__", runtime.make_native_function("weakref.ProxyType.__iter__", weakref_proxy_forward, const_cast<char*>("__iter__"))},
         {"__next__", runtime.make_native_function("weakref.ProxyType.__next__", weakref_proxy_forward, const_cast<char*>("__next__"))},
         {"__getitem__", runtime.make_native_function("weakref.ProxyType.__getitem__", weakref_proxy_forward, const_cast<char*>("__getitem__"))},
         {"__setitem__", runtime.make_native_function("weakref.ProxyType.__setitem__", weakref_proxy_forward, const_cast<char*>("__setitem__"))},
         {"__delitem__", runtime.make_native_function("weakref.ProxyType.__delitem__", weakref_proxy_forward, const_cast<char*>("__delitem__"))},
         {"__contains__", runtime.make_native_function("weakref.ProxyType.__contains__", weakref_proxy_contains)}});
  }
  return proxy_type;
}

Value weakref_callable_proxy_type(Runtime& runtime) {
  static Value proxy_type = Value::invalid();
  if (proxy_type.tag == ValueTag::Invalid) {
    proxy_type = Value::class_object(
        "CallableProxyType",
        {{"__module__", Value::string("weakref")},
         {"__getattr__", runtime.make_native_function(
             "weakref.CallableProxyType.__getattr__", weakref_proxy_getattr)},
         {"__setattr__", runtime.make_native_function(
             "weakref.CallableProxyType.__setattr__", weakref_proxy_setattr)},
         {"__delattr__", runtime.make_native_function(
             "weakref.CallableProxyType.__delattr__", weakref_proxy_delattr)},
         {"__call__", runtime.make_native_function(
             "weakref.CallableProxyType.__call__", weakref_callable_proxy_call,
             nullptr, nullptr, nullptr, false, weakref_callable_proxy_call_kw)},
         {"__bool__", runtime.make_native_function("weakref.CallableProxyType.__bool__", weakref_proxy_forward, const_cast<char*>("__bool__"))},
         {"__eq__", runtime.make_native_function("weakref.CallableProxyType.__eq__", weakref_proxy_forward, const_cast<char*>("__eq__"))},
         {"__ne__", runtime.make_native_function("weakref.CallableProxyType.__ne__", weakref_proxy_forward, const_cast<char*>("__ne__"))},
         {"__hash__", runtime.make_native_function("weakref.CallableProxyType.__hash__", weakref_proxy_hash)},
         {"__str__", runtime.make_native_function("weakref.CallableProxyType.__str__", weakref_proxy_forward, const_cast<char*>("__str__"))},
         {"__repr__", runtime.make_native_function("weakref.CallableProxyType.__repr__", weakref_proxy_forward, const_cast<char*>("__repr__"))},
         {"__bytes__", runtime.make_native_function("weakref.CallableProxyType.__bytes__", weakref_proxy_forward, const_cast<char*>("__bytes__"))},
         {"__int__", runtime.make_native_function("weakref.CallableProxyType.__int__", weakref_proxy_forward, const_cast<char*>("__int__"))},
         {"__add__", runtime.make_native_function("weakref.CallableProxyType.__add__", weakref_proxy_forward, const_cast<char*>("__add__"))},
         {"__sub__", runtime.make_native_function("weakref.CallableProxyType.__sub__", weakref_proxy_forward, const_cast<char*>("__sub__"))},
         {"__mul__", runtime.make_native_function("weakref.CallableProxyType.__mul__", weakref_proxy_forward, const_cast<char*>("__mul__"))},
         {"__floordiv__", runtime.make_native_function("weakref.CallableProxyType.__floordiv__", weakref_proxy_forward, const_cast<char*>("__floordiv__"))},
         {"__ifloordiv__", runtime.make_native_function("weakref.CallableProxyType.__ifloordiv__", weakref_proxy_forward, const_cast<char*>("__ifloordiv__"))},
         {"__matmul__", runtime.make_native_function("weakref.CallableProxyType.__matmul__", weakref_proxy_forward, const_cast<char*>("__matmul__"))},
         {"__rmatmul__", runtime.make_native_function("weakref.CallableProxyType.__rmatmul__", weakref_proxy_forward, const_cast<char*>("__rmatmul__"))},
         {"__imatmul__", runtime.make_native_function("weakref.CallableProxyType.__imatmul__", weakref_proxy_forward, const_cast<char*>("__imatmul__"))},
         {"__index__", runtime.make_native_function("weakref.CallableProxyType.__index__", weakref_proxy_forward, const_cast<char*>("__index__"))},
         {"__neg__", runtime.make_native_function("weakref.CallableProxyType.__neg__", weakref_proxy_forward, const_cast<char*>("__neg__"))},
         {"__invert__", runtime.make_native_function("weakref.CallableProxyType.__invert__", weakref_proxy_forward, const_cast<char*>("__invert__"))},
         {"__len__", runtime.make_native_function("weakref.CallableProxyType.__len__", weakref_proxy_forward, const_cast<char*>("__len__"))},
         {"__iter__", runtime.make_native_function("weakref.CallableProxyType.__iter__", weakref_proxy_forward, const_cast<char*>("__iter__"))},
         {"__next__", runtime.make_native_function("weakref.CallableProxyType.__next__", weakref_proxy_forward, const_cast<char*>("__next__"))},
         {"__getitem__", runtime.make_native_function("weakref.CallableProxyType.__getitem__", weakref_proxy_forward, const_cast<char*>("__getitem__"))},
         {"__setitem__", runtime.make_native_function("weakref.CallableProxyType.__setitem__", weakref_proxy_forward, const_cast<char*>("__setitem__"))},
         {"__delitem__", runtime.make_native_function("weakref.CallableProxyType.__delitem__", weakref_proxy_forward, const_cast<char*>("__delitem__"))},
         {"__contains__", runtime.make_native_function("weakref.CallableProxyType.__contains__", weakref_proxy_contains)}});
  }
  return proxy_type;
}

bool weakref_proxy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "weakref.proxy() expected object and optional callback";
    return false;
  }
  if (!weakrefable_target(args[0])) {
    error = "cannot create weak reference to object";
    return false;
  }
  Value call_method;
  std::string callable_error;
  const bool callable = object_get_attr(args[0], "__call__", call_method, callable_error);
  out = Value::instance(callable ? weakref_callable_proxy_type(runtime) : weakref_proxy_type(runtime));
  if (!object_set_attr(
          out,
          kWeakrefCallbackAttr,
          argc == 2 ? args[1] : Value::none(),
          error)) {
    return false;
  }
  register_weakref_instance(out, args[0]);
  return true;
}

bool weakref_getweakrefcount(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.getweakrefcount() expected object";
    return false;
  }
  runtime.release_dead_frame_registers();
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

bool weakref_getweakrefs(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "weakref.getweakrefs() expected object";
    return false;
  }
  runtime.release_dead_frame_registers();
  std::vector<Value> refs;
  if (args[0].tag == ValueTag::Object) {
    for (const auto& entry : weakref_registry()) {
      if (entry.target != nullptr && entry.target == args[0].as.obj) {
        Value borrowed;
        borrowed.tag = ValueTag::Object;
        borrowed.flags = kXlangValueBorrowedRefFlag;
        borrowed.as.obj = entry.ref;
        refs.push_back(borrowed);
      }
    }
  }
  out = Value::list(std::move(refs));
  return true;
}

bool weakref_remove_dead_weakref(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_weakref._remove_dead_weakref() expected dict and key";
    return false;
  }
  Value mapping = args[0];
  std::string ignored;
  (void)mapping_delete_item(mapping, args[1], ignored);
  value_set_none(out);
  return true;
}

void add_weakref_exports(NativeModuleBuilder& builder, Runtime& runtime) {
  Value proxy_factory = runtime.make_native_function("weakref.proxy", weakref_proxy);
  Value reference_type = weakref_reference_type(runtime);
  Value proxy_type = weakref_proxy_type(runtime);
  Value callable_proxy_type = weakref_callable_proxy_type(runtime);
  builder.value("ref", reference_type)
      .value("ReferenceType", std::move(reference_type))
      .value("proxy", proxy_factory)
      .value("ProxyType", std::move(proxy_type))
      .value("CallableProxyType", std::move(callable_proxy_type))
      .function("getweakrefcount", weakref_getweakrefcount)
      .function("getweakrefs", weakref_getweakrefs)
      .function("_remove_dead_weakref", weakref_remove_dead_weakref);
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

bool weakref_find_ref(const Value& target, Value& out) {
  for (const auto& entry : weakref_registry()) {
    if (entry.target != nullptr && target.tag == ValueTag::Object && entry.target == target.as.obj) {
      Value borrowed;
      borrowed.tag = ValueTag::Object;
      borrowed.flags = kXlangValueBorrowedRefFlag;
      borrowed.as.obj = entry.ref;
      value_assign_fast(out, borrowed);
      return true;
    }
  }
  return false;
}

void weakref_invalidate_target(Object* target) {
  if (target == nullptr) {
    return;
  }
  auto& refs = weakref_registry();
  for (auto entry = refs.rbegin(); entry != refs.rend(); ++entry) {
    if (entry->target == target) {
      Value borrowed;
      borrowed.tag = ValueTag::Object;
      borrowed.flags = kXlangValueBorrowedRefFlag;
      borrowed.as.obj = entry->ref;
      Value callback;
      std::string ignored;
      if (entry->ref != target && object_get_attr(borrowed, kWeakrefCallbackAttr, callback, ignored) &&
          callback.tag != ValueTag::None) {
        pending_weakref_callbacks().push_back(borrowed);
      }
      entry->target = nullptr;
    }
  }
  refs.erase(
      std::remove_if(
          refs.begin(), refs.end(),
          [&](const WeakrefEntry& entry) { return entry.ref == target; }),
      refs.end());
}

void weakref_dispatch_callbacks(Runtime& runtime) {
  auto callbacks = std::move(pending_weakref_callbacks());
  pending_weakref_callbacks().clear();
  for (const auto& ref : callbacks) {
    Value callback;
    std::string ignored;
    if (!object_get_attr(ref, kWeakrefCallbackAttr, callback, ignored) || callback.tag == ValueTag::None) continue;
    Value result;
    if (!runtime_call_callable(runtime, callback, &ref, 1, result, ignored)) {
      Value pending;
      (void)runtime.take_pending_exception(pending);
    }
  }
}

uint64_t weakref_collect_cycles() {
  std::vector<ClassObject*> candidates;
  std::unordered_set<ClassObject*> seen;
  for (const auto& entry : weakref_registry()) {
    if (entry.target != nullptr && entry.target->kind == ObjectKind::Class) {
      auto* klass = reinterpret_cast<ClassObject*>(entry.target);
      if (seen.insert(klass).second) {
        candidates.push_back(klass);
      }
    }
  }

  uint64_t collected = 0;
  for (auto* klass : candidates) {
    const Object* candidate_object = &klass->header;
    const bool still_registered = std::any_of(
        weakref_registry().begin(),
        weakref_registry().end(),
        [&](const WeakrefEntry& entry) { return entry.target == candidate_object; });
    if (!still_registered) {
      continue;
    }
    std::unordered_map<InstanceObject*, uint32_t> instance_attr_refs;
    std::vector<std::string> cyclic_attrs;
    for (const auto& attr : klass->attrs) {
      auto* instance = value_as_instance(attr.second);
      if (instance != nullptr && value_as_class(instance->klass) == klass) {
        ++instance_attr_refs[instance];
        cyclic_attrs.push_back(attr.first);
      }
    }
    if (instance_attr_refs.empty() ||
        klass->header.refcnt.load(std::memory_order_relaxed) != instance_attr_refs.size()) {
      continue;
    }
    bool isolated = true;
    for (const auto& item : instance_attr_refs) {
      if (item.first->header.refcnt.load(std::memory_order_relaxed) != item.second) {
        isolated = false;
        break;
      }
    }
    if (!isolated) {
      continue;
    }

    Value borrowed;
    borrowed.tag = ValueTag::Object;
    borrowed.flags = kXlangValueBorrowedRefFlag;
    borrowed.as.obj = &klass->header;
    Value keep_alive;
    value_assign_fast(keep_alive, borrowed);
    for (const auto& name : cyclic_attrs) {
      auto attr = klass->attrs.find(name);
      if (attr != klass->attrs.end()) {
        value_set_invalid(attr->second);
      }
    }
    collected += 1 + instance_attr_refs.size();
    value_set_invalid(keep_alive);
  }
  return collected;
}

void register_weakref_module(Runtime& runtime) {
  NativeModuleBuilder low_level(runtime, "_weakref");
  add_weakref_exports(low_level, runtime);
  runtime.register_module("_weakref", low_level.finish());
}

} // namespace xlang3
