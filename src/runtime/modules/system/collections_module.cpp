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
#include "xlang3/value_hash.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <sstream>

namespace xlang3 {

namespace {

constexpr const char* kDequeNativeType = "_collections.deque";
constexpr const char* kDefaultDictNativeType = "collections.defaultdict";
constexpr const char* kCounterNativeType = "collections.Counter";
constexpr const char* kChainMapNativeType = "collections.ChainMap";
constexpr const char* kNamedTupleFieldsType = "collections.namedtuple.fields";

struct DequeState {
  std::deque<Value> items;
};

struct DefaultDictState {
  Value default_factory;
  std::vector<std::pair<Value, Value>> items;
};

struct CounterState {
  Value storage;
};

struct ChainMapState {
  std::vector<Value> maps;
};

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

DefaultDictState* defaultdict_state(const Value& self, std::string& error) {
  auto* state = static_cast<DefaultDictState*>(instance_get_native_data(self, kDefaultDictNativeType));
  if (state == nullptr) {
    error = "invalid defaultdict object";
  }
  return state;
}

void defaultdict_cleanup(void* data) {
  delete static_cast<DefaultDictState*>(data);
}

void counter_cleanup(void* data) {
  delete static_cast<CounterState*>(data);
}

void chain_map_cleanup(void* data) {
  delete static_cast<ChainMapState*>(data);
}

void namedtuple_fields_cleanup(void* data) {
  delete static_cast<std::vector<std::string>*>(data);
}

std::vector<std::string> parse_namedtuple_fields(const Value& value) {
  std::vector<std::string> fields;
  if (auto* str = value_as_string(value)) {
    std::string text = string_object_to_string(*str);
    for (char& ch : text) {
      if (ch == ',') {
        ch = ' ';
      }
    }
    std::istringstream input(text);
    std::string field;
    while (input >> field) {
      fields.push_back(std::move(field));
    }
    return fields;
  }
  if (auto* list = value_as_list(value)) {
    fields.reserve(list->items.size());
    for (const auto& item : list->items) {
      if (auto* field = value_as_string(item)) {
        fields.push_back(string_object_to_string(*field));
      }
    }
  }
  return fields;
}

Value* defaultdict_find(DefaultDictState& state, const Value& key) {
  for (auto& item : state.items) {
    if (value_key_equal(item.first, key)) {
      return &item.second;
    }
  }
  return nullptr;
}

bool deque_extend_from_iterable(DequeState& state, const Value& iterable, bool left, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(iterable, iterator, error)) {
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

bool deque_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "deque.__init__ expected optional iterable";
    return false;
  }
  auto* state = new DequeState();
  if (argc == 2 && !deque_extend_from_iterable(*state, args[1], false, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_data(args[0], kDequeNativeType, state, deque_cleanup, error)) {
    delete state;
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

bool deque_extend(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.extend() expected one iterable";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr || !deque_extend_from_iterable(*state, args[1], false, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool deque_extendleft(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "deque.extendleft() expected one iterable";
    return false;
  }
  auto* state = deque_state(args[0], error);
  if (state == nullptr || !deque_extend_from_iterable(*state, args[1], true, error)) {
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

Value make_deque_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_collections.deque.__init__", deque_init)});
  attrs.push_back({"append", runtime.make_native_function("_collections.deque.append", deque_append)});
  attrs.push_back({"appendleft", runtime.make_native_function("_collections.deque.appendleft", deque_appendleft)});
  attrs.push_back({"pop", runtime.make_native_function("_collections.deque.pop", deque_pop)});
  attrs.push_back({"popleft", runtime.make_native_function("_collections.deque.popleft", deque_popleft)});
  attrs.push_back({"clear", runtime.make_native_function("_collections.deque.clear", deque_clear)});
  attrs.push_back({"extend", runtime.make_native_function("_collections.deque.extend", deque_extend)});
  attrs.push_back({"extendleft", runtime.make_native_function("_collections.deque.extendleft", deque_extendleft)});
  attrs.push_back({"count", runtime.make_native_function("_collections.deque.count", deque_count)});
  attrs.push_back({"__len__", runtime.make_native_function("_collections.deque.__len__", deque_len)});
  attrs.push_back({"to_list", runtime.make_native_function("_collections.deque.to_list", deque_to_list)});
  return Value::class_object("deque", std::move(attrs));
}

bool defaultdict_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "defaultdict.__init__ expected optional default_factory";
    return false;
  }
  auto* state = new DefaultDictState();
  if (argc == 2) {
    state->default_factory = args[1];
  } else {
    value_set_none(state->default_factory);
  }
  if (!instance_set_native_data(args[0], kDefaultDictNativeType, state, defaultdict_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool defaultdict_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "defaultdict.__getitem__() expected one key";
    return false;
  }
  auto* state = defaultdict_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (auto* existing = defaultdict_find(*state, args[1])) {
    value_assign_fast(out, *existing);
    return true;
  }
  if (state->default_factory.tag == ValueTag::None) {
    error = "missing key";
    return false;
  }
  Value created;
  if (!runtime_call_callable(runtime, state->default_factory, nullptr, 0, created, error)) {
    return false;
  }
  state->items.push_back(std::make_pair(args[1], created));
  value_assign_fast(out, state->items.back().second);
  return true;
}

bool defaultdict_setitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "defaultdict.__setitem__() expected key and value";
    return false;
  }
  auto* state = defaultdict_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (auto* existing = defaultdict_find(*state, args[1])) {
    *existing = args[2];
  } else {
    state->items.push_back(std::make_pair(args[1], args[2]));
  }
  value_set_none(out);
  return true;
}

bool defaultdict_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "defaultdict.get() expected key and optional default";
    return false;
  }
  auto* state = defaultdict_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (auto* existing = defaultdict_find(*state, args[1])) {
    value_assign_fast(out, *existing);
    return true;
  }
  if (argc == 3) {
    value_assign_fast(out, args[2]);
  } else {
    value_set_none(out);
  }
  (void)runtime;
  return true;
}

