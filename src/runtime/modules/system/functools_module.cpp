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
#include "xlang3/value_hash.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string_view>
#include <vector>

namespace xlang3 {
namespace {

struct LruEntry {
  Value key;
  Value result;
  size_t hash = 0;
  uint64_t age = 0;
};

struct LruState {
  Value function;
  Value cache_info_type;
  Value keyword_marker;
  int64_t maxsize = 128;  // -1 means unbounded; zero disables storage.
  bool typed = false;
  uint64_t hits = 0;
  uint64_t misses = 0;
  uint64_t clock = 0;
  // functools' reference implementation uses an RLock because hashing and
  // equality may re-enter Python. Keep that contract in this native wrapper.
  std::recursive_mutex mutex;
  std::vector<LruEntry> entries;
};

using LruStateRef = std::shared_ptr<LruState>;

void lru_state_cleanup(void* data) { delete static_cast<LruStateRef*>(data); }

bool make_cache_key(Runtime& runtime, LruState& state,
                    const Value* args, uint32_t argc,
                    const NativeKeywordArg* kwargs, uint32_t kwargc,
                    Value& key, std::string& error) {
  // Match functools._make_key's flat tuple layout. In particular, the
  // one-argument int/string fast path avoids allocating a key tuple on every
  // hit, which matters because inspect._shadowed_dict is called per member.
  if (!state.typed && kwargc == 0 && argc == 1 &&
      (args[0].tag == ValueTag::Int64 || value_as_string(args[0]) != nullptr)) {
    key = args[0];
    return true;
  }

  std::vector<Value> items;
  items.reserve(static_cast<size_t>(argc) + (kwargc ? 1 + kwargc * 2 : 0) +
                (state.typed ? static_cast<size_t>(argc) + kwargc : 0));
  for (uint32_t i = 0; i < argc; ++i) items.push_back(args[i]);
  if (kwargc != 0) items.push_back(state.keyword_marker);
  for (uint32_t i = 0; i < kwargc; ++i) {
    items.push_back(Value::string(kwargs[i].name == nullptr ? "" : kwargs[i].name));
    items.push_back(*kwargs[i].value);
  }
  if (state.typed) {
    for (uint32_t i = 0; i < argc; ++i) {
      Value type;
      if (!runtime_type_of_value(runtime, args[i], type)) {
        error = "cannot determine argument type for typed lru_cache";
        return false;
      }
      items.push_back(std::move(type));
    }
    for (uint32_t i = 0; i < kwargc; ++i) {
      Value type;
      if (!runtime_type_of_value(runtime, *kwargs[i].value, type)) {
        error = "cannot determine argument type for typed lru_cache";
        return false;
      }
      items.push_back(std::move(type));
    }
  }
  key = Value::tuple(std::move(items));
  return true;
}

bool keys_equal(Runtime& runtime, const Value& left, const Value& right,
                bool& equal, std::string& error) {
  Value compared;
  if (!runtime_value_compare(runtime, "==", left, right, compared, error)) return false;
  equal = value_truthy(compared);
  return true;
}

bool flat_positional_tuple_hash(Runtime& runtime, const Value* args, uint32_t argc,
                                size_t& out, std::string& error) {
  size_t hash = 0x345678ul;
  for (uint32_t i = 0; i < argc; ++i) {
    size_t item_hash = 0;
    if (!runtime_value_hash_key(runtime, args[i], item_hash, error)) return false;
    hash = (hash ^ item_hash) * 1000003ul;
    hash ^= argc;
  }
  out = hash == static_cast<size_t>(-1) ? static_cast<size_t>(-2) : hash;
  return true;
}

bool flat_positional_tuple_equal(Runtime& runtime, const Value& stored_key,
                                 const Value* args, uint32_t argc,
                                 bool& equal, std::string& error) {
  const auto* tuple = value_as_tuple(stored_key);
  if (tuple == nullptr || tuple->items.size() != argc) {
    equal = false;
    return true;
  }
  for (uint32_t i = 0; i < argc; ++i) {
    // Python tuple comparison skips rich comparison when corresponding
    // elements are identical; preserve that behavior while avoiding a
    // temporary candidate tuple on this lru_cache hit path.
    if (value_is(tuple->items[i], args[i])) continue;
    Value compared;
    if (!runtime_value_compare(runtime, "==", tuple->items[i], args[i], compared, error)) return false;
    if (!value_truthy(compared)) {
      equal = false;
      return true;
    }
  }
  equal = true;
  return true;
}

Value flat_positional_tuple_key(const Value* args, uint32_t argc) {
  std::vector<Value> items;
  items.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) items.push_back(args[i]);
  return Value::tuple(std::move(items));
}

bool lru_wrapper_call(Runtime& runtime, const Value* args, uint32_t argc,
                      const NativeKeywordArg* kwargs, uint32_t kwargc,
                      Value& out, std::string& error, void* user_data) {
  auto& state = **static_cast<LruStateRef*>(user_data);
  auto restore_cache_hash_exception = [&]() {
    // CPython's native _lru_cache_wrapper propagates key-hash exceptions.
    // typing._tp_cache catches TypeError to retry unhashable type arguments
    // without caching, so preserve the exception class across this ABI.
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
    } else {
      runtime.set_pending_exception(runtime.make_exception(
          "TypeError", error.empty() ? "unhashable cache key" : error));
    }
  };
  if (state.maxsize == 0) {
    {
      std::lock_guard<std::recursive_mutex> lock(state.mutex);
      ++state.misses;
    }
    std::vector<std::pair<std::string, Value>> keyword_values;
    keyword_values.reserve(kwargc);
    for (uint32_t i = 0; i < kwargc; ++i)
      keyword_values.emplace_back(kwargs[i].name == nullptr ? "" : kwargs[i].name, *kwargs[i].value);
    return runtime_call_callable_kw(runtime, state.function, args, argc, keyword_values, out, error);
  }

