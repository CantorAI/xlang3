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
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"

#include <deque>

namespace xlang3 {

namespace {

constexpr const char* kDequeNativeType = "_collections.deque";

struct TupleGetterState {
  int64_t index = 0;
};

struct DequeState {
  std::deque<Value> items;
  int64_t maxlen = -1;
  uint64_t version = 0;
};

struct DequeIteratorState {
  Value deque;
  uint64_t version = 0;
  size_t index = 0;
  bool reverse = false;
};

void deque_mark_modified(DequeState& state) {
  ++state.version;
}

bool deque_item_equals(
    Runtime& runtime,
    DequeState& state,
    const Value& item,
    const Value& target,
    uint64_t version,
    bool& equal,
    std::string& error) {
  Value comparison;
  if (!runtime_value_compare(runtime, "==", item, target, comparison, error) ||
      !runtime_truthy(runtime, comparison, equal, error)) {
    return false;
  }
  if (state.version != version) {
    error = "deque mutated during iteration";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  return true;
}

bool collections_value_is_callable(Runtime& runtime, const Value& value) {
  if (value_as_function(value) != nullptr ||
      value_as_native_function(value) != nullptr ||
      value_as_bound_method(value) != nullptr ||
      value_as_class(value) != nullptr) {
    return true;
  }
  Value call;
  std::string ignored;
  return object_get_attr(value, "__call__", call, ignored);
}

bool collections_update_mapping_or_pairs(Runtime& runtime, Value& target, const Value& source, std::string& error) {
  if (mapping_is_mapping(source)) {
    Value iterator;
    if (!mapping_get_iter(source, iterator, error)) {
      return false;
    }
    for (;;) {
      bool done = false;
      Value key;
      if (!mapping_iter_next(iterator, done, key, error)) {
        return false;
      }
      if (done) {
        return true;
      }
      Value value;
      if (!mapping_get_item(source, key, value, error) || !mapping_set_item(target, key, value, error)) {
        return false;
      }
    }
  }

  Value iterator;
  if (!runtime_get_iter(runtime, source, iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value pair;
    if (!sequence_iter_next(iterator, done, pair, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    Value key;
    Value value;
    if (auto* tuple = value_as_tuple(pair)) {
      if (tuple->items.size() != 2) {
        error = "dictionary update sequence element has length " + std::to_string(tuple->items.size()) + "; 2 is required";
        return false;
      }
      key = tuple->items[0];
      value = tuple->items[1];
    } else if (auto* list = value_as_list(pair)) {
      if (list->items.size() != 2) {
        error = "dictionary update sequence element has length " + std::to_string(list->items.size()) + "; 2 is required";
        return false;
      }
      key = list->items[0];
      value = list->items[1];
    } else {
      error = "dictionary update sequence element is not a pair";
      return false;
    }
    if (!mapping_set_item(target, key, value, error)) {
      return false;
    }
  }
}

void tuplegetter_cleanup(void* data) {
  delete static_cast<TupleGetterState*>(data);
}

bool tuplegetter_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "_tuplegetter getter expected instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<TupleGetterState*>(user_data);
  if (state == nullptr) {
    error = "_tuplegetter getter state is missing";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  Value index = Value::int64(state->index);
  if (!sequence_get_item(args[0], index, out, error)) {
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  return true;
}

bool collections_tuplegetter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2 || args[0].tag != ValueTag::Int64) {
    error = "_collections._tuplegetter() expected index and optional doc";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value doc = Value::none();
  if (argc == 2) {
    value_assign_fast(doc, args[1]);
  }
  auto* state = new TupleGetterState{args[0].as.i64};
  Value getter = runtime.make_native_function(
      "_collections._tuplegetter.get",
      tuplegetter_get,
      state,
      tuplegetter_cleanup);
  out = Value::property(std::move(getter), Value::none(), Value::none(), std::move(doc));
  return true;
}

bool defaultdict_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "defaultdict.__init__ expected self";
    return false;
  }
  if (argc > 3) {
    error = "defaultdict expected at most 2 positional arguments";
    return false;
  }
  const Value factory = argc >= 2 ? args[1] : Value::none();
  if (factory.tag != ValueTag::None && !collections_value_is_callable(runtime, factory)) {
    error = "first argument must be callable or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target = args[0];
  if (!object_set_attr(target, "default_factory", factory, error)) {
    return false;
  }
  if (argc == 3 && !collections_update_mapping_or_pairs(runtime, target, args[2], error)) {
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (!mapping_set_item(target, Value::string(kwargs[i].name), *kwargs[i].value, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool defaultdict_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return defaultdict_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool defaultdict_missing(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "defaultdict.__missing__ expected key";
    return false;
  }
  Value factory;
  if (!object_get_attr(args[0], "default_factory", factory, error)) {
    return false;
  }
  if (factory.tag == ValueTag::None) {
    error = "key not found";
    Value exception = runtime.make_exception("KeyError", error);
    std::string ignored;
    object_set_attr(exception, "args", Value::tuple({args[1]}), ignored);
    runtime.set_pending_exception(std::move(exception));
    return false;
  }
  if (!runtime_call_callable(runtime, factory, nullptr, 0, out, error)) {
    return false;
  }
  Value target = args[0];
  if (!mapping_set_item(target, args[1], out, error)) {
    return false;
  }
  return true;
}

bool defaultdict_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "defaultdict.__getitem__ expected key";
    return false;
  }
  if (mapping_get_item(args[0], args[1], out, error)) {
    return true;
  }
  error.clear();
  return defaultdict_missing(runtime, args, argc, out, error, nullptr);
}

bool defaultdict_copy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "defaultdict.copy expected no arguments";
    return false;
  }
  Value factory;
  if (!object_get_attr(args[0], "default_factory", factory, error)) {
    return false;
  }
  const Value* defaultdict_class = runtime.find_builtin("defaultdict");
  if (defaultdict_class == nullptr) {
    error = "defaultdict class is not registered";
    return false;
  }
  Value copied = Value::instance(*defaultdict_class);
  auto* source_instance = value_as_instance(args[0]);
  if (source_instance == nullptr) {
    error = "defaultdict.copy expected a defaultdict";
    return false;
  }
  Value init_args[] = {copied, factory, mapping_copy(source_instance->mapping_storage)};
  Value ignored;
  if (!defaultdict_init(runtime, init_args, 3, ignored, error, nullptr)) {
    return false;
  }
  out = std::move(copied);
  return true;
}

DequeState* deque_state(const Value& self, std::string& error) {
  auto* state = static_cast<DequeState*>(instance_get_native_data(self, kDequeNativeType));
  if (state == nullptr) {
    error = "invalid deque object";
  }
  return state;
}

void deque_cleanup(void* data) {
  delete static_cast<DequeState*>(data);
}

void deque_iterator_cleanup(void* data) {
  delete static_cast<DequeIteratorState*>(data);
}

bool deque_truthy(const void* data) {
  auto* state = static_cast<const DequeState*>(data);
  return state != nullptr && !state->items.empty();
}

// deque follows the sequence protocol for every integer argument.  In
// particular, values implementing __index__ are accepted; requiring an
// already-materialized int breaks ordinary CPython callers such as enum-like
// index objects.
bool deque_as_index(
    Runtime& runtime,
    const Value& value,
    int64_t& out,
    std::string& error) {
  if (value_int_like_to_i64(value, out) || value_bigint_to_i64(value, out)) {
    return true;
  }
  Value index_method;
  std::string lookup_error;
  if (!object_get_attr(value, "__index__", index_method, lookup_error)) {
    error = "an integer is required";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value converted;
  if (!runtime_call_callable(runtime, index_method, nullptr, 0, converted, error)) {
    return false;
  }
  if (value_int_like_to_i64(converted, out) || value_bigint_to_i64(converted, out)) {
    return true;
  }
  error = "__index__ returned non-int";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool deque_extend_from_iterable(Runtime& runtime, DequeState& state, const Value& iterable, bool left, std::string& error) {
  Value iterator;
  if (!runtime_get_iter(runtime, iterable, iterator, error)) {
    return false;
  }
  std::vector<Value> values;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    values.push_back(std::move(item));
  }
  if (left) {
    for (auto it = values.begin(); it != values.end(); ++it) {
      state.items.push_front(*it);
    }
  } else {
    for (auto& value : values) {
      state.items.push_back(std::move(value));
    }
  }
  return true;
}

void deque_trim(DequeState& state, bool added_left) {
  while (state.maxlen >= 0 && state.items.size() > static_cast<size_t>(state.maxlen)) {
    if (added_left) state.items.pop_back();
    else state.items.pop_front();
  }
}

bool deque_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    error = "deque.__init__ expected optional iterable and maxlen";
    return false;
  }
  Value maxlen_value = argc == 3 ? args[2] : Value::none();
  bool has_maxlen = argc == 3;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name = kwargs[i].name == nullptr ? std::string_view() : std::string_view(kwargs[i].name);
    if (name != "maxlen" || kwargs[i].value == nullptr) {
      error = "deque.__init__ got an unexpected keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (has_maxlen) {
      error = "deque.__init__ got multiple values for argument 'maxlen'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    maxlen_value = *kwargs[i].value;
    has_maxlen = true;
  }
  auto* state = new DequeState();
  if (has_maxlen && maxlen_value.tag != ValueTag::None) {
    if (!deque_as_index(runtime, maxlen_value, state->maxlen, error)) {
      delete state;
      return false;
    }
    if (state->maxlen < 0) {
      delete state;
      error = "maxlen must be non-negative";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  }
  if (argc >= 2 && !deque_extend_from_iterable(runtime, *state, args[1], false, error)) {
    delete state;
    return false;
  }
  deque_trim(*state, false);
  if (!instance_set_native_data(args[0], kDequeNativeType, state, deque_cleanup, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_truthy(args[0], deque_truthy, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool deque_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return deque_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool deque_append(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.append() expected one argument";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->items.push_back(args[1]);
  deque_trim(*state, false);
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_appendleft(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.appendleft() expected one argument";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->items.push_front(args[1]);
  deque_trim(*state, true);
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_pop(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.pop() expected no arguments";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->items.empty()) {
    error = "pop from an empty deque";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  value_assign_fast(out, state->items.back());
  state->items.pop_back();
  deque_mark_modified(*state);
  return true;
}

bool deque_popleft(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.popleft() expected no arguments";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->items.empty()) {
    error = "pop from an empty deque";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  value_assign_fast(out, state->items.front());
  state->items.pop_front();
  deque_mark_modified(*state);
  return true;
}

bool deque_clear(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.clear() expected no arguments";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->items.clear();
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_extend(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.extend() expected one iterable";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr || !deque_extend_from_iterable(runtime, *state, args[1], false, error)) {
    return false;
  }
  deque_trim(*state, false);
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_extendleft(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.extendleft() expected one iterable";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr || !deque_extend_from_iterable(runtime, *state, args[1], true, error)) {
    return false;
  }
  deque_trim(*state, true);
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_len(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.__len__() expected no arguments";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(state->items.size()));
  return true;
}

bool deque_sizeof(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.__sizeof__() expected no arguments";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  value_set_int64(out, static_cast<int64_t>(sizeof(DequeState) +
      state->items.size() * sizeof(Value)));
  return true;
}

bool deque_count(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.count() expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  int64_t count = 0;
  const uint64_t version = state->version;
  const size_t size = state->items.size();
  for (size_t index = 0; index < size; ++index) {
    Value item = state->items[index];
    bool equal = false;
    if (!deque_item_equals(runtime, *state, item, args[1], version, equal, error)) return false;
    if (equal) {
      ++count;
    }
  }
  value_set_int64(out, count);
  return true;
}

bool deque_remove(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.remove() expected one argument";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const uint64_t version = state->version;
  const size_t size = state->items.size();
  for (size_t index = 0; index < size; ++index) {
    Value item = state->items[index];
    bool equal = false;
    if (!deque_item_equals(runtime, *state, item, args[1], version, equal, error)) return false;
    if (equal) {
      value_set_invalid(state->items[index]);
      state->items.erase(state->items.begin() + static_cast<std::ptrdiff_t>(index));
      deque_mark_modified(*state);
      value_set_none(out);
      return true;
    }
  }
  error = "deque.remove(x): x not in deque";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool deque_to_list(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.to_list() expected no arguments";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(state->items.size());
  for (const auto& item : state->items) {
    values.push_back(item);
  }
  out = Value::list(std::move(values));
  return true;
}

bool deque_snapshot_list(const Value& self, Value& out, std::string& error) {
  auto* state = deque_state(self, error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(state->items.size());
  for (const auto& item : state->items) {
    values.push_back(item);
  }
  out = Value::list(std::move(values));
  return true;
}

bool deque_iterator_next(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) {
    error = "deque iterator step expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* iterator = static_cast<DequeIteratorState*>(user_data);
  if (iterator == nullptr) {
    error = "invalid deque iterator";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  auto* deque = deque_state(iterator->deque, error);
  if (deque == nullptr) {
    return false;
  }
  if (deque->version != iterator->version) {
    error = "deque mutated during iteration";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  if (iterator->index >= deque->items.size()) {
    runtime.raise_class_error("StopIteration", "");
    return false;
  }
  const size_t item_index = iterator->reverse
      ? deque->items.size() - 1 - iterator->index
      : iterator->index;
  ++iterator->index;
  value_assign_fast(out, deque->items[item_index]);
  return true;
}

bool deque_iter_impl(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, bool reverse) {
  if (argc != 1) {
    error = "deque.__iter__() expected no arguments";
    return false;
  }
  auto* deque = deque_state(args[0], error);
  if (deque == nullptr) {
    return false;
  }
  auto* state = new DequeIteratorState{args[0], deque->version, 0, reverse};
  Value step = runtime.make_native_function(
      "_collections.deque_iterator.__next__", deque_iterator_next,
      state, deque_iterator_cleanup);
  out = functional_callable_iterator(&runtime, std::move(step), Value::invalid());
  return true;
}

bool deque_iter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return deque_iter_impl(runtime, args, argc, out, error, false);
}

bool deque_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.__getitem__() expected integer index";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  int64_t index = 0;
  if (!deque_as_index(runtime, args[1], index, error)) return false;
  if (index < 0) {
    index += static_cast<int64_t>(state->items.size());
  }
  if (index < 0 || index >= static_cast<int64_t>(state->items.size())) {
    error = "deque index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  value_assign_fast(out, state->items[static_cast<size_t>(index)]);
  return true;
}

bool deque_setitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "deque.__setitem__() expected integer index and value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  int64_t index = 0;
  if (!deque_as_index(runtime, args[1], index, error)) return false;
  if (index < 0) index += static_cast<int64_t>(state->items.size());
  if (index < 0 || index >= static_cast<int64_t>(state->items.size())) {
    error = "deque index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  value_assign_fast(state->items[static_cast<size_t>(index)], args[2]);
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_delitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.__delitem__() expected integer index";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  int64_t index = 0;
  if (!deque_as_index(runtime, args[1], index, error)) return false;
  if (index < 0) index += static_cast<int64_t>(state->items.size());
  if (index < 0 || index >= static_cast<int64_t>(state->items.size())) {
    error = "deque index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  auto item = state->items.begin() + static_cast<std::ptrdiff_t>(index);
  value_set_invalid(*item);
  state->items.erase(item);
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_contains(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.__contains__() expected value";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const uint64_t version = state->version;
  const size_t size = state->items.size();
  for (size_t index = 0; index < size; ++index) {
    Value item = state->items[index];
    bool equal = false;
    if (!deque_item_equals(runtime, *state, item, args[1], version, equal, error)) return false;
    if (equal) {
      value_set_bool(out, true);
      return true;
    }
  }
  value_set_bool(out, false);
  return true;
}

bool deque_reversed(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "deque.__reversed__() expected no arguments"; return false; }
  return deque_iter_impl(runtime, args, argc, out, error, true);
}

bool defaultdict_reduce(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "defaultdict.__reduce__() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value factory;
  if (!object_get_attr(args[0], "default_factory", factory, error)) return false;
  Value klass;
  if (!runtime_type_of_value(runtime, args[0], klass)) return false;
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "defaultdict.__reduce__ expected a defaultdict";
    return false;
  }
  out = Value::tuple({klass, Value::tuple({factory, mapping_copy(instance->mapping_storage)})});
  return true;
}

bool defaultdict_reduce_ex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "defaultdict.__reduce_ex__() expected a protocol integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return defaultdict_reduce(runtime, args, 1, out, error, nullptr);
}

bool defaultdict_or(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "defaultdict.__or__ expected one mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value copy_args[] = {args[0]};
  if (!defaultdict_copy(runtime, copy_args, 1, out, error, nullptr)) return false;
  Value target = out;
  return collections_update_mapping_or_pairs(runtime, target, args[1], error);
}

bool defaultdict_ror(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "defaultdict.__ror__ expected one mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value copy_args[] = {args[0]};
  if (!defaultdict_copy(runtime, copy_args, 1, out, error, nullptr)) return false;
  Value target = out;
  return collections_update_mapping_or_pairs(runtime, target, args[1], error);
}

bool defaultdict_ior(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "defaultdict.__ior__ expected one mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value target = args[0];
  if (!collections_update_mapping_or_pairs(runtime, target, args[1], error)) return false;
  value_assign_fast(out, args[0]);
  return true;
}

bool deque_hash(Runtime& runtime, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 1) { error = "deque.__hash__() expected no arguments"; runtime.raise_class_error("TypeError", error); return false; }
  error = "unhashable type: 'deque'";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool deque_copy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "deque.copy() expected no arguments"; return false; }
  auto* source = deque_state(args[0], error);
  auto* instance = value_as_instance(args[0]);
  if (source == nullptr || instance == nullptr) return false;
  std::vector<Value> values;
  values.reserve(source->items.size());
  for (const auto& item : source->items) values.push_back(item);
  out = Value::instance(instance->klass);
  Value init_args[] = {out, Value::list(std::move(values)), source->maxlen < 0 ? Value::none() : Value::int64(source->maxlen)};
  Value ignored;
  return deque_init(runtime, init_args, 3, ignored, error, nullptr);
}

bool deque_reduce(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.__reduce__() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  std::vector<Value> values(state->items.begin(), state->items.end());
  Value klass;
  if (!runtime_type_of_value(runtime, args[0], klass)) return false;
  const Value maxlen = state->maxlen < 0 ? Value::none() : Value::int64(state->maxlen);
  out = Value::tuple({klass, Value::tuple({Value::list(std::move(values)), maxlen})});
  return true;
}

bool deque_reduce_ex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "deque.__reduce_ex__() expected a protocol integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return deque_reduce(runtime, args, 1, out, error, nullptr);
}

bool deque_compare(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const char* op) {
  if (argc != 2) {
    error = "deque comparison expected one argument";
    return false;
  }
  const auto* left = deque_state(args[0], error);
  const auto* right = deque_state(args[1], error);
  if (left == nullptr || right == nullptr) {
    if (const Value* not_implemented = runtime.find_builtin("NotImplemented")) {
      value_assign_fast(out, *not_implemented);
    } else {
      value_set_bool(out, std::string_view(op) == "!=");
    }
    return true;
  }
  Value left_values;
  Value right_values;
  if (!deque_snapshot_list(args[0], left_values, error) ||
      !deque_snapshot_list(args[1], right_values, error)) {
    return false;
  }
  return runtime_value_compare(runtime, op, left_values, right_values, out, error);
}

#define XLANG3_DEQUE_COMPARE_METHOD(function_name, op_text) \
  bool function_name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) { \
    return deque_compare(runtime, args, argc, out, error, op_text); \
  }

XLANG3_DEQUE_COMPARE_METHOD(deque_eq, "==")
XLANG3_DEQUE_COMPARE_METHOD(deque_ne, "!=")
XLANG3_DEQUE_COMPARE_METHOD(deque_lt, "<")
XLANG3_DEQUE_COMPARE_METHOD(deque_le, "<=")
XLANG3_DEQUE_COMPARE_METHOD(deque_gt, ">")
XLANG3_DEQUE_COMPARE_METHOD(deque_ge, ">=")

#undef XLANG3_DEQUE_COMPARE_METHOD

bool deque_add(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || deque_state(args[1], error) == nullptr) {
    error = "can only concatenate deque to deque";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value copy_args[] = {args[0]};
  if (!deque_copy(runtime, copy_args, 1, out, error, nullptr)) return false;
  Value extend_args[] = {out, args[1]};
  Value ignored;
  return deque_extend(runtime, extend_args, 2, ignored, error, nullptr);
}

bool deque_iadd(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.__iadd__ expected one argument";
    return false;
  }
  Value ignored;
  if (!deque_extend(runtime, args, argc, ignored, error, nullptr)) return false;
  value_assign_fast(out, args[0]);
  return true;
}

bool deque_mul_impl(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, bool in_place) {
  if (argc != 2) {
    error = "deque repetition count must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* source = deque_state(args[0], error);
  if (source == nullptr) return false;
  int64_t count = 0;
  if (!deque_as_index(runtime, args[1], count, error)) return false;
  count = std::max<int64_t>(0, count);
  std::vector<Value> original(source->items.begin(), source->items.end());
  if (in_place) {
    source->items.clear();
    for (int64_t repeat = 0; repeat < count; ++repeat) {
      for (const auto& item : original) source->items.push_back(item);
    }
    deque_trim(*source, false);
    deque_mark_modified(*source);
    value_assign_fast(out, args[0]);
    return true;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) return false;
  out = Value::instance(instance->klass);
  std::vector<Value> values;
  for (int64_t repeat = 0; repeat < count; ++repeat) {
    values.insert(values.end(), original.begin(), original.end());
  }
  Value init_args[] = {out, Value::list(std::move(values)), source->maxlen < 0 ? Value::none() : Value::int64(source->maxlen)};
  Value ignored;
  return deque_init(runtime, init_args, 3, ignored, error, nullptr);
}

bool deque_mul(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return deque_mul_impl(runtime, args, argc, out, error, false);
}

bool deque_imul(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return deque_mul_impl(runtime, args, argc, out, error, true);
}

bool deque_reverse(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "deque.reverse() expected no arguments"; return false; }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  std::reverse(state->items.begin(), state->items.end());
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_rotate(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "deque.rotate() expected optional integer count";
    return false;
  }
  int64_t amount = 1;
  if (argc == 2 && !deque_as_index(runtime, args[1], amount, error)) return false;
  auto* state = deque_state(args[0], error);
  if (state == nullptr || state->items.empty()) { value_set_none(out); return state != nullptr; }
  amount %= static_cast<int64_t>(state->items.size());
  if (amount < 0) amount += static_cast<int64_t>(state->items.size());
  std::rotate(state->items.rbegin(), state->items.rbegin() + amount, state->items.rend());
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool deque_index(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) { error = "deque.index() expected value and optional bounds"; return false; }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  int64_t start = 0;
  int64_t stop = static_cast<int64_t>(state->items.size());
  if (argc >= 3 && !deque_as_index(runtime, args[2], start, error)) return false;
  if (argc >= 4 && !deque_as_index(runtime, args[3], stop, error)) return false;
  const int64_t size = static_cast<int64_t>(state->items.size());
  if (start < 0) start = std::max<int64_t>(0, start + size);
  if (stop < 0) stop = std::max<int64_t>(0, stop + size);
  stop = std::min(stop, size);
  const uint64_t version = state->version;
  for (int64_t index = start; index < stop; ++index) {
    Value item = state->items[static_cast<size_t>(index)];
    bool equal = false;
    if (!deque_item_equals(runtime, *state, item, args[1], version, equal, error)) return false;
    if (equal) { value_set_int64(out, index); return true; }
  }
  error = "deque.index(x): x not in deque";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool deque_insert(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) { error = "deque.insert() expected integer index and value"; return false; }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  int64_t index = 0;
  if (!deque_as_index(runtime, args[1], index, error)) return false;
  if (state->maxlen >= 0 && state->items.size() >= static_cast<size_t>(state->maxlen)) {
    error = "deque already at its maximum size";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  if (index < 0) index = std::max<int64_t>(0, index + static_cast<int64_t>(state->items.size()));
  index = std::min<int64_t>(index, state->items.size());
  state->items.insert(state->items.begin() + index, args[2]);
  deque_mark_modified(*state);
  value_set_none(out);
  return true;
}

bool defaultdict_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "defaultdict.__repr__ expected no arguments";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "defaultdict.__repr__ expected a defaultdict";
    return false;
  }
  Value factory = Value::none();
  std::string ignored;
  (void)object_get_attr(args[0], "default_factory", factory, ignored);
  out = Value::string(
      "defaultdict(" + value_to_repr(factory) + ", " + value_to_repr(instance->mapping_storage) + ")");
  return true;
}

bool deque_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.__repr__() expected no arguments";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string text = "deque([";
  for (size_t i = 0; i < state->items.size(); ++i) {
    if (i != 0) text += ", ";
    text += value_to_repr(state->items[i]);
  }
  text += "])";
  if (state->maxlen >= 0) text = text.substr(0, text.size() - 1) + ", maxlen=" + std::to_string(state->maxlen) + ")";
  out = Value::string(std::move(text));
  return true;
}

bool deque_maxlen(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.maxlen getter expected a deque";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) return false;
  if (state->maxlen < 0) value_set_none(out);
  else value_set_int64(out, state->maxlen);
  return true;
}

Value make_deque_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("collections")});
  attrs.push_back({"__qualname__", Value::string("deque")});
  attrs.push_back({"__init__", runtime.make_native_function(
      "_collections.deque.__init__", deque_init, nullptr, nullptr, nullptr, false, deque_init_kw)});
  attrs.push_back({"maxlen", Value::property(
      runtime.make_native_function("_collections.deque.maxlen", deque_maxlen),
      Value::none(), Value::none(), Value::none())});
  attrs.push_back({"append", runtime.make_native_function("_collections.deque.append", deque_append)});
  attrs.push_back({"appendleft", runtime.make_native_function("_collections.deque.appendleft", deque_appendleft)});
  attrs.push_back({"pop", runtime.make_native_function("_collections.deque.pop", deque_pop)});
  attrs.push_back({"popleft", runtime.make_native_function("_collections.deque.popleft", deque_popleft)});
  attrs.push_back({"clear", runtime.make_native_function("_collections.deque.clear", deque_clear)});
  attrs.push_back({"extend", runtime.make_native_function("_collections.deque.extend", deque_extend)});
  attrs.push_back({"extendleft", runtime.make_native_function("_collections.deque.extendleft", deque_extendleft)});
  attrs.push_back({"count", runtime.make_native_function("_collections.deque.count", deque_count)});
  attrs.push_back({"remove", runtime.make_native_function("_collections.deque.remove", deque_remove)});
  attrs.push_back({"copy", runtime.make_native_function("_collections.deque.copy", deque_copy)});
  attrs.push_back({"__reduce__", runtime.make_native_function("_collections.deque.__reduce__", deque_reduce)});
  attrs.push_back({"__reduce_ex__", runtime.make_native_function("_collections.deque.__reduce_ex__", deque_reduce_ex)});
  attrs.push_back({"reverse", runtime.make_native_function("_collections.deque.reverse", deque_reverse)});
  attrs.push_back({"rotate", runtime.make_native_function("_collections.deque.rotate", deque_rotate)});
  attrs.push_back({"index", runtime.make_native_function("_collections.deque.index", deque_index)});
  attrs.push_back({"insert", runtime.make_native_function("_collections.deque.insert", deque_insert)});
  attrs.push_back({"__len__", runtime.make_native_function("_collections.deque.__len__", deque_len)});
  attrs.push_back({"__sizeof__", runtime.make_native_function("_collections.deque.__sizeof__", deque_sizeof)});
  attrs.push_back({"__iter__", runtime.make_native_function("_collections.deque.__iter__", deque_iter)});
  attrs.push_back({"__reversed__", runtime.make_native_function("_collections.deque.__reversed__", deque_reversed)});
  attrs.push_back({"__hash__", runtime.make_native_function("_collections.deque.__hash__", deque_hash)});
  attrs.push_back({"__getitem__", runtime.make_native_function("_collections.deque.__getitem__", deque_getitem)});
  attrs.push_back({"__setitem__", runtime.make_native_function("_collections.deque.__setitem__", deque_setitem)});
  attrs.push_back({"__delitem__", runtime.make_native_function("_collections.deque.__delitem__", deque_delitem)});
  attrs.push_back({"__contains__", runtime.make_native_function("_collections.deque.__contains__", deque_contains)});
  attrs.push_back({"__eq__", runtime.make_native_function("_collections.deque.__eq__", deque_eq)});
  attrs.push_back({"__ne__", runtime.make_native_function("_collections.deque.__ne__", deque_ne)});
  attrs.push_back({"__lt__", runtime.make_native_function("_collections.deque.__lt__", deque_lt)});
  attrs.push_back({"__le__", runtime.make_native_function("_collections.deque.__le__", deque_le)});
  attrs.push_back({"__gt__", runtime.make_native_function("_collections.deque.__gt__", deque_gt)});
  attrs.push_back({"__ge__", runtime.make_native_function("_collections.deque.__ge__", deque_ge)});
  attrs.push_back({"__add__", runtime.make_native_function("_collections.deque.__add__", deque_add)});
  attrs.push_back({"__iadd__", runtime.make_native_function("_collections.deque.__iadd__", deque_iadd)});
  attrs.push_back({"__mul__", runtime.make_native_function("_collections.deque.__mul__", deque_mul)});
  attrs.push_back({"__rmul__", runtime.make_native_function("_collections.deque.__rmul__", deque_mul)});
  attrs.push_back({"__imul__", runtime.make_native_function("_collections.deque.__imul__", deque_imul)});
  attrs.push_back({"__repr__", runtime.make_native_function("_collections.deque.__repr__", deque_repr)});
  attrs.push_back({"to_list", runtime.make_native_function("_collections.deque.to_list", deque_to_list)});
  return Value::class_object("deque", std::move(attrs));
}

Value make_defaultdict_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("collections")});
  attrs.push_back({"__qualname__", Value::string("defaultdict")});
  attrs.push_back({"default_factory", slot_descriptor("defaultdict", "default_factory", 0)});
  attrs.push_back({"__init__", runtime.make_native_function(
        "_collections.defaultdict.__init__",
        defaultdict_init,
        nullptr,
        nullptr,
        nullptr,
        false,
        defaultdict_init_kw)});
  attrs.push_back({"__getitem__", runtime.make_native_function("_collections.defaultdict.__getitem__", defaultdict_getitem)});
  attrs.push_back({"__missing__", runtime.make_native_function("_collections.defaultdict.__missing__", defaultdict_missing)});
  attrs.push_back({"copy", runtime.make_native_function("_collections.defaultdict.copy", defaultdict_copy)});
  attrs.push_back({"__reduce__", runtime.make_native_function("_collections.defaultdict.__reduce__", defaultdict_reduce)});
  attrs.push_back({"__reduce_ex__", runtime.make_native_function("_collections.defaultdict.__reduce_ex__", defaultdict_reduce_ex)});
  attrs.push_back({"__or__", runtime.make_native_function("_collections.defaultdict.__or__", defaultdict_or)});
  attrs.push_back({"__ror__", runtime.make_native_function("_collections.defaultdict.__ror__", defaultdict_ror)});
  attrs.push_back({"__ior__", runtime.make_native_function("_collections.defaultdict.__ior__", defaultdict_ior)});
  attrs.push_back({"__repr__", runtime.make_native_function("_collections.defaultdict.__repr__", defaultdict_repr)});
  Value base = runtime.find_builtin("dict") != nullptr ? *runtime.find_builtin("dict") : Value::invalid();
  Value klass = Value::class_object("defaultdict", std::move(attrs), std::move(base));
  if (auto* class_object = value_as_class(klass)) {
    dict_install_class_methods(runtime, *class_object);
    class_object->attrs["__init__"] = runtime.make_native_function(
        "_collections.defaultdict.__init__",
        defaultdict_init,
        nullptr,
        nullptr,
        nullptr,
        false,
        defaultdict_init_kw);
    class_object->attrs["__getitem__"] = runtime.make_native_function("_collections.defaultdict.__getitem__", defaultdict_getitem);
    class_object->attrs["__missing__"] = runtime.make_native_function("_collections.defaultdict.__missing__", defaultdict_missing);
    class_object->attrs["copy"] = runtime.make_native_function("_collections.defaultdict.copy", defaultdict_copy);
    class_object->attrs["__reduce__"] = runtime.make_native_function("_collections.defaultdict.__reduce__", defaultdict_reduce);
    class_object->attrs["__reduce_ex__"] = runtime.make_native_function("_collections.defaultdict.__reduce_ex__", defaultdict_reduce_ex);
    class_object->attrs["__or__"] = runtime.make_native_function("_collections.defaultdict.__or__", defaultdict_or);
    class_object->attrs["__ror__"] = runtime.make_native_function("_collections.defaultdict.__ror__", defaultdict_ror);
    class_object->attrs["__ior__"] = runtime.make_native_function("_collections.defaultdict.__ior__", defaultdict_ior);
    class_object->attrs["__repr__"] = runtime.make_native_function("_collections.defaultdict.__repr__", defaultdict_repr);
    class_object->attrs["default_factory"] = slot_descriptor("defaultdict", "default_factory", 0);
    slot_descriptor_set_owner_class(class_object->attrs["default_factory"], klass);
    ++class_object->version;
  }
  return klass;
}

} // namespace

void register_collections_module(Runtime& runtime) {
  Value deque_class = make_deque_class(runtime);
  Value defaultdict_class = make_defaultdict_class(runtime);
  runtime.register_builtin("defaultdict", defaultdict_class);

  NativeModuleBuilder builder(runtime, "_collections");
  builder.value("deque", deque_class);
  builder.value("defaultdict", defaultdict_class);
  builder.function("_tuplegetter", collections_tuplegetter);
  runtime.register_module("_collections", builder.finish());
}

} // namespace xlang3