Value make_defaultdict_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("collections.defaultdict.__init__", defaultdict_init)});
  attrs.push_back({"__getitem__", runtime.make_native_function("collections.defaultdict.__getitem__", defaultdict_getitem)});
  attrs.push_back({"__setitem__", runtime.make_native_function("collections.defaultdict.__setitem__", defaultdict_setitem)});
  attrs.push_back({"get", runtime.make_native_function("collections.defaultdict.get", defaultdict_get)});
  return Value::class_object("defaultdict", std::move(attrs));
}

Value* counter_storage(const Value& self, std::string& error) {
  auto* state = static_cast<CounterState*>(instance_get_native_data(self, kCounterNativeType));
  if (state == nullptr || value_as_dict(state->storage) == nullptr) {
    error = "invalid Counter object";
    return nullptr;
  }
  return &state->storage;
}

bool counter_add_count(Value& storage, const Value& key, int64_t delta, std::string& error) {
  Value current;
  std::string ignored;
  int64_t value = 0;
  if (mapping_get_item(storage, key, current, ignored)) {
    if (current.tag != ValueTag::Int64) {
      error = "Counter count is not an int";
      return false;
    }
    value = current.as.i64;
  }
  return mapping_set_item(storage, key, Value::int64(value + delta), error);
}

bool counter_update_from_iterable(Value& storage, const Value& iterable, int64_t delta, std::string& error) {
  if (auto* counter = static_cast<CounterState*>(instance_get_native_data(iterable, kCounterNativeType))) {
    return counter_update_from_iterable(storage, counter->storage, delta, error);
  }
  if (auto* instance = value_as_instance(iterable)) {
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return counter_update_from_iterable(storage, instance->mapping_storage, delta, error);
    }
  }
  if (auto* dict = value_as_dict(iterable)) {
    for (const auto& entry : dict->entries) {
      if (entry.second.tag != ValueTag::Int64) {
        error = "Counter mapping counts must be int";
        return false;
      }
      if (!counter_add_count(storage, entry.first, entry.second.as.i64 * delta, error)) {
        return false;
      }
    }
    return true;
  }
  Value iterator;
  if (!sequence_get_iter(iterable, iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    if (!counter_add_count(storage, item, delta, error)) {
      return false;
    }
  }
}

