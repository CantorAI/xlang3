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

#include <algorithm>
#include <deque>
#include <limits>
#include <mutex>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kSimpleQueueNativeType = "_queue.SimpleQueue";
constexpr const char* kQueueNativeType = "queue.Queue";

enum class QueueMode : uint8_t {
  Fifo,
  Lifo,
  Priority,
};

struct SimpleQueueState {
  std::mutex mutex;
  std::deque<Value> items;
};

struct QueueClassSpec {
  const char* class_name;
  const char* native_name;
  QueueMode mode;
};

struct QueueState {
  std::mutex mutex;
  std::deque<Value> items;
  int64_t maxsize = 0;
  int64_t unfinished_tasks = 0;
  QueueMode mode = QueueMode::Fifo;
  bool shutdown = false;
  bool immediate_shutdown = false;
};

SimpleQueueState* simple_queue_state(const Value& self, std::string& error) {
  auto* state = static_cast<SimpleQueueState*>(instance_get_native_data(self, kSimpleQueueNativeType));
  if (state == nullptr) {
    error = "invalid SimpleQueue object";
  }
  return state;
}

void simple_queue_cleanup(void* data) {
  delete static_cast<SimpleQueueState*>(data);
}

QueueState* queue_state(const Value& self, std::string& error) {
  auto* state = static_cast<QueueState*>(instance_get_native_data(self, kQueueNativeType));
  if (state == nullptr) {
    error = "invalid Queue object";
  }
  return state;
}

void queue_cleanup(void* data) {
  delete static_cast<QueueState*>(data);
}

bool value_to_maxsize(const Value& value, int64_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "queue maxsize must be an integer";
    return false;
  }
  out = value.as.i64;
  return true;
}

bool parse_blocking_args(const char* name, uint32_t argc, uint32_t max_argc, std::string& error) {
  if (argc > max_argc) {
    error = std::string(name) + "() expected item and optional block/timeout";
    return false;
  }
  return true;
}

bool validate_block_timeout_keywords(
    const char* name,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    std::string& error) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = std::string(name) + "() got invalid keyword argument";
      return false;
    }
    const std::string key(kwargs[i].name);
    if (key != "block" && key != "timeout") {
      error = std::string(name) + "() got an unexpected keyword argument '" + key + "'";
      return false;
    }
  }
  return true;
}

void raise_queue_exception(Runtime& runtime, const char* class_name, std::string message) {
  runtime.set_pending_exception(runtime.make_exception(class_name, std::move(message)));
}

bool priority_less(const Value& lhs, const Value& rhs) {
  Value result;
  std::string ignored;
  if (!value_compare("<", lhs, rhs, result, ignored) || result.tag != ValueTag::Bool) {
    return false;
  }
  return result.as.b;
}

std::deque<Value>::iterator queue_priority_insert_position(std::deque<Value>& items, const Value& item) {
  return std::upper_bound(items.begin(), items.end(), item, [](const Value& lhs, const Value& rhs) {
    return priority_less(lhs, rhs);
  });
}

bool simple_queue_raise_empty(Runtime& runtime, std::string message) {
  raise_queue_exception(runtime, "Empty", std::move(message));
  return false;
}

bool simple_queue_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SimpleQueue.__init__ expected no arguments";
    return false;
  }
  auto* state = new SimpleQueueState();
  if (!instance_set_native_data(args[0], kSimpleQueueNativeType, state, simple_queue_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool simple_queue_put(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "SimpleQueue.put() expected item and optional block/timeout";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->items.push_back(args[1]);
  }
  value_set_none(out);
  return true;
}

bool simple_queue_get(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 3) {
    error = "SimpleQueue.get() expected optional block/timeout";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->items.empty()) {
    error = "queue is empty";
    return false;
  }
  value_assign_fast(out, state->items.front());
  state->items.pop_front();
  return true;
}

bool simple_queue_get_exc(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!simple_queue_get(runtime, args, argc, out, error, user_data)) {
    if (error == "queue is empty") {
      return simple_queue_raise_empty(runtime, error);
    }
    return false;
  }
  return true;
}

bool simple_queue_empty(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SimpleQueue.empty() expected no arguments";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  out = Value::boolean(state->items.empty());
  return true;
}

bool simple_queue_qsize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SimpleQueue.qsize() expected no arguments";
    return false;
  }
  auto* state = simple_queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  value_set_int64(out, static_cast<int64_t>(state->items.size()));
  return true;
}

