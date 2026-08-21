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
#include "xlang3/value_hash.h"

#include <deque>

namespace xlang3 {

namespace {

constexpr const char* kDequeNativeType = "_collections.deque";

struct DequeState {
  std::deque<Value> items;
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

} // namespace

void register_collections_module(Runtime& runtime) {
  Value deque_class = make_deque_class(runtime);

  NativeModuleBuilder builder(runtime, "_collections");
  builder.value("deque", deque_class);
  runtime.register_module("_collections", builder.finish());

  NativeModuleBuilder facade(runtime, "collections");
  facade.value("deque", std::move(deque_class));
  runtime.register_module("collections", facade.finish());
}

} // namespace xlang3
