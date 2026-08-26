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
#include "xlang3/value_hash.h"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace xlang3 {

namespace {

struct PartialState {
  Value callable;
  std::vector<Value> bound_args;
};

struct WrapsState {
  Value wrapped;
};

struct CmpToKeyClassState {
  Value cmp;
};

struct CmpToKeyObjectState {
  Value object;
  Value cmp;
};

struct LruCacheEntry {
  size_t hash = 0;
  Value key;
  Value value;
  uint64_t serial = 0;
};

struct LruCacheState {
  Value callable;
  int64_t maxsize = 128;
  bool unlimited = false;
  bool typed = false;
  uint64_t hits = 0;
  uint64_t misses = 0;
  uint64_t serial = 0;
  std::vector<LruCacheEntry> entries;
};

struct LruCacheConfig {
  int64_t maxsize = 128;
  bool unlimited = false;
  bool typed = false;
};

enum class TotalOrderingOp : uintptr_t {
  Le = 1,
  Gt = 2,
  Ge = 3,
};

void wraps_cleanup(void* data) {
  delete static_cast<WrapsState*>(data);
}

void partial_cleanup(void* data) {
  delete static_cast<PartialState*>(data);
}

void cmp_to_key_object_cleanup(void* data) {
  delete static_cast<CmpToKeyObjectState*>(data);
}

void lru_cache_config_cleanup(void* data) {
  delete static_cast<LruCacheConfig*>(data);
}

void lru_cache_shared_cleanup(void* data) {
  delete static_cast<std::shared_ptr<LruCacheState>*>(data);
}

bool functools_is_callable(const Value& value) {
  return value_as_function(value) != nullptr ||
         value_as_native_function(value) != nullptr ||
         value_as_bound_method(value) != nullptr;
}

std::shared_ptr<LruCacheState>* lru_cache_state_handle(void* user_data, std::string& error) {
  auto* handle = static_cast<std::shared_ptr<LruCacheState>*>(user_data);
  if (handle == nullptr || *handle == nullptr) {
    error = "invalid functools cache state";
    return nullptr;
  }
  return handle;
}

Value lru_cache_make_key(const Value* args, uint32_t argc, bool typed) {
  if (!typed && argc == 1) {
    return args[0];
  }
  std::vector<Value> items;
  items.reserve(static_cast<size_t>(argc) + (typed ? argc + 1 : 0));
  for (uint32_t i = 0; i < argc; ++i) {
    items.push_back(args[i]);
  }
  if (typed) {
    items.push_back(Value::string("__typed__"));
    for (uint32_t i = 0; i < argc; ++i) {
      items.push_back(Value::int64(static_cast<int64_t>(args[i].tag)));
      if (args[i].tag == ValueTag::Object && args[i].as.obj != nullptr) {
        items.push_back(Value::int64(static_cast<int64_t>(args[i].as.obj->kind)));
      } else {
        items.push_back(Value::int64(-1));
      }
    }
  }
  return Value::tuple(std::move(items));
}

bool lru_cache_lookup(LruCacheState& state, const Value& key, size_t hash, Value& out) {
  for (auto& entry : state.entries) {
    if (entry.hash == hash && value_key_equal(entry.key, key)) {
      ++state.hits;
      entry.serial = ++state.serial;
      value_assign_fast(out, entry.value);
      return true;
    }
  }
  ++state.misses;
  return false;
}

void lru_cache_store(LruCacheState& state, Value key, size_t hash, const Value& value) {
  if (state.maxsize == 0 && !state.unlimited) {
    return;
  }
  LruCacheEntry entry;
  entry.hash = hash;
  entry.key = std::move(key);
  entry.value = value;
  entry.serial = ++state.serial;
  state.entries.push_back(std::move(entry));
  if (!state.unlimited && static_cast<int64_t>(state.entries.size()) > state.maxsize) {
    auto it = std::min_element(state.entries.begin(), state.entries.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.serial < rhs.serial;
    });
    if (it != state.entries.end()) {
      state.entries.erase(it);
    }
  }
}