Value make_simple_queue_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_queue.SimpleQueue.__init__", simple_queue_init)});
  attrs.push_back({"put", runtime.make_native_function("_queue.SimpleQueue.put", simple_queue_put)});
  attrs.push_back({"put_nowait", runtime.make_native_function("_queue.SimpleQueue.put_nowait", simple_queue_put)});
  attrs.push_back({"get", runtime.make_native_function("_queue.SimpleQueue.get", simple_queue_get_exc)});
  attrs.push_back({"get_nowait", runtime.make_native_function("_queue.SimpleQueue.get_nowait", simple_queue_get_exc)});
  attrs.push_back({"empty", runtime.make_native_function("_queue.SimpleQueue.empty", simple_queue_empty)});
  attrs.push_back({"qsize", runtime.make_native_function("_queue.SimpleQueue.qsize", simple_queue_qsize)});
  return Value::class_object("SimpleQueue", std::move(attrs));
}

bool queue_init_impl(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc > 2) {
    error = "Queue.__init__ expected optional maxsize";
    return false;
  }
  auto* spec = static_cast<const QueueClassSpec*>(user_data);
  auto* state = new QueueState();
  state->mode = spec == nullptr ? QueueMode::Fifo : spec->mode;
  if (argc == 2 && !value_to_maxsize(args[1], state->maxsize, error)) {
    delete state;
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string(kwargs[i].name) == "maxsize") {
      if (!value_to_maxsize(*kwargs[i].value, state->maxsize, error)) {
        delete state;
        return false;
      }
    } else {
      error = std::string("Queue.__init__ got unexpected keyword argument '") + kwargs[i].name + "'";
      delete state;
      return false;
    }
  }
  if (!instance_set_native_data(args[0], kQueueNativeType, state, queue_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool queue_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return queue_init_impl(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool queue_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  return queue_init_impl(runtime, args, argc, kwargs, kwargc, out, error, user_data);
}

bool queue_put(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || !parse_blocking_args("Queue.put", argc, 4, error)) {
    return false;
  }
  auto* state = queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->shutdown) {
    raise_queue_exception(runtime, "ShutDown", "queue is shut down");
    return false;
  }
  if (state->maxsize > 0 && static_cast<int64_t>(state->items.size()) >= state->maxsize) {
    raise_queue_exception(runtime, "Full", "queue is full");
    return false;
  }
  if (state->mode == QueueMode::Priority) {
    state->items.insert(queue_priority_insert_position(state->items, args[1]), args[1]);
  } else {
    state->items.push_back(args[1]);
  }
  ++state->unfinished_tasks;
  value_set_none(out);
  return true;
}

bool queue_put_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!validate_block_timeout_keywords("Queue.put", kwargs, kwargc, error)) {
    return false;
  }
  return queue_put(runtime, args, argc, out, error, user_data);
}

bool queue_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!parse_blocking_args("Queue.get", argc, 3, error)) {
    return false;
  }
  auto* state = queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->items.empty()) {
    if (state->shutdown) {
      raise_queue_exception(runtime, "ShutDown", "queue is shut down");
      return false;
    }
    raise_queue_exception(runtime, "Empty", "queue is empty");
    return false;
  }
  if (state->mode == QueueMode::Lifo) {
    value_assign_fast(out, state->items.back());
    state->items.pop_back();
  } else {
    value_assign_fast(out, state->items.front());
    state->items.pop_front();
  }
  return true;
}

bool queue_get_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!validate_block_timeout_keywords("Queue.get", kwargs, kwargc, error)) {
    return false;
  }
  return queue_get(runtime, args, argc, out, error, user_data);
}

bool queue_empty(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Queue.empty() expected no arguments";
    return false;
  }
  auto* state = queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  out = Value::boolean(state->items.empty());
  return true;
}

bool queue_full(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Queue.full() expected no arguments";
    return false;
  }
  auto* state = queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  out = Value::boolean(state->maxsize > 0 && static_cast<int64_t>(state->items.size()) >= state->maxsize);
  return true;
}

bool queue_qsize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Queue.qsize() expected no arguments";
    return false;
  }
  auto* state = queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  value_set_int64(out, static_cast<int64_t>(state->items.size()));
  return true;
}

bool queue_task_done(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Queue.task_done() expected no arguments";
    return false;
  }
  auto* state = queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  if (state->unfinished_tasks <= 0) {
    runtime.raise_class_error("ValueError", "task_done() called too many times");
    return false;
  }
  --state->unfinished_tasks;
  value_set_none(out);
  return true;
}

