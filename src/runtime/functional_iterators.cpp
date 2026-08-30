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

#include "xlang3/builtins.h"
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

Value functional_getitem_iterator(Runtime* runtime, Value iterable) {
  Value value;
  value.tag = ValueTag::Object;
  auto* obj = allocate_functional_iterator<ProtocolIteratorObject>(ObjectKind::ProtocolIterator);
  obj->runtime = runtime;
  obj->iterator = std::move(iterable);
  obj->use_getitem = true;
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
    Value callable_name = Value::string(native->name);
    Value code = Value::none();
    if (!sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCall, code, -1, &callable_name, error)) {
      return false;
    }
    if (!native->callback(runtime, args, argc, out, error, native->user_data)) {
      std::string monitoring_error;
      (void)sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCRaise, code, -1, &callable_name, monitoring_error);
      return false;
    }
    return sys_monitoring_dispatch_event(runtime, kSysMonitoringEventCReturn, code, -1, &callable_name, error);
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
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        error = result.errors.front();
        runtime.set_pending_exception(std::move(pending));
        return false;
      }
      error = result.errors.front();
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
    value_assign_fast(out, result.value);
    return true;
  }

  if (auto* klass = value_as_class(callable)) {
    auto own_new_it = klass->attrs.find("__new__");
    if (!class_has_builtin_base_name(klass, "type") &&
        own_new_it != klass->attrs.end() &&
        (value_as_function(own_new_it->second) != nullptr || value_as_native_function(own_new_it->second) != nullptr)) {
      std::vector<Value> new_args;
      new_args.reserve(static_cast<size_t>(argc) + 1);
      new_args.push_back(callable);
      for (uint32_t i = 0; i < argc; ++i) {
        new_args.push_back(args[i]);
      }
      return runtime_call_callable(
          runtime,
          own_new_it->second,
          new_args.data(),
          static_cast<uint32_t>(new_args.size()),
          out,
          error);
    }

    Value instance = Value::instance(callable);
    Value init;
    std::string init_error;
    if (object_get_attr(instance, "__init__", init, init_error)) {
      Value ignored;
      if (!runtime_call_callable(runtime, init, args, argc, ignored, error)) {
        return false;
      }
    } else {
      if (argc != 0) {
        return raise_type_error(runtime, "class construction expected no arguments", error);
      }
    }
    value_assign_fast(out, instance);
    return true;
  }

  if (auto* instance = value_as_instance(callable)) {
    Value call_method;
    std::string call_error;
    if (object_get_attr(callable, "__call__", call_method, call_error)) {
      if (auto* bound = value_as_bound_method(call_method)) {
        if (value_as_instance(bound->self) == value_as_instance(callable)) {
          return runtime_call_callable(runtime, call_method, args, argc, out, error);
        }
      }
    }
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

bool runtime_get_iter(Runtime& runtime, const Value& iterable, Value& out, std::string& error) {
  if (sequence_get_iter(iterable, out, error)) {
    return true;
  }

  if (auto* klass = value_as_class(iterable)) {
    if (klass->metaclass.tag != ValueTag::Invalid) {
      Value meta_iter;
      std::string meta_error;
      if (object_lookup_class_attr(klass->metaclass, "__iter__", meta_iter, meta_error)) {
        Value iter_method;
        if (auto* method = value_as_static_method(meta_iter)) {
          value_assign_fast(iter_method, method->function);
        } else if (auto* method = value_as_class_method(meta_iter)) {
          Value function;
          value_assign_fast(function, method->function);
          iter_method = Value::bound_method(klass->metaclass, std::move(function));
        } else if (value_as_function(meta_iter) != nullptr || value_as_native_function(meta_iter) != nullptr) {
          iter_method = Value::bound_method(iterable, std::move(meta_iter));
        } else {
          value_assign_fast(iter_method, meta_iter);
        }
        Value iter_result;
        std::string call_error;
        if (!runtime_call_callable(runtime, iter_method, nullptr, 0, iter_result, call_error)) {
          error = call_error.empty() ? "__iter__ call failed" : call_error;
          return false;
        }
        std::string concrete_error;
        if (sequence_get_iter(iter_result, out, concrete_error)) {
          error.clear();
          return true;
        }
        error = concrete_error.empty() ? "__iter__ returned non-iterator" : concrete_error;
        return false;
      }
    }
  }

  Value iter_method;
  std::string attr_error;
  if (!attribute_get(iterable, "__iter__", iter_method, attr_error)) {
    Value getitem_method;
    std::string getitem_error;
    if (attribute_get(iterable, "__getitem__", getitem_method, getitem_error)) {
      out = functional_getitem_iterator(&runtime, iterable);
      error.clear();
      return true;
    }
    error = error.empty() ? "object is not iterable" : error;
    return false;
  }

  Value iter_result;
  std::string call_error;
  if (!runtime_call_callable(runtime, iter_method, nullptr, 0, iter_result, call_error)) {
    error = call_error.empty() ? "__iter__ call failed" : call_error;
    return false;
  }

  std::string concrete_error;
  if (sequence_get_iter(iter_result, out, concrete_error)) {
    error.clear();
    return true;
  }

  out = functional_protocol_iterator(&runtime, std::move(iter_result));
  error.clear();
  return true;
}

bool runtime_collect_iterable(Runtime& runtime, const Value& iterable, std::vector<Value>& out, std::string& error) {
  Value iterator;
  if (!runtime_get_iter(runtime, iterable, iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      return true;
    }
    out.push_back(std::move(item));
  }
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
    if (obj->use_getitem) {
      Value getitem;
      std::string attr_error;
      if (!attribute_get(obj->iterator, "__getitem__", getitem, attr_error)) {
        error = attr_error;
        return false;
      }
      Value index = Value::int64(static_cast<int64_t>(obj->index));
      if (!runtime_call_callable(*obj->runtime, getitem, &index, 1, out, error)) {
        Value pending;
        if (obj->runtime->take_pending_exception(pending)) {
          if (auto* klass = value_as_class(obj->runtime->exception_type(pending)); klass != nullptr && klass->name == "IndexError") {
            done = true;
            value_set_none(out);
            return true;
          }
          obj->runtime->set_pending_exception(std::move(pending));
        }
        return false;
      }
      ++obj->index;
      done = false;
      return true;
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
