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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <algorithm>

namespace xlang3 {

namespace {

int64_t g_cache_token = 0;
static constexpr const char* kRegistryAttr = "__xlang3_abc_registry__";
static constexpr const char* kCacheAttr = "__xlang3_abc_cache__";
static constexpr const char* kNegativeCacheAttr = "__xlang3_abc_negative_cache__";
static constexpr const char* kNegativeCacheVersionAttr = "__xlang3_abc_negative_cache_version__";

struct AbcWeakRefState {
  Value target;
};

void abc_weakref_state_cleanup(void* data) {
  delete static_cast<AbcWeakRefState*>(data);
}

bool abc_weakref_call(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) {
    error = "weakref object expected no arguments";
    return false;
  }
  auto* state = static_cast<AbcWeakRefState*>(user_data);
  if (state == nullptr || state->target.tag == ValueTag::Invalid) {
    value_set_none(out);
    return true;
  }
  value_assign_fast(out, state->target);
  return true;
}

Value make_abc_weakref(Runtime& runtime, const Value& target) {
  auto* state = new AbcWeakRefState();
  value_assign_fast(state->target, target);
  return runtime.make_native_function("weakref.ref", abc_weakref_call, state, abc_weakref_state_cleanup);
}

Value make_abc_weakref_set(Runtime& runtime, const Value& list_value) {
  std::vector<Value> refs;
  if (auto* list = value_as_list(list_value)) {
    refs.reserve(list->items.size());
    for (const auto& item : list->items) {
      refs.push_back(make_abc_weakref(runtime, item));
    }
  }
  return Value::set(std::move(refs));
}

bool ensure_class(Runtime& runtime, const Value& value, const char* name, std::string& error) {
  if (value_as_class(value) != nullptr) {
    return true;
  }
  error = std::string(name) + " must be a class";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool abc_type_error(Runtime& runtime, std::string& error, std::string message) {
  error = std::move(message);
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool abc_state_list(Value& abc_class, const char* attr, Value& out, std::string& error) {
  std::string ignored;
  if (object_get_attr(abc_class, attr, out, ignored) && value_as_list(out) != nullptr) {
    return true;
  }
  out = Value::list({});
  return object_set_attr(abc_class, attr, out, error);
}

bool registry_list(Value& abc_class, Value& out, std::string& error) {
  return abc_state_list(abc_class, kRegistryAttr, out, error);
}

bool list_contains_identity(const Value& list_value, const Value& needle) {
  auto* list = value_as_list(list_value);
  if (list == nullptr) {
    return false;
  }
  for (const auto& item : list->items) {
    if (value_is(item, needle)) {
      return true;
    }
  }
  return false;
}

bool append_unique_identity(Value& list_value, const Value& item) {
  auto* list = value_as_list(list_value);
  if (list == nullptr || list_contains_identity(list_value, item)) {
    return false;
  }
  list->items.push_back(item);
  return true;
}

bool clear_abc_list_attr(Value& abc_class, const char* attr, std::string& error) {
  return object_set_attr(abc_class, attr, Value::list({}), error);
}

bool value_has_abstract_marker(const Value& value) {
  Value marker;
  std::string ignored;
  return object_get_attr(value, "__isabstractmethod__", marker, ignored) && value_truthy(marker);
}

void add_abstract_name(std::vector<Value>& names, const std::string& name) {
  for (const auto& item : names) {
    auto* string = value_as_string(item);
    if (string != nullptr && string_object_to_string(*string) == name) {
      return;
    }
  }
  names.push_back(Value::string(name));
}

void collect_abstract_names(const Value& value, std::vector<std::string>& names) {
  auto add_name = [&names](const Value& item) {
    auto* string = value_as_string(item);
    if (string == nullptr) {
      return;
    }
    const auto name = string_object_to_string(*string);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  };
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      add_name(item);
    }
  } else if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      add_name(item);
    }
  } else if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      add_name(item);
    }
  }
}

