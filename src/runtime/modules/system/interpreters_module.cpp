/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"

#include "serialize/block_stream.h"
#include "serialize/value_graph.h"
#include "xlang3/attribute.h"
#include "xlang3/compiler.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xlang3 {
namespace {

constexpr int64_t kWhenceUnknown = 0;
constexpr int64_t kWhenceRuntime = 1;
constexpr int64_t kWhenceLegacyCapi = 2;
constexpr int64_t kWhenceCapi = 3;
constexpr int64_t kWhenceXi = 4;
constexpr int64_t kWhenceStdlib = 5;

struct InterpreterEntry {
  int64_t id = -1;
  int64_t main_id = -1;
  int64_t whence = kWhenceRuntime;
  Runtime* runtime = nullptr;
  std::unique_ptr<Runtime> owned_runtime;
  std::mutex execution_mutex;
  int64_t requested_refs = 0;
  std::atomic_bool running{false};
};

struct QueueItem {
  std::string graph;
  int64_t owner_interpreter = -1;
  int64_t unbound_op = -1;
};

struct QueueEntry {
  int64_t id = -1;
  int64_t maxsize = 0;
  int64_t default_unbound_op = -1;
  int64_t fallback = -1;
  int64_t refs = 1;
  std::deque<QueueItem> items;
};

std::mutex g_interpreters_mutex;
std::unordered_map<int64_t, std::shared_ptr<InterpreterEntry>> g_interpreters;
std::unordered_map<Runtime*, int64_t> g_runtime_ids;
std::atomic<int64_t> g_next_interpreter_id{0};

std::mutex g_queues_mutex;
std::unordered_map<int64_t, QueueEntry> g_queues;
std::atomic<int64_t> g_next_queue_id{1};

std::shared_ptr<InterpreterEntry> ensure_runtime_entry(Runtime& runtime) {
  std::lock_guard<std::mutex> lock(g_interpreters_mutex);
  auto known = g_runtime_ids.find(&runtime);
  if (known != g_runtime_ids.end()) {
    auto found = g_interpreters.find(known->second);
    if (found != g_interpreters.end()) return found->second;
  }
  auto entry = std::make_shared<InterpreterEntry>();
  entry->id = g_next_interpreter_id.fetch_add(1, std::memory_order_relaxed);
  entry->main_id = entry->id;
  entry->runtime = &runtime;
  g_runtime_ids[&runtime] = entry->id;
  g_interpreters[entry->id] = entry;
  return entry;
}

std::shared_ptr<InterpreterEntry> find_interpreter(int64_t id) {
  std::lock_guard<std::mutex> lock(g_interpreters_mutex);
  auto found = g_interpreters.find(id);
  return found == g_interpreters.end() ? nullptr : found->second;
}

bool integer_id(const Value& value, int64_t& id, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "interpreter or queue ID must be an integer";
    return false;
  }
  id = value.as.i64;
  return true;
}

bool keyword_value(
    const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name,
    const Value*& value, std::string& error) {
  value = nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && name == std::string(kwargs[i].name)) {
      if (value != nullptr) {
        error = std::string("multiple values for keyword '") + name + "'";
        return false;
      }
      value = kwargs[i].value;
    }
  }
  return true;
}

bool reject_unknown_keywords(
    const NativeKeywordArg* kwargs, uint32_t kwargc,
    std::initializer_list<const char*> accepted, std::string& error) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (std::find_if(accepted.begin(), accepted.end(), [&](const char* item) {
          return name == item;
        }) == accepted.end()) {
      error = "unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  return true;
}

bool serialize_value(Runtime& runtime, const Value& value, std::string& bytes, std::string& error) {
  serialize::BlockStream stream;
  if (!serialize::write_value_graph(runtime, stream, value, error)) return false;
  bytes.resize(static_cast<size_t>(stream.Size()));
  return bytes.empty() || stream.FullCopyTo(bytes.data(), static_cast<serialize::STREAM_SIZE>(bytes.size()));
}

bool deserialize_value(Runtime& runtime, const std::string& bytes, Value& value, std::string& error) {
  serialize::BlockStream stream(
      const_cast<char*>(bytes.data()), static_cast<serialize::STREAM_SIZE>(bytes.size()), false);
  return serialize::read_value_graph(runtime, stream, value, error);
}