bool lru_cache_wrapper_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* handle = lru_cache_state_handle(user_data, error);
  if (handle == nullptr) {
    return false;
  }
  auto& state = **handle;
  Value key = lru_cache_make_key(args, argc, state.typed);
  size_t hash = 0;
  if (!value_hash_key(key, hash, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (lru_cache_lookup(state, key, hash, out)) {
    return true;
  }
  Value computed;
  if (!runtime_call_callable(runtime, state.callable, args, argc, computed, error)) {
    return false;
  }
  lru_cache_store(state, std::move(key), hash, computed);
  value_assign_fast(out, computed);
  return true;
}

bool lru_cache_wrapper_call_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (kwargc != 0) {
    error = "functools cache keyword calls are not implemented";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return lru_cache_wrapper_call(runtime, args, argc, out, error, user_data);
}

bool lru_cache_info_call(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) {
    error = "cache_info() takes no arguments";
    return false;
  }
  auto* handle = lru_cache_state_handle(user_data, error);
  if (handle == nullptr) {
    return false;
  }
  const auto& state = **handle;
  out = Value::tuple({
      Value::int64(static_cast<int64_t>(state.hits)),
      Value::int64(static_cast<int64_t>(state.misses)),
      state.unlimited ? Value::none() : Value::int64(state.maxsize),
      Value::int64(static_cast<int64_t>(state.entries.size()))});
  return true;
}

bool lru_cache_clear_call(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) {
    error = "cache_clear() takes no arguments";
    return false;
  }
  auto* handle = lru_cache_state_handle(user_data, error);
  if (handle == nullptr) {
    return false;
  }
  auto& state = **handle;
  state.entries.clear();
  state.hits = 0;
  state.misses = 0;
  state.serial = 0;
  value_set_none(out);
  return true;
}

bool lru_cache_parameters_call(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) {
    error = "cache_parameters() takes no arguments";
    return false;
  }
  auto* handle = lru_cache_state_handle(user_data, error);
  if (handle == nullptr) {
    return false;
  }
  const auto& state = **handle;
  out = Value::dict({
      {Value::string("maxsize"), state.unlimited ? Value::none() : Value::int64(state.maxsize)},
      {Value::string("typed"), Value::boolean(state.typed)}});
  return true;
}

bool lru_cache_build_wrapper(Runtime& runtime, const Value& callable, const LruCacheConfig& config, Value& out, std::string& error) {
  if (!functools_is_callable(callable)) {
    error = "the first argument must be callable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto state = std::make_shared<LruCacheState>();
  state->callable = callable;
  state->maxsize = config.maxsize;
  state->unlimited = config.unlimited;
  state->typed = config.typed;
  auto* wrapper_handle = new std::shared_ptr<LruCacheState>(state);
  out = runtime.make_native_function(
      "functools._lru_cache_wrapper",
      lru_cache_wrapper_call,
      wrapper_handle,
      lru_cache_shared_cleanup,
      nullptr,
      false,
      lru_cache_wrapper_call_kw);

  auto* info_handle = new std::shared_ptr<LruCacheState>(state);
  auto* clear_handle = new std::shared_ptr<LruCacheState>(state);
  auto* parameters_handle = new std::shared_ptr<LruCacheState>(state);
  if (!object_set_attr(out, "cache_info", runtime.make_native_function("functools._lru_cache_wrapper.cache_info", lru_cache_info_call, info_handle, lru_cache_shared_cleanup), error) ||
      !object_set_attr(out, "cache_clear", runtime.make_native_function("functools._lru_cache_wrapper.cache_clear", lru_cache_clear_call, clear_handle, lru_cache_shared_cleanup), error) ||
      !object_set_attr(out, "cache_parameters", runtime.make_native_function("functools._lru_cache_wrapper.cache_parameters", lru_cache_parameters_call, parameters_handle, lru_cache_shared_cleanup), error) ||
      !object_set_attr(out, "__wrapped__", callable, error)) {
    return false;
  }
  return true;
}

bool lru_cache_decorator_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "lru_cache decorator expected one callable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* config = static_cast<LruCacheConfig*>(user_data);
  if (config == nullptr) {
    error = "invalid lru_cache decorator";
    return false;
  }
  return lru_cache_build_wrapper(runtime, args[0], *config, out, error);
}

