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
#include "xlang3/builtin_methods.h"
#include "xlang3/contextvars_runtime.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <array>
#include <atomic>
#include <unordered_map>

namespace xlang3 {

namespace {

constexpr const char* kContextVarNativeType = "_contextvars.ContextVar";
constexpr const char* kContextNativeType = "_contextvars.Context";
constexpr const char* kTokenNativeType = "_contextvars.Token";
constexpr const char* kContextViewNativeType = "_contextvars.ContextView";

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
  std::atomic_bool entered{false};
};

struct ContextViewState {
  std::vector<Value> entries;
  size_t index = 0;
};

thread_local std::unordered_map<Object*, Value> g_root_context_values;
thread_local std::unordered_map<Object*, Value>* g_context_values = &g_root_context_values;
Value g_token_class = Value::invalid();
Value g_missing = Value::invalid();
std::array<Value, 3> g_context_view_classes;

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

ContextViewState* context_view_state(const Value& self, std::string& error) {
  auto* state = static_cast<ContextViewState*>(instance_get_native_data(self, kContextViewNativeType));
  if (state == nullptr) error = "invalid Context view object";
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

void context_view_cleanup(void* data) {
  delete static_cast<ContextViewState*>(data);
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
  auto found = g_context_values->find(args[0].as.obj);
  if (found != g_context_values->end()) {
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
  auto found = g_context_values->find(key);
  if (found != g_context_values->end()) {
    state->has_old_value = true;
    state->old_value = found->second;
  }
  (*g_context_values)[key] = args[1];
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
    (*g_context_values)[key] = token->old_value;
  } else {
    g_context_values->erase(key);
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

bool make_context_view(const Value& context, size_t kind, Value& out, std::string& error) {
  auto* context_data = context_state(context, error);
  if (context_data == nullptr) return false;
  auto* view = new ContextViewState();
  view->entries.reserve(context_data->values.size());
  for (const auto& entry : context_data->values) {
    const Value key = borrowed_object_value(entry.first);
    if (kind == 0) view->entries.emplace_back(key);
    else if (kind == 1) view->entries.push_back(Value::tuple({key, entry.second}));
    else view->entries.push_back(entry.second);
  }
  out = Value::instance(g_context_view_classes[kind]);
  if (!instance_set_native_data(out, kContextViewNativeType, view,
                                context_view_cleanup, error)) {
    delete view;
    return false;
  }
  return true;
}

bool context_view_method(Runtime&, const Value* args, uint32_t argc,
                         Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "Context view method expected self";
    return false;
  }
  return make_context_view(args[0], reinterpret_cast<size_t>(user_data), out, error);
}

bool context_view_iter(Runtime&, const Value* args, uint32_t argc,
                       Value& out, std::string& error, void*) {
  if (argc != 1 || context_view_state(args[0], error) == nullptr) return false;
  value_assign_fast(out, args[0]);
  return true;
}

bool context_view_next(Runtime& runtime, const Value* args, uint32_t argc,
                       Value& out, std::string& error, void*) {
  if (argc != 1) return false;
  auto* state = context_view_state(args[0], error);
  if (state == nullptr) return false;
  if (state->index == state->entries.size()) {
    runtime.raise_class_error("StopIteration", "");
    return false;
  }
  value_assign_fast(out, state->entries[state->index++]);
  return true;
}

bool context_view_len(Runtime&, const Value* args, uint32_t argc,
                      Value& out, std::string& error, void*) {
  if (argc != 1) return false;
  auto* state = context_view_state(args[0], error);
  if (state == nullptr) return false;
  out = Value::int64(static_cast<int64_t>(state->entries.size()));
  return true;
}

bool context_iter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Context.__iter__ expected self";
    return false;
  }
  (void)runtime;
  return make_context_view(args[0], 0, out, error);
}

bool context_contains(Runtime&, const Value* args, uint32_t argc,
                      Value& out, std::string& error, void*) {
  if (argc != 2) return false;
  auto* state = context_state(args[0], error);
  Object* key = context_var_key(args[1], error);
  if (state == nullptr || key == nullptr) return false;
  out = Value::boolean(state->values.find(key) != state->values.end());
  return true;
}

bool context_get(Runtime&, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) return false;
  auto* state = context_state(args[0], error);
  Object* key = context_var_key(args[1], error);
  if (state == nullptr || key == nullptr) return false;
  const auto found = state->values.find(key);
  if (found != state->values.end()) value_assign_fast(out, found->second);
  else if (argc == 3) value_assign_fast(out, args[2]);
  else value_set_none(out);
  return true;
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

bool context_run_kw(Runtime& runtime, const Value* args, uint32_t argc,
                    const NativeKeywordArg* kwargs, uint32_t kwargc,
                    Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "Context.run() expected callable and optional arguments";
    return false;
  }
  auto* state = context_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<std::pair<std::string, Value>> keyword_values;
  if (kwargc != 0) {
    keyword_values.reserve(kwargc);
    for (uint32_t index = 0; index < kwargc; ++index) {
      if (kwargs[index].name == nullptr || kwargs[index].value == nullptr) {
        error = "Context.run() received invalid keyword argument";
        return false;
      }
      keyword_values.emplace_back(kwargs[index].name, *kwargs[index].value);
    }
  }
  // Context.run is on every asyncio Task step. Switch the thread-local active
  // map pointer, matching CPython's current-Context pointer model. Keeping each
  // Context's bindings in place avoids four unordered_map swaps per callback;
  // the guard restores nested calls and clears entered state on every return.
  bool expected_not_entered = false;
  if (!state->entered.compare_exchange_strong(
          expected_not_entered, true, std::memory_order_acq_rel)) {
    error = "cannot enter context: context is already entered";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  struct ActiveContextGuard {
    std::unordered_map<Object*, Value>* previous;
    ContextState& state;
    ActiveContextGuard(ContextState& entered_state)
        : previous(g_context_values), state(entered_state) {
      g_context_values = &state.values;
    }
    ~ActiveContextGuard() {
      g_context_values = previous;
      state.entered.store(false, std::memory_order_release);
    }
  } active_context_guard(*state);
  // asyncio's Handle.run path supplies positional callback args only. Keep
  // that per-Task path out of the general keyword-call adapter.
  const bool ok = kwargc == 0
      ? runtime_call_callable(runtime, args[1], args + 2, argc - 2, out, error)
      : runtime_call_callable_kw(
            runtime, args[1], args + 2, argc - 2, keyword_values, out, error);
  return ok;
}

bool context_run(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* user_data) {
  return context_run_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
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
  state->values = *g_context_values;
  if (!instance_set_native_data(out, kContextNativeType, state, context_cleanup, error)) {
    delete state;
    return false;
  }
  return true;
}

bool context_generic_class_getitem(
    Runtime& runtime, const Value* args, uint32_t argc,
    Value& out, std::string& error, void*) {
  if (argc != 2 || value_as_class(args[0]) == nullptr) {
    error = "__class_getitem__ expects a class and one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value parameters;
  if (value_as_tuple(args[1]) != nullptr) value_assign_fast(parameters, args[1]);
  else parameters = Value::tuple({args[1]});
  out = Value::generic_alias(args[0], std::move(parameters));
  return true;
}

Value make_context_var_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_contextvars")});
  attrs.push_back({"__class_getitem__", Value::class_method(
      runtime.make_native_function("_contextvars.ContextVar.__class_getitem__",
                                   context_generic_class_getitem))});
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
  attrs.push_back({"__class_getitem__", Value::class_method(
      runtime.make_native_function("_contextvars.Token.__class_getitem__",
                                   context_generic_class_getitem))});
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
  attrs.push_back({"__contains__", runtime.make_native_function("_contextvars.Context.__contains__", context_contains)});
  attrs.push_back({"get", runtime.make_native_function("_contextvars.Context.get", context_get)});
  attrs.push_back({"keys", runtime.make_native_function("_contextvars.Context.keys", context_view_method, reinterpret_cast<void*>(0))});
  attrs.push_back({"items", runtime.make_native_function("_contextvars.Context.items", context_view_method, reinterpret_cast<void*>(1))});
  attrs.push_back({"values", runtime.make_native_function("_contextvars.Context.values", context_view_method, reinterpret_cast<void*>(2))});
  attrs.push_back({"copy", runtime.make_native_function("_contextvars.Context.copy", context_copy)});
  // Direct Context.run calls use the register-backed path. Asyncio's Handle
  // also invokes it with star-expanded callback args, which remains on the
  // generic CallMethodEx path until the VM can vectorcall expanded arguments.
  attrs.push_back({"run", runtime.make_native_function(
      "_contextvars.Context.run", context_run, nullptr, nullptr,
      builtin_method_fast_adapter<context_run, 2>,
      false, context_run_kw)});
  return Value::class_object("Context", std::move(attrs));
}

} // namespace