Value exception_info(Runtime& runtime, const std::string& message) {
  Value cls = Value::class_object("_ExceptionInfo", {});
  Value info = Value::instance(cls);
  std::string ignored;
  object_set_attr(info, "type", Value::none(), ignored);
  object_set_attr(info, "msg", Value::string(message), ignored);
  object_set_attr(info, "formatted", Value::string(message), ignored);
  object_set_attr(info, "errdisplay", Value::string(message), ignored);
  return info;
}

bool raise_module_error(Runtime& runtime, const char* module_name, const char* class_name,
                        const std::string& message) {
  Value module;
  Value klass;
  std::string ignored;
  if (mapping_get_item(runtime.module_registry_dict(), Value::string(module_name), module, ignored) &&
      module_get_attr(module, class_name, klass, ignored) && value_as_class(klass) != nullptr) {
    runtime.set_pending_exception(runtime.make_exception_from_class(std::move(klass), message));
  } else {
    runtime.raise_class_error("RuntimeError", message);
  }
  return false;
}

bool interpreter_not_found(Runtime& runtime, int64_t id) {
  return raise_module_error(runtime, "_interpreters", "InterpreterNotFoundError",
                            "interpreter " + std::to_string(id) + " not found");
}

bool queue_not_found(Runtime& runtime, int64_t id) {
  return raise_module_error(runtime, "_interpqueues", "QueueNotFoundError",
                            "queue " + std::to_string(id) + " not found");
}

bool queue_error(Runtime& runtime, const char* class_name, const std::string& message) {
  return raise_module_error(runtime, "_interpqueues", class_name, message);
}

Value get_or_create_main(Runtime& runtime) {
  Value main_module;
  std::string ignored;
  if (mapping_get_item(runtime.module_registry_dict(), Value::string("__main__"), main_module, ignored)) {
    return main_module;
  }
  main_module = Value::module("__main__");
  module_set_attr(main_module, "__spec__", Value::none(), ignored);
  runtime.register_module("__main__", main_module);
  return main_module;
}

bool execute_source(Runtime& runtime, const std::string& source, std::string& error) {
  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    error = parsed.errors.front();
    return false;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = lowered.errors.front();
    return false;
  }
  auto module = std::make_shared<ir::Module>(std::move(lowered.module));
  module->source_file = "<string>";
  Interpreter interpreter(runtime);
  auto result = interpreter.run_module(*module, get_or_create_main(runtime), module);
  if (!result.errors.empty()) {
    error = result.errors.front();
    return false;
  }
  return true;
}

bool interp_create_kw(Runtime& runtime, const Value* args, uint32_t argc,
                      const NativeKeywordArg* kwargs, uint32_t kwargc,
                      Value& out, std::string& error, void*) {
  if (argc > 1 || !reject_unknown_keywords(kwargs, kwargc, {"reqrefs"}, error)) {
    if (error.empty()) error = "create() expected at most one configuration argument";
    return false;
  }
  const Value* reqrefs = nullptr;
  if (!keyword_value(kwargs, kwargc, "reqrefs", reqrefs, error)) return false;
  auto parent = ensure_runtime_entry(runtime);
  auto child = std::make_unique<Runtime>(std::cout);
  auto entry = ensure_runtime_entry(*child);
  {
    std::lock_guard<std::mutex> lock(g_interpreters_mutex);
    entry->whence = kWhenceStdlib;
    entry->main_id = parent->main_id;
    entry->requested_refs = reqrefs != nullptr && value_truthy(*reqrefs) ? 0 : -1;
    entry->owned_runtime = std::move(child);
  }
  out = Value::int64(entry->id);
  return true;
}