  // For the common multi-positional form, lru_cache's key is exactly a tuple
  // of args. Hash and compare that virtual tuple directly on hits; allocating
  // a short-lived tuple for every hit is pure VM/GC overhead. Keep ordinary
  // key construction on misses and use the same dynamic item hash/equality
  // rules as tuple so custom key types and exceptions retain their behavior.
  const bool flat_positional_key = !state.typed && kwargc == 0 && argc > 1;
  Value key;
  size_t hash = 0;
  if (flat_positional_key) {
    if (!flat_positional_tuple_hash(runtime, args, argc, hash, error)) {
      restore_cache_hash_exception();
      return false;
    }
  } else {
    if (!make_cache_key(runtime, state, args, argc, kwargs, kwargc, key, error)) {
      return false;
    }
    if (!runtime_value_hash_key(runtime, key, hash, error)) {
      restore_cache_hash_exception();
      return false;
    }
  }
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    for (auto& entry : state.entries) {
      if (entry.hash != hash) continue;
      bool equal = false;
      const bool compared = flat_positional_key
          ? flat_positional_tuple_equal(runtime, entry.key, args, argc, equal, error)
          : keys_equal(runtime, entry.key, key, equal, error);
      if (!compared) return false;
      if (!equal) continue;
      ++state.hits;
      entry.age = ++state.clock;
      out = entry.result;
      return true;
    }
    ++state.misses;
  }

  if (flat_positional_key) key = flat_positional_tuple_key(args, argc);
  std::vector<std::pair<std::string, Value>> keyword_values;
  keyword_values.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i)
    keyword_values.emplace_back(kwargs[i].name == nullptr ? "" : kwargs[i].name, *kwargs[i].value);
  Value result;
  if (!runtime_call_callable_kw(runtime, state.function, args, argc, keyword_values, result, error)) return false;

  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  // The wrapped call runs outside the lock to preserve reentrancy. If another
  // call populated this key meanwhile, keep that result and only return ours.
  for (auto& entry : state.entries) {
    if (entry.hash != hash) continue;
    bool equal = false;
    if (!keys_equal(runtime, entry.key, key, equal, error)) return false;
    if (equal) { out = result; return true; }
  }
  if (state.maxsize < 0 || state.entries.size() < static_cast<size_t>(state.maxsize)) {
    state.entries.push_back({std::move(key), result, hash, ++state.clock});
  } else if (!state.entries.empty()) {
    auto oldest = std::min_element(state.entries.begin(), state.entries.end(),
        [](const LruEntry& a, const LruEntry& b) { return a.age < b.age; });
    *oldest = {std::move(key), result, hash, ++state.clock};
  }
  out = std::move(result);
  return true;
}