Object* contextvar_exact_getter_key(const Value& bound_getter) {
  auto* bound = value_as_bound_method(bound_getter);
  auto* native_getter = bound == nullptr ? nullptr : value_as_native_function(bound->function);
  auto* instance = bound == nullptr ? nullptr : value_as_instance(bound->self);
  if (instance == nullptr || instance->native_type != kContextVarNativeType ||
      instance->native_data == nullptr || native_getter == nullptr ||
      native_getter->callback != context_var_get) return nullptr;
  return &instance->header;
}

bool contextvar_lookup_if_set(Object* variable_key, Value& out) {
  if (variable_key == nullptr) return false;
  // Internal runtime clients already hold this exact ContextVar object. Read
  // its thread-local binding directly instead of dispatching its get method
  // for every hot Decimal operation; absent values still use the full API.
  const auto found = g_context_values->find(variable_key);
  if (found == g_context_values->end()) return false;
  value_assign_fast(out, found->second);
  return true;
}

void register_contextvars_module(Runtime& runtime) {
  for (size_t index = 0; index < g_context_view_classes.size(); ++index) {
    std::vector<std::pair<std::string, Value>> attrs;
    attrs.push_back({"__module__", Value::string("builtins")});
    attrs.push_back({"__iter__", runtime.make_native_function("_contextvars.ContextView.__iter__", context_view_iter)});
    attrs.push_back({"__next__", runtime.make_native_function("_contextvars.ContextView.__next__", context_view_next)});
    attrs.push_back({"__len__", runtime.make_native_function("_contextvars.ContextView.__len__", context_view_len)});
    g_context_view_classes[index] = Value::class_object(
        index == 0 ? "keys" : index == 1 ? "items" : "values", std::move(attrs));
  }
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