bool parse_lru_cache_options(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    LruCacheConfig& config,
    bool& direct_callable,
    Value& callable,
    std::string& error) {
  direct_callable = false;
  if (argc > 1) {
    error = "lru_cache() expected at most one positional argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 1) {
    if (functools_is_callable(args[0])) {
      direct_callable = true;
      value_assign_fast(callable, args[0]);
    } else if (args[0].tag == ValueTag::None) {
      config.unlimited = true;
    } else if (args[0].tag == ValueTag::Int64) {
      config.maxsize = args[0].as.i64 < 0 ? 0 : args[0].as.i64;
    } else {
      error = "lru_cache() maxsize must be an integer, None, or callable";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      error = "lru_cache() received invalid keyword argument";
      return false;
    }
    if (name == "maxsize") {
      if (kwargs[i].value->tag == ValueTag::None) {
        config.unlimited = true;
      } else if (kwargs[i].value->tag == ValueTag::Int64) {
        config.unlimited = false;
        config.maxsize = kwargs[i].value->as.i64 < 0 ? 0 : kwargs[i].value->as.i64;
      } else {
        error = "lru_cache() maxsize must be an integer or None";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    } else if (name == "typed") {
      config.typed = value_truthy(*kwargs[i].value);
    } else {
      error = "lru_cache() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return true;
}

bool lru_cache_entry(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  LruCacheConfig config;
  bool direct_callable = false;
  Value callable;
  if (!parse_lru_cache_options(runtime, args, argc, kwargs, kwargc, config, direct_callable, callable, error)) {
    return false;
  }
  if (direct_callable) {
    return lru_cache_build_wrapper(runtime, callable, config, out, error);
  }
  auto* stored = new LruCacheConfig(config);
  out = runtime.make_native_function("functools.lru_cache.<decorator>", lru_cache_decorator_call, stored, lru_cache_config_cleanup);
  return true;
}

bool lru_cache_entry_positional(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return lru_cache_entry(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool cache_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "cache() expected one callable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  LruCacheConfig config;
  config.unlimited = true;
  return lru_cache_build_wrapper(runtime, args[0], config, out, error);
}

CmpToKeyObjectState* cmp_to_key_object_state(const Value& self, std::string& error) {
  auto* state = static_cast<CmpToKeyObjectState*>(instance_get_native_data(self, "functools.cmp_to_key.KeyWrapper"));
  if (state == nullptr) {
    error = "invalid cmp_to_key object";
  }
  return state;
}

bool partial_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<PartialState*>(user_data);
  if (state == nullptr) {
    error = "invalid functools.partial object";
    return false;
  }
  std::vector<Value> call_args;
  call_args.reserve(state->bound_args.size() + argc);
  for (const auto& arg : state->bound_args) {
    call_args.push_back(arg);
  }
  for (uint32_t i = 0; i < argc; ++i) {
    call_args.push_back(args[i]);
  }
  return runtime_call_callable(
      runtime,
      state->callable,
      call_args.empty() ? nullptr : call_args.data(),
      static_cast<uint32_t>(call_args.size()),
      out,
      error);
}

bool partial_call_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return partial_call(runtime, args, argc, out, error, user_data);
}

bool partial_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "functools.partial() expected callable";
    return false;
  }
  auto* state = new PartialState();
  state->callable = args[0];
  state->bound_args.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    state->bound_args.push_back(args[i]);
  }
  out = runtime.make_native_function(
      "functools.partial.<call>",
      partial_call,
      state,
      partial_cleanup,
      nullptr,
      false,
      partial_call_kw);
  std::vector<Value> bound_args;
  bound_args.reserve(state->bound_args.size());
  for (const auto& arg : state->bound_args) {
    bound_args.push_back(arg);
  }
  if (!object_set_attr(out, "func", state->callable, error) ||
      !object_set_attr(out, "args", Value::tuple(std::move(bound_args)), error) ||
      !object_set_attr(out, "keywords", Value::none(), error)) {
    return false;
  }
  return true;
}

bool reduce_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "functools.reduce() expected function, iterable, and optional initializer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  bool done = false;
  Value accumulator;
  if (argc == 3) {
    value_assign_fast(accumulator, args[2]);
  } else {
    if (!sequence_iter_next(iterator, done, accumulator, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      error = "reduce() of empty iterable with no initial value";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }

  for (;;) {
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      value_assign_fast(out, accumulator);
      return true;
    }
    Value call_args[2] = {accumulator, item};
    Value next;
    if (!runtime_call_callable(runtime, args[0], call_args, 2, next, error)) {
      return false;
    }
    value_assign_fast(accumulator, next);
  }
}