bool counter_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "Counter.__init__() expected optional iterable or mapping";
    return false;
  }
  auto* state = new CounterState();
  state->storage = Value::dict({});
  if (argc == 2 && !counter_update_from_iterable(state->storage, args[1], 1, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_data(args[0], kCounterNativeType, state, counter_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool counter_getitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Counter.__getitem__() expected key";
    return false;
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  std::string ignored;
  if (!mapping_get_item(*storage, args[1], out, ignored)) {
    value_set_int64(out, 0);
  }
  return true;
}

bool counter_setitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "Counter.__setitem__() expected key and value";
    return false;
  }
  if (args[2].tag != ValueTag::Int64) {
    error = "Counter count must be int";
    return false;
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr || !mapping_set_item(*storage, args[1], args[2], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool counter_update(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Counter.update() expected optional iterable or mapping";
    return false;
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  if (argc == 2 && !counter_update_from_iterable(*storage, args[1], 1, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool counter_subtract(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Counter.subtract() expected optional iterable or mapping";
    return false;
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  if (argc == 2 && !counter_update_from_iterable(*storage, args[1], -1, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool counter_elements(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Counter.elements() expected no arguments";
    return false;
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  std::vector<Value> values;
  for (const auto& entry : value_as_dict(*storage)->entries) {
    if (entry.second.tag != ValueTag::Int64 || entry.second.as.i64 <= 0) {
      continue;
    }
    for (int64_t i = 0; i < entry.second.as.i64; ++i) {
      values.push_back(entry.first);
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool counter_most_common(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "Counter.most_common() expected optional n";
    return false;
  }
  int64_t limit = std::numeric_limits<int64_t>::max();
  if (argc == 2) {
    if (args[1].tag == ValueTag::None) {
      limit = std::numeric_limits<int64_t>::max();
    } else if (args[1].tag == ValueTag::Int64) {
      limit = args[1].as.i64;
    } else {
      error = "Counter.most_common() n must be int or None";
      return false;
    }
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  std::vector<std::pair<Value, int64_t>> pairs;
  for (const auto& entry : value_as_dict(*storage)->entries) {
    if (entry.second.tag == ValueTag::Int64) {
      pairs.push_back({entry.first, entry.second.as.i64});
    }
  }
  std::stable_sort(pairs.begin(), pairs.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second > rhs.second;
  });
  std::vector<Value> values;
  for (const auto& item : pairs) {
    if (limit-- <= 0) {
      break;
    }
    values.push_back(Value::tuple({item.first, Value::int64(item.second)}));
  }
  out = Value::list(std::move(values));
  return true;
}

bool counter_total(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Counter.total() expected no arguments";
    return false;
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  int64_t total = 0;
  for (const auto& entry : value_as_dict(*storage)->entries) {
    if (entry.second.tag == ValueTag::Int64) {
      total += entry.second.as.i64;
    }
  }
  value_set_int64(out, total);
  return true;
}

bool counter_items(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Counter.items() expected no arguments";
    return false;
  }
  auto* storage = counter_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  out = mapping_items_view(*storage);
  return true;
}

Value make_counter_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("collections")});
  attrs.push_back({"__init__", runtime.make_native_function("collections.Counter.__init__", counter_init)});
  attrs.push_back({"__getitem__", runtime.make_native_function("collections.Counter.__getitem__", counter_getitem)});
  attrs.push_back({"__setitem__", runtime.make_native_function("collections.Counter.__setitem__", counter_setitem)});
  attrs.push_back({"update", runtime.make_native_function("collections.Counter.update", counter_update)});
  attrs.push_back({"subtract", runtime.make_native_function("collections.Counter.subtract", counter_subtract)});
  attrs.push_back({"elements", runtime.make_native_function("collections.Counter.elements", counter_elements)});
  attrs.push_back({"most_common", runtime.make_native_function("collections.Counter.most_common", counter_most_common)});
  attrs.push_back({"total", runtime.make_native_function("collections.Counter.total", counter_total)});
  attrs.push_back({"items", runtime.make_native_function("collections.Counter.items", counter_items)});
  return Value::class_object("Counter", std::move(attrs));
}

bool namedtuple_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* fields = static_cast<std::vector<std::string>*>(user_data);
  if (fields == nullptr || argc != fields->size() + 1) {
    error = "namedtuple constructor argument count mismatch";
    return false;
  }
  for (size_t i = 0; i < fields->size(); ++i) {
    if (!object_set_attr(const_cast<Value&>(args[0]), (*fields)[i], args[static_cast<uint32_t>(i + 1)], error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool namedtuple_factory(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "namedtuple() expected typename and field_names";
    return false;
  }
  auto* type_name = value_as_string(args[0]);
  if (type_name == nullptr) {
    error = "namedtuple typename must be str";
    return false;
  }

  auto fields = parse_namedtuple_fields(args[1]);
  auto* captured_fields = new std::vector<std::string>(fields);
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"_fields", [&fields]() {
                     std::vector<Value> values;
                     values.reserve(fields.size());
                     for (const auto& field : fields) {
                       values.push_back(Value::string(field));
                     }
                     return Value::tuple(std::move(values));
                   }()});
  attrs.push_back({"__init__",
                   runtime.make_native_function(
                       "collections.namedtuple.__init__",
                       namedtuple_init,
                       captured_fields,
                       namedtuple_fields_cleanup)});
  out = Value::class_object(string_object_to_string(*type_name), std::move(attrs));
  return true;
}

Value* ordered_dict_storage(const Value& self, std::string& error);

bool ordered_dict_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "OrderedDict.__init__() expected optional mapping";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  if (argc == 2) {
    const Value* source = &args[1];
    if (auto* source_instance = value_as_instance(*source)) {
      if (value_as_dict(source_instance->mapping_storage) != nullptr) {
        source = &source_instance->mapping_storage;
      }
    }
    auto* source_dict = value_as_dict(*source);
    if (source_dict == nullptr) {
      error = "OrderedDict.__init__() mapping must be a dict";
      return false;
    }
    auto* target_dict = value_as_dict(*storage);
    target_dict->entries.clear();
    for (const auto& entry : source_dict->entries) {
      target_dict->entries.push_back(entry);
    }
  }
  value_set_none(out);
  return true;
}

Value* ordered_dict_storage(const Value& self, std::string& error) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr || value_as_dict(instance->mapping_storage) == nullptr) {
    error = "invalid OrderedDict object";
    return nullptr;
  }
  return &instance->mapping_storage;
}

bool ordered_dict_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "OrderedDict.__getitem__() expected key";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  if (!mapping_get_item(*storage, args[1], out, error)) {
    runtime.raise_class_error("KeyError", error);
    return false;
  }
  return true;
}

bool ordered_dict_setitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "OrderedDict.__setitem__() expected key and value";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  if (!mapping_set_item(*storage, args[1], args[2], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool ordered_dict_delitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "OrderedDict.__delitem__() expected key";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  if (!mapping_delete_item(*storage, args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool ordered_dict_len(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "OrderedDict.__len__() expected no arguments";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  return mapping_len(*storage, out, error);
}

bool ordered_dict_contains(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "OrderedDict.__contains__() expected key";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  bool contains = false;
  if (!mapping_contains(*storage, args[1], contains, error)) {
    return false;
  }
  value_set_bool(out, contains);
  return true;
}

bool ordered_dict_get(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "OrderedDict.get() expected key and optional default";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  std::string ignored;
  if (mapping_get_item(*storage, args[1], out, ignored)) {
    return true;
  }
  if (argc == 3) {
    value_assign_fast(out, args[2]);
  } else {
    value_set_none(out);
  }
  return true;
}

bool ordered_dict_pop(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "OrderedDict.pop() expected key and optional default";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  std::string ignored;
  if (mapping_get_item(*storage, args[1], out, ignored)) {
    return mapping_delete_item(*storage, args[1], error);
  }
  if (argc == 3) {
    value_assign_fast(out, args[2]);
    return true;
  }
  error = "key not found";
  return false;
}

bool ordered_dict_items(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "OrderedDict.items() expected no arguments";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  auto* dict = value_as_dict(*storage);
  std::vector<Value> values;
  values.reserve(dict->entries.size());
  for (const auto& entry : dict->entries) {
    values.push_back(Value::tuple({entry.first, entry.second}));
  }
  out = Value::list(std::move(values));
  return true;
}

bool ordered_dict_keys(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "OrderedDict.keys() expected no arguments";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  auto* dict = value_as_dict(*storage);
  std::vector<Value> values;
  values.reserve(dict->entries.size());
  for (const auto& entry : dict->entries) {
    values.push_back(entry.first);
  }
  out = Value::list(std::move(values));
  return true;
}

bool ordered_dict_values(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "OrderedDict.values() expected no arguments";
    return false;
  }
  auto* storage = ordered_dict_storage(args[0], error);
  if (storage == nullptr) {
    return false;
  }
  auto* dict = value_as_dict(*storage);
  std::vector<Value> values;
  values.reserve(dict->entries.size());
  for (const auto& entry : dict->entries) {
    values.push_back(entry.second);
  }
  out = Value::list(std::move(values));
  return true;
}

ChainMapState* chain_map_state(const Value& self, std::string& error) {
  auto* state = static_cast<ChainMapState*>(instance_get_native_data(self, kChainMapNativeType));
  if (state == nullptr) {
    error = "invalid ChainMap object";
  }
  return state;
}

bool chain_map_accept_map(const Value& value, Value& out) {
  if (value_as_dict(value) != nullptr) {
    value_assign_fast(out, value);
    return true;
  }
  if (auto* instance = value_as_instance(value)) {
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      value_assign_fast(out, instance->mapping_storage);
      return true;
    }
  }
  return false;
}

Value chain_map_maps_list(const ChainMapState& state) {
  std::vector<Value> maps;
  maps.reserve(state.maps.size());
  for (const auto& map : state.maps) {
    maps.push_back(map);
  }
  return Value::list(std::move(maps));
}

bool chain_map_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  auto* state = new ChainMapState();
  if (argc == 1) {
    state->maps.push_back(Value::dict({}));
  } else {
    state->maps.reserve(argc - 1);
    for (uint32_t i = 1; i < argc; ++i) {
      Value map;
      if (!chain_map_accept_map(args[i], map)) {
        delete state;
        error = "ChainMap arguments must be mappings";
        return false;
      }
      state->maps.push_back(std::move(map));
    }
  }
  if (!instance_set_native_data(args[0], kChainMapNativeType, state, chain_map_cleanup, error)) {
    delete state;
    return false;
  }
  if (!object_set_attr(const_cast<Value&>(args[0]), "maps", chain_map_maps_list(*state), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool chain_map_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ChainMap.__getitem__() expected key";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  for (const auto& map : state->maps) {
    std::string ignored;
    if (mapping_get_item(map, args[1], out, ignored)) {
      return true;
    }
  }
  runtime.raise_class_error("KeyError", value_to_string(args[1]));
  return false;
}

bool chain_map_setitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "ChainMap.__setitem__() expected key and value";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->maps.empty()) {
    state->maps.push_back(Value::dict({}));
  }
  if (!mapping_set_item(state->maps.front(), args[1], args[2], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool chain_map_contains(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ChainMap.__contains__() expected key";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  for (const auto& map : state->maps) {
    bool contains = false;
    std::string ignored;
    if (mapping_contains(map, args[1], contains, ignored) && contains) {
      value_set_bool(out, true);
      return true;
    }
  }
  value_set_bool(out, false);
  return true;
}

bool chain_map_get(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "ChainMap.get() expected key and optional default";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  for (const auto& map : state->maps) {
    std::string ignored;
    if (mapping_get_item(map, args[1], out, ignored)) {
      return true;
    }
  }
  if (argc == 3) {
    value_assign_fast(out, args[2]);
  } else {
    value_set_none(out);
  }
  return true;
}

void chain_map_add_unique_key(std::vector<Value>& keys, const Value& key) {
  for (const auto& existing : keys) {
    if (value_key_equal(existing, key)) {
      return;
    }
  }
  keys.push_back(key);
}

std::vector<Value> chain_map_unique_keys(const ChainMapState& state) {
  std::vector<Value> keys;
  for (const auto& map : state.maps) {
    auto* dict = value_as_dict(map);
    if (dict == nullptr) {
      continue;
    }
    for (const auto& entry : dict->entries) {
      chain_map_add_unique_key(keys, entry.first);
    }
  }
  return keys;
}

bool chain_map_len(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ChainMap.__len__() expected no arguments";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(chain_map_unique_keys(*state).size()));
  return true;
}

bool chain_map_keys(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ChainMap.keys() expected no arguments";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::list(chain_map_unique_keys(*state));
  return true;
}

bool chain_map_items(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ChainMap.items() expected no arguments";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> values;
  for (const auto& key : chain_map_unique_keys(*state)) {
    Value item;
    std::string ignored;
    for (const auto& map : state->maps) {
      if (mapping_get_item(map, key, item, ignored)) {
        values.push_back(Value::tuple({key, item}));
        break;
      }
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool chain_map_values(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ChainMap.values() expected no arguments";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> values;
  for (const auto& key : chain_map_unique_keys(*state)) {
    Value item;
    std::string ignored;
    for (const auto& map : state->maps) {
      if (mapping_get_item(map, key, item, ignored)) {
        values.push_back(item);
        break;
      }
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool chain_map_new_child(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "ChainMap.new_child() expected optional mapping";
    return false;
  }
  auto* state = chain_map_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> child_maps;
  Value first;
  if (argc == 2) {
    if (!chain_map_accept_map(args[1], first)) {
      error = "ChainMap.new_child() argument must be a mapping";
      return false;
    }
  } else {
    first = Value::dict({});
  }
  child_maps.push_back(std::move(first));
  for (const auto& map : state->maps) {
    child_maps.push_back(map);
  }
  Value klass = value_as_instance(args[0])->klass;
  out = Value::instance(std::move(klass));
  auto* child_state = new ChainMapState();
  child_state->maps = std::move(child_maps);
  if (!instance_set_native_data(out, kChainMapNativeType, child_state, chain_map_cleanup, error)) {
    delete child_state;
    return false;
  }
  return object_set_attr(out, "maps", chain_map_maps_list(*child_state), error);
}

Value make_chain_map_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("collections")});
  attrs.push_back({"__init__", runtime.make_native_function("collections.ChainMap.__init__", chain_map_init)});
  attrs.push_back({"__getitem__", runtime.make_native_function("collections.ChainMap.__getitem__", chain_map_getitem)});
  attrs.push_back({"__setitem__", runtime.make_native_function("collections.ChainMap.__setitem__", chain_map_setitem)});
  attrs.push_back({"__contains__", runtime.make_native_function("collections.ChainMap.__contains__", chain_map_contains)});
  attrs.push_back({"__len__", runtime.make_native_function("collections.ChainMap.__len__", chain_map_len)});
  attrs.push_back({"get", runtime.make_native_function("collections.ChainMap.get", chain_map_get)});
  attrs.push_back({"keys", runtime.make_native_function("collections.ChainMap.keys", chain_map_keys)});
  attrs.push_back({"items", runtime.make_native_function("collections.ChainMap.items", chain_map_items)});
  attrs.push_back({"values", runtime.make_native_function("collections.ChainMap.values", chain_map_values)});
  attrs.push_back({"new_child", runtime.make_native_function("collections.ChainMap.new_child", chain_map_new_child)});
  return Value::class_object("ChainMap", std::move(attrs));
}

Value make_ordered_dict_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("collections")});
  attrs.push_back({"__init__", runtime.make_native_function("collections.OrderedDict.__init__", ordered_dict_init)});
  attrs.push_back({"__getitem__", runtime.make_native_function("collections.OrderedDict.__getitem__", ordered_dict_getitem)});
  attrs.push_back({"__setitem__", runtime.make_native_function("collections.OrderedDict.__setitem__", ordered_dict_setitem)});
  attrs.push_back({"__delitem__", runtime.make_native_function("collections.OrderedDict.__delitem__", ordered_dict_delitem)});
  attrs.push_back({"__iter__", runtime.make_native_function("collections.OrderedDict.__iter__", ordered_dict_keys)});
  attrs.push_back({"__len__", runtime.make_native_function("collections.OrderedDict.__len__", ordered_dict_len)});
  attrs.push_back({"__contains__", runtime.make_native_function("collections.OrderedDict.__contains__", ordered_dict_contains)});
  attrs.push_back({"get", runtime.make_native_function("collections.OrderedDict.get", ordered_dict_get)});
  attrs.push_back({"pop", runtime.make_native_function("collections.OrderedDict.pop", ordered_dict_pop)});
  attrs.push_back({"items", runtime.make_native_function("collections.OrderedDict.items", ordered_dict_items)});
  attrs.push_back({"keys", runtime.make_native_function("collections.OrderedDict.keys", ordered_dict_keys)});
  attrs.push_back({"values", runtime.make_native_function("collections.OrderedDict.values", ordered_dict_values)});
  return Value::class_object("OrderedDict", std::move(attrs));
}

} // namespace

void register_collections_module(Runtime& runtime) {
  Value deque_class = make_deque_class(runtime);
  Value defaultdict_class = make_defaultdict_class(runtime);
  Value ordered_dict_class = make_ordered_dict_class(runtime);
  Value counter_class = make_counter_class(runtime);
  Value chain_map_class = make_chain_map_class(runtime);

  NativeModuleBuilder builder(runtime, "_collections");
  builder.value("deque", deque_class);
  runtime.register_module("_collections", builder.finish());

  NativeModuleBuilder facade(runtime, "collections");
  facade.value("deque", std::move(deque_class))
      .value("defaultdict", std::move(defaultdict_class))
      .value("OrderedDict", std::move(ordered_dict_class))
      .value("Counter", std::move(counter_class))
      .value("ChainMap", std::move(chain_map_class))
      .function("namedtuple", namedtuple_factory);
  runtime.register_module("collections", facade.finish());
}

} // namespace xlang3
