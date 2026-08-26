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
#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

int64_t g_cache_token = 0;
static constexpr const char* kRegistryAttr = "__xlang3_abc_registry__";

bool ensure_class(Runtime& runtime, const Value& value, const char* name, std::string& error) {
  if (value_as_class(value) != nullptr) {
    return true;
  }
  error = std::string(name) + " must be a class";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool registry_list(Value& abc_class, Value& out, std::string& error) {
  std::string ignored;
  if (object_get_attr(abc_class, kRegistryAttr, out, ignored) && value_as_list(out) != nullptr) {
    return true;
  }
  out = Value::list({});
  return object_set_attr(abc_class, kRegistryAttr, out, error);
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

bool abc_subclass_matches(const Value& abc_class, const Value& subclass) {
  auto* abc = value_as_class(abc_class);
  auto* sub = value_as_class(subclass);
  if (abc == nullptr || sub == nullptr) {
    return false;
  }
  return class_is_subclass(sub, abc) || registry_contains_registered_base(abc_class, subclass);
}

bool abc_return_none(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool abc_get_cache_token(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "_abc.get_cache_token() expected no arguments";
    return false;
  }
  out = Value::int64(g_cache_token);
  return true;
}

bool abc_register(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_abc._abc_register() expected class and subclass";
    return false;
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_register() class", error) ||
      !ensure_class(runtime, args[1], "_abc._abc_register() subclass", error)) {
    return false;
  }
  Value registry;
  Value abc_class = args[0];
  if (!registry_list(abc_class, registry, error)) {
    return false;
  }
  auto* list = value_as_list(registry);
  bool present = false;
  for (const auto& item : list->items) {
    if (value_is(item, args[1])) {
      present = true;
      break;
    }
  }
  if (!present) {
    list->items.push_back(args[1]);
    ++g_cache_token;
  }
  value_assign_fast(out, args[1]);
  return true;
}

bool abc_subclasscheck(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_abc._abc_subclasscheck() expected class and subclass";
    return false;
  }
  if (!ensure_class(runtime, args[0], "_abc._abc_subclasscheck() class", error) ||
      !ensure_class(runtime, args[1], "_abc._abc_subclasscheck() subclass", error)) {
    return false;
  }
  value_set_bool(out, abc_subclass_matches(args[0], args[1]));
  return true;
}

bool abc_instancecheck(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_abc._abc_instancecheck() expected class and instance";
    return false;
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
  value_set_bool(out, instance_class.tag != ValueTag::Invalid && abc_subclass_matches(args[0], instance_class));
  return true;
}

bool abc_get_dump(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_abc._get_dump() expected class";
    return false;
  }
  if (!ensure_class(runtime, args[0], "_abc._get_dump() class", error)) {
    return false;
  }
  Value registry;
  Value abc_class = args[0];
  if (!registry_list(abc_class, registry, error)) {
    return false;
  }
  auto* list = value_as_list(registry);
  out = Value::tuple({Value::set(list == nullptr ? std::vector<Value>{} : list->items), Value::set({}), Value::set({}), Value::int64(g_cache_token)});
  return true;
}

bool abc_reset_registry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_abc._reset_registry() expected class";
    return false;
  }
  if (!ensure_class(runtime, args[0], "_abc._reset_registry() class", error)) {
    return false;
  }
  Value abc_class = args[0];
  if (!object_set_attr(abc_class, kRegistryAttr, Value::list({}), error)) {
    return false;
  }
  ++g_cache_token;
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

bool abc_abstractmethod(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "abc.abstractmethod() expected function";
    return false;
  }
  Value target = args[0];
  std::string ignored;
  object_set_attr(target, "__isabstractmethod__", Value::boolean(true), ignored);
  value_assign_fast(out, args[0]);
  return true;
}

} // namespace

void register_abc_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_abc");
  builder.function("get_cache_token", abc_get_cache_token)
      .function("_abc_init", abc_return_none)
      .function("_abc_register", abc_register)
      .function("_abc_instancecheck", abc_instancecheck)
      .function("_abc_subclasscheck", abc_subclasscheck)
      .function("_get_dump", abc_get_dump)
      .function("_reset_registry", abc_reset_registry)
      .function("_reset_caches", abc_return_none);
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
  NativeModuleBuilder public_builder(runtime, "abc");
  public_builder.value("ABCMeta", abc_meta)
      .value("ABC", abc_class)
      .function("get_cache_token", abc_get_cache_token)
      .function("abstractmethod", abc_abstractmethod)
      .function("abstractclassmethod", abc_abstractmethod)
      .function("abstractstaticmethod", abc_abstractmethod)
      .function("abstractproperty", abc_abstractmethod);
  runtime.register_module("abc", public_builder.finish());
}

} // namespace xlang3