bool queue_join(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Queue.join() expected no arguments";
    return false;
  }
  if (queue_state(args[0], error) == nullptr) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool queue_shutdown_impl(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc > 2) {
    error = "Queue.shutdown() expected optional immediate";
    return false;
  }
  bool immediate = false;
  if (argc == 2) {
    immediate = value_truthy(args[1]);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "Queue.shutdown() got invalid keyword argument";
      return false;
    }
    const std::string key(kwargs[i].name);
    if (key != "immediate") {
      error = "Queue.shutdown() got an unexpected keyword argument '" + key + "'";
      return false;
    }
    immediate = value_truthy(*kwargs[i].value);
  }
  auto* state = queue_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->shutdown = true;
    state->immediate_shutdown = immediate;
    if (immediate) {
      state->items.clear();
      state->unfinished_tasks = 0;
    }
  }
  value_set_none(out);
  return true;
}

bool queue_shutdown(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return queue_shutdown_impl(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool queue_shutdown_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  return queue_shutdown_impl(runtime, args, argc, kwargs, kwargc, out, error, user_data);
}

Value make_queue_class(Runtime& runtime, const QueueClassSpec& spec) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("queue")});
  attrs.push_back({"__init__",
                   runtime.make_native_function(
                       std::string("queue.") + spec.class_name + ".__init__",
                       queue_init,
                       const_cast<QueueClassSpec*>(&spec),
                       nullptr,
                       nullptr,
                       false,
                       queue_init_kw)});
  attrs.push_back({"put",
                   runtime.make_native_function(std::string("queue.") + spec.class_name + ".put", queue_put, nullptr, nullptr, nullptr, false, queue_put_kw)});
  attrs.push_back({"put_nowait",
                   runtime.make_native_function(std::string("queue.") + spec.class_name + ".put_nowait", queue_put, nullptr, nullptr, nullptr, false, queue_put_kw)});
  attrs.push_back({"get",
                   runtime.make_native_function(std::string("queue.") + spec.class_name + ".get", queue_get, nullptr, nullptr, nullptr, false, queue_get_kw)});
  attrs.push_back({"get_nowait",
                   runtime.make_native_function(std::string("queue.") + spec.class_name + ".get_nowait", queue_get, nullptr, nullptr, nullptr, false, queue_get_kw)});
  attrs.push_back({"empty", runtime.make_native_function(std::string("queue.") + spec.class_name + ".empty", queue_empty)});
  attrs.push_back({"full", runtime.make_native_function(std::string("queue.") + spec.class_name + ".full", queue_full)});
  attrs.push_back({"qsize", runtime.make_native_function(std::string("queue.") + spec.class_name + ".qsize", queue_qsize)});
  attrs.push_back({"task_done", runtime.make_native_function(std::string("queue.") + spec.class_name + ".task_done", queue_task_done)});
  attrs.push_back({"join", runtime.make_native_function(std::string("queue.") + spec.class_name + ".join", queue_join)});
  attrs.push_back({"shutdown",
                   runtime.make_native_function(std::string("queue.") + spec.class_name + ".shutdown", queue_shutdown, nullptr, nullptr, nullptr, false, queue_shutdown_kw)});
  return Value::class_object(spec.class_name, std::move(attrs));
}

} // namespace

void register_queue_module(Runtime& runtime) {
  static const QueueClassSpec queue_spec{"Queue", kQueueNativeType, QueueMode::Fifo};
  static const QueueClassSpec lifo_spec{"LifoQueue", kQueueNativeType, QueueMode::Lifo};
  static const QueueClassSpec priority_spec{"PriorityQueue", kQueueNativeType, QueueMode::Priority};

  Value simple_queue_class = make_simple_queue_class(runtime);
  Value queue_class = make_queue_class(runtime, queue_spec);
  Value lifo_queue_class = make_queue_class(runtime, lifo_spec);
  Value priority_queue_class = make_queue_class(runtime, priority_spec);
  Value exception_base = runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid();
  Value empty_class = Value::class_object("Empty", {}, exception_base);
  Value full_class = Value::class_object("Full", {}, exception_base);
  Value shutdown_class = Value::class_object("ShutDown", {}, exception_base);
  runtime.register_builtin("Empty", empty_class);
  runtime.register_builtin("Full", full_class);
  runtime.register_builtin("ShutDown", shutdown_class);

  NativeModuleBuilder builder(runtime, "_queue");
  builder.value("SimpleQueue", simple_queue_class);
  runtime.register_module("_queue", builder.finish());

  NativeModuleBuilder facade(runtime, "queue");
  facade.value("SimpleQueue", simple_queue_class)
      .value("Queue", queue_class)
      .value("LifoQueue", lifo_queue_class)
      .value("PriorityQueue", priority_queue_class)
      .value("Empty", empty_class)
      .value("Full", full_class)
      .value("ShutDown", shutdown_class)
      .value("deque", Value::class_object("deque", {}));
  runtime.register_module("queue", facade.finish());
}

} // namespace xlang3