bool lru_wrapper_call_plain(Runtime& runtime, const Value* args, uint32_t argc,
                            Value& out, std::string& error, void* user_data) {
  return lru_wrapper_call(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool lru_cache_info(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void* user_data) {
  auto* state_ref = static_cast<LruStateRef*>(user_data);
  LruState* state = state_ref->get();
  uint64_t hits, misses;
  size_t size;
  {
    std::lock_guard<std::recursive_mutex> lock(state->mutex);
    hits = state->hits;
    misses = state->misses;
    size = state->entries.size();
  }
  Value values[] = {Value::int64(static_cast<int64_t>(hits)),
                    Value::int64(static_cast<int64_t>(misses)),
                    state->maxsize < 0 ? Value::none() : Value::int64(state->maxsize),
                    Value::int64(static_cast<int64_t>(size))};
  return runtime_call_callable_kw(runtime, state->cache_info_type, values, 4, {}, out, error);
}

bool lru_cache_clear(Runtime&, const Value* args, uint32_t argc,
                     Value& out, std::string&, void* user_data) {
  LruState* state = static_cast<LruStateRef*>(user_data)->get();
  {
    std::lock_guard<std::recursive_mutex> lock(state->mutex);
    state->entries.clear();
    state->hits = state->misses = state->clock = 0;
  }
  out = Value::none();
  return true;
}

bool lru_cache_wrapper_factory(Runtime& runtime, const Value* args, uint32_t argc,
                               const NativeKeywordArg* kwargs, uint32_t kwargc,
                               Value& out, std::string& error, void*) {
  if (argc != 4 || kwargc != 0) {
    error = "_lru_cache_wrapper() takes exactly 4 positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto state = std::make_shared<LruState>();
  state->function = args[0];
  state->cache_info_type = args[3];
  state->keyword_marker = Value::class_object("functools._lru_cache_keyword_marker", {});
  state->typed = value_truthy(args[2]);
  if (args[1].tag == ValueTag::None) {
    state->maxsize = -1;
  } else {
    int64_t maxsize = 0;
    if (!value_int_like_to_i64(args[1], maxsize) || maxsize < 0) {
      state.reset();
      error = "maxsize must be a non-negative integer or None";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->maxsize = maxsize;
  }
  // CPython's _lru_cache_wrapper is a descriptor: decorating an instance
  // method must bind `self` before the cache key is built and the wrapped
  // function is called. Keep descriptor binding enabled on this native ABI.
  Value wrapper = runtime.make_native_function(
      "functools._lru_cache_wrapper", lru_wrapper_call_plain,
      new LruStateRef(state),
      lru_state_cleanup, nullptr, false, lru_wrapper_call, true);
  // functools.update_wrapper assigns these fields on the returned wrapper.
  // Native functions normally expose them as immutable implementation names,
  // so predeclare the per-wrapper slots that the decorator will copy.
  auto* native_wrapper = value_as_native_function(wrapper);
  native_wrapper->attrs_dict = new Value(Value::dict({
      {Value::string("__module__"), Value::none()},
      {Value::string("__name__"), Value::string("_lru_cache_wrapper")},
      {Value::string("__doc__"), Value::none()},
  }));
  Value cache_info = runtime.make_native_function(
      "functools._lru_cache_info", lru_cache_info,
      new LruStateRef(state), lru_state_cleanup, nullptr, false, nullptr, false);
  Value cache_clear = runtime.make_native_function(
      "functools._lru_cache_clear", lru_cache_clear,
      new LruStateRef(state), lru_state_cleanup, nullptr, false, nullptr, false);
  if (!object_set_attr(wrapper, "cache_info", cache_info, error) ||
      !object_set_attr(wrapper, "cache_clear", cache_clear, error)) return false;
  out = std::move(wrapper);
  return true;
}

bool lru_cache_wrapper_factory_plain(Runtime& runtime, const Value* args,
                                    uint32_t argc, Value& out,
                                    std::string& error, void* user_data) {
  return lru_cache_wrapper_factory(runtime, args, argc, nullptr, 0, out, error, user_data);
}

} // namespace

void register_functools_module(Runtime& runtime) {
  // CPython replaces functools.py's Python wrapper with this `_functools`
  // entry point. Keeping that same import hook removes repeated Python key,
  // dict, and linked-list work from inspect._shadowed_dict's hot protocol path.
  NativeModuleBuilder builder(runtime, "_functools");
  builder.value("_lru_cache_wrapper", runtime.make_native_function(
      "_functools._lru_cache_wrapper", lru_cache_wrapper_factory_plain,
      nullptr, nullptr, nullptr, false, lru_cache_wrapper_factory, false));
  runtime.register_module("_functools", builder.finish());
}

} // namespace xlang3
