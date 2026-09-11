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
#include "xlang3/builtin_methods.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <algorithm>

namespace xlang3 {

namespace {

bool value_is_builtin_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return false;
  }
  switch (value.as.obj->kind) {
    case ObjectKind::RangeIterator:
    case ObjectKind::SequenceIterator:
    case ObjectKind::DictIterator:
    case ObjectKind::SetIterator:
    case ObjectKind::EnumerateIterator:
    case ObjectKind::ZipIterator:
    case ObjectKind::MapIterator:
    case ObjectKind::FilterIterator:
    case ObjectKind::CallableIterator:
    case ObjectKind::ChainIterator:
    case ObjectKind::ProtocolIterator:
      return true;
    default:
      return false;
  }
}

bool iterator_iter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "iterator.__iter__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!value_is_builtin_iterator(args[0])) {
    error = "iterator.__iter__ target is not an iterator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool iterator_next_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "iterator.__next__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator = args[0];
  bool done = false;
  if (!sequence_iter_next(iterator, done, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (done) {
    error.clear();
    runtime.raise_class_error("StopIteration", error);
    return false;
  }
  return true;
}

bool iterator_reduce_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "iterator.__reduce__", error) ||
      !value_is_builtin_iterator(args[0])) {
    runtime.raise_class_error("TypeError", error.empty() ? "invalid iterator" : error);
    return false;
  }
  if (args[0].as.obj->kind == ObjectKind::MapIterator) {
    auto* map = reinterpret_cast<MapIteratorObject*>(args[0].as.obj);
    const Value* constructor = runtime.find_builtin("map");
    if (constructor == nullptr) {
      error = "map constructor is not registered";
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
    std::vector<Value> constructor_args;
    constructor_args.reserve(map->iterators.size() + 1);
    constructor_args.push_back(map->callable);
    constructor_args.insert(constructor_args.end(), map->iterators.begin(), map->iterators.end());
    out = Value::tuple({*constructor, Value::tuple(std::move(constructor_args))});
    return true;
  }
  if (args[0].as.obj->kind == ObjectKind::SequenceIterator) {
    auto* iterator = reinterpret_cast<SequenceIteratorObject*>(args[0].as.obj);
    const Value* constructor = runtime.find_builtin("iter");
    if (constructor == nullptr) {
      error = "iter constructor is not registered";
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
    out = Value::tuple({
        *constructor,
        Value::tuple({iterator->source}),
        Value::int64(static_cast<int64_t>(iterator->index))});
    return true;
  }
  if (args[0].as.obj->kind == ObjectKind::ProtocolIterator) {
    auto* iterator = reinterpret_cast<ProtocolIteratorObject*>(args[0].as.obj);
    Value reduce;
    if (!object_get_attr(iterator->iterator, "__reduce__", reduce, error)) {
      runtime.raise_class_error("TypeError", "cannot pickle this iterator");
      return false;
    }
    Value payload;
    if (!runtime_call_callable(runtime, reduce, nullptr, 0, payload, error)) return false;
    auto* tuple = value_as_tuple(payload);
    if (tuple == nullptr || tuple->items.size() != 3) {
      error = "cannot pickle this iterator";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    auto* constructor_args = value_as_tuple(tuple->items[1]);
    if (constructor_args == nullptr || constructor_args->items.size() != 1) {
      error = "cannot pickle this iterator";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const Value* constructor = runtime.find_builtin("iter");
    if (constructor == nullptr) {
      error = "iter constructor is not registered";
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
    out = Value::tuple({*constructor, Value::tuple({constructor_args->items[0]}), tuple->items[2]});
    return true;
  }
  error = "cannot pickle this iterator";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool iterator_setstate_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 2 && args[0].tag == ValueTag::Object && args[0].as.obj != nullptr &&
      args[0].as.obj->kind == ObjectKind::ProtocolIterator) {
    auto* iterator = reinterpret_cast<ProtocolIteratorObject*>(args[0].as.obj);
    Value setstate;
    if (!object_get_attr(iterator->iterator, "__setstate__", setstate, error)) return false;
    Value state_args[] = {args[1]};
    if (!runtime_call_callable(runtime, setstate, state_args, 1, out, error)) return false;
    value_set_none(out);
    return true;
  }
  auto* iterator = argc >= 1 && args[0].tag == ValueTag::Object && args[0].as.obj != nullptr &&
          args[0].as.obj->kind == ObjectKind::SequenceIterator
      ? reinterpret_cast<SequenceIteratorObject*>(args[0].as.obj)
      : nullptr;
  int64_t index = 0;
  if (argc != 2 || iterator == nullptr || !value_int_like_to_i64(args[1], index)) {
    error = "iterator.__setstate__() requires an integer state";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  iterator->index = static_cast<uint64_t>(std::max<int64_t>(0, index));
  value_set_none(out);
  return true;
}

static constexpr BuiltinMethodSpec kIteratorMethods[] = {
    {"__iter__", "iterator.__iter__", iterator_iter_method},
    {"__next__", "iterator.__next__", iterator_next_method},
    {"__reduce__", "iterator.__reduce__", iterator_reduce_method},
    {"__setstate__", "iterator.__setstate__", iterator_setstate_method},
};

} // namespace

bool iterator_get_method(const Value& object, const std::string& name, Value& out) {
  if (!value_is_builtin_iterator(object)) {
    return false;
  }
  return bind_builtin_method_from_table(object, name, kIteratorMethods, std::size(kIteratorMethods), out);
}

} // namespace xlang3
