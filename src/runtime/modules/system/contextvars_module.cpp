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

#include <unordered_map>

namespace xlang3 {

namespace {

constexpr const char* kContextVarNativeType = "_contextvars.ContextVar";
constexpr const char* kContextNativeType = "_contextvars.Context";
constexpr const char* kTokenNativeType = "_contextvars.Token";

struct ContextVarState {
  std::string name;
  bool has_default = false;
  Value default_value = Value::none();
};

struct TokenState {
  Value var = Value::none();
  Value old_value = Value::none();
  bool has_old_value = false;
  bool used = false;
};

struct ContextState {
  std::unordered_map<Object*, Value> values;
};

thread_local std::unordered_map<Object*, Value> g_context_values;
Value g_token_class = Value::invalid();
Value g_missing = Value::invalid();

Value borrowed_object_value(Object* object) {
  Value out;
  out.tag = ValueTag::Object;
  out.flags = kXlangValueBorrowedRefFlag;
  out.as.obj = object;
  return out;
}

ContextVarState* context_var_state(const Value& self, std::string& error) {
  auto* state = static_cast<ContextVarState*>(instance_get_native_data(self, kContextVarNativeType));
  if (state == nullptr) {
    error = "invalid ContextVar object";
  }
  return state;
}

TokenState* token_state(const Value& self, std::string& error) {
  auto* state = static_cast<TokenState*>(instance_get_native_data(self, kTokenNativeType));
  if (state == nullptr) {
    error = "invalid Token object";
  }
  return state;
}

ContextState* context_state(const Value& self, std::string& error) {
  auto* state = static_cast<ContextState*>(instance_get_native_data(self, kContextNativeType));
  if (state == nullptr) {
    error = "invalid Context object";
  }
  return state;
}

Object* context_var_key(const Value& value, std::string& error) {
  if (context_var_state(value, error) == nullptr) {
    return nullptr;
  }
  return value.as.obj;
}

void context_var_cleanup(void* data) {
  delete static_cast<ContextVarState*>(data);
}

void token_cleanup(void* data) {
  delete static_cast<TokenState*>(data);
}

void context_cleanup(void* data) {
  delete static_cast<ContextState*>(data);
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  auto* text = value_as_string(value);
  if (text == nullptr) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = string_object_to_string(*text);
  return true;
}

bool raise_context_lookup_error(Runtime& runtime, std::string& error) {
  error = "ContextVar has no value";
  runtime.raise_class_error("LookupError", error);
  return false;
}

Value missing_value(Runtime& runtime) {
  if (g_missing.tag == ValueTag::Invalid) {
    Value object_class = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
    g_missing = Value::instance(object_class);
  }
  return g_missing;
}

bool context_var_init_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 2 || argc > 3) {
    error = "ContextVar() expected name and optional default";
    return false;
  }
  auto* state = new ContextVarState();
  if (!get_string_arg(args[1], "ContextVar name", state->name, error)) {
    delete state;
    return false;
  }
  if (argc == 3) {
    state->has_default = true;
    state->default_value = args[2];
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string key(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      delete state;
      error = "ContextVar() received invalid keyword argument";
      return false;
    }
    if (key == "default") {
      state->has_default = true;
      state->default_value = *kwargs[i].value;
    } else {
      delete state;
      error = "ContextVar() got an unexpected keyword argument '" + key + "'";
      return false;
    }
  }
  if (!instance_set_native_data(args[0], kContextVarNativeType, state, context_var_cleanup, error)) {
    delete state;
    return false;
  }
  Value self = args[0];
  object_set_attr(self, "name", args[1], error);
  value_set_none(out);
  return true;
}

bool context_var_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return context_var_init_common(runtime, args, argc, nullptr, 0, out, error);
}

bool context_var_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return context_var_init_common(runtime, args, argc, kwargs, kwargc, out, error);
}

bool context_var_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ContextVar.get() expected optional default";
    return false;
  }
  auto* state = context_var_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  auto found = g_context_values.find(args[0].as.obj);
  if (found != g_context_values.end()) {
    value_assign_fast(out, found->second);
    return true;
  }
  if (argc == 2) {
    value_assign_fast(out, args[1]);
    return true;
  }
  if (state->has_default) {
    value_assign_fast(out, state->default_value);
    return true;
  }
  return raise_context_lookup_error(runtime, error);
}

bool context_var_set(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ContextVar.set() expected value";
    return false;
  }
  Object* key = context_var_key(args[0], error);
  if (key == nullptr) {
    return false;
  }
  if (g_token_class.tag == ValueTag::Invalid) {
    error = "Token class is not initialized";
    return false;
  }
  Value token = Value::instance(g_token_class);
  auto* state = new TokenState();
  state->var = args[0];
  auto found = g_context_values.find(key);
  if (found != g_context_values.end()) {
    state->has_old_value = true;
    state->old_value = found->second;
  }
  g_context_values[key] = args[1];
  if (!instance_set_native_data(token, kTokenNativeType, state, token_cleanup, error)) {
    delete state;
    return false;
  }
  Value old_value = state->has_old_value ? state->old_value : missing_value(runtime);
  object_set_attr(token, "var", args[0], error);
  object_set_attr(token, "old_value", old_value, error);
  value_assign_fast(out, token);
  return true;
}