bool interp_create(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void* data) {
  return interp_create_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool interp_destroy_kw(Runtime& runtime, const Value* args, uint32_t argc,
                       const NativeKeywordArg* kwargs, uint32_t kwargc,
                       Value& out, std::string& error, void*) {
  if (argc != 1 || !reject_unknown_keywords(kwargs, kwargc, {"restrict"}, error)) {
    if (error.empty()) error = "destroy() expected one interpreter ID";
    return false;
  }
  int64_t id;
  if (!integer_id(args[0], id, error)) return false;
  std::unique_ptr<Runtime> owned;
  {
    std::lock_guard<std::mutex> lock(g_interpreters_mutex);
    auto found = g_interpreters.find(id);
    if (found == g_interpreters.end()) return interpreter_not_found(runtime, id);
    if (found->second->runtime == &runtime) {
      return raise_module_error(runtime, "_interpreters", "InterpreterError",
                                "cannot destroy the current interpreter");
    }
    if (!found->second->owned_runtime) {
      return raise_module_error(runtime, "_interpreters", "InterpreterError",
                                "cannot destroy a runtime-owned interpreter");
    }
    if (found->second->running) {
      return raise_module_error(runtime, "_interpreters", "InterpreterError",
                                "interpreter is running");
    }
    g_runtime_ids.erase(found->second->runtime);
    owned = std::move(found->second->owned_runtime);
    g_interpreters.erase(found);
  }
  owned.reset();
  value_set_none(out);
  return true;
}

bool interp_destroy(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void* data) {
  return interp_destroy_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool interp_get_current(Runtime& runtime, const Value*, uint32_t argc,
                        Value& out, std::string& error, void*) {
  if (argc != 0) { error = "get_current() expected no arguments"; return false; }
  auto entry = ensure_runtime_entry(runtime);
  out = Value::tuple({Value::int64(entry->id), Value::int64(entry->whence)});
  return true;
}

bool interp_get_main(Runtime& runtime, const Value*, uint32_t argc,
                     Value& out, std::string& error, void*) {
  if (argc != 0) { error = "get_main() expected no arguments"; return false; }
  auto current = ensure_runtime_entry(runtime);
  std::shared_ptr<InterpreterEntry> main;
  {
    std::lock_guard<std::mutex> lock(g_interpreters_mutex);
    auto found = g_interpreters.find(current->main_id);
    if (found != g_interpreters.end()) main = found->second;
  }
  if (!main) main = std::move(current);
  out = Value::tuple({Value::int64(main->id), Value::int64(main->whence)});
  return true;
}

bool interp_list_all(Runtime&, const Value*, uint32_t argc,
                     Value& out, std::string& error, void*) {
  if (argc != 0) { error = "list_all() expected no arguments"; return false; }
  std::vector<std::pair<int64_t, int64_t>> ids;
  {
    std::lock_guard<std::mutex> lock(g_interpreters_mutex);
    for (const auto& item : g_interpreters) ids.push_back({item.first, item.second->whence});
  }
  std::sort(ids.begin(), ids.end());
  std::vector<Value> values;
  for (const auto& item : ids) values.push_back(Value::tuple({Value::int64(item.first), Value::int64(item.second)}));
  out = Value::list(std::move(values));
  return true;
}

bool interp_whence(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  int64_t id;
  if (argc != 1 || !integer_id(args[0], id, error)) {
    if (error.empty()) error = "whence() expected one interpreter ID";
    return false;
  }
  auto entry = find_interpreter(id);
  if (!entry) return interpreter_not_found(runtime, id);
  out = Value::int64(entry->whence);
  return true;
}

bool interp_is_running(Runtime& runtime, const Value* args, uint32_t argc,
                       Value& out, std::string& error, void*) {
  int64_t id;
  if (argc != 1 || !integer_id(args[0], id, error)) {
    if (error.empty()) error = "is_running() expected one interpreter ID";
    return false;
  }
  auto entry = find_interpreter(id);
  if (!entry) return interpreter_not_found(runtime, id);
  out = Value::boolean(entry->running.load(std::memory_order_acquire));
  return true;
}

bool interp_ref(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void* delta_ptr) {
  int64_t id;
  if (argc != 1 || !integer_id(args[0], id, error)) {
    if (error.empty()) error = "interpreter reference operation expected one ID";
    return false;
  }
  auto entry = find_interpreter(id);
  if (!entry) return interpreter_not_found(runtime, id);
  const int delta = static_cast<int>(reinterpret_cast<intptr_t>(delta_ptr));
  std::lock_guard<std::mutex> lock(g_interpreters_mutex);
  if (delta > 0) ++entry->requested_refs;
  else if (entry->requested_refs > 0) --entry->requested_refs;
  value_set_none(out);
  return true;
}

bool interp_is_shareable(Runtime& runtime, const Value* args, uint32_t argc,
                         Value& out, std::string& error, void*) {
  if (argc != 1) { error = "is_shareable() expected one object"; return false; }
  std::string bytes;
  std::string serialization_error;
  out = Value::boolean(serialize_value(runtime, args[0], bytes, serialization_error));
  return true;
}

bool interp_set_main_kw(Runtime& runtime, const Value* args, uint32_t argc,
                        const NativeKeywordArg* kwargs, uint32_t kwargc,
                        Value& out, std::string& error, void*) {
  if (argc != 2 || !reject_unknown_keywords(kwargs, kwargc, {"restrict"}, error)) {
    if (error.empty()) error = "set___main___attrs() expected an ID and mapping";
    return false;
  }
  int64_t id;
  if (!integer_id(args[0], id, error)) return false;
  auto* values = value_as_dict(args[1]);
  if (!values) { error = "attributes must be a dict"; return false; }
  auto entry = find_interpreter(id);
  if (!entry) return interpreter_not_found(runtime, id);
  std::string graph;
  if (!serialize_value(runtime, args[1], graph, error)) return false;
  std::lock_guard<std::mutex> execution(entry->execution_mutex);
  Value copied;
  if (!deserialize_value(*entry->runtime, graph, copied, error)) return false;
  auto* copied_dict = value_as_dict(copied);
  Value main_module = get_or_create_main(*entry->runtime);
  for (const auto& item : copied_dict->entries) {
    auto* name = value_as_string(item.first);
    if (!name || !module_set_attr(main_module, std::string(string_object_view(*name)), item.second, error)) {
      if (!name) error = "attribute names must be strings";
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool interp_set_main(Runtime& runtime, const Value* args, uint32_t argc,
                     Value& out, std::string& error, void* data) {
  return interp_set_main_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool interp_exec_kw(Runtime& runtime, const Value* args, uint32_t argc,
                    const NativeKeywordArg* kwargs, uint32_t kwargc,
                    Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3 || !reject_unknown_keywords(kwargs, kwargc, {"restrict"}, error)) {
    if (error.empty()) error = "exec() expected an ID, source, and optional shared mapping";
    return false;
  }
  int64_t id;
  if (!integer_id(args[0], id, error)) return false;
  auto* source = value_as_string(args[1]);
  if (!source) { error = "exec() code must be a string"; return false; }
  auto entry = find_interpreter(id);
  if (!entry) return interpreter_not_found(runtime, id);
  std::lock_guard<std::mutex> execution(entry->execution_mutex);
  entry->running.store(true, std::memory_order_release);
  const bool ok = execute_source(*entry->runtime, std::string(string_object_view(*source)), error);
  entry->running.store(false, std::memory_order_release);
  if (ok) value_set_none(out);
  else { out = exception_info(runtime, error); error.clear(); }
  return true;
}

bool interp_exec(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* data) {
  return interp_exec_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool interp_call_kw(Runtime& runtime, const Value* args, uint32_t argc,
                    const NativeKeywordArg* kwargs, uint32_t kwargc,
                    Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4 || !reject_unknown_keywords(kwargs, kwargc, {"restrict"}, error)) {
    if (error.empty()) error = "call() expected an ID, callable, optional args and kwargs";
    return false;
  }
  int64_t id;
  if (!integer_id(args[0], id, error)) return false;
  auto entry = find_interpreter(id);
  if (!entry) return interpreter_not_found(runtime, id);
  Value positional = argc >= 3 && args[2].tag != ValueTag::None ? args[2] : Value::tuple({});
  Value named = argc >= 4 && args[3].tag != ValueTag::None ? args[3] : Value::dict({});
  if (!value_as_tuple(positional) || !value_as_dict(named)) {
    error = "call() args must be a tuple and kwargs must be a dict";
    return false;
  }
  std::string graph;
  if (!serialize_value(runtime, Value::tuple({args[1], positional, named}), graph, error)) {
    return raise_module_error(runtime, "_interpreters", "NotShareableError", error);
  }
  std::lock_guard<std::mutex> execution(entry->execution_mutex);
  entry->running.store(true, std::memory_order_release);
  Value transferred;
  bool ok = deserialize_value(*entry->runtime, graph, transferred, error);
  Value target_result;
  if (ok) {
    auto* bundle = value_as_tuple(transferred);
    auto* target_args = bundle == nullptr ? nullptr : value_as_tuple(bundle->items[1]);
    auto* target_kwargs = bundle == nullptr ? nullptr : value_as_dict(bundle->items[2]);
    if (!bundle || !target_args || !target_kwargs) {
      error = "invalid transferred call data";
      ok = false;
    } else {
      std::vector<std::pair<std::string, Value>> named_args;
      for (const auto& item : target_kwargs->entries) {
        auto* name = value_as_string(item.first);
        if (!name) { error = "keyword names must be strings"; ok = false; break; }
        named_args.push_back({std::string(string_object_view(*name)), item.second});
      }
      if (ok) ok = runtime_call_callable_kw(*entry->runtime, bundle->items[0],
          target_args->items.begin(), static_cast<uint32_t>(target_args->items.size()),
          named_args, target_result, error);
    }
  }
  Value excinfo = Value::none();
  if (!ok) {
    excinfo = exception_info(runtime, error);
    target_result = Value::none();
  }
  if (ok) {
    std::string result_graph;
    ok = serialize_value(*entry->runtime, target_result, result_graph, error) &&
         deserialize_value(runtime, result_graph, target_result, error);
    if (!ok) excinfo = exception_info(runtime, error);
  }
  entry->running.store(false, std::memory_order_release);
  out = Value::tuple({ok ? target_result : Value::none(), excinfo});
  return true;
}

bool interp_call(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* data) {
  return interp_call_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool queue_create(Runtime&, const Value* args, uint32_t argc,
                  Value& out, std::string& error, void*) {
  if (argc != 3 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64 ||
      args[2].tag != ValueTag::Int64 || args[0].as.i64 < 0) {
    error = "create() expected maxsize, unbound operation, and fallback integers";
    return false;
  }
  QueueEntry queue;
  queue.id = g_next_queue_id.fetch_add(1, std::memory_order_relaxed);
  queue.maxsize = args[0].as.i64;
  queue.default_unbound_op = args[1].as.i64;
  queue.fallback = args[2].as.i64;
  {
    std::lock_guard<std::mutex> lock(g_queues_mutex);
    g_queues.emplace(queue.id, queue);
  }
  out = Value::int64(queue.id);
  return true;
}

bool queue_bind_release(Runtime& runtime, const Value* args, uint32_t argc,
                        Value& out, std::string& error, void* delta_ptr) {
  int64_t id;
  if (argc != 1 || !integer_id(args[0], id, error)) return false;
  const int delta = static_cast<int>(reinterpret_cast<intptr_t>(delta_ptr));
  std::lock_guard<std::mutex> lock(g_queues_mutex);
  auto found = g_queues.find(id);
  if (found == g_queues.end()) return queue_not_found(runtime, id);
  if (delta > 0) ++found->second.refs;
  else if (--found->second.refs <= 0) g_queues.erase(found);
  value_set_none(out);
  return true;
}

bool queue_destroy(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  int64_t id;
  if (argc != 1 || !integer_id(args[0], id, error)) return false;
  std::lock_guard<std::mutex> lock(g_queues_mutex);
  if (g_queues.erase(id) == 0) return queue_not_found(runtime, id);
  value_set_none(out);
  return true;
}

bool queue_put(Runtime& runtime, const Value* args, uint32_t argc,
               Value& out, std::string& error, void*) {
  int64_t id;
  if ((argc != 2 && argc != 3) || !integer_id(args[0], id, error)) return false;
  QueueItem item;
  item.owner_interpreter = ensure_runtime_entry(runtime)->id;
  item.unbound_op = argc == 3 && args[2].tag == ValueTag::Int64 ? args[2].as.i64 : -1;
  if (!serialize_value(runtime, args[1], item.graph, error)) {
    return raise_module_error(runtime, "_interpreters", "NotShareableError", error);
  }
  std::lock_guard<std::mutex> lock(g_queues_mutex);
  auto found = g_queues.find(id);
  if (found == g_queues.end()) return queue_not_found(runtime, id);
  if (found->second.maxsize > 0 && static_cast<int64_t>(found->second.items.size()) >= found->second.maxsize) {
    return queue_error(runtime, "QueueFull", "queue is full");
  }
  if (item.unbound_op < 0) item.unbound_op = found->second.default_unbound_op;
  found->second.items.push_back(std::move(item));
  value_set_none(out);
  return true;
}

bool queue_get(Runtime& runtime, const Value* args, uint32_t argc,
               Value& out, std::string& error, void*) {
  int64_t id;
  if (argc != 1 || !integer_id(args[0], id, error)) return false;
  QueueItem item;
  {
    std::lock_guard<std::mutex> lock(g_queues_mutex);
    auto found = g_queues.find(id);
    if (found == g_queues.end()) return queue_not_found(runtime, id);
    if (found->second.items.empty()) return queue_error(runtime, "QueueEmpty", "queue is empty");
    item = std::move(found->second.items.front());
    found->second.items.pop_front();
  }
  Value value;
  if (!deserialize_value(runtime, item.graph, value, error)) return false;
  out = Value::tuple({std::move(value), Value::none()});
  return true;
}

enum class QueueQuery { Count, Maxsize, Full, Defaults };
bool queue_query(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* kind_ptr) {
  int64_t id;
  if (argc != 1 || !integer_id(args[0], id, error)) return false;
  std::lock_guard<std::mutex> lock(g_queues_mutex);
  auto found = g_queues.find(id);
  if (found == g_queues.end()) return queue_not_found(runtime, id);
  switch (static_cast<QueueQuery>(reinterpret_cast<intptr_t>(kind_ptr))) {
    case QueueQuery::Count: out = Value::int64(static_cast<int64_t>(found->second.items.size())); break;
    case QueueQuery::Maxsize: out = Value::int64(found->second.maxsize); break;
    case QueueQuery::Full: out = Value::boolean(found->second.maxsize > 0 && static_cast<int64_t>(found->second.items.size()) >= found->second.maxsize); break;
    case QueueQuery::Defaults: out = Value::tuple({Value::int64(found->second.default_unbound_op), Value::int64(found->second.fallback)}); break;
  }
  return true;
}

bool queue_list_all(Runtime&, const Value*, uint32_t argc,
                    Value& out, std::string& error, void*) {
  if (argc != 0) { error = "list_all() expected no arguments"; return false; }
  std::vector<Value> values;
  std::lock_guard<std::mutex> lock(g_queues_mutex);
  for (const auto& item : g_queues) values.push_back(Value::tuple({
      Value::int64(item.first), Value::int64(item.second.default_unbound_op), Value::int64(item.second.fallback)}));
  out = Value::list(std::move(values));
  return true;
}

bool queue_register_types(Runtime&, const Value* args, uint32_t argc,
                          Value& out, std::string& error, void* module_ptr) {
  if (argc != 3 || !value_as_class(args[0]) || !value_as_class(args[1]) || !value_as_class(args[2])) {
    error = "_register_heap_types() expected Queue, QueueEmpty, and QueueFull classes";
    return false;
  }
  auto* module = static_cast<Value*>(module_ptr);
  std::string ignored;
  module_set_attr(*module, "QueueEmpty", args[1], ignored);
  module_set_attr(*module, "QueueFull", args[2], ignored);
  value_set_none(out);
  return true;
}

} // namespace

void unregister_interpreter_runtime(Runtime& runtime) {
  std::lock_guard<std::mutex> lock(g_interpreters_mutex);
  auto known = g_runtime_ids.find(&runtime);
  if (known == g_runtime_ids.end()) return;
  auto entry = g_interpreters.find(known->second);
  if (entry != g_interpreters.end() && !entry->second->owned_runtime) g_interpreters.erase(entry);
  g_runtime_ids.erase(known);
}

void register_interpreter_modules(Runtime& runtime) {
  ensure_runtime_entry(runtime);
  const Value exception = runtime.find_builtin("Exception") ? *runtime.find_builtin("Exception") : Value::invalid();
  Value interpreter_error = Value::class_object("InterpreterError", {}, exception);
  Value interpreter_not_found_error = Value::class_object("InterpreterNotFoundError", {}, interpreter_error);
  Value not_shareable_error = Value::class_object("NotShareableError", {}, interpreter_error);

  NativeModuleBuilder interpreters(runtime, "_interpreters");
  interpreters
      .value("InterpreterError", interpreter_error)
      .value("InterpreterNotFoundError", interpreter_not_found_error)
      .value("NotShareableError", not_shareable_error)
      .value("WHENCE_UNKNOWN", Value::int64(kWhenceUnknown))
      .value("WHENCE_RUNTIME", Value::int64(kWhenceRuntime))
      .value("WHENCE_LEGACY_CAPI", Value::int64(kWhenceLegacyCapi))
      .value("WHENCE_CAPI", Value::int64(kWhenceCapi))
      .value("WHENCE_XI", Value::int64(kWhenceXi))
      .value("WHENCE_STDLIB", Value::int64(kWhenceStdlib))
      .function("create", interp_create, nullptr, false, interp_create_kw)
      .function("destroy", interp_destroy, nullptr, false, interp_destroy_kw)
      .function("get_current", interp_get_current)
      .function("get_main", interp_get_main)
      .function("list_all", interp_list_all)
      .function("whence", interp_whence)
      .function("is_running", interp_is_running)
      .function("is_shareable", interp_is_shareable)
      .function("set___main___attrs", interp_set_main, nullptr, false, interp_set_main_kw)
      .function("exec", interp_exec, nullptr, false, interp_exec_kw)
      .function("call", interp_call, nullptr, false, interp_call_kw)
      .value("incref", runtime.make_native_function("_interpreters.incref", interp_ref,
          reinterpret_cast<void*>(static_cast<intptr_t>(1))))
      .value("decref", runtime.make_native_function("_interpreters.decref", interp_ref,
          reinterpret_cast<void*>(static_cast<intptr_t>(-1))));
  runtime.register_module("_interpreters", interpreters.finish());

  Value queue_error_class = Value::class_object("QueueError", {}, exception);
  Value queue_not_found_class = Value::class_object("QueueNotFoundError", {}, queue_error_class);
  Value queue_empty_class = Value::class_object("QueueEmpty", {}, queue_error_class);
  Value queue_full_class = Value::class_object("QueueFull", {}, queue_error_class);
  NativeModuleBuilder queues(runtime, "_interpqueues");
  queues.value("QueueError", queue_error_class)
      .value("QueueNotFoundError", queue_not_found_class)
      .value("QueueEmpty", queue_empty_class)
      .value("QueueFull", queue_full_class)
      .function("create", queue_create)
      .value("bind", runtime.make_native_function("_interpqueues.bind", queue_bind_release,
          reinterpret_cast<void*>(static_cast<intptr_t>(1))))
      .value("release", runtime.make_native_function("_interpqueues.release", queue_bind_release,
          reinterpret_cast<void*>(static_cast<intptr_t>(-1))))
      .function("destroy", queue_destroy)
      .function("put", queue_put)
      .function("get", queue_get)
      .value("get_count", runtime.make_native_function("_interpqueues.get_count", queue_query,
          reinterpret_cast<void*>(static_cast<intptr_t>(QueueQuery::Count))))
      .value("get_maxsize", runtime.make_native_function("_interpqueues.get_maxsize", queue_query,
          reinterpret_cast<void*>(static_cast<intptr_t>(QueueQuery::Maxsize))))
      .value("is_full", runtime.make_native_function("_interpqueues.is_full", queue_query,
          reinterpret_cast<void*>(static_cast<intptr_t>(QueueQuery::Full))))
      .value("get_queue_defaults", runtime.make_native_function("_interpqueues.get_queue_defaults", queue_query,
          reinterpret_cast<void*>(static_cast<intptr_t>(QueueQuery::Defaults))))
      .function("list_all", queue_list_all);
  Value queue_module = queues.finish();
  auto* module_copy = new Value(queue_module);
  runtime.register_native_package_cleanup(module_copy, [](void* data) { delete static_cast<Value*>(data); });
  std::string ignored;
  module_set_attr(queue_module, "_register_heap_types",
      runtime.make_native_function("_interpqueues._register_heap_types", queue_register_types, module_copy),
      ignored);
  runtime.register_module("_interpqueues", std::move(queue_module));
}

} // namespace xlang3