Value abc_abstract_methods_for_class(ClassObject& klass) {
  std::vector<Value> abstracts;
  std::vector<std::string> inherited_names;
  for (const auto& base : klass.bases) {
    Value base_abstracts;
    std::string ignored;
    if (object_get_attr(base, "__abstractmethods__", base_abstracts, ignored)) {
      collect_abstract_names(base_abstracts, inherited_names);
    }
  }
  for (const auto& name : inherited_names) {
    auto override_it = klass.attrs.find(name);
    if (override_it == klass.attrs.end() || value_has_abstract_marker(override_it->second)) {
      add_abstract_name(abstracts, name);
    }
  }
  for (const auto& attr : klass.attrs) {
    if (value_has_abstract_marker(attr.second)) {
      add_abstract_name(abstracts, attr.first);
    }
  }
  return Value::frozenset(std::move(abstracts));
}

int64_t abc_negative_cache_version(const Value& abc_class) {
  Value version;
  std::string ignored;
  if (!object_get_attr(abc_class, kNegativeCacheVersionAttr, version, ignored) || version.tag != ValueTag::Int64) {
    return -1;
  }
  return version.as.i64;
}

bool set_negative_cache_version(Value& abc_class, int64_t version, std::string& error) {
  return object_set_attr(abc_class, kNegativeCacheVersionAttr, Value::int64(version), error);
}

bool clear_stale_negative_cache(Value& abc_class, std::string& error) {
  if (abc_negative_cache_version(abc_class) == g_cache_token) {
    return true;
  }
  return clear_abc_list_attr(abc_class, kNegativeCacheAttr, error) &&
         set_negative_cache_version(abc_class, g_cache_token, error);
}

bool registry_contains_registered_base(const Value& abc_class, const Value& subclass) {
  Value registry;
  std::string ignored;
  if (!object_get_attr(abc_class, kRegistryAttr, registry, ignored)) {
    return false;
  }
  auto* list = value_as_list(registry);
  auto* subclass_class = value_as_class(subclass);
  if (list == nullptr || subclass_class == nullptr) {
    return false;
  }
  for (const auto& item : list->items) {
    auto* registered = value_as_class(item);
    if (registered != nullptr && class_is_subclass(subclass_class, registered)) {
      return true;
    }
  }
  return false;
}

bool abc_subclass_matches(Runtime& runtime, const Value& abc_class, const Value& subclass, bool& out, std::string& error) {
  auto* abc = value_as_class(abc_class);
  auto* sub = value_as_class(subclass);
  if (abc == nullptr || sub == nullptr) {
    out = false;
    return true;
  }
  Value mutable_abc = abc_class;
  if (!clear_stale_negative_cache(mutable_abc, error)) {
    return false;
  }
  Value positive_cache;
  if (!abc_state_list(mutable_abc, kCacheAttr, positive_cache, error)) {
    return false;
  }
  if (list_contains_identity(positive_cache, subclass)) {
    out = true;
    return true;
  }
  Value negative_cache;
  if (!abc_state_list(mutable_abc, kNegativeCacheAttr, negative_cache, error)) {
    return false;
  }
  if (list_contains_identity(negative_cache, subclass)) {
    out = false;
    return true;
  }
  Value hook;
  std::string hook_error;
  if (object_get_attr(abc_class, "__subclasshook__", hook, hook_error)) {
    Value hook_result;
    if (!runtime_call_callable(runtime, hook, &subclass, 1, hook_result, error)) {
      return false;
    }
    const Value* not_implemented = runtime.find_builtin("NotImplemented");
    if (not_implemented == nullptr || !value_is(hook_result, *not_implemented)) {
      out = value_truthy(hook_result);
      if (out) {
        append_unique_identity(positive_cache, subclass);
      } else {
        append_unique_identity(negative_cache, subclass);
      }
      return true;
    }
  }
  const bool direct_subclass = class_is_subclass(sub, abc);
  const bool registered_subclass = registry_contains_registered_base(abc_class, subclass);
  out = direct_subclass || registered_subclass;
  if (direct_subclass) {
    append_unique_identity(positive_cache, subclass);
  } else if (!registered_subclass) {
    append_unique_identity(negative_cache, subclass);
  }
  return true;
}

