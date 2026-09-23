/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"

#include "xlang3/attribute.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xlang3 {
namespace {

constexpr const char* kProfilerType = "_lsprof.Profiler";

struct ProfilerModuleState {
  Value entry_class = Value::invalid();
  Value subentry_class = Value::invalid();
};

struct ProfileSubentry {
  int64_t callcount = 0;
  int64_t reccallcount = 0;
  double totaltime = 0.0;
  double inlinetime = 0.0;
};

struct ProfileEntry {
  Value code = Value::invalid();
  std::string key;
  int64_t callcount = 0;
  int64_t reccallcount = 0;
  int64_t active = 0;
  double totaltime = 0.0;
  double inlinetime = 0.0;
  std::unordered_map<size_t, ProfileSubentry> calls;
};

struct ActiveCall {
  size_t entry = 0;
  size_t caller = static_cast<size_t>(-1);
  double started = 0.0;
  double child_time = 0.0;
};

struct ProfilerState {
  std::atomic_uint32_t references{1};
  ProfilerModuleState* module = nullptr;
  std::vector<ProfileEntry> entries;
  std::unordered_map<const Object*, size_t> object_entries;
  std::unordered_map<std::string, size_t> string_entries;
  std::vector<ActiveCall> stack;
  bool enabled = false;
  bool subcalls = true;
  bool builtins = true;