bool cmp_to_key_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "cmp_to_key key wrapper expected one object";
    return false;
  }
  auto* class_state = static_cast<CmpToKeyClassState*>(user_data);
  if (class_state == nullptr) {
    error = "invalid cmp_to_key class";
    return false;
  }
  auto* state = new CmpToKeyObjectState();
  state->object = args[1];
  state->cmp = class_state->cmp;
  if (!instance_set_native_data(args[0], "functools.cmp_to_key.KeyWrapper", state, cmp_to_key_object_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool cmp_to_key_compare(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "cmp_to_key comparison expected other";
    return false;
  }
  auto* lhs = cmp_to_key_object_state(args[0], error);
  auto* rhs = cmp_to_key_object_state(args[1], error);
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  Value cmp_args[2] = {lhs->object, rhs->object};
  Value result;
  if (!runtime_call_callable(runtime, lhs->cmp, cmp_args, 2, result, error)) {
    return false;
  }
  if (result.tag != ValueTag::Int64) {
    error = "cmp_to_key comparison function must return int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const intptr_t op = reinterpret_cast<intptr_t>(user_data);
  bool ok = false;
  switch (op) {
    case -2: ok = result.as.i64 < 0; break;
    case -1: ok = result.as.i64 <= 0; break;
    case 0: ok = result.as.i64 == 0; break;
    case 1: ok = result.as.i64 > 0; break;
    case 2: ok = result.as.i64 >= 0; break;
    default: ok = result.as.i64 != 0; break;
  }
  value_set_bool(out, ok);
  return true;
}

bool cmp_to_key_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "functools.cmp_to_key() expected a comparison function";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = new CmpToKeyClassState();
  state->cmp = args[0];
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("functools")});
  attrs.push_back({"__init__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__init__", cmp_to_key_init, state)});
  attrs.push_back({"__lt__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__lt__", cmp_to_key_compare, reinterpret_cast<void*>(-2))});
  attrs.push_back({"__le__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__le__", cmp_to_key_compare, reinterpret_cast<void*>(-1))});
  attrs.push_back({"__eq__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__eq__", cmp_to_key_compare, reinterpret_cast<void*>(0))});
  attrs.push_back({"__gt__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__gt__", cmp_to_key_compare, reinterpret_cast<void*>(1))});
  attrs.push_back({"__ge__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__ge__", cmp_to_key_compare, reinterpret_cast<void*>(2))});
  attrs.push_back({"__ne__", runtime.make_native_function("functools.cmp_to_key.KeyWrapper.__ne__", cmp_to_key_compare, reinterpret_cast<void*>(3))});
  out = Value::class_object("KeyWrapper", std::move(attrs));
  return true;
}

bool call_self_compare(
    Runtime& runtime,
    const Value& self,
    const char* method,
    const Value& other,
    Value& out,
    std::string& error) {
  Value callable;
  if (!object_get_attr(self, method, callable, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return runtime_call_callable(runtime, callable, &other, 1, out, error);
}

bool total_ordering_compare(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "total_ordering comparison expected other";
    return false;
  }
  Value lt;
  if (!call_self_compare(runtime, args[0], "__lt__", args[1], lt, error)) {
    return false;
  }
  const auto op = static_cast<TotalOrderingOp>(reinterpret_cast<uintptr_t>(user_data));
  if (op == TotalOrderingOp::Gt) {
    if (value_truthy(lt)) {
      value_set_bool(out, false);
      return true;
    }
    Value eq;
    if (!call_self_compare(runtime, args[0], "__eq__", args[1], eq, error)) {
      return false;
    }
    value_set_bool(out, !value_truthy(eq));
    return true;
  }
  if (op == TotalOrderingOp::Le) {
    if (value_truthy(lt)) {
      value_set_bool(out, true);
      return true;
    }
    Value eq;
    if (!call_self_compare(runtime, args[0], "__eq__", args[1], eq, error)) {
      return false;
    }
    value_set_bool(out, value_truthy(eq));
    return true;
  }
  Value eq;
  if (!call_self_compare(runtime, args[0], "__eq__", args[1], eq, error)) {
    return false;
  }
  value_set_bool(out, value_truthy(lt) || value_truthy(eq));
  return true;
}

bool class_has_local_attr(const Value& klass, const char* name) {
  Value ignored;
  std::string error;
  return object_get_attr(klass, name, ignored, error);
}

bool total_ordering_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_class(args[0]) == nullptr) {
    error = "functools.total_ordering() expected a class";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value klass = args[0];
  if (!class_has_local_attr(klass, "__le__")) {
    if (!object_set_attr(
            klass,
            "__le__",
            runtime.make_native_function("functools.total_ordering.__le__", total_ordering_compare, reinterpret_cast<void*>(static_cast<uintptr_t>(TotalOrderingOp::Le))),
            error)) {
      return false;
    }
  }
  if (!class_has_local_attr(klass, "__gt__")) {
    if (!object_set_attr(
            klass,
            "__gt__",
            runtime.make_native_function("functools.total_ordering.__gt__", total_ordering_compare, reinterpret_cast<void*>(static_cast<uintptr_t>(TotalOrderingOp::Gt))),
            error)) {
      return false;
    }
  }
  if (!class_has_local_attr(klass, "__ge__")) {
    if (!object_set_attr(
            klass,
            "__ge__",
            runtime.make_native_function("functools.total_ordering.__ge__", total_ordering_compare, reinterpret_cast<void*>(static_cast<uintptr_t>(TotalOrderingOp::Ge))),
            error)) {
      return false;
    }
  }
  value_assign_fast(out, klass);
  return true;
}

