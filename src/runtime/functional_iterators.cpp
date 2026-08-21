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
#include "xlang3/functional_iterators.h"

#include "xlang3/interpreter.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/sequence.h"

#include <utility>

namespace xlang3 {

namespace {

template <typename T>
T* allocate_functional_iterator(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
  return obj;
}

bool raise_type_error(Runtime& runtime, std::string message, std::string& error) {
  error = std::move(message);
  runtime.raise_class_error("TypeError", error);
  return false;
}

} // namespace

Value functional_enumerate_iterator(Value iterator, int64_t start) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<EnumerateIteratorObject>(ObjectKind::EnumerateIterator);
  obj->iterator = std::move(iterator);
  obj->index = start;
  value.as.obj = &obj->header;
  return value;
}

Value functional_zip_iterator(std::vector<Value> iterators) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<ZipIteratorObject>(ObjectKind::ZipIterator);
  obj->iterators = std::move(iterators);
  value.as.obj = &obj->header;
  return value;
}

Value functional_map_iterator(Runtime* runtime, Value callable, std::vector<Value> iterators) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<MapIteratorObject>(ObjectKind::MapIterator);
  obj->runtime = runtime;
  obj->callable = std::move(callable);
  obj->iterators = std::move(iterators);
  value.as.obj = &obj->header;
  return value;
}

Value functional_filter_iterator(Runtime* runtime, Value predicate, Value iterator) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<FilterIteratorObject>(ObjectKind::FilterIterator);
  obj->runtime = runtime;
  obj->predicate = std::move(predicate);
  obj->iterator = std::move(iterator);
  value.as.obj = &obj->header;
  return value;
}

bool value_is_functional_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return false;
  }
  return value.as.obj->kind == ObjectKind::EnumerateIterator ||
         value.as.obj->kind == ObjectKind::ZipIterator ||
         value.as.obj->kind == ObjectKind::MapIterator ||
         value.as.obj->kind == ObjectKind::FilterIterator;
}

void functional_iterator_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::EnumerateIterator:
      delete reinterpret_cast<EnumerateIteratorObject*>(object);
      break;
    case ObjectKind::ZipIterator:
      delete reinterpret_cast<ZipIteratorObject*>(object);
      break;
    case ObjectKind::MapIterator:
      delete reinterpret_cast<MapIteratorObject*>(object);
      break;
    case ObjectKind::FilterIterator:
      delete reinterpret_cast<FilterIteratorObject*>(object);
      break;
    default:
      break;
  }
}

std::string functional_iterator_to_string(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return "<iterator>";
  }
  switch (value.as.obj->kind) {
    case ObjectKind::EnumerateIterator:
      return "<enumerate object>";
    case ObjectKind::ZipIterator:
      return "<zip object>";
    case ObjectKind::MapIterator:
      return "<map object>";
    case ObjectKind::FilterIterator:
      return "<filter object>";
    default:
      return "<iterator>";
  }
}

bool runtime_call_callable(
    Runtime& runtime,
    const Value& callable,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error) {
  if (auto* native = value_as_native_function(callable)) {
    if (native->callback == nullptr) {
      return raise_type_error(runtime, "native callable does not support this call path", error);
    }
    return native->callback(runtime, args, argc, out, error, native->user_data);
  }

  if (auto* function = value_as_function(callable)) {
    CallArgsView call_args;
    call_args.leading = args;
    call_args.leading_count = argc;
    Interpreter interpreter(runtime);
    RuntimeResult result = interpreter.run_function_value(function, call_args);
    if (!result.errors.empty()) {
      error = result.errors.front();
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
    value_assign_fast(out, result.value);
    return true;
  }

  if (auto* bound = value_as_bound_method(callable)) {
    std::vector<Value> bound_args;
    bound_args.reserve(static_cast<size_t>(argc) + 1);
    bound_args.push_back(bound->self);
    for (uint32_t i = 0; i < argc; ++i) {
      bound_args.push_back(args[i]);
    }
    return runtime_call_callable(
        runtime,
        bound->function,
        bound_args.data(),
        static_cast<uint32_t>(bound_args.size()),
        out,
        error);
  }

  return raise_type_error(runtime, "object is not callable", error);
}

bool functional_iterator_next(Value& iterator, bool& done, Value& out, std::string& error) {
  done = false;
  if (iterator.tag != ValueTag::Object || iterator.as.obj == nullptr) {
    error = "invalid iterator";
    return false;
  }

  if (iterator.as.obj->kind == ObjectKind::EnumerateIterator) {
    auto* obj = reinterpret_cast<EnumerateIteratorObject*>(iterator.as.obj);
    Value item;
    if (!sequence_iter_next(obj->iterator, done, item, error)) {
      return false;
    }
    if (done) {
      value_set_none(out);
      return true;
    }
    out = Value::tuple({Value::int64(obj->index), std::move(item)});
    ++obj->index;
    return true;
  }

  if (iterator.as.obj->kind == ObjectKind::ZipIterator) {
    auto* obj = reinterpret_cast<ZipIteratorObject*>(iterator.as.obj);
    std::vector<Value> row;
    row.reserve(obj->iterators.size());
    for (auto& child : obj->iterators) {
      Value item;
      if (!sequence_iter_next(child, done, item, error)) {
        return false;
      }
      if (done) {
        value_set_none(out);
        return true;
      }
      row.push_back(std::move(item));
    }
    out = Value::tuple(std::move(row));
    return true;
  }

  if (iterator.as.obj->kind == ObjectKind::MapIterator) {
    auto* obj = reinterpret_cast<MapIteratorObject*>(iterator.as.obj);
    if (obj->runtime == nullptr) {
      error = "map iterator has no runtime";
      return false;
    }
    std::vector<Value> call_args;
    call_args.reserve(obj->iterators.size());
    for (auto& child : obj->iterators) {
      Value item;
      if (!sequence_iter_next(child, done, item, error)) {
        return false;
      }
      if (done) {
        value_set_none(out);
        return true;
      }
      call_args.push_back(std::move(item));
    }
    return runtime_call_callable(
        *obj->runtime,
        obj->callable,
        call_args.data(),
        static_cast<uint32_t>(call_args.size()),
        out,
        error);
  }

  if (iterator.as.obj->kind == ObjectKind::FilterIterator) {
    auto* obj = reinterpret_cast<FilterIteratorObject*>(iterator.as.obj);
    if (obj->runtime == nullptr) {
      error = "filter iterator has no runtime";
      return false;
    }
    for (;;) {
      Value item;
      if (!sequence_iter_next(obj->iterator, done, item, error)) {
        return false;
      }
      if (done) {
        value_set_none(out);
        return true;
      }
      bool keep = false;
      if (obj->predicate.tag == ValueTag::None) {
        keep = value_truthy(item);
      } else {
        Value predicate_result;
        if (!runtime_call_callable(*obj->runtime, obj->predicate, &item, 1, predicate_result, error)) {
          return false;
        }
        keep = value_truthy(predicate_result);
      }
      if (keep) {
        out = std::move(item);
        return true;
      }
    }
  }

  error = "invalid iterator";
  return false;
}

} // namespace xlang3