bool abc_return_none(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool abc_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, "_abc._abc_init() expected class");
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_init() class", error)) {
    return false;
  }
  Value abc_class = args[0];
  auto* klass = value_as_class(abc_class);
  if (klass == nullptr ||
      !object_set_attr(abc_class, "__abstractmethods__", abc_abstract_methods_for_class(*klass), error) ||
      !object_set_attr(abc_class, kRegistryAttr, Value::list({}), error) ||
      !object_set_attr(abc_class, kCacheAttr, Value::list({}), error) ||
      !object_set_attr(abc_class, kNegativeCacheAttr, Value::list({}), error) ||
      !set_negative_cache_version(abc_class, g_cache_token, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool abc_get_cache_token(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    return abc_type_error(runtime, error, "_abc.get_cache_token() expected no arguments");
  }
  out = Value::int64(g_cache_token);
  return true;
}

bool abc_update_abstractmethods(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, "abc.update_abstractmethods() expected class");
  }
  value_assign_fast(out, args[0]);
  auto* klass = value_as_class(args[0]);
  if (klass == nullptr) {
    return true;
  }
  Value existing;
  std::string ignored;
  if (!object_get_attr(args[0], "__abstractmethods__", existing, ignored)) {
    return true;
  }
  Value cls = args[0];
  return object_set_attr(cls, "__abstractmethods__", abc_abstract_methods_for_class(*klass), error);
}

bool abc_register(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return abc_type_error(runtime, error, "_abc._abc_register() expected class and subclass");
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_register() class", error) ||
      !ensure_class(runtime, args[1], "_abc._abc_register() subclass", error)) {
    return false;
  }
  auto* abc = value_as_class(args[0]);
  auto* subclass = value_as_class(args[1]);
  if (abc != nullptr && subclass != nullptr) {
    if (class_is_subclass(subclass, abc)) {
      value_assign_fast(out, args[1]);
      return true;
    }
    if (class_is_subclass(abc, subclass)) {
      error = "Refusing to create an inheritance cycle";
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
  }
  Value registry;
  Value abc_class = args[0];
  if (!registry_list(abc_class, registry, error)) {
    return false;
  }
  auto* list = value_as_list(registry);
  if (list != nullptr && !list_contains_identity(registry, args[1])) {
    list->items.push_back(args[1]);
    ++g_cache_token;
  }
  value_assign_fast(out, args[1]);
  return true;
}

bool abc_subclasscheck(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return abc_type_error(runtime, error, "_abc._abc_subclasscheck() expected class and subclass");
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_subclasscheck() class", error) ||
      !ensure_class(runtime, args[1], "_abc._abc_subclasscheck() subclass", error)) {
    return false;
  }
  bool matches = false;
  if (!abc_subclass_matches(runtime, args[0], args[1], matches, error)) {
    return false;
  }
  value_set_bool(out, matches);
  return true;
}

bool abc_instancecheck(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return abc_type_error(runtime, error, "_abc._abc_instancecheck() expected class and instance");
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_instancecheck() class", error)) {
    return false;
  }
  Value instance_class;
  if (auto* instance = value_as_instance(args[1])) {
    value_assign_fast(instance_class, instance->klass);
  } else if (!runtime_type_of_value(runtime, args[1], instance_class)) {
    instance_class = Value::invalid();
  }
  bool matches = false;
  if (instance_class.tag != ValueTag::Invalid &&
      !abc_subclass_matches(runtime, args[0], instance_class, matches, error)) {
    return false;
  }
  value_set_bool(out, instance_class.tag != ValueTag::Invalid && matches);
  return true;
}

bool abc_get_dump(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, "_abc._get_dump() expected class");
  }
  if (!ensure_class(runtime, args[0], "_abc._get_dump() class", error)) {
    return false;
  }
  Value registry;
  Value abc_class = args[0];
  if (!registry_list(abc_class, registry, error)) {
    return false;
  }
  Value positive_cache;
  Value negative_cache;
  if (!abc_state_list(abc_class, kCacheAttr, positive_cache, error) ||
      !clear_stale_negative_cache(abc_class, error) ||
      !abc_state_list(abc_class, kNegativeCacheAttr, negative_cache, error)) {
    return false;
  }
  out = Value::tuple({
      make_abc_weakref_set(runtime, registry),
      make_abc_weakref_set(runtime, positive_cache),
      make_abc_weakref_set(runtime, negative_cache),
      Value::int64(abc_negative_cache_version(abc_class))});
  return true;
}

