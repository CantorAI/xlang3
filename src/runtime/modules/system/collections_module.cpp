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
};

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
    runtime.raise_class_error("KeyError", error);
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
  Value init_args[] = {copied, factory, mapping_copy(args[0])};
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

bool deque_truthy(const void* data) {
  auto* state = static_cast<const DequeState*>(data);
  return state != nullptr && !state->items.empty();
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

bool deque_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "deque.__init__ expected optional iterable";
    return false;
  }
  auto* state = new DequeState();
  if (argc == 2 && !deque_extend_from_iterable(runtime, *state, args[1], false, error)) {
    delete state;
    return false;
  }
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
  value_set_none(out);
  return true;
}

bool deque_pop(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
    return false;
  }
  value_assign_fast(out, state->items.back());
  state->items.pop_back();
  return true;
}

bool deque_popleft(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
    return false;
  }
  value_assign_fast(out, state->items.front());
  state->items.pop_front();
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

bool deque_count(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.count() expected one argument";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  int64_t count = 0;
  for (const auto& item : state->items) {
    if (value_key_equal(item, args[1])) {
      ++count;
    }
  }
  value_set_int64(out, count);
  return true;
}

bool deque_remove(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.remove() expected one argument";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  for (auto it = state->items.begin(); it != state->items.end(); ++it) {
    if (value_key_equal(*it, args[1])) {
      value_set_invalid(*it);
      state->items.erase(it);
      value_set_none(out);
      return true;
    }
  }
  error = "deque.remove(x): x not in deque";
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

bool deque_iter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "deque.__iter__() expected no arguments";
    return false;
  }
  Value snapshot;
  if (!deque_snapshot_list(args[0], snapshot, error)) {
    return false;
  }
  out = Value::sequence_iterator(std::move(snapshot), 0);
  return true;
}

bool deque_getitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "deque.__getitem__() expected integer index";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  int64_t index = args[1].as.i64;
  if (index < 0) {
    index += static_cast<int64_t>(state->items.size());
  }
  if (index < 0 || index >= static_cast<int64_t>(state->items.size())) {
    error = "deque index out of range";
    return false;
  }
  value_assign_fast(out, state->items[static_cast<size_t>(index)]);
  return true;
}

bool deque_contains(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.__contains__() expected value";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  for (const auto& item : state->items) {
    if (value_key_equal(item, args[1])) {
      value_set_bool(out, true);
      return true;
    }
  }
  value_set_bool(out, false);
  return true;
}

Value make_deque_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("collections")});
  attrs.push_back({"__qualname__", Value::string("deque")});
  attrs.push_back({"__init__", runtime.make_native_function("_collections.deque.__init__", deque_init)});
  attrs.push_back({"append", runtime.make_native_function("_collections.deque.append", deque_append)});
  attrs.push_back({"appendleft", runtime.make_native_function("_collections.deque.appendleft", deque_appendleft)});
  attrs.push_back({"pop", runtime.make_native_function("_collections.deque.pop", deque_pop)});
  attrs.push_back({"popleft", runtime.make_native_function("_collections.deque.popleft", deque_popleft)});
  attrs.push_back({"clear", runtime.make_native_function("_collections.deque.clear", deque_clear)});
  attrs.push_back({"extend", runtime.make_native_function("_collections.deque.extend", deque_extend)});
  attrs.push_back({"extendleft", runtime.make_native_function("_collections.deque.extendleft", deque_extendleft)});
  attrs.push_back({"count", runtime.make_native_function("_collections.deque.count", deque_count)});
  attrs.push_back({"remove", runtime.make_native_function("_collections.deque.remove", deque_remove)});
  attrs.push_back({"__len__", runtime.make_native_function("_collections.deque.__len__", deque_len)});
  attrs.push_back({"__iter__", runtime.make_native_function("_collections.deque.__iter__", deque_iter)});
  attrs.push_back({"__getitem__", runtime.make_native_function("_collections.deque.__getitem__", deque_getitem)});
  attrs.push_back({"__contains__", runtime.make_native_function("_collections.deque.__contains__", deque_contains)});
  attrs.push_back({"to_list", runtime.make_native_function("_collections.deque.to_list", deque_to_list)});
  return Value::class_object("deque", std::move(attrs));
}

Value make_defaultdict_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("collections")});
  attrs.push_back({"__qualname__", Value::string("defaultdict")});
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
