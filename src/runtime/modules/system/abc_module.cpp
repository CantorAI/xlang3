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
#include "xlang3/mapping.h"
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

Value make_abc_weakref_set(Runtime& runtime, const Value& list_value) {
  std::vector<Value> refs;
  if (auto* list = value_as_list(list_value)) {
    refs.reserve(list->items.size());
    for (const auto& item : list->items) {
      Value target;
      if (weakref_get_target(item, target)) {
        refs.push_back(item);
      } else if (value_as_class(item) != nullptr) {
        refs.push_back(make_weakref_ref(runtime, item));
      }
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

bool abc_no_keyword_args(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t kwargc, Value&, std::string& error, void*) {
  if (kwargc == 0) {
    return true;
  }
  return abc_type_error(runtime, error, "takes no keyword arguments");
}

bool abc_bind_one_keyword(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& bound,
    const char* function_name,
    const char* parameter_name,
    std::string& error) {
  if (kwargc == 0) {
    return true;
  }
  if (argc > 1) {
    return abc_type_error(
        runtime,
        error,
        std::string(function_name) + "() takes 1 positional argument but " + std::to_string(argc) + " were given");
  }
  bool has_arg = argc == 1;
  if (has_arg) {
    value_assign_fast(bound, args[0]);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name != parameter_name) {
      return abc_type_error(runtime, error, std::string(function_name) + "() got an unexpected keyword argument '" + name + "'");
    }
    if (has_arg) {
      return abc_type_error(runtime, error, std::string(function_name) + "() got multiple values for argument '" + parameter_name + "'");
    }
    if (kwargs[i].value == nullptr) {
      return abc_type_error(runtime, error, std::string(function_name) + "() got an invalid keyword argument");
    }
    value_assign_fast(bound, *kwargs[i].value);
    has_arg = true;
  }
  if (!has_arg) {
    return abc_type_error(runtime, error, std::string(function_name) + "() missing 1 required positional argument: '" + parameter_name + "'");
  }
  return true;
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

bool abc_entry_matches_target(const Value& entry, const Value& target) {
  Value ref_target;
  if (weakref_get_target(entry, ref_target)) {
    return value_is(ref_target, target);
  }
  return value_is(entry, target);
}

bool abc_list_contains_target(const Value& list_value, const Value& target) {
  auto* list = value_as_list(list_value);
  if (list == nullptr) {
    return false;
  }
  for (const auto& item : list->items) {
    if (abc_entry_matches_target(item, target)) {
      return true;
    }
  }
  return false;
}

bool append_unique_abc_ref(Runtime& runtime, Value& list_value, const Value& item) {
  auto* list = value_as_list(list_value);
  if (list == nullptr || abc_list_contains_target(list_value, item)) {
    return false;
  }
  list->items.push_back(make_weakref_ref(runtime, item));
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
    Value registered_value;
    const Value& registered_entry = weakref_get_target(item, registered_value) ? registered_value : item;
    auto* registered = value_as_class(registered_entry);
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
  if (abc_list_contains_target(positive_cache, subclass)) {
    out = true;
    return true;
  }
  Value negative_cache;
  if (!abc_state_list(mutable_abc, kNegativeCacheAttr, negative_cache, error)) {
    return false;
  }
  if (abc_list_contains_target(negative_cache, subclass)) {
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
        append_unique_abc_ref(runtime, positive_cache, subclass);
      } else {
        append_unique_abc_ref(runtime, negative_cache, subclass);
      }
      return true;
    }
  }
  const bool direct_subclass = class_is_subclass(sub, abc);
  const bool registered_subclass = registry_contains_registered_base(abc_class, subclass);
  out = direct_subclass || registered_subclass;
  if (direct_subclass) {
    append_unique_abc_ref(runtime, positive_cache, subclass);
  } else if (!registered_subclass) {
    append_unique_abc_ref(runtime, negative_cache, subclass);
  }
  return true;
}

bool abc_return_none(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

Value abc_native_function(
    Runtime& runtime,
    const std::string& module_name,
    const std::string& qualified_name,
    const std::string& function_name,
    NativeFunctionCallback callback,
    const std::string& doc,
    NativeKeywordFunctionCallback keyword_callback = nullptr) {
  Value function = runtime.make_native_function(qualified_name, callback, nullptr, nullptr, nullptr, false, keyword_callback);
  if (auto* native = value_as_native_function(function)) {
    native->attrs_dict = new Value(Value::dict({
        {Value::string("__module__"), Value::string(module_name)},
        {Value::string("__name__"), Value::string(function_name)},
        {Value::string("__doc__"), Value::string(doc)},
    }));
  }
  return function;
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
  if (list != nullptr && !abc_list_contains_target(registry, args[1])) {
    list->items.push_back(make_weakref_ref(runtime, args[1]));
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

bool abc_meta_new(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    return abc_type_error(runtime, error, "ABCMeta.__new__() expected cls, name, bases, and namespace");
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    return abc_type_error(runtime, error, "ABCMeta.__new__() name must be a string");
  }
  auto* bases = value_as_tuple(args[2]);
  if (bases == nullptr) {
    return abc_type_error(runtime, error, "ABCMeta.__new__() bases must be a tuple");
  }
  auto* ns = value_as_dict(args[3]);
  if (ns == nullptr) {
    return abc_type_error(runtime, error, "ABCMeta.__new__() namespace must be a dict");
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.reserve(ns->entries.size() + 1);
  bool has_module = false;
  for (const auto& entry : ns->entries) {
    auto* key = value_as_string(entry.first);
    if (key == nullptr) {
      return abc_type_error(runtime, error, "ABCMeta.__new__() namespace keys must be strings");
    }
    std::string key_text = string_object_to_string(*key);
    if (key_text == "__module__") {
      has_module = true;
    }
    attrs.push_back({std::move(key_text), entry.second});
  }
  if (!has_module) {
    attrs.push_back({"__module__", Value::string("abc")});
  }

  out = Value::class_object(
      string_object_to_string(*name),
      std::move(attrs),
      Value::invalid(),
      {},
      args[0]);
  for (const auto& base : bases->items) {
    if (value_as_class(base) == nullptr) {
      return abc_type_error(runtime, error, "ABCMeta.__new__() bases must be classes");
    }
    if (!class_set_base(out, base, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  Value ignored;
  return abc_init(runtime, &out, 1, ignored, error, nullptr);
}

bool abc_meta_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 4) {
    return abc_type_error(runtime, error, "ABCMeta.__init__() expected cls, name, bases, and namespace");
  }
  value_set_none(out);
  return true;
}

bool abc_abstractmethod(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, "abc.abstractmethod() expected function");
  }
  Value target = args[0];
  if (!object_set_attr(target, "__isabstractmethod__", Value::boolean(true), error)) {
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
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
  if (argc > 4) {
    return abc_type_error(runtime, error, "property() takes at most 4 arguments (" + std::to_string(argc) + " given)");
  }
  Value values[4] = {Value::none(), Value::none(), Value::none(), Value::none()};
  for (uint32_t i = 0; i < argc; ++i) {
    value_assign_fast(values[i], args[i]);
  }
  out = Value::property(values[0], values[1], values[2], values[3], true);
  return true;
}

bool abc_update_abstractmethods_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  Value bound;
  if (!abc_bind_one_keyword(runtime, args, argc, kwargs, kwargc, bound, "abc.update_abstractmethods", "cls", error)) {
    return false;
  }
  if (kwargc == 0) {
    return true;
  }
  return abc_update_abstractmethods(runtime, &bound, 1, out, error, nullptr);
}

bool abc_abstractmethod_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  Value bound;
  if (!abc_bind_one_keyword(runtime, args, argc, kwargs, kwargc, bound, "abc.abstractmethod", "funcobj", error)) {
    return false;
  }
  if (kwargc == 0) {
    return true;
  }
  return abc_abstractmethod(runtime, &bound, 1, out, error, nullptr);
}

bool abc_abstractclassmethod_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  Value bound;
  if (!abc_bind_one_keyword(runtime, args, argc, kwargs, kwargc, bound, "abc.abstractclassmethod", "callable", error)) {
    return false;
  }
  if (kwargc == 0) {
    return true;
  }
  return abc_abstractclassmethod(runtime, &bound, 1, out, error, nullptr);
}

bool abc_abstractstaticmethod_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  Value bound;
  if (!abc_bind_one_keyword(runtime, args, argc, kwargs, kwargc, bound, "abc.abstractstaticmethod", "callable", error)) {
    return false;
  }
  if (kwargc == 0) {
    return true;
  }
  return abc_abstractstaticmethod(runtime, &bound, 1, out, error, nullptr);
}

