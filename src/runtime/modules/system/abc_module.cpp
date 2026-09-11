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

std::string abc_exactly_one_arg_message(const char* function_name, uint32_t argc) {
  return std::string(function_name) + "() takes exactly one argument (" + std::to_string(argc) + " given)";
}

std::string abc_expected_two_args_message(const char* function_name, uint32_t argc) {
  return std::string(function_name) + " expected 2 arguments, got " + std::to_string(argc);
}

bool abc_no_keyword_args(Runtime& runtime, const Value*, uint32_t, const NativeKeywordArg*, uint32_t kwargc, Value&, std::string& error, void* user_data) {
  if (kwargc == 0) {
    return true;
  }
  const char* name = static_cast<const char*>(user_data);
  if (name != nullptr && name[0] != '\0') {
    return abc_type_error(runtime, error, std::string(name) + "() takes no keyword arguments");
  }
  return abc_type_error(runtime, error, "takes no keyword arguments");
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

bool value_has_abstract_marker(Runtime& runtime, const Value& value, bool& out, std::string& error) {
  Value marker;
  std::string ignored;
  if (!attribute_get(value, "__isabstractmethod__", marker, ignored)) {
    out = false;
    return true;
  }
  return runtime_truthy(runtime, marker, out, error);
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

bool inherited_concrete_attr(ClassObject& klass, const std::string& name) {
  auto concrete_in = [&](ClassObject* candidate) -> bool {
    auto it = candidate->attrs.find(name);
    if (it == candidate->attrs.end()) return false;
    Value marker;
    std::string ignored;
    return !attribute_get(it->second, "__isabstractmethod__", marker, ignored) ||
           !value_truthy(marker);
  };
  std::vector<ClassObject*> stack;
  for (const auto& base : klass.bases) {
    if (auto* base_class = value_as_class(base)) {
      stack.push_back(base_class);
    }
  }
  while (!stack.empty()) {
    auto* current = stack.front();
    stack.erase(stack.begin());
    if (concrete_in(current)) {
      return true;
    }
    for (const auto& base : current->bases) {
      if (auto* base_class = value_as_class(base)) {
        stack.push_back(base_class);
      }
    }
  }
  return false;
}

bool abc_abstract_methods_for_class(
    Runtime& runtime,
    ClassObject& klass,
    Value& out,
    std::string& error) {
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
    if (override_it == klass.attrs.end()) {
      if (!inherited_concrete_attr(klass, name)) {
        add_abstract_name(abstracts, name);
      }
    } else {
      bool is_abstract = false;
      if (!value_has_abstract_marker(runtime, override_it->second, is_abstract, error)) return false;
      if (is_abstract) add_abstract_name(abstracts, name);
    }
  }
  for (const auto& attr : klass.attrs) {
    bool is_abstract = false;
    if (!value_has_abstract_marker(runtime, attr.second, is_abstract, error)) return false;
    if (is_abstract) {
      add_abstract_name(abstracts, attr.first);
    }
  }
  out = Value::frozenset(std::move(abstracts));
  return true;
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

bool abc_subclass_matches(Runtime& runtime, const Value& abc_class, const Value& subclass, bool& out, std::string& error);

bool registry_contains_registered_base(
    Runtime& runtime,
    const Value& abc_class,
    const Value& subclass,
    bool& out,
    std::string& error) {
  out = false;
  Value registry;
  std::string ignored;
  if (!object_get_attr(abc_class, kRegistryAttr, registry, ignored)) {
    return true;
  }
  auto* list = value_as_list(registry);
  auto* subclass_class = value_as_class(subclass);
  if (list == nullptr || subclass_class == nullptr) {
    return true;
  }
  for (const auto& item : list->items) {
    Value registered_value;
    const Value& registered_entry = weakref_get_target(item, registered_value) ? registered_value : item;
    auto* registered = value_as_class(registered_entry);
    if (registered != nullptr && class_is_subclass(subclass_class, registered)) {
      out = true;
      return true;
    }
    Value nested_registry;
    std::string nested_error;
    if (registered != nullptr &&
        object_get_attr(registered_entry, kRegistryAttr, nested_registry, nested_error)) {
      bool nested_match = false;
      if (!abc_subclass_matches(runtime, registered_entry, subclass, nested_match, error)) {
        return false;
      }
      if (nested_match) {
        out = true;
        return true;
      }
    }
  }
  return true;
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
    Value hook_callable;
    std::vector<Value> hook_args;
    if (auto* bound = value_as_bound_method(hook)) {
      value_assign_fast(hook_callable, bound->function);
      hook_args.push_back(bound->self);
    } else {
      value_assign_fast(hook_callable, hook);
      if (value_as_function(hook_callable) != nullptr || value_as_native_function(hook_callable) != nullptr) {
        hook_args.push_back(abc_class);
      }
    }
    hook_args.push_back(subclass);
    Value hook_result;
    if (!runtime_call_callable(
            runtime,
            hook_callable,
            hook_args.empty() ? nullptr : hook_args.data(),
            static_cast<uint32_t>(hook_args.size()),
            hook_result,
            error)) {
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
  bool registered_subclass = false;
  if (!registry_contains_registered_base(
          runtime, abc_class, subclass, registered_subclass, error)) {
    return false;
  }
  bool registered_through_subclass = false;
  if (!direct_subclass && !registered_subclass) {
    auto custom_subclasses = abc->attrs.find("__subclasses__");
    if (custom_subclasses != abc->attrs.end()) {
      Value children;
      if (!runtime_call_callable(runtime, custom_subclasses->second, nullptr, 0, children, error)) {
        return false;
      }
      auto* child_list = value_as_list(children);
      if (child_list == nullptr) {
        error = "__subclasses__() must return a list";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      for (const auto& child : child_list->items) {
        if (value_as_class(child) == nullptr) {
          error = "__subclasses__() returned a non-class";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
      }
    }
    for (auto* child : abc->subclasses) {
      if (child == nullptr) continue;
      Value child_value;
      child_value.tag = ValueTag::Object;
      child_value.as.obj = &child->header;
      retain(child_value);
      bool child_matches = false;
      if (!abc_subclass_matches(runtime, child_value, subclass, child_matches, error)) {
        return false;
      }
      if (child_matches) {
        registered_through_subclass = true;
        break;
      }
    }
  }
  out = direct_subclass || registered_subclass || registered_through_subclass;
  if (direct_subclass || registered_through_subclass) {
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
    NativeKeywordFunctionCallback keyword_callback = nullptr,
    void* keyword_user_data = nullptr,
    const char* text_signature = nullptr,
    bool is_abstract_descriptor = false) {
  Value function = runtime.make_native_function(qualified_name, callback, keyword_user_data, nullptr, nullptr, false, keyword_callback);
  if (auto* native = value_as_native_function(function)) {
    std::vector<std::pair<Value, Value>> attrs = {
        {Value::string("__module__"), Value::string(module_name)},
        {Value::string("__name__"), Value::string(function_name)},
        {Value::string("__qualname__"), Value::string(function_name)},
        {Value::string("__doc__"), Value::string(doc)},
    };
    if (text_signature != nullptr) {
      attrs.push_back({Value::string("__text_signature__"), Value::string(text_signature)});
    }
    if (is_abstract_descriptor) {
      attrs.push_back({Value::string("__isabstractmethod__"), Value::boolean(true)});
    }
    native->attrs_dict = new Value(Value::dict(std::move(attrs)));
  }
  return function;
}

bool abc_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return abc_type_error(runtime, error, abc_exactly_one_arg_message("_abc._abc_init", argc));
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_init() class", error)) {
    return false;
  }
  Value abc_class = args[0];
  auto* klass = value_as_class(abc_class);
  Value abstract_methods;
  if (klass == nullptr || !abc_abstract_methods_for_class(runtime, *klass, abstract_methods, error) ||
      !object_set_attr(abc_class, "__abstractmethods__", abstract_methods, error) ||
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
    return abc_type_error(
        runtime,
        error,
        "_abc.get_cache_token() takes no arguments (" + std::to_string(argc) + " given)");
  }
  out = Value::int64(g_cache_token);
  return true;
}

bool abc_register(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    return abc_type_error(runtime, error, abc_expected_two_args_message("_abc_register", argc));
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_register() class", error)) {
    return false;
  }
  if (value_as_class(args[1]) == nullptr) {
    error = "Can only register classes";
    runtime.raise_class_error("TypeError", error);
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
    return abc_type_error(runtime, error, abc_expected_two_args_message("_abc_subclasscheck", argc));
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
    return abc_type_error(runtime, error, abc_expected_two_args_message("_abc_instancecheck", argc));
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
    return abc_type_error(runtime, error, abc_exactly_one_arg_message("_abc._get_dump", argc));
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
    return abc_type_error(runtime, error, abc_exactly_one_arg_message("_abc._reset_registry", argc));
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
    return abc_type_error(runtime, error, abc_exactly_one_arg_message("_abc._reset_caches", argc));
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

} // namespace

void register_abc_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_abc");
  builder.value("__doc__", Value::string("Module contains fast helpers for abc.py."))
      .value(
          "get_cache_token",
          abc_native_function(
              runtime,
              "_abc",
              "_abc.get_cache_token",
              "get_cache_token",
              abc_get_cache_token,
              "Returns the current ABC cache token.\n\n"
              "The token is an opaque object (supporting equality testing) identifying\n"
              "the current version of the ABC cache for virtual subclasses.  The token\n"
              "changes with every call to register() on any ABC.",
              abc_no_keyword_args,
              const_cast<char*>("_abc.get_cache_token"),
              "($module, /)"))
      .value("_abc_init",
             abc_native_function(
                 runtime,
                 "_abc",
                 "_abc._abc_init",
                 "_abc_init",
                 abc_init,
                 "Internal ABC helper for class set-up. Should be never used outside abc module.",
                 abc_no_keyword_args,
                 const_cast<char*>("_abc._abc_init"),
                 "($module, self, /)"))
      .value(
          "_abc_register",
          abc_native_function(
              runtime,
              "_abc",
              "_abc._abc_register",
              "_abc_register",
              abc_register,
              "Internal ABC helper for subclasss registration. Should be never used outside abc module.",
              abc_no_keyword_args,
              const_cast<char*>("_abc._abc_register"),
              "($module, self, subclass, /)"))
      .value(
          "_abc_instancecheck",
          abc_native_function(
              runtime,
              "_abc",
              "_abc._abc_instancecheck",
              "_abc_instancecheck",
              abc_instancecheck,
              "Internal ABC helper for instance checks. Should be never used outside abc module.",
              abc_no_keyword_args,
              const_cast<char*>("_abc._abc_instancecheck"),
              "($module, self, instance, /)"))
      .value(
          "_abc_subclasscheck",
          abc_native_function(
              runtime,
              "_abc",
              "_abc._abc_subclasscheck",
              "_abc_subclasscheck",
              abc_subclasscheck,
              "Internal ABC helper for subclasss checks. Should be never used outside abc module.",
              abc_no_keyword_args,
              const_cast<char*>("_abc._abc_subclasscheck"),
              "($module, self, subclass, /)"))
      .value("_get_dump",
             abc_native_function(
                 runtime,
                 "_abc",
                 "_abc._get_dump",
                 "_get_dump",
                 abc_get_dump,
                 "Internal ABC helper for cache and registry debugging.\n\n"
                 "Return shallow copies of registry, of both caches, and\n"
                 "negative cache version. Don't call this function directly,\n"
                 "instead use ABC._dump_registry() for a nice repr.",
                 abc_no_keyword_args,
                 const_cast<char*>("_abc._get_dump"),
                 "($module, self, /)"))
      .value(
          "_reset_registry",
          abc_native_function(
              runtime,
              "_abc",
              "_abc._reset_registry",
              "_reset_registry",
              abc_reset_registry,
              "Internal ABC helper to reset registry of a given class.\n\n"
              "Should be only used by refleak.py",
              abc_no_keyword_args,
              const_cast<char*>("_abc._reset_registry"),
              "($module, self, /)"))
      .value("_reset_caches",
             abc_native_function(
                 runtime,
                 "_abc",
                 "_abc._reset_caches",
                 "_reset_caches",
                 abc_reset_caches,
                 "Internal ABC helper to reset both caches of a given class.\n\n"
                 "Should be only used by refleak.py",
                 abc_no_keyword_args,
                 const_cast<char*>("_abc._reset_caches"),
                 "($module, self, /)"));
  runtime.register_module("_abc", builder.finish());
}

} // namespace xlang3
