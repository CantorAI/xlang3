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
// Own native counterpart of coverage 7.3.2's
// coverage/ctracer/{tracer.c,filedisp.c,datastack.c}; no CPython C API or DLL.
// Performance boundary: retain native event/state handling here while the
// original Python Collector and policy callbacks keep their Python semantics.
// Replacing this with Python PyTracer restores per-event Python call overhead;
// validate any delivery change with the original coverage benchmark and gate.
#include "xlang3/builtins.h"
#include "../thread/runtime_lock.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/ir.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/native_trace_hook.h"
#include "xlang3/object_model.h"
#include "xlang3/set_object.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {
namespace {
constexpr const char* kTracer = "coverage.CTracer";
constexpr const char* kDisposition = "coverage.CFileDispositionType";
enum Field : size_t {
  ShouldTrace, CheckInclude, Warn, ConcurId, Data, FileTracers,
  TraceCache, TraceArcs, StartContext, SwitchContext, DisablePlugin, FieldCount
};
constexpr std::array<const char*, FieldCount> kFields = {
  "should_trace", "check_include", "warn", "concur_id_func", "data",
  "file_tracers", "should_trace_cache", "trace_arcs", "should_start_context",
  "switch_context", "disable_plugin"
};
constexpr std::array<const char*, 7> kDispositionFields = {
  "original_filename", "canonical_filename", "source_filename", "trace",
  "reason", "file_tracer", "has_dynamic_filename"
};
struct Entry {
  Value file_data = Value::none();
  Value file_tracer = Value::none();
  Value disposition = Value::none();
  int64_t last_line = 0;
  int64_t first_line = 0;
  bool started_context = false;
};
struct Stack { std::vector<std::shared_ptr<Entry>> entries; };
struct Payload {
  explicit Payload(bool is_tracer) : tracer(is_tracer) {
    for (auto& field : fields) field = Value::none();
  }
  bool tracer;
  std::array<Value, FieldCount> fields;
  std::atomic_bool started{false};
  std::atomic_bool activity{false};
  bool arcs = false;
  Value context = Value::none();
  Value stack_index = Value::none(); // Original weakref.WeakKeyDictionary.
  std::shared_ptr<Stack> main_stack = std::make_shared<Stack>();
  std::vector<std::shared_ptr<Stack>> concurrent_stacks;
  std::shared_ptr<Stack> current_stack = main_stack;
};
using Owner = std::shared_ptr<Payload>;
struct TypeContext {
  bool tracer;
  Value klass;
};
void destroy_type_context(void* data) { delete static_cast<TypeContext*>(data); }

Owner payload(const Value& self, bool tracer) {
  auto* owner = static_cast<Owner*>(instance_get_native_data(
      self, tracer ? kTracer : kDisposition));
  return owner == nullptr ? Owner{} : *owner;
}
bool fail(Runtime& runtime, const char* kind, std::string message,
          std::string& error) {
  error = std::move(message);
  runtime.raise_class_error(kind, error);
  return false;
}
bool is_true(const Value& v) { return v.tag == ValueTag::Bool && v.as.b; }
bool is_none(const Value& v) {
  return v.tag == ValueTag::None || v.tag == ValueTag::Invalid;
}
// A field is copied immediately before each callback, never borrowed through
// Python. A callback may reassign any public field or reinitialize the tracer.
bool call_field(Runtime& runtime, const Owner& state, Field field,
                const Value* args, uint32_t argc, Value& out,
                std::string& error) {
  Value function(state->fields[field]);
  return runtime_call_callable(runtime, function, args, argc, out, error);
}
bool get_attr(Runtime& runtime, const Value& value, const char* name,
              Value& out, std::string& error) {
  return runtime_getattr(runtime, value, Value::string(name), out, error);
}
bool call_method(Runtime& runtime, const Value& object, const char* name,
                 const Value* args, uint32_t argc, Value& out,
                 std::string& error) {
  Value object_owner(object), function;
  return get_attr(runtime, object_owner, name, function, error) &&
      runtime_call_callable(runtime, function, args, argc, out, error);
}
bool dict_lookup(Runtime& runtime, const Value& dictionary, const Value& key,
                 bool& found, Value& out, std::string& error) {
  Value owner(dictionary);
  if (value_as_dict(owner) == nullptr)
    return fail(runtime, "TypeError", "CTracer requires a dict field", error);
  found = mapping_get_item_runtime(runtime, owner, key, out, error, false);
  if (found) return true;
  if (error != "key not found") return false;
  // This primitive reports a missing exact-dict key without Python dispatch.
  error.clear();
  return true;
}
bool dict_store(Runtime& runtime, const Value& dictionary, const Value& key,
                const Value& value, std::string& error) {
  Value owner(dictionary);
  if (value_as_dict(owner) == nullptr)
    return fail(runtime, "TypeError", "CTracer requires a dict field", error);
  return mapping_set_item_runtime(runtime, owner, key, value, error);
}
void visit(const Value& v, NativeGCReferenceVisitor visitor, void* context) {
  if (v.tag == ValueTag::Object && v.as.obj != nullptr) visitor(v.as.obj, context);
}
void visit_stack(const std::shared_ptr<Stack>& stack,
                 NativeGCReferenceVisitor visitor, void* context) {
  if (!stack) return;
  for (const auto& entry : stack->entries) {
    visit(entry->file_data, visitor, context);
    visit(entry->file_tracer, visitor, context);
    visit(entry->disposition, visitor, context);
  }
}
void traverse(void* data, NativeGCReferenceVisitor visitor, void* context) {
  const auto& state = *static_cast<Owner*>(data);
  if (!state) return;
  const size_t count = state->tracer ? FieldCount : kDispositionFields.size();
  for (size_t i = 0; i < count; ++i) visit(state->fields[i], visitor, context);
  visit(state->context, visitor, context);
  visit(state->stack_index, visitor, context);
  visit_stack(state->main_stack, visitor, context);
  for (const auto& stack : state->concurrent_stacks) visit_stack(stack, visitor, context);
  // current_stack aliases main_stack or concurrent_stacks; count its Value
  // edges only once. The collector already traverses ordinary instance attrs.
}
void clear(void* data) {
  Owner state = *static_cast<Owner*>(data);
  if (!state) return;
  state->started.store(false, std::memory_order_release);
  // Publish the empty internal graph before retiring callback/file owners.
  std::array<Value, FieldCount> retired;
  for (size_t i = 0; i < FieldCount; ++i) {
    retired[i] = std::move(state->fields[i]);
    state->fields[i] = Value::none();
  }
  Value old_context = std::move(state->context);
  Value old_index = std::move(state->stack_index);
  auto old_main = std::move(state->main_stack);
  auto old_stacks = std::move(state->concurrent_stacks);
  state->context = Value::none();
  state->stack_index = Value::none();
  state->main_stack.reset();
  state->current_stack.reset();
}
void destroy(void* data) {
  auto* owner = static_cast<Owner*>(data);
  // Leave a valid, empty holder visible while payload field finalizers retire.
  // Reentry sees an uninitialized payload, never a deleted owner pointer.
  Owner retired = std::move(*owner);
  retired.reset();
  delete owner;
}

// Concurrency object ownership follows coverage's weak dictionary; native
// stacks contain only trace data, never a strong greenlet/task key reference.
bool select_stack(Runtime& runtime, const Owner& state,
                   std::shared_ptr<Stack>& selected, std::string& error) {
  if (is_none(state->fields[ConcurId])) {
    if (!state->main_stack) state->main_stack = std::make_shared<Stack>();
    selected = state->main_stack;
    state->current_stack = selected;
    return true;
  }
  if (is_none(state->stack_index)) {
    Value weakref, klass, dictionary;
    if (!runtime.import_module("weakref", weakref, error) ||
        !module_get_attr(weakref, "WeakKeyDictionary", klass, error) ||
        !runtime_call_callable(runtime, klass, nullptr, 0, dictionary, error)) return false;
    // Initialization callbacks may reenter. Keep a previously published index.
    if (is_none(state->stack_index)) state->stack_index = std::move(dictionary);
  }
  Value key, index;
  if (!call_field(runtime, state, ConcurId, nullptr, 0, key, error)) return false;
  Value dictionary(state->stack_index);
  if (!call_method(runtime, dictionary, "__getitem__", &key, 1, index, error)) {
    // CTracer_set_pdata_stack clears any lookup exception before assigning a
    // new weak index. A failed subsequent insertion still propagates normally.
    Value discarded_exception;
    runtime.take_pending_exception(discarded_exception);
    error.clear();
    const size_t number = state->concurrent_stacks.size();
    if (number > static_cast<size_t>(std::numeric_limits<int64_t>::max()))
      return fail(runtime, "OverflowError", "too many tracing stacks", error);
    auto stack = std::make_shared<Stack>();
    state->concurrent_stacks.push_back(stack);
    Value put[] = {key, Value::int64(static_cast<int64_t>(number))};
    Value ignored;
    if (!call_method(runtime, dictionary, "__setitem__", put, 2, ignored, error)) return false;
    index = put[1];
  }
  int64_t number = 0;
  if (!value_int_like_to_i64(index, number) || number < 0 ||
      static_cast<uint64_t>(number) >= state->concurrent_stacks.size())
    return fail(runtime, "RuntimeError", "invalid tracing concurrency index", error);
  selected = state->concurrent_stacks[static_cast<size_t>(number)];
  state->current_stack = selected;
  return true;
}
bool frame_number(Runtime& runtime, const Value& object, const char* name,
                   int64_t& number, std::string& error) {
  Value value;
  if (!get_attr(runtime, object, name, value, error)) return false;
  if (!value_int_like_to_i64(value, number))
    return fail(runtime, "TypeError", std::string(name) + " must be an integer", error);
  return true;
}
bool record_line(Runtime& runtime, const Owner& state,
                  const std::shared_ptr<Entry>& entry, int64_t line,
                  std::string& error) {
  Value set_owner(entry->file_data);
  Value datum;
  if (state->arcs) {
    // Collector.flush_data decodes sign bits40/41 and two20-bit magnitudes.
    const int64_t from = entry->last_line;
    uint64_t packed = (from < 0 ? uint64_t{1} << 40 : 0) |
        (line < 0 ? uint64_t{1} << 41 : 0);
    const uint64_t from_abs = from < 0 ? uint64_t(-(from + 1)) + 1 : uint64_t(from);
    const uint64_t to_abs = line < 0 ? uint64_t(-(line + 1)) + 1 : uint64_t(line);
    packed |= (to_abs << 20) + from_abs;
    if (packed > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return fail(runtime, "OverflowError", "packed trace arc exceeds Int64", error);
    datum = Value::int64(static_cast<int64_t>(packed));
  } else datum = Value::int64(line);
  return set_add_runtime(runtime, set_owner, datum, error);
}
// PyErr_Print semantics are expressed through original Python sys.excepthook,
// not a native reimplementation of traceback formatting. Policy exceptions
// are printed and consumed, unlike ordinary should_trace/context exceptions.
void print_and_clear(Runtime& runtime, const std::string& original_error) {
  Value exception;
  if (!runtime.take_pending_exception(exception))
    exception = runtime.make_exception("RuntimeError", original_error);
  Value traceback = Value::none(), sys, hook, ignored;
  std::string print_error;
  (void)object_get_attr(exception, "__traceback__", traceback, print_error);
  print_error.clear();
  Value args[] = {runtime.exception_type(exception), exception, traceback};
  const bool printed = runtime.import_module("sys", sys, print_error) &&
      module_get_attr(sys, "excepthook", hook, print_error) &&
      runtime_call_callable(runtime, hook, args, 3, ignored, print_error);
  if (!printed) {
    Value discarded;
    runtime.take_pending_exception(discarded);
    std::fputs((original_error + "\n").c_str(), stderr);
  }
}
void disable_plugin(Runtime& runtime, const Owner& state,
                      const Value& disposition, std::string& error) {
  const std::string original_error = error;
  error.clear();
  print_and_clear(runtime, original_error);
  Value ignored;
  if (!call_field(runtime, state, DisablePlugin, &disposition, 1, ignored, error)) {
    std::fputs("Error occurred while disabling plug-in:\n", stderr);
    print_and_clear(runtime, error);
  }
  error.clear();
}
bool handle_call(Runtime& runtime, const Value& self, const Owner& state,
                   const Value& frame, uint8_t flags, std::string& error) {
  std::shared_ptr<Stack> stack;
  if (!select_stack(runtime, state, stack, error)) return false;
  auto entry = std::make_shared<Entry>();
  stack->entries.push_back(entry); // Publish before all policy callbacks.
  if (!is_none(state->fields[StartContext]) && is_none(state->context)) {
    Value context;
    if (!call_field(runtime, state, StartContext, &frame, 1, context, error)) return false;
    if (!is_none(context)) {
      Value old_context = std::move(state->context);
      state->context = std::move(context);
      entry->started_context = true;
      Value context_arg(state->context), ignored;
      if (!call_field(runtime, state, SwitchContext, &context_arg, 1, ignored, error)) return false;
    }
  }
  Value code, filename, disposition;
  if (!get_attr(runtime, frame, "f_code", code, error) ||
      !get_attr(runtime, code, "co_filename", filename, error) ||
      !frame_number(runtime, code, "co_firstlineno", entry->first_line, error)) return false;
  bool found = false;
  Value cache(state->fields[TraceCache]);
  if (!dict_lookup(runtime, cache, filename, found, disposition, error)) return false;
  if (!found) {
    Value args[] = {filename, frame};
    if (!call_field(runtime, state, ShouldTrace, args, 2, disposition, error) ||
        !dict_store(runtime, state->fields[TraceCache], filename, disposition, error)) return false;
  }
  entry->disposition = disposition;
  Value trace, tracename = Value::none(), file_tracer = Value::none(), plugin_name;
  if (!is_none(disposition)) {
    if (!get_attr(runtime, disposition, "trace", trace, error)) return false;
    if (is_true(trace)) {
      Value dynamic;
      if (!get_attr(runtime, disposition, "source_filename", tracename, error) ||
          !get_attr(runtime, disposition, "file_tracer", file_tracer, error) ||
          !get_attr(runtime, disposition, "has_dynamic_filename", dynamic, error)) return false;
      if (!is_none(file_tracer)) {
        Value plugin;
        if (!get_attr(runtime, file_tracer, "_coverage_plugin", plugin, error) ||
            !get_attr(runtime, plugin, "_coverage_plugin_name", plugin_name, error)) return false;
      }
      if (is_true(dynamic)) {
        Value args[] = {tracename, frame}, next;
        if (!call_method(runtime, file_tracer, "dynamic_source_filename", args, 2, next, error)) {
          disable_plugin(runtime, state, disposition, error);
          return true;
        }
        tracename = std::move(next);
        if (!is_none(tracename)) {
          Value included;
          if (!dict_lookup(runtime, state->fields[TraceCache], tracename, found, included, error)) return false;
          if (!found) {
            Value args2[] = {tracename, frame};
            if (!call_field(runtime, state, CheckInclude, args2, 2, included, error)) return false;
            const bool include = is_true(included);
            if (!dict_store(runtime, state->fields[TraceCache], tracename,
                  include ? disposition : Value::none(), error)) return false;
            if (!include) tracename = Value::none();
          } else if (is_none(included)) tracename = Value::none();
        }
      }
    }
  }
  if (!is_none(tracename)) {
    Value file_data;
    if (!dict_lookup(runtime, state->fields[Data], tracename, found, file_data, error)) return false;
    if (!found) {
      file_data = Value::set({});
      if (!dict_store(runtime, state->fields[Data], tracename, file_data, error)) return false;
      if (!is_none(file_tracer) && !dict_store(runtime, state->fields[FileTracers],
                                              tracename, plugin_name, error)) return false;
    }
    entry->file_data = std::move(file_data);
    entry->file_tracer = std::move(file_tracer);
  } else if (auto* visible = value_as_frame(frame)) visible->trace_lines = false;
  if (auto* visible = value_as_frame(frame)) value_assign_fast(visible->trace, self);
  if ((flags & kNativeTraceResume) == 0) entry->last_line = -entry->first_line;
  else if (!frame_number(runtime, frame, "f_lineno", entry->last_line, error)) return false;
  return true;
}
bool handle_line(Runtime& runtime, const Owner& state,
                   const Value& frame, std::string& error) {
  auto stack = state->current_stack;
  if (!stack || stack->entries.empty()) return true;
  auto entry = stack->entries.back();
  if (is_none(entry->file_data)) return true;
  int64_t from = -1, to = -1;
  if (!is_none(entry->file_tracer)) {
    Value range;
    if (!call_method(runtime, entry->file_tracer, "line_number_range", &frame, 1, range, error)) {
      disable_plugin(runtime, state, entry->disposition, error);
      return true;
    }
    auto* pair = value_as_tuple(range);
    if (pair == nullptr || pair->items.size() != 2) {
      fail(runtime, "TypeError", "line_number_range must return 2-tuple", error);
      disable_plugin(runtime, state, entry->disposition, error);
      return true;
    }
    if (!value_int_like_to_i64(pair->items[0], from) ||
        !value_int_like_to_i64(pair->items[1], to) ||
        from < std::numeric_limits<int>::min() || from > std::numeric_limits<int>::max() ||
        to < std::numeric_limits<int>::min() || to > std::numeric_limits<int>::max()) {
      fail(runtime, "TypeError", "line_number_range must contain C int values", error);
      disable_plugin(runtime, state, entry->disposition, error);
      return true;
    }
  } else if (!frame_number(runtime, frame, "f_lineno", from, error)) return false;
  else to = from;
  if (from == -1) return true;
  for (; from <= to; ++from) {
    if (!record_line(runtime, state, entry, from, error)) return false;
    entry->last_line = from;
  }
  return true;
}
bool handle_return(Runtime& runtime, const Owner& state,
                     uint8_t flags, std::string& error) {
  std::shared_ptr<Stack> stack;
  if (!select_stack(runtime, state, stack, error)) return false;
  if (stack->entries.empty()) return true;
  auto entry = stack->entries.back();
  if (state->arcs && !is_none(entry->file_data) &&
      (flags & kNativeTraceSuspended) == 0 &&
      !record_line(runtime, state, entry, -entry->first_line, error)) return false;
  if (entry->started_context) {
    Value old_context = std::move(state->context);
    state->context = Value::none();
    Value arg = Value::none(), ignored;
    if (!call_field(runtime, state, SwitchContext, &arg, 1, ignored, error)) return false;
  }
  // Callback reentry may have added another entry. Never pop a different one.
  if (!stack->entries.empty() && stack->entries.back() == entry) stack->entries.pop_back();
  else return fail(runtime, "RuntimeError", "tracer stack changed during return", error);
  return true;
}
bool deliver(Runtime& runtime, const Value& self, const Value* args,
               uint32_t argc, uint8_t flags, Value& out, std::string& error) {
  XlangRuntimeExecutionGuard execution_guard;
  Value self_owner(self);
  Owner state = payload(self_owner, true);
  if (!state) return fail(runtime, "TypeError", "uninitialized CTracer", error);
  if (argc != 3 || value_as_frame(args[0]) == nullptr || value_as_string(args[1]) == nullptr)
    return fail(runtime, "TypeError", "CTracer needs frame, event, arg", error);
  Value frame(args[0]), event(args[1]), arg(args[2]);
  const std::string name = string_object_to_string(*value_as_string(event));
  if (!state->started.load(std::memory_order_acquire)) {
    // Only this event's thread uninstalls its trace hook. stop() itself never
    // changes the stopping thread's hook or another thread's TLS state.
    runtime.set_trace_function(Value::none());
    value_assign_fast(out, self_owner);
    return true;
  }
  state->activity.store(true, std::memory_order_relaxed);
  bool ok = true;
  if (name == "call") ok = handle_call(runtime, self_owner, state, frame, flags, error);
  else if (name == "line") ok = handle_line(runtime, state, frame, error);
  else if (name == "return") ok = handle_return(runtime, state, flags, error);
  // exception/opcode/native events are intentionally ignored, as CTracer_trace.
  if (ok) value_assign_fast(out, self_owner);
  return ok;
}
uint8_t manual_flags(const Value& frame, const std::string& event) {
  auto* visible = value_as_frame(frame);
  if (visible == nullptr || !visible->module ||
      visible->function_id >= visible->module->functions.size()) return 0;
  const auto& fn = visible->module->functions[visible->function_id];
  const size_t ip = visible->instruction_index;
  if (event == "call" && ip != 0 && (fn.is_generator || fn.is_coroutine || fn.is_async))
    return kNativeTraceResume;
  if (event == "return" && ip < fn.code.size()) {
    const auto op = fn.code[ip].op;
    if (op == ir::Op::Yield || op == ir::Op::YieldFrom || op == ir::Op::Await)
      return kNativeTraceSuspended;
  }
  return 0;
}
bool tracer_call(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  XlangRuntimeExecutionGuard execution_guard;
  if (argc != 4 || value_as_frame(args[1]) == nullptr || value_as_string(args[2]) == nullptr)
    return fail(runtime, "TypeError", "CTracer needs frame, event, arg", error);
  Value self(args[0]), event(args[2]);
  const std::string name = string_object_to_string(*value_as_string(event));
  const bool ok = deliver(runtime, self, args + 1, 3,
                          manual_flags(args[1], name), out, error);
  // Manual __call__ rearms only on call. Direct delivery never rearms, even
  // if should_trace installed another tracer. This is why a bridge is needed.
  if (name == "call") runtime.set_native_trace_function(self);
  return ok;
}
bool tracer_call_kw(Runtime& runtime, const Value* args, uint32_t argc,
                      const NativeKeywordArg* kwargs, uint32_t kwargc,
                      Value& out, std::string& error, void* data) {
  XlangRuntimeExecutionGuard execution_guard;
  if (argc < 1 || argc > 4)
    return fail(runtime, "TypeError", "CTracer needs frame, event, arg", error);
  std::array<Value, 4> values;
  std::array<bool, 4> assigned{};
  for (uint32_t i = 0; i < argc; ++i) { values[i] = args[i]; assigned[i] = true; }
  for (uint32_t i = 0; i < kwargc; ++i) {
    size_t slot = 0;
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name == "frame") slot = 1;
    else if (name == "event") slot = 2;
    else if (name == "arg") slot = 3;
    else return fail(runtime, "TypeError", "unexpected CTracer keyword: " + name, error);
    if (assigned[slot]) return fail(runtime, "TypeError", "duplicate CTracer argument: " + name, error);
    values[slot] = *kwargs[i].value;
    assigned[slot] = true;
  }
  if (!assigned[1] || !assigned[2] || !assigned[3])
    return fail(runtime, "TypeError", "CTracer needs frame, event, arg", error);
  return tracer_call(runtime, values.data(), 4, out, error, data);
}
bool field_get(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* token) {
  XlangRuntimeExecutionGuard execution_guard;
  const size_t index = reinterpret_cast<uintptr_t>(token) & 15;
  const bool tracer = (reinterpret_cast<uintptr_t>(token) & 16) != 0;
  if (argc != 1) return fail(runtime, "TypeError", "member getter needs self", error);
  Value self(args[0]);
  auto state = payload(self, tracer);
  if (!state) return fail(runtime, "TypeError", "invalid native member receiver", error);
  Value selected(state->fields[index]);
  value_assign_fast(out, selected);
  return true;
}
bool field_set(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* token) {
  XlangRuntimeExecutionGuard execution_guard;
  const size_t index = reinterpret_cast<uintptr_t>(token) & 15;
  const bool tracer = (reinterpret_cast<uintptr_t>(token) & 16) != 0;
  if (argc != 2) return fail(runtime, "TypeError", "member setter needs self, value", error);
  Value self(args[0]);
  auto state = payload(self, tracer);
  if (!state) return fail(runtime, "TypeError", "invalid native member receiver", error);
  Value incoming(args[1]), retired = std::move(state->fields[index]);
  state->fields[index] = std::move(incoming); // Publish before old value finalizers.
  value_set_none(out);
  return true;
}
bool field_delete(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void* token) {
  XlangRuntimeExecutionGuard execution_guard;
  if (argc != 1) return fail(runtime, "TypeError", "member delete needs self", error);
  Value values[] = {args[0], Value::none()};
  return field_set(runtime, values, 2, out, error, token);
}
bool make_instance(Runtime& runtime, const Value* args, uint32_t argc,
                     Value& out, std::string& error, void* token) {
  XlangRuntimeExecutionGuard execution_guard;
  auto& type = *static_cast<TypeContext*>(token);
  const bool tracer = type.tracer;
  if (argc < 1 || value_as_class(args[0]) == nullptr)
    return fail(runtime, "TypeError", "native tracer __new__ requires a type", error);
  Value requested_type(args[0]), expected_type(type.klass);
  if (!class_is_subclass(value_as_class(requested_type), value_as_class(expected_type)))
    return fail(runtime, "TypeError", "native tracer __new__ requires a subtype", error);
  Value instance = Value::instance(requested_type);
  auto owner = std::make_unique<Owner>(std::make_shared<Payload>(tracer));
  if (!instance_set_native_data(instance, tracer ? kTracer : kDisposition,
                                owner.get(), destroy, error)) return false;
  owner.release();
  if (!instance_set_native_gc_traversal(instance, traverse, clear, error)) return false;
  if (tracer && !instance_set_native_trace_hook(instance, deliver, error)) return false;
  out = std::move(instance);
  return true;
}
bool make_instance_kw(Runtime& runtime, const Value* args, uint32_t argc,
                        const NativeKeywordArg*, uint32_t,
                        Value& out, std::string& error, void* token) {
  XlangRuntimeExecutionGuard execution_guard;
  return make_instance(runtime, args, argc, out, error, token);
}
bool initialize(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void* token) {
  XlangRuntimeExecutionGuard execution_guard;
  const bool tracer = token != nullptr;
  if (argc < 1 || (!tracer && argc != 1))
    return fail(runtime, "TypeError", "CFileDisposition takes no arguments", error);
  auto state = payload(args[0], tracer);
  if (!state) return fail(runtime, "TypeError", "uninitialized native tracer", error);
  if (tracer) {
    // Same attachment for the entire instance lifetime; replacing a payload
    // during __init__ would retire a state still used by a native callback.
    Value previous = std::move(state->context);
    state->context = Value::none();
    auto replacement = std::make_shared<Stack>();
    auto old_main = std::move(state->main_stack);
    state->main_stack = std::move(replacement);
    state->current_stack = state->main_stack;
  }
  value_set_none(out);
  return true;
}
bool initialize_kw(Runtime& runtime, const Value* args, uint32_t argc,
                     const NativeKeywordArg*, uint32_t kwargc,
                     Value& out, std::string& error, void* token) {
  XlangRuntimeExecutionGuard execution_guard;
  if (token == nullptr && kwargc != 0)
    return fail(runtime, "TypeError", "CFileDisposition takes no keyword arguments", error);
  return initialize(runtime, args, argc, out, error, token);
}
bool tracer_method(Runtime& runtime, const Value* args, uint32_t argc,
                     Value& out, std::string& error, void* operation) {
  XlangRuntimeExecutionGuard execution_guard;
  if (argc < 1) return fail(runtime, "TypeError", "CTracer method needs self", error);
  Value self(args[0]);
  auto state = payload(self, true);
  if (!state) return fail(runtime, "TypeError", "uninitialized CTracer", error);
  switch (reinterpret_cast<uintptr_t>(operation)) {
    case 0: {
      runtime.set_native_trace_function(self);
      state->started.store(true, std::memory_order_release);
      Value arcs(state->fields[TraceArcs]);
      bool tracing = false;
      if (!runtime_truthy(runtime, arcs, tracing, error)) return false;
      state->arcs = tracing;
      value_assign_fast(out, self);
      return true;
    }
    case 1: state->started.store(false, std::memory_order_release); break;
    case 2: value_set_bool(out, state->activity.load(std::memory_order_relaxed)); return true;
    case 3: state->activity.store(false, std::memory_order_relaxed); break;
    case 4: break; // Native coverage build without COLLECT_STATS returns None.
  }
  value_set_none(out);
  return true;
}
bool tracer_method_kw(Runtime& runtime, const Value* args, uint32_t argc,
                         const NativeKeywordArg*, uint32_t kwargc,
                         Value& out, std::string& error, void* operation) {
  XlangRuntimeExecutionGuard execution_guard;
  static constexpr const char* names[] = {"start", "stop", "activity", "reset_activity", "get_stats"};
  if (kwargc != 0)
    return fail(runtime, "TypeError", std::string(names[reinterpret_cast<uintptr_t>(operation)]) +
        "() takes no keyword arguments", error);
  return tracer_method(runtime, args, argc, out, error, operation);
}
Value make_type(Runtime& runtime, bool tracer) {
  const char* name = tracer ? "CTracer" : "CFileDispositionType";
  const std::string prefix = std::string("coverage.") + name;
  void* kind = tracer ? reinterpret_cast<void*>(uintptr_t{1}) : nullptr;
  auto type_context = std::make_unique<TypeContext>();
  type_context->tracer = tracer;
  std::vector<std::pair<std::string, Value>> attrs = {
    {"__module__", Value::string("coverage")},
    {"__slots__", Value::tuple({})},
    {"__new__", runtime.make_native_function(prefix + ".__new__", make_instance,
        type_context.get(), nullptr, nullptr, false, make_instance_kw)},
    {"__init__", runtime.make_native_function(prefix + ".__init__", initialize,
        kind, nullptr, nullptr, false, initialize_kw)}
  };
  if (tracer) {
    attrs.emplace_back("__call__", runtime.make_native_function(prefix + ".__call__",
        tracer_call, nullptr, nullptr, nullptr, false, tracer_call_kw));
    const char* methods[] = {"start", "stop", "activity", "reset_activity", "get_stats"};
    for (uintptr_t i = 0; i < 5; ++i)
      attrs.emplace_back(methods[i], runtime.make_native_function(prefix + "." + methods[i],
          tracer_method, reinterpret_cast<void*>(i), nullptr, nullptr, false, tracer_method_kw));
  }
  const size_t count = tracer ? FieldCount : kDispositionFields.size();
  for (size_t i = 0; i < count; ++i) {
    const std::string member = tracer ? kFields[i] : kDispositionFields[i];
    void* token = reinterpret_cast<void*>(uintptr_t(i) | (tracer ? 16 : 0));
    attrs.emplace_back(member, Value::property(
        runtime.make_native_function(prefix + "." + member + ".get", field_get, token),
        runtime.make_native_function(prefix + "." + member + ".set", field_set, token),
        runtime.make_native_function(prefix + "." + member + ".delete", field_delete, token),
        Value::none()));
  }
  const Value* object = runtime.find_builtin("object");
  Value klass = Value::class_object(name, std::move(attrs),
      object == nullptr ? Value::invalid() : *object);
  type_context->klass = klass;
  runtime.register_native_package_cleanup(type_context.get(), destroy_type_context);
  type_context.release();
  return klass;
}
} // namespace

void register_coverage_tracer_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "coverage.tracer");
  builder.value("CTracer", make_type(runtime, true))
      .value("CFileDisposition", make_type(runtime, false));
  runtime.register_module("coverage.tracer", builder.finish());
}
} // namespace xlang3