  ~ProfilerState() {
    for (auto& entry : entries) value_set_invalid(entry.code);
  }
};

double profile_now() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

void profiler_state_release(ProfilerState* state) {
  if (state != nullptr && state->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete state;
}

void profiler_instance_cleanup(void* data) {
  profiler_state_release(static_cast<ProfilerState*>(data));
}

void profiler_callback_cleanup(void* data) {
  profiler_state_release(static_cast<ProfilerState*>(data));
}

ProfilerState* profiler_state(Runtime& runtime, const Value& self, std::string& error) {
  auto* state = static_cast<ProfilerState*>(instance_get_native_data(self, kProfilerType));
  if (state == nullptr) {
    error = "uninitialized Profiler object";
    runtime.raise_class_error("TypeError", error);
  }
  return state;
}

size_t entry_for_code(ProfilerState& state, const Value& code, std::string key) {
  if (code.tag == ValueTag::Object && code.as.obj != nullptr) {
    auto found = state.object_entries.find(code.as.obj);
    if (found != state.object_entries.end()) return found->second;
    const size_t index = state.entries.size();
    ProfileEntry entry;
    value_assign_fast(entry.code, code);
    state.entries.push_back(std::move(entry));
    state.object_entries.emplace(code.as.obj, index);
    return index;
  }
  auto found = state.string_entries.find(key);
  if (found != state.string_entries.end()) return found->second;
  const size_t index = state.entries.size();
  ProfileEntry entry;
  entry.code = Value::string(key);
  entry.key = key;
  state.entries.push_back(std::move(entry));
  state.string_entries.emplace(std::move(key), index);
  return index;
}

std::string callable_profile_name(const Value& callable) {
  Value name;
  std::string ignored;
  if ((object_get_attr(callable, "__qualname__", name, ignored) ||
       object_get_attr(callable, "__name__", name, ignored)) &&
      value_as_string(name) != nullptr) {
    return "<" + string_object_to_string(*value_as_string(name)) + ">";
  }
  if (auto* native = value_as_native_function(callable)) return "<" + native->name + ">";
  return "<built-in>";
}

void finish_call(ProfilerState& state, size_t position, double now) {
  ActiveCall active = state.stack[position];
  state.stack.erase(state.stack.begin() + static_cast<std::ptrdiff_t>(position));
  if (active.entry >= state.entries.size()) return;
  auto& entry = state.entries[active.entry];
  const double elapsed = now >= active.started ? now - active.started : 0.0;
  const double inline_time = elapsed >= active.child_time ? elapsed - active.child_time : 0.0;
  entry.totaltime += elapsed;
  entry.inlinetime += inline_time;
  if (entry.active > 0) --entry.active;

  if (!state.stack.empty()) state.stack.back().child_time += elapsed;
  if (state.subcalls && active.caller != static_cast<size_t>(-1) &&
      active.caller < state.entries.size()) {
    auto& sub = state.entries[active.caller].calls[active.entry];
    ++sub.callcount;
    if (entry.active > 0) ++sub.reccallcount;
    sub.totaltime += elapsed;
    sub.inlinetime += inline_time;
  }
}

bool profiler_event(Runtime&, const Value* args, uint32_t argc, Value& out,
                    std::string& error, void* data) {
  auto* state = static_cast<ProfilerState*>(data);
  if (state == nullptr || !state->enabled || argc != 3) {
    value_set_none(out);
    return true;
  }
  auto* event_text = value_as_string(args[1]);
  if (event_text == nullptr) {
    error = "profile event name must be a string";
    return false;
  }
  const std::string event = string_object_to_string(*event_text);
  const bool is_native = event == "c_call" || event == "c_return" || event == "c_exception";
  if (is_native && !state->builtins) {
    value_set_none(out);
    return true;
  }

  if (event == "call" || event == "c_call") {
    Value code;
    std::string key;
    if (event == "call") {
      if (!object_get_attr(args[0], "f_code", code, error)) return false;
    } else {
      key = callable_profile_name(args[2]);
      code = Value::string(key);
    }
    const size_t index = entry_for_code(*state, code, std::move(key));
    auto& entry = state->entries[index];
    ++entry.callcount;
    if (entry.active > 0) ++entry.reccallcount;
    ++entry.active;
    state->stack.push_back({index,
                            state->stack.empty() ? static_cast<size_t>(-1) : state->stack.back().entry,
                            profile_now(), 0.0});
  } else if ((event == "return" || event == "exception" ||
              event == "c_return" || event == "c_exception") && !state->stack.empty()) {
    // Normal profile events are properly nested. Find a matching Python code
    // when possible so an unmatched enable()/disable() C event cannot corrupt
    // the active Python stack.
    size_t position = state->stack.size() - 1;
    if (event == "return" || event == "exception") {
      Value code;
      if (object_get_attr(args[0], "f_code", code, error) && code.tag == ValueTag::Object) {
        auto found = state->object_entries.find(code.as.obj);
        if (found != state->object_entries.end()) {
          for (size_t i = state->stack.size(); i-- > 0;) {
            if (state->stack[i].entry == found->second) { position = i; break; }
          }
        }
      } else {
        error.clear();
      }
    }
    finish_call(*state, position, profile_now());
  }
  value_set_none(out);
  return true;
}

bool profiler_init_kw(Runtime& runtime, const Value* args, uint32_t argc,
                      const NativeKeywordArg* kwargs, uint32_t kwargc,
                      Value& out, std::string& error, void* data) {
  if (argc < 1 || argc > 5) {
    error = "Profiler() takes at most 4 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = new ProfilerState();
  state->module = static_cast<ProfilerModuleState*>(data);
  if (argc >= 4) state->subcalls = value_truthy(args[3]);
  if (argc >= 5) state->builtins = value_truthy(args[4]);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (kwargs[i].value == nullptr) continue;
    if (name == "subcalls") state->subcalls = value_truthy(*kwargs[i].value);
    else if (name == "builtins") state->builtins = value_truthy(*kwargs[i].value);
    else if (name != "timer" && name != "timeunit") {
      delete state;
      error = "Profiler() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (!instance_set_native_data(args[0], kProfilerType, state, profiler_instance_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool profiler_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                   std::string& error, void* data) {
  return profiler_init_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool profiler_enable_kw(Runtime& runtime, const Value* args, uint32_t argc,
                        const NativeKeywordArg* kwargs, uint32_t kwargc,
                        Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "enable() takes at most 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = profiler_state(runtime, args[0], error);
  if (state == nullptr) return false;
  if (argc >= 2) state->subcalls = value_truthy(args[1]);
  if (argc >= 3) state->builtins = value_truthy(args[2]);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (kwargs[i].value == nullptr) continue;
    if (name == "subcalls") state->subcalls = value_truthy(*kwargs[i].value);
    else if (name == "builtins") state->builtins = value_truthy(*kwargs[i].value);
    else {
      error = "enable() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (!state->enabled) {
    state->enabled = true;
    state->references.fetch_add(1, std::memory_order_relaxed);
    runtime.set_profile_function(runtime.make_native_function(
        "_lsprof.Profiler.callback", profiler_event, state, profiler_callback_cleanup));
  }
  value_set_none(out);
  return true;
}

bool profiler_enable(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                     std::string& error, void* data) {
  return profiler_enable_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool profiler_disable(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc != 1) {
    error = "disable() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = profiler_state(runtime, args[0], error);
  if (state == nullptr) return false;
  if (state->enabled) {
    const double now = profile_now();
    while (!state->stack.empty()) finish_call(*state, state->stack.size() - 1, now);
    state->enabled = false;
    runtime.set_profile_function(Value::none());
  }
  value_set_none(out);
  return true;
}

bool profiler_clear(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                    std::string& error, void*) {
  if (argc != 1) {
    error = "clear() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = profiler_state(runtime, args[0], error);
  if (state == nullptr) return false;
  for (auto& entry : state->entries) value_set_invalid(entry.code);
  state->entries.clear();
  state->object_entries.clear();
  state->string_entries.clear();
  state->stack.clear();
  value_set_none(out);
  return true;
}

Value make_stats_record(const Value& klass, const Value& code, int64_t callcount,
                        int64_t reccallcount, double totaltime, double inlinetime,
                        Value calls) {
  Value record = Value::instance(klass);
  std::string ignored;
  object_set_attr(record, "code", code, ignored);
  object_set_attr(record, "callcount", Value::int64(callcount), ignored);
  object_set_attr(record, "reccallcount", Value::int64(reccallcount), ignored);
  object_set_attr(record, "totaltime", Value::number(totaltime), ignored);
  object_set_attr(record, "inlinetime", Value::number(inlinetime), ignored);
  object_set_attr(record, "calls", calls, ignored);
  return record;
}

bool profiler_getstats(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                       std::string& error, void*) {
  if (argc != 1) {
    error = "getstats() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = profiler_state(runtime, args[0], error);
  if (state == nullptr) return false;
  std::vector<Value> records;
  records.reserve(state->entries.size());
  for (const auto& entry : state->entries) {
    std::vector<Value> subrecords;
    subrecords.reserve(entry.calls.size());
    for (const auto& call : entry.calls) {
      if (call.first >= state->entries.size()) continue;
      const auto& callee = state->entries[call.first];
      const auto& sub = call.second;
      subrecords.push_back(make_stats_record(
          state->module->subentry_class, callee.code, sub.callcount,
          sub.reccallcount, sub.totaltime, sub.inlinetime, Value::none()));
    }
    records.push_back(make_stats_record(
        state->module->entry_class, entry.code, entry.callcount,
        entry.reccallcount, entry.totaltime, entry.inlinetime,
        subrecords.empty() ? Value::none() : Value::list(std::move(subrecords))));
  }
  out = Value::list(std::move(records));
  return true;
}

void profiler_module_cleanup(void* data) {
  auto* state = static_cast<ProfilerModuleState*>(data);
  value_set_invalid(state->entry_class);
  value_set_invalid(state->subentry_class);
  delete state;
}

} // namespace

void register_lsprof_module(Runtime& runtime) {
  auto* module_state = new ProfilerModuleState();
  module_state->entry_class = Value::class_object(
      "profiler_entry", {{"__module__", Value::string("_lsprof")}});
  module_state->subentry_class = Value::class_object(
      "profiler_subentry", {{"__module__", Value::string("_lsprof")}});
  runtime.register_native_package_cleanup(module_state, profiler_module_cleanup);

  Value profiler = Value::class_object(
      "Profiler",
      {{"__module__", Value::string("_lsprof")},
       {"__init__", runtime.make_native_function(
          "_lsprof.Profiler.__init__", profiler_init, module_state, nullptr,
          nullptr, false, profiler_init_kw)},
       {"enable", runtime.make_native_function(
          "_lsprof.Profiler.enable", profiler_enable, nullptr, nullptr,
          nullptr, false, profiler_enable_kw)},
       {"disable", runtime.make_native_function("_lsprof.Profiler.disable", profiler_disable)},
       {"clear", runtime.make_native_function("_lsprof.Profiler.clear", profiler_clear)},
       {"getstats", runtime.make_native_function("_lsprof.Profiler.getstats", profiler_getstats)}});
  std::string ignored;
  if (const Value* object = runtime.find_builtin("object")) class_set_base(profiler, *object, ignored);

  NativeModuleBuilder builder(runtime, "_lsprof");
  builder.value("Profiler", std::move(profiler))
      .value("profiler_entry", module_state->entry_class)
      .value("profiler_subentry", module_state->subentry_class);
  runtime.register_module("_lsprof", builder.finish());
}

} // namespace xlang3