bool abc_abstractproperty_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  if (argc > 4) {
    return abc_type_error(runtime, error, "property() takes at most 4 arguments (" + std::to_string(argc) + " given)");
  }
  Value values[4] = {Value::none(), Value::none(), Value::none(), Value::none()};
  bool has_value[4] = {false, false, false, false};
  for (uint32_t i = 0; i < argc; ++i) {
    value_assign_fast(values[i], args[i]);
    has_value[i] = true;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    int slot = -1;
    if (name == "fget") {
      slot = 0;
    } else if (name == "fset") {
      slot = 1;
    } else if (name == "fdel") {
      slot = 2;
    } else if (name == "doc") {
      slot = 3;
    } else {
      return abc_type_error(runtime, error, "property() got an unexpected keyword argument '" + name + "'");
    }
    if (has_value[slot]) {
      return abc_type_error(runtime, error, "property() got multiple values for argument '" + name + "'");
    }
    if (kwargs[i].value == nullptr) {
      return abc_type_error(runtime, error, "property() got an invalid keyword argument");
    }
    value_assign_fast(values[slot], *kwargs[i].value);
    has_value[slot] = true;
  }
  return abc_abstractproperty(runtime, values, 4, out, error, nullptr);
}

} // namespace

void register_abc_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_abc");
  builder.value("__doc__", Value::string("Module contains fast helpers for abc.py."))
      .value(
          "get_cache_token",
          abc_native_function(runtime, "_abc", "_abc.get_cache_token", "get_cache_token", abc_get_cache_token, "Returns the current ABC cache token.", abc_no_keyword_args))
      .value("_abc_init", abc_native_function(runtime, "_abc", "_abc._abc_init", "_abc_init", abc_init, "Internal ABC class initialization.", abc_no_keyword_args))
      .value(
          "_abc_register",
          abc_native_function(runtime, "_abc", "_abc._abc_register", "_abc_register", abc_register, "Register a virtual subclass of an ABC.", abc_no_keyword_args))
      .value(
          "_abc_instancecheck",
          abc_native_function(runtime, "_abc", "_abc._abc_instancecheck", "_abc_instancecheck", abc_instancecheck, "Internal ABC instance check.", abc_no_keyword_args))
      .value(
          "_abc_subclasscheck",
          abc_native_function(runtime, "_abc", "_abc._abc_subclasscheck", "_abc_subclasscheck", abc_subclasscheck, "Internal ABC subclass check.", abc_no_keyword_args))
      .value("_get_dump", abc_native_function(runtime, "_abc", "_abc._get_dump", "_get_dump", abc_get_dump, "Return ABC registry and cache snapshots.", abc_no_keyword_args))
      .value(
          "_reset_registry",
          abc_native_function(runtime, "_abc", "_abc._reset_registry", "_reset_registry", abc_reset_registry, "Clear the ABC registry.", abc_no_keyword_args))
      .value("_reset_caches", abc_native_function(runtime, "_abc", "_abc._reset_caches", "_reset_caches", abc_reset_caches, "Clear ABC caches.", abc_no_keyword_args));
  runtime.register_module("_abc", builder.finish());

  std::vector<std::pair<std::string, Value>> abc_meta_attrs;
  abc_meta_attrs.push_back({"__module__", Value::string("abc")});
  abc_meta_attrs.push_back({"__qualname__", Value::string("ABCMeta")});
  abc_meta_attrs.push_back(
      {"__doc__",
       Value::string(
           "Metaclass for defining Abstract Base Classes (ABCs).\n\n"
           "Use this metaclass to create an ABC.  An ABC can be subclassed\n"
           "directly, and then acts as a mix-in class.  You can also register\n"
           "unrelated concrete classes (even built-in classes) and unrelated\n"
           "ABCs as 'virtual subclasses' -- these and their descendants will\n"
           "be considered subclasses of the registering ABC by the built-in\n"
           "issubclass() function, but the registering ABC won't show up in\n"
           "their MRO (Method Resolution Order) nor will method\n"
           "implementations defined by the registering ABC be callable (not\n"
           "even via super()).\n")});
  abc_meta_attrs.push_back({"__new__", Value::static_method(abc_native_function(runtime, "abc", "ABCMeta.__new__", "__new__", abc_meta_new, "Create a new ABC class."))});
  abc_meta_attrs.push_back({"__init__", Value::static_method(abc_native_function(runtime, "abc", "ABCMeta.__init__", "__init__", abc_meta_init, "Initialize a new ABC class."))});
  abc_meta_attrs.push_back({"register", abc_native_function(runtime, "abc", "ABCMeta.register", "register", abc_meta_register, "Register a virtual subclass of an ABC.")});
  abc_meta_attrs.push_back({
      "__instancecheck__",
      abc_native_function(runtime, "abc", "ABCMeta.__instancecheck__", "__instancecheck__", abc_meta_instancecheck, "Override isinstance(instance, cls).")});
  abc_meta_attrs.push_back({
      "__subclasscheck__",
      abc_native_function(runtime, "abc", "ABCMeta.__subclasscheck__", "__subclasscheck__", abc_meta_subclasscheck, "Override issubclass(subclass, cls).")});
  const Value* type_base = runtime.find_builtin("type");
  Value abc_meta = Value::class_object(
      "ABCMeta",
      std::move(abc_meta_attrs),
      type_base != nullptr ? *type_base : Value::invalid(),
      {},
      type_base != nullptr ? *type_base : Value::invalid());
  Value abc_class = Value::class_object(
      "ABC",
      {
          {"__module__", Value::string("abc")},
          {"__qualname__", Value::string("ABC")},
          {"__doc__",
           Value::string(
               "Helper class that provides a standard way to create an ABC using\n"
               "inheritance.\n")},
      },
      Value::invalid(),
      {},
      abc_meta);
  std::string error;
  object_set_attr(abc_class, kRegistryAttr, Value::list({}), error);
  object_set_attr(abc_class, kCacheAttr, Value::list({}), error);
  object_set_attr(abc_class, kNegativeCacheAttr, Value::list({}), error);
  object_set_attr(abc_class, kNegativeCacheVersionAttr, Value::int64(g_cache_token), error);
  NativeModuleBuilder public_builder(runtime, "abc");
  public_builder.value("ABCMeta", abc_meta)
      .value("ABC", abc_class)
      .value(
          "get_cache_token",
          abc_native_function(runtime, "abc", "abc.get_cache_token", "get_cache_token", abc_get_cache_token, "Returns the current ABC cache token.", abc_no_keyword_args))
      .value("abstractmethod", abc_native_function(runtime, "abc", "abc.abstractmethod", "abstractmethod", abc_abstractmethod, "A decorator indicating abstract methods.", abc_abstractmethod_kw))
      .value(
          "update_abstractmethods",
          abc_native_function(runtime, "abc", "abc.update_abstractmethods", "update_abstractmethods", abc_update_abstractmethods, "Recalculate abstract methods.", abc_update_abstractmethods_kw))
      .value(
          "abstractclassmethod",
          abc_native_function(runtime, "abc", "abc.abstractclassmethod", "abstractclassmethod", abc_abstractclassmethod, "A decorator indicating abstract classmethods.", abc_abstractclassmethod_kw))
      .value(
          "abstractstaticmethod",
          abc_native_function(runtime, "abc", "abc.abstractstaticmethod", "abstractstaticmethod", abc_abstractstaticmethod, "A decorator indicating abstract staticmethods.", abc_abstractstaticmethod_kw))
      .value(
          "abstractproperty",
          abc_native_function(runtime, "abc", "abc.abstractproperty", "abstractproperty", abc_abstractproperty, "A decorator indicating abstract properties.", abc_abstractproperty_kw));
  runtime.register_module("abc", public_builder.finish());
}

} // namespace xlang3