bool context_var_reset(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ContextVar.reset() expected token";
    return false;
  }
  Object* key = context_var_key(args[0], error);
  if (key == nullptr) {
    return false;
  }
  auto* token = token_state(args[1], error);
  if (token == nullptr) {
    return false;
  }
  if (token->used) {
    error = "Token has already been used";
    return false;
  }
  if (!value_is(token->var, args[0])) {
    error = "Token was created by a different ContextVar";
    return false;
  }
  if (token->has_old_value) {
    g_context_values[key] = token->old_value;
  } else {
    g_context_values.erase(key);
  }
  token->used = true;
  value_set_none(out);
  return true;
}

bool context_var_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ContextVar.__repr__ expected self";
    return false;
  }
  auto* state = context_var_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::string("<ContextVar name='" + state->name + "'>");
  return true;
}

bool context_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Context() expected no arguments";
    return false;
  }
  auto* state = new ContextState();
  if (!instance_set_native_data(args[0], kContextNativeType, state, context_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool context_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Context.__getitem__ expected key";
    return false;
  }
  auto* state = context_state(args[0], error);
  Object* key = context_var_key(args[1], error);
  if (state == nullptr || key == nullptr) {
    return false;
  }
  auto found = state->values.find(key);
  if (found == state->values.end()) {
    return raise_context_lookup_error(runtime, error);
  }
  value_assign_fast(out, found->second);
  return true;
}

bool context_len(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Context.__len__ expected self";
    return false;
  }
  auto* state = context_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::int64(static_cast<int64_t>(state->values.size()));
  return true;
}

bool context_iter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Context.__iter__ expected self";
    return false;
  }
  auto* state = context_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> keys;
  keys.reserve(state->values.size());
  for (const auto& entry : state->values) {
    keys.push_back(borrowed_object_value(entry.first));
  }
  out = Value::list(std::move(keys));
  return runtime_get_iter(runtime, out, out, error);
}

bool context_copy(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Context.copy() expected self";
    return false;
  }
  auto* state = context_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::instance(args[0].as.obj != nullptr ? value_as_instance(args[0])->klass : Value::invalid());
  auto* copy = new ContextState();
  copy->values = state->values;
  if (!instance_set_native_data(out, kContextNativeType, copy, context_cleanup, error)) {
    delete copy;
    return false;
  }
  return true;
}

bool context_run(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "Context.run() expected callable and optional arguments";
    return false;
  }
  auto* state = context_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  auto previous = g_context_values;
  g_context_values = state->values;
  const bool ok = runtime_call_callable(runtime, args[1], args + 2, argc - 2, out, error);
  state->values = g_context_values;
  g_context_values = std::move(previous);
  return ok;
}

bool copy_context(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "copy_context() takes no arguments";
    return false;
  }
  Value context_class;
  std::string ignored;
  if (!object_get_attr(g_token_class, "__xlang3_context_class__", context_class, ignored)) {
    error = "Context class is not initialized";
    return false;
  }
  out = Value::instance(context_class);
  auto* state = new ContextState();
  state->values = g_context_values;
  if (!instance_set_native_data(out, kContextNativeType, state, context_cleanup, error)) {
    delete state;
    return false;
  }
  return true;
}

Value make_context_var_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_contextvars")});
  attrs.push_back({"__init__", runtime.make_native_function("_contextvars.ContextVar.__init__", context_var_init, nullptr, nullptr, nullptr, false, context_var_init_kw)});
  attrs.push_back({"get", runtime.make_native_function("_contextvars.ContextVar.get", context_var_get)});
  attrs.push_back({"set", runtime.make_native_function("_contextvars.ContextVar.set", context_var_set)});
  attrs.push_back({"reset", runtime.make_native_function("_contextvars.ContextVar.reset", context_var_reset)});
  attrs.push_back({"__repr__", runtime.make_native_function("_contextvars.ContextVar.__repr__", context_var_repr)});
  return Value::class_object("ContextVar", std::move(attrs));
}

Value make_token_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_contextvars")});
  attrs.push_back({"MISSING", missing_value(runtime)});
  return Value::class_object("Token", std::move(attrs));
}

Value make_context_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_contextvars")});
  attrs.push_back({"__init__", runtime.make_native_function("_contextvars.Context.__init__", context_init)});
  attrs.push_back({"__getitem__", runtime.make_native_function("_contextvars.Context.__getitem__", context_getitem)});
  attrs.push_back({"__len__", runtime.make_native_function("_contextvars.Context.__len__", context_len)});
  attrs.push_back({"__iter__", runtime.make_native_function("_contextvars.Context.__iter__", context_iter)});
  attrs.push_back({"copy", runtime.make_native_function("_contextvars.Context.copy", context_copy)});
  attrs.push_back({"run", runtime.make_native_function("_contextvars.Context.run", context_run)});
  return Value::class_object("Context", std::move(attrs));
}

} // namespace

void register_contextvars_module(Runtime& runtime) {
  Value context_var_class = make_context_var_class(runtime);
  Value context_class = make_context_class(runtime);
  g_token_class = make_token_class(runtime);
  std::string ignored;
  object_set_attr(g_token_class, "__xlang3_context_class__", context_class, ignored);

  NativeModuleBuilder builder(runtime, "_contextvars");
  builder.value("ContextVar", context_var_class)
      .value("Context", context_class)
      .value("Token", g_token_class)
      .function("copy_context", copy_context);
  runtime.register_module("_contextvars", builder.finish());
}

} // namespace xlang3