bool abc_reset_registry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, "_abc._reset_registry() expected class");
  }
  if (!ensure_class(runtime, args[0], "_abc._reset_registry() class", error)) {
    return false;
  }
  Value abc_class = args[0];
  if (!object_set_attr(abc_class, kRegistryAttr, Value::list({}), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool abc_reset_caches(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, "_abc._reset_caches() expected class");
  }
  if (!ensure_class(runtime, args[0], "_abc._reset_caches() class", error)) {
    return false;
  }
  Value abc_class = args[0];
  if (!clear_abc_list_attr(abc_class, kCacheAttr, error) ||
      !clear_abc_list_attr(abc_class, kNegativeCacheAttr, error) ||
      !set_negative_cache_version(abc_class, g_cache_token, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool abc_meta_register(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return abc_register(runtime, args, argc, out, error, user_data);
}

bool abc_meta_instancecheck(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return abc_instancecheck(runtime, args, argc, out, error, user_data);
}

bool abc_meta_subclasscheck(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return abc_subclasscheck(runtime, args, argc, out, error, user_data);
}

bool abc_abstractmethod(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, "abc.abstractmethod() expected function");
  }
  Value target = args[0];
  std::string ignored;
  object_set_attr(target, "__isabstractmethod__", Value::boolean(true), ignored);
  value_assign_fast(out, args[0]);
  return true;
}

bool abc_abstractclassmethod(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!abc_abstractmethod(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  out = Value::class_method(args[0]);
  return object_set_attr(out, "__isabstractmethod__", Value::boolean(true), error);
}

bool abc_abstractstaticmethod(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!abc_abstractmethod(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  out = Value::static_method(args[0]);
  return object_set_attr(out, "__isabstractmethod__", Value::boolean(true), error);
}

bool abc_abstractproperty(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!abc_abstractmethod(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  out = Value::property(args[0], Value::none(), Value::none(), Value::none());
  return true;
}

} // namespace

void register_abc_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_abc");
  builder.function("get_cache_token", abc_get_cache_token)
      .function("_abc_init", abc_init)
      .function("_abc_register", abc_register)
      .function("_abc_instancecheck", abc_instancecheck)
      .function("_abc_subclasscheck", abc_subclasscheck)
      .function("_get_dump", abc_get_dump)
      .function("_reset_registry", abc_reset_registry)
      .function("_reset_caches", abc_reset_caches);
  runtime.register_module("_abc", builder.finish());

  std::vector<std::pair<std::string, Value>> abc_meta_attrs;
  abc_meta_attrs.push_back({"register", runtime.make_native_function("ABCMeta.register", abc_meta_register)});
  abc_meta_attrs.push_back({"__instancecheck__", runtime.make_native_function("ABCMeta.__instancecheck__", abc_meta_instancecheck)});
  abc_meta_attrs.push_back({"__subclasscheck__", runtime.make_native_function("ABCMeta.__subclasscheck__", abc_meta_subclasscheck)});
  const Value* type_base = runtime.find_builtin("type");
  Value abc_meta = Value::class_object(
      "ABCMeta",
      std::move(abc_meta_attrs),
      type_base != nullptr ? *type_base : Value::invalid(),
      {},
      type_base != nullptr ? *type_base : Value::invalid());
  Value abc_class = Value::class_object("ABC", {}, Value::invalid(), {}, abc_meta);
  std::string error;
  object_set_attr(abc_class, kRegistryAttr, Value::list({}), error);
  object_set_attr(abc_class, kCacheAttr, Value::list({}), error);
  object_set_attr(abc_class, kNegativeCacheAttr, Value::list({}), error);
  object_set_attr(abc_class, kNegativeCacheVersionAttr, Value::int64(g_cache_token), error);
  NativeModuleBuilder public_builder(runtime, "abc");
  public_builder.value("ABCMeta", abc_meta)
      .value("ABC", abc_class)
      .function("get_cache_token", abc_get_cache_token)
      .function("abstractmethod", abc_abstractmethod)
      .function("update_abstractmethods", abc_update_abstractmethods)
      .function("abstractclassmethod", abc_abstractclassmethod)
      .function("abstractstaticmethod", abc_abstractstaticmethod)
      .function("abstractproperty", abc_abstractproperty);
  runtime.register_module("abc", public_builder.finish());
}

} // namespace xlang3
