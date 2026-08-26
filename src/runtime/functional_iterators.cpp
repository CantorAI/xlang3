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

#include "xlang3/generator.h"
#include "xlang3/interpreter.h"
#include "xlang3/attribute.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"

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

Value functional_callable_iterator(Runtime* runtime, Value callable, Value sentinel) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<CallableIteratorObject>(ObjectKind::CallableIterator);
  obj->runtime = runtime;
  obj->callable = std::move(callable);
  obj->sentinel = std::move(sentinel);
  value.as.obj = &obj->header;
  return value;
}

Value functional_chain_iterator(std::vector<Value> iterators) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<ChainIteratorObject>(ObjectKind::ChainIterator);
  obj->iterators = std::move(iterators);
  obj->index = 0;
  value.as.obj = &obj->header;
  return value;
}

Value functional_protocol_iterator(Runtime* runtime, Value iterator) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<ProtocolIteratorObject>(ObjectKind::ProtocolIterator);
  obj->runtime = runtime;
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
         value.as.obj->kind == ObjectKind::FilterIterator ||
         value.as.obj->kind == ObjectKind::CallableIterator ||
         value.as.obj->kind == ObjectKind::ChainIterator ||
         value.as.obj->kind == ObjectKind::ProtocolIterator;
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
    case ObjectKind::CallableIterator:
      delete reinterpret_cast<CallableIteratorObject*>(object);
      break;
    case ObjectKind::ChainIterator:
      delete reinterpret_cast<ChainIteratorObject*>(object);
      break;
    case ObjectKind::ProtocolIterator:
      delete reinterpret_cast<ProtocolIteratorObject*>(object);
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
    case ObjectKind::CallableIterator:
      return "<callable_iterator>";
    case ObjectKind::ChainIterator:
      return "<itertools.chain object>";
    case ObjectKind::ProtocolIterator:
      return "<iterator>";
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
    if (function->module != nullptr &&
        function->function_id < function->module->functions.size() &&
        function->module->functions[function->function_id].is_generator) {
      std::vector<Value> generator_args;
      generator_args.reserve(argc);
      for (uint32_t i = 0; i < argc; ++i) {
        generator_args.push_back(args[i]);
      }
      const auto& target_fn = function->module->functions[function->function_id];
      out = Value::generator(&runtime, callable, std::move(generator_args), target_fn.is_async, target_fn.is_coroutine);
      return true;
    }
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

  if (iterator.as.obj->kind == ObjectKind::CallableIterator) {
    auto* obj = reinterpret_cast<CallableIteratorObject*>(iterator.as.obj);
    if (obj->runtime == nullptr) {
      error = "callable iterator has no runtime";
      return false;
    }
    if (!runtime_call_callable(*obj->runtime, obj->callable, nullptr, 0, out, error)) {
      return false;
    }
    if (value_key_equal(out, obj->sentinel)) {
      done = true;
      value_set_none(out);
      return true;
    }
    done = false;
    return true;
  }

  if (iterator.as.obj->kind == ObjectKind::ChainIterator) {
    auto* obj = reinterpret_cast<ChainIteratorObject*>(iterator.as.obj);
    while (obj->index < obj->iterators.size()) {
      Value item;
      bool child_done = false;
      if (!sequence_iter_next(obj->iterators[obj->index], child_done, item, error)) {
        return false;
      }
      if (!child_done) {
        done = false;
        out = std::move(item);
        return true;
      }
      ++obj->index;
    }
    done = true;
    value_set_none(out);
    return true;
  }

  if (iterator.as.obj->kind == ObjectKind::ProtocolIterator) {
    auto* obj = reinterpret_cast<ProtocolIteratorObject*>(iterator.as.obj);
    if (obj->runtime == nullptr) {
      error = "protocol iterator has no runtime";
      return false;
    }
    Value next_method;
    std::string attr_error;
    if (!attribute_get(obj->iterator, "__next__", next_method, attr_error)) {
      error = attr_error;
      return false;
    }
    if (!runtime_call_callable(*obj->runtime, next_method, nullptr, 0, out, error)) {
      Value pending;
      if (obj->runtime->take_pending_exception(pending)) {
        if (auto* klass = value_as_class(obj->runtime->exception_type(pending)); klass != nullptr && klass->name == "StopIteration") {
          done = true;
          value_set_none(out);
          return true;
        }
        obj->runtime->set_pending_exception(std::move(pending));
      }
      return false;
    }
    done = false;
    return true;
  }

  error = "invalid iterator";
  return false;
}

} // namespace xlang3