bool copy_attr_if_present(Value& wrapper, const Value& wrapped, const char* name, std::string& error) {
  Value attr;
  std::string ignored;
  if (!object_get_attr(wrapped, name, attr, ignored)) {
    return true;
  }
  return object_set_attr(wrapper, name, attr, error);
}

bool update_wrapper_common(Value wrapper, const Value& wrapped, Value& out, std::string& error) {
  if (!copy_attr_if_present(wrapper, wrapped, "__module__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__name__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__qualname__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__doc__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__annotations__", error)) {
    return false;
  }
  if (!object_set_attr(wrapper, "__wrapped__", wrapped, error)) {
    return false;
  }
  value_assign_fast(out, wrapper);
  return true;
}

bool update_wrapper_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "update_wrapper() expected wrapper and wrapped";
    return false;
  }
  return update_wrapper_common(args[0], args[1], out, error);
}

bool wraps_apply(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "wraps decorator expected one function";
    return false;
  }
  auto* state = static_cast<WrapsState*>(user_data);
  if (state == nullptr) {
    error = "invalid functools.wraps decorator";
    return false;
  }
  return update_wrapper_common(args[0], state->wrapped, out, error);
}

bool wraps_entry(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "wraps() expected wrapped function";
    return false;
  }
  auto* state = new WrapsState();
  state->wrapped = args[0];
  out = runtime.make_native_function("functools.wraps.<decorator>", wraps_apply, state, wraps_cleanup);
  return true;
}

bool wraps_entry_positional(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return wraps_entry(runtime, args, argc, nullptr, 0, out, error, nullptr);
}

bool identity_decorator_factory(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 1 && (value_as_function(args[0]) != nullptr || value_as_native_function(args[0]) != nullptr)) {
    value_assign_fast(out, args[0]);
    return true;
  }
  if (argc > 1) {
    error = "decorator expected optional function";
    return false;
  }
  out = runtime.make_native_function("functools.identity_decorator.<decorator>", wraps_apply);
  return true;
}

} // namespace

void register_functools_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "functools");
  builder.function("update_wrapper", update_wrapper_entry)
      .value(
          "wraps",
          runtime.make_native_function(
              "functools.wraps",
              wraps_entry_positional,
              nullptr,
              nullptr,
              nullptr,
              false,
              wraps_entry))
      .value(
          "lru_cache",
          runtime.make_native_function(
              "functools.lru_cache",
              lru_cache_entry_positional,
              nullptr,
              nullptr,
              nullptr,
              false,
              lru_cache_entry))
      .function("cache", cache_entry)
      .function("partial", partial_entry)
      .function("reduce", reduce_entry)
      .function("cmp_to_key", cmp_to_key_entry)
      .function("total_ordering", total_ordering_entry);
  runtime.register_module("functools", builder.finish());
}

} // namespace xlang3
