/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0.
*/
#include "asyncio_native.h"

#include "xlang3/builtin_methods.h"
#include "xlang3/interpreter.h"
#include "xlang3/set_object.h"

#include <algorithm>
#include <cstddef>

namespace xlang3::asyncio_native {
namespace {

bool arity(Runtime& runtime, uint32_t count, uint32_t minimum,
           uint32_t maximum, const char* message, std::string& error) {
  return (count >= minimum && count <= maximum) ||
      raise(runtime, "TypeError", message, error);
}

void cleanup_future(void* payload) {
  auto* fs = static_cast<FutureState*>(payload);
  unregister(*fs);
  delete fs;
}

bool schedule_one(Runtime& runtime, const Value& loop, const Value& self,
                   const Callback& callback, std::string& error) {
  Value args[] = {callback.function, self};
  Value ignored;
  return call_method(runtime, loop, "call_soon", args, 2,
                     {{"context", callback.context}}, ignored, error);
}

bool schedule_callbacks(Runtime& runtime, const Value& self, FutureState& fs,
                         std::string& error) {
  unregister(fs);
  // CPython detaches the inline callback first. Additional callbacks stay
  // visible to a reentrant call_soon until that first scheduling call returns.
  // Removing them there must prevent subsequent scheduling. Once the extra
  // list is detached, later reentrant removals cannot mutate its snapshot.
  if (fs.has_first_callback) {
    Callback first = std::move(fs.first_callback);
    fs.first_callback = {};
    fs.has_first_callback = false;
    if (!publish_edges(self, fs, error)) return false;
    if (!schedule_one(runtime, fs.loop, self, first, error)) {
      std::vector<Callback> retired = std::move(fs.additional_callbacks);
      if (!publish_edges(self, fs, error)) return false;
      return false;
    }
    // Drop local ownership before observing the remaining callbacks, matching
    // finalizer reentrancy when a custom loop does not retain the callback.
    first = {};
  }
  std::vector<Callback> remaining = std::move(fs.additional_callbacks);
  fs.additional_callbacks.clear();
  if (!publish_edges(self, fs, error)) return false;
  for (const auto& callback : remaining) {
    if (!schedule_one(runtime, fs.loop, self, callback, error)) return false;
  }
  return true;
}

bool future_new_kw(Runtime& runtime, const Value* args, uint32_t argc,
                   const NativeKeywordArg*, uint32_t, Value& out,
                   std::string& error, void* native_future_data) {
  if (argc == 0 || value_as_class(args[0]) == nullptr)
    return raise(runtime, "TypeError", "Future.__new__ requires a type", error);
  auto* native_future = static_cast<ClassObject*>(native_future_data);
  Value imported_native_future;
  if (native_future == nullptr) {
    if (!import_attribute(runtime, "_asyncio", "Future", imported_native_future, error)) return false;
    native_future = value_as_class(imported_native_future);
  }
  if (native_future == nullptr ||
      !class_is_subclass(value_as_class(args[0]), native_future))
    return raise(runtime, "TypeError", "type is not a subtype of Future", error);
  out = Value::instance(args[0]);
  auto* fs = new FutureState();
  fs->owner = value_as_instance(out);
  if (!instance_set_native_data(out, kFutureData, fs, cleanup_future, error)) {
    delete fs;
    return false;
  }
  return true;
}

bool future_new(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void* data) {
  return future_new_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool future_init_kw(Runtime& runtime, const Value* args, uint32_t argc,
                    const NativeKeywordArg* kwargs, uint32_t kwargc,
                    Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "Future() takes no positional arguments", error)) return false;
  Value loop = Value::none();
  bool supplied = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || std::string_view(kwargs[i].name) != "loop" || supplied)
      return raise(runtime, "TypeError", "Future() received an invalid keyword", error);
    supplied = true;
    loop = *kwargs[i].value;
  }
  auto* fs = state(runtime, args[0], error, false);
  if (fs == nullptr || !initialize_future(runtime, args[0], *fs, loop, error)) return false;
  value_set_none(out);
  return true;
}

bool future_init(Runtime& runtime, const Value* args, uint32_t argc,
                 Value& out, std::string& error, void* data) {
  return future_init_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool construct_future_direct(Runtime& runtime, const ClassObject& klass,
                             const CallArgsView& args, bool& handled,
                             Value& out, std::string& error) {
  handled = false;
  // The pyperformance asyncio tree repeatedly constructs the exact native
  // Future with no arguments. Preserve the generic path for explicit loop
  // keywords, expansions, subclass construction, and any patched class.
  if (args.size() != 0 || args.has_keywords() || args.has_expansion()) {
    return true;
  }

  handled = true;
  Value type_value;
  type_value.tag = ValueTag::Object;
  type_value.flags = kXlangValueBorrowedRefFlag;
  type_value.as.obj = const_cast<Object*>(&klass.header);
  out = Value::instance(std::move(type_value));

  auto* fs = new FutureState();
  fs->owner = value_as_instance(out);
  if (!instance_set_native_data(out, kFutureData, fs, cleanup_future, error)) {
    delete fs;
    return false;
  }
  return initialize_future(runtime, out, *fs, Value::none(), error);
}

bool get_result(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "result() takes no arguments", error)) return false;
  auto* fs = state(runtime, args[0], error);
  return fs != nullptr && future_result(runtime, args[0], *fs, out, error);
}

bool get_exception(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "exception() takes no arguments", error)) return false;
  auto* fs = state(runtime, args[0], error);
  if (fs == nullptr) return false;
  if (fs->completion == Completion::Cancelled) {
    Value exception;
    if (!cancelled_error(runtime, args[0], *fs, exception, error)) return false;
    runtime.set_pending_exception(std::move(exception));
    return false;
  }
  if (fs->completion != Completion::Finished)
    return raise(runtime, "InvalidStateError", "Exception is not set.", error);
  fs->log_traceback = false;
  out = fs->exception;
  return true;
}

bool get_done(Runtime& runtime, const Value* args, uint32_t argc,
              Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "done() takes no arguments", error)) return false;
  auto* fs = static_cast<FutureState*>(instance_get_native_data(args[0], kFutureData));
  out = Value::boolean(fs != nullptr && fs->loop.tag != ValueTag::None &&
                        fs->completion != Completion::Pending);
  return true;
}

bool get_cancelled(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "cancelled() takes no arguments", error)) return false;
  auto* fs = static_cast<FutureState*>(instance_get_native_data(args[0], kFutureData));
  out = Value::boolean(fs != nullptr && fs->loop.tag != ValueTag::None &&
                        fs->completion == Completion::Cancelled);
  return true;
}

bool get_loop(Runtime& runtime, const Value* args, uint32_t argc,
              Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "get_loop() takes no arguments", error)) return false;
  auto* fs = state(runtime, args[0], error);
  if (fs == nullptr) return false;
  out = fs->loop;
  return true;
}

bool make_cancelled_error(Runtime& runtime, const Value* args, uint32_t argc,
                          Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "_make_cancelled_error() takes no arguments", error)) return false;
  auto* fs = state(runtime, args[0], error);
  return fs != nullptr && cancelled_error(runtime, args[0], *fs, out, error);
}

bool set_result(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 2, 2, "set_result() requires one argument", error)) return false;
  auto* fs = state(runtime, args[0], error);
  if (fs == nullptr || !finish_result(runtime, args[0], *fs, args[1], error)) return false;
  value_set_none(out);
  return true;
}

bool set_exception(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 2, 2, "set_exception() requires one argument", error)) return false;
  auto* fs = state(runtime, args[0], error);
  if (fs == nullptr || !finish_exception(runtime, args[0], *fs, args[1], error)) return false;
  value_set_none(out);
  return true;
}

bool cancel_kw(Runtime& runtime, const Value* args, uint32_t argc,
               const NativeKeywordArg* kwargs, uint32_t kwargc,
               Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 2, "cancel() accepts one optional message", error)) return false;
  Value message = argc == 2 ? args[1] : Value::none();
  bool supplied = argc == 2;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || std::string_view(kwargs[i].name) != "msg" || supplied)
      return raise(runtime, "TypeError", "cancel() received an invalid keyword", error);
    supplied = true;
    message = *kwargs[i].value;
  }
  auto* fs = state(runtime, args[0], error);
  bool changed = false;
  if (fs == nullptr || !cancel_future(runtime, args[0], *fs, message, changed, error)) return false;
  out = Value::boolean(changed);
  return true;
}

bool cancel(Runtime& runtime, const Value* args, uint32_t argc,
             Value& out, std::string& error, void* data) {
  return cancel_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool add_callback_kw(Runtime& runtime, const Value* args, uint32_t argc,
                     const NativeKeywordArg* kwargs, uint32_t kwargc,
                     Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 2, 2, "add_done_callback() requires one callback", error)) return false;
  Value context = Value::none();
  bool supplied = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || std::string_view(kwargs[i].name) != "context" || supplied)
      return raise(runtime, "TypeError", "add_done_callback() received an invalid keyword", error);
    supplied = true;
    context = *kwargs[i].value;
  }
  auto* fs = state(runtime, args[0], error);
  if (fs == nullptr) return false;
  // When omitted, capture even for an already finished Future, before loop code can
  // change the current context. Completion only queues callbacks; never invoke
  // them inline or change their registration order to save dispatch cost.
  // Explicit context=None remains None for call_soon's later context capture.
  if (!supplied && !copy_context(runtime, context, error)) return false;
  if (!add_native_callback(runtime, args[0], *fs, {args[1], context}, error)) return false;
  value_set_none(out);
  return true;
}

bool add_callback(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void* data) {
  return add_callback_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool equal_callback(Runtime& runtime, const Value& callback, const Value& target,
                     bool& equal, std::string& error) {
  if (value_is(callback, target)) { equal = true; return true; }
  Value result;
  return runtime_value_compare(runtime, "==", callback, target, result, error) &&
      runtime_truthy(runtime, result, equal, error);
}

bool remove_callback(Runtime& runtime, const Value* args, uint32_t argc,
                      Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 2, 2, "remove_done_callback() requires one callback", error)) return false;
  auto* fs = state(runtime, args[0], error);
  if (fs == nullptr) return false;
  int64_t removed_first = 0;
  if (fs->has_first_callback) {
    // Equality can remove callbacks or complete the Future recursively. Keep
    // each compared function/context alive; never borrow a vector element
    // across a Python equality call that can resize or clear the vector.
    Callback first = fs->first_callback;
    bool equal = false;
    if (!equal_callback(runtime, first.function, args[1], equal, error)) return false;
    if (equal) {
      Callback retired = std::move(fs->first_callback);
      fs->first_callback = {};
      fs->has_first_callback = false;
      removed_first = 1;
      if (!publish_edges(args[0], *fs, error)) return false;
    }
  }
  const size_t original_count = fs->additional_callbacks.size();
  if (original_count == 1) {
    Callback callback = fs->additional_callbacks.front();
    bool equal = false;
    if (!equal_callback(runtime, callback.function, args[1], equal, error)) return false;
    std::vector<Callback> retired;
    if (equal) retired = std::move(fs->additional_callbacks);
    out = Value::int64(removed_first + (equal ? 1 : 0));
    return publish_edges(args[0], *fs, error);
  }
  std::vector<Callback> filtered;
  filtered.reserve(original_count);
  for (size_t index = 0; index < fs->additional_callbacks.size(); ++index) {
    Callback callback = fs->additional_callbacks[index];
    bool equal = false;
    if (!equal_callback(runtime, callback.function, args[1], equal, error)) return false;
    if (!equal) filtered.push_back(std::move(callback));
  }
  if (filtered.empty() || fs->additional_callbacks.empty()) {
    std::vector<Callback> retired = std::move(fs->additional_callbacks);
    out = Value::int64(static_cast<int64_t>(original_count) + removed_first);
    return publish_edges(args[0], *fs, error);
  } else {
    const int64_t removed = static_cast<int64_t>(fs->additional_callbacks.size()) -
        static_cast<int64_t>(filtered.size());
    std::vector<Callback> retired;
    if (removed != 0) {
      retired = std::move(fs->additional_callbacks);
      fs->additional_callbacks = std::move(filtered);
    }
    out = Value::int64(removed + removed_first);
    return publish_edges(args[0], *fs, error);
  }
}

enum class Field : intptr_t {
  State, Loop, Result, Exception, Callbacks, SourceTraceback, CancelMessage,
  LogTraceback, Blocking, AwaitedBy
};

bool field_get(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void* data) {
  if (!arity(runtime, argc, 1, 1, "Future field getter requires self", error)) return false;
  auto* fs = state(runtime, args[0], error, false);
  if (fs == nullptr) return false;
  const auto field = static_cast<Field>(reinterpret_cast<intptr_t>(data));
  if (fs->loop.tag == ValueTag::None &&
      (field == Field::State || field == Field::Result ||
       field == Field::Exception || field == Field::LogTraceback))
    return raise(runtime, "RuntimeError", "Future object is not initialized.", error);
  switch (field) {
    case Field::State:
      out = Value::string(fs->completion == Completion::Pending ? "PENDING" :
                          fs->completion == Completion::Cancelled ? "CANCELLED" : "FINISHED");
      break;
    case Field::Loop: out = fs->loop; break;
    case Field::Result: out = fs->result; break;
    case Field::Exception: out = fs->exception; break;
    case Field::SourceTraceback: out = fs->source_traceback; break;
    case Field::CancelMessage: out = fs->cancel_message; break;
    case Field::LogTraceback: out = Value::boolean(fs->log_traceback); break;
    case Field::Blocking:
      out = Value::boolean(fs->loop.tag != ValueTag::None && fs->blocking);
      break;
    case Field::AwaitedBy:
      if (fs->awaited_by.tag == ValueTag::None) out = Value::none();
      else {
        out = fs->awaited_by_is_set
            ? Value::frozenset(value_as_set(fs->awaited_by)->items)
            : Value::frozenset({fs->awaited_by});
      }
      break;
    case Field::Callbacks: {
      if (fs->loop.tag == ValueTag::None)
        return raise(runtime, "RuntimeError", "Future object is not initialized.", error);
      if (!fs->has_first_callback && fs->additional_callbacks.empty()) {
        out = Value::none();
        break;
      }
      std::vector<Value> callbacks;
      callbacks.reserve(fs->additional_callbacks.size() + fs->has_first_callback);
      if (fs->has_first_callback)
        callbacks.push_back(Value::tuple({fs->first_callback.function, fs->first_callback.context}));
      for (const auto& callback : fs->additional_callbacks)
        callbacks.push_back(Value::tuple({callback.function, callback.context}));
      out = Value::list(std::move(callbacks));
      break;
    }
  }
  return true;
}

bool field_set(Runtime& runtime, const Value* args, uint32_t argc,
                Value& out, std::string& error, void* data) {
  if (!arity(runtime, argc, 2, 2, "Future field setter requires a value", error)) return false;
  auto* fs = state(runtime, args[0], error, false);
  if (fs == nullptr) return false;
  const auto field = static_cast<Field>(reinterpret_cast<intptr_t>(data));
  if (field == Field::CancelMessage) {
    Value previous = std::move(fs->cancel_message);
    fs->cancel_message = args[1];
    if (!publish_edges(args[0], *fs, error)) return false;
  } else {
    bool value = false;
    if (!runtime_truthy(runtime, args[1], value, error)) return false;
    if (field == Field::LogTraceback) {
      if (value) return raise(runtime, "ValueError", "_log_traceback can only be set to False", error);
      fs->log_traceback = false;
    } else if (field == Field::Blocking) fs->blocking = value;
    else return raise(runtime, "AttributeError", "Future field is read-only", error);
  }
  value_set_none(out);
  return true;
}

bool future_repr(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "Future.__repr__ requires self", error)) return false;
  Value function;
  return helper(runtime, "asyncio.base_futures", "_future_repr", function, error) &&
      runtime_call_callable(runtime, function, args, 1, out, error);
}

bool future_finalize(Runtime& runtime, const Value* args, uint32_t argc,
                       Value& out, std::string& error, void*) {
  value_set_none(out);
  if (argc != 1) return true;
  auto* fs = static_cast<FutureState*>(instance_get_native_data(args[0], kFutureData));
  if (fs == nullptr || !fs->log_traceback || fs->loop.tag == ValueTag::None) return true;
  fs->log_traceback = false;
  Value type_name;
  if (!object_get_attr(value_as_instance(args[0])->klass, "__name__", type_name, error)) return false;
  std::vector<std::pair<Value, Value>> items{
      {Value::string("message"), Value::string(value_to_string(type_name) + " exception was never retrieved")},
      {Value::string("exception"), fs->exception},
      {Value::string("future"), args[0]}};
  if (fs->source_traceback.tag != ValueTag::None)
    items.emplace_back(Value::string("source_traceback"), fs->source_traceback);
  Value context = Value::dict(std::move(items));
  return call_method(runtime, fs->loop, "call_exception_handler", &context, 1, {}, out, error);
}

bool generic_alias(Runtime& runtime, const Value* args, uint32_t argc,
                     Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 2, 2, "__class_getitem__ requires one argument", error)) return false;
  Value parameters = value_as_tuple(args[1]) == nullptr ? Value::tuple({args[1]}) : args[1];
  out = Value::generic_alias(args[0], std::move(parameters));
  return true;
}

void iterator_clear(void* payload) {
  value_set_none(static_cast<AwaitState*>(payload)->future);
}

void iterator_cleanup(void* payload) { delete static_cast<AwaitState*>(payload); }

bool iterator_release(const Value& self, AwaitState& iterator, std::string& error) {
  Value retired = std::move(iterator.future);
  iterator.future = Value::none();
  return instance_set_native_gc_references(self, nullptr, 0, iterator_clear, error);
}

bool future_await(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "Future.__await__ takes no arguments", error)) return false;
  if (state(runtime, args[0], error) == nullptr) return false;
  Value klass;
  if (!import_attribute(runtime, "_asyncio", "_FutureIter", klass, error)) return false;
  out = Value::instance(klass);
  auto* iterator = new AwaitState{args[0]};
  if (!instance_set_native_data(out, kIteratorData, iterator, iterator_cleanup, error)) {
    delete iterator;
    return false;
  }
  return instance_set_native_gc_references(out, &iterator->future, 1, iterator_clear, error);
}

bool iterator_iter(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "FutureIter.__iter__ takes no arguments", error)) return false;
  out = args[0];
  return true;
}

bool iterator_next(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "FutureIter.__next__ takes no arguments", error)) return false;
  auto* iterator = static_cast<AwaitState*>(instance_get_native_data(args[0], kIteratorData));
  if (iterator == nullptr) return raise(runtime, "RuntimeError", "invalid Future iterator", error);
  if (iterator->future.tag == ValueTag::None)
    return raise(runtime, "StopIteration", "", error);
  auto* fs = state(runtime, iterator->future, error);
  if (fs == nullptr) return false;
  if (fs->completion == Completion::Pending) {
    if (fs->blocking) return raise(runtime, "RuntimeError", "await wasn't used with future", error);
    fs->blocking = true;
    out = iterator->future;
    return true;
  }
  // Read native state directly, matching CPython's Future iterator. Calling
  // overridden done()/result() methods here changes subclass semantics and
  // allocates the Python fallback's generator frame on every await.
  Value result;
  if (!future_result(runtime, iterator->future, *fs, result, error)) return false;
  const Value* stop_iteration = runtime.find_builtin("StopIteration");
  Value exception;
  if (stop_iteration == nullptr ||
      !runtime_call_callable(runtime, *stop_iteration, &result, 1, exception, error)) return false;
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool iterator_send(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void* data) {
  if (!arity(runtime, argc, 2, 2, "FutureIter.send requires one argument", error)) return false;
  return iterator_next(runtime, args, 1, out, error, data);
}

bool iterator_close(Runtime& runtime, const Value* args, uint32_t argc,
                     Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 1, 1, "FutureIter.close takes no arguments", error)) return false;
  auto* iterator = static_cast<AwaitState*>(instance_get_native_data(args[0], kIteratorData));
  if (iterator == nullptr) return raise(runtime, "RuntimeError", "invalid Future iterator", error);
  value_set_none(out);
  return iterator_release(args[0], *iterator, error);
}

bool iterator_throw(Runtime& runtime, const Value* args, uint32_t argc,
                     Value& out, std::string& error, void*) {
  if (!arity(runtime, argc, 2, 4, "throw requires one to three arguments", error)) return false;
  if (argc > 2) {
    Value warn;
    if (!helper(runtime, "warnings", "warn", warn, error)) return false;
    Value warning_args[] = {Value::string("the (type, exc, tb) signature of throw() is deprecated, use the single-arg signature instead."),
                            *runtime.find_builtin("DeprecationWarning")};
    Value ignored;
    if (!runtime_call_callable_kw(runtime, warn, warning_args, 2,
                                   {{"stacklevel", Value::int64(2)}}, ignored, error)) return false;
  }
  const Value traceback = argc == 4 ? args[3] : Value::none();
  if (traceback.tag != ValueTag::None && value_as_traceback(traceback) == nullptr)
    return raise(runtime, "TypeError", "throw() third argument must be a traceback", error);
  Value exception;
  if (auto* klass = value_as_class(args[1]);
      klass != nullptr && class_has_builtin_base_name(klass, "BaseException")) {
    if (argc < 3 || args[2].tag == ValueTag::None) {
      if (!runtime_call_callable(runtime, args[1], nullptr, 0, exception, error)) return false;
    } else if (auto* instance = value_as_instance(args[2]);
               instance != nullptr && class_is_subclass(value_as_class(instance->klass), klass)) {
      exception = args[2];
    } else if (auto* tuple = value_as_tuple(args[2])) {
      if (!runtime_call_callable(runtime, args[1], tuple->items.begin(),
                                  static_cast<uint32_t>(tuple->items.size()), exception, error)) return false;
    } else if (!runtime_call_callable(runtime, args[1], args + 2, 1, exception, error)) return false;
  } else if (exception_is(runtime, args[1], "BaseException")) {
    if (argc > 2 && args[2].tag != ValueTag::None)
      return raise(runtime, "TypeError", "instance exception may not have a separate value", error);
    exception = args[1];
  } else {
    return raise(runtime, "TypeError", "exceptions must derive from BaseException", error);
  }
  if (!exception_is(runtime, exception, "BaseException"))
    return raise(runtime, "TypeError", "exception constructor returned an invalid exception", error);
  if (traceback.tag != ValueTag::None &&
      !object_set_attr(exception, "__traceback__", traceback, error)) return false;
  auto* iterator = static_cast<AwaitState*>(instance_get_native_data(args[0], kIteratorData));
  if (iterator == nullptr || !iterator_release(args[0], *iterator, error)) return false;
  runtime.set_pending_exception(std::move(exception));
  value_set_none(out);
  return false;
}

} // namespace

FutureState* state(Runtime& runtime, const Value& self, std::string& error,
                    bool require_initialized) {
  auto* fs = static_cast<FutureState*>(instance_get_native_data(self, kFutureData));
  if (fs == nullptr || (require_initialized && fs->loop.tag == ValueTag::None)) {
    raise(runtime, "RuntimeError", "Future object is not initialized.", error);
    return nullptr;
  }
  return fs;
}

void unregister(FutureState& fs) {
  if (!fs.task.has_value() || fs.task->registry == nullptr || !fs.task->registered) return;
  std::lock_guard lock(fs.task->registry->mutex);
  auto& task = *fs.task;
  if (task.previous != nullptr) task.previous->next = task.next;
  else fs.task->registry->task_head = task.next;
  if (task.next != nullptr) task.next->previous = task.previous;
  task.previous = task.next = nullptr;
  task.registered = false;
}

void clear_future(void* payload) {
  auto& fs = *static_cast<FutureState*>(payload);
  unregister(fs);
  fs.log_traceback = false;
  value_set_none(fs.loop);
  value_set_none(fs.result);
  value_set_none(fs.exception);
  value_set_none(fs.exception_traceback);
  value_set_none(fs.source_traceback);
  value_set_none(fs.cancel_message);
  value_set_none(fs.cancelled_exception);
  value_set_none(fs.awaited_by);
  fs.first_callback = {};
  fs.has_first_callback = false;
  fs.additional_callbacks.clear();
  fs.task.reset();
}

void trace_future_value(const Value& value,
                        NativeGCReferenceVisitor visitor, void* context) {
  if (value.tag == ValueTag::Object && value.as.obj != nullptr)
    visitor(value.as.obj, context);
}

void trace_future_references(void* payload,
                             NativeGCReferenceVisitor visitor, void* context) {
  const auto& fs = *static_cast<const FutureState*>(payload);
  // Keep this list aligned with every owning Value in FutureState and TaskData.
  // Unlike the old published mirror, this cold collector walk leaves setters
  // and each Task step free of edge-array rebuilding and repeated edge scans.
  trace_future_value(fs.loop, visitor, context);
  trace_future_value(fs.result, visitor, context);
  trace_future_value(fs.exception, visitor, context);
  trace_future_value(fs.exception_traceback, visitor, context);
  trace_future_value(fs.source_traceback, visitor, context);
  trace_future_value(fs.cancel_message, visitor, context);
  trace_future_value(fs.cancelled_exception, visitor, context);
  trace_future_value(fs.awaited_by, visitor, context);
  if (fs.has_first_callback) {
    trace_future_value(fs.first_callback.function, visitor, context);
    trace_future_value(fs.first_callback.context, visitor, context);
  }
  for (const auto& callback : fs.additional_callbacks) {
    trace_future_value(callback.function, visitor, context);
    trace_future_value(callback.context, visitor, context);
  }
  if (fs.task.has_value()) {
    trace_future_value(fs.task->coroutine, visitor, context);
    trace_future_value(fs.task->waiter, visitor, context);
    trace_future_value(fs.task->context, visitor, context);
    trace_future_value(fs.task->name, visitor, context);
  }
}

bool publish_edges(const Value& self, FutureState&, std::string& error) {
  auto* instance = value_as_instance(self);
  // Match CPython's tp_traverse model: the collector reads the live Future or
  // Task payload only when it collects. Rebuilding a mirror vector on every
  // Task state transition taxes the hot path and can scan the same 12 edges
  // multiple times per step. The traversal callback is stable after its first
  // registration, so later mutations need no publication work.
  if (instance != nullptr &&
      instance->native_gc_traverse == trace_future_references &&
      instance->native_data_clear == clear_future) return true;
  return instance_set_native_gc_traversal(
      self, trace_future_references, clear_future, error);
}

bool add_native_callback(Runtime& runtime, const Value& self, FutureState& fs,
                          Callback callback, std::string& error) {
  if (fs.completion != Completion::Pending)
    return schedule_one(runtime, fs.loop, self, callback, error);
  if (!fs.has_first_callback) {
    fs.first_callback = std::move(callback);
    fs.has_first_callback = true;
  } else fs.additional_callbacks.push_back(std::move(callback));
  return publish_edges(self, fs, error);
}

bool cancelled_error(Runtime& runtime, const Value& self, FutureState& fs,
                      Value& out, std::string& error) {
  if (fs.cancelled_exception.tag != ValueTag::None) {
    out = std::move(fs.cancelled_exception);
    value_set_none(fs.cancelled_exception);
    return publish_edges(self, fs, error);
  }
  Value klass;
  if (!helper(runtime, "asyncio.exceptions", "CancelledError", klass, error)) return false;
  return runtime_call_callable(runtime, klass,
      fs.cancel_message.tag == ValueTag::None ? nullptr : &fs.cancel_message,
      fs.cancel_message.tag == ValueTag::None ? 0 : 1, out, error);
}

bool future_result(Runtime& runtime, const Value& self, FutureState& fs,
                    Value& out, std::string& error) {
  if (fs.completion == Completion::Cancelled) {
    Value exception;
    if (!cancelled_error(runtime, self, fs, exception, error)) return false;
    runtime.set_pending_exception(std::move(exception));
    return false;
  }
  if (fs.completion != Completion::Finished)
    return raise(runtime, "InvalidStateError", "Result is not set.", error);
  fs.log_traceback = false;
  if (fs.exception.tag != ValueTag::None) {
    Value exception = fs.exception;
    if (!object_set_attr(exception, "__traceback__", fs.exception_traceback, error)) return false;
    value_set_none(fs.exception_traceback);
    if (!publish_edges(self, fs, error)) return false;
    runtime.set_pending_exception(std::move(exception));
    return false;
  }
  out = fs.result;
  return true;
}

bool finish_result(Runtime& runtime, const Value& self, FutureState& fs,
                    Value result, std::string& error) {
  if (fs.completion != Completion::Pending)
    return raise(runtime, "InvalidStateError", "invalid state", error);
  fs.result = std::move(result);
  fs.completion = Completion::Finished;
  return schedule_callbacks(runtime, self, fs, error);
}

bool finish_exception(Runtime& runtime, const Value& self, FutureState& fs,
                       Value exception, std::string& error) {
  if (fs.completion != Completion::Pending)
    return raise(runtime, "InvalidStateError", "invalid state", error);
  if (value_as_class(exception) != nullptr) {
    Value normalized;
    if (!runtime_call_callable(runtime, exception, nullptr, 0, normalized, error)) return false;
    exception = std::move(normalized);
  }
  if (!exception_is(runtime, exception, "BaseException"))
    return raise(runtime, "TypeError", "invalid exception object", error);
  if (exception_is(runtime, exception, "StopIteration")) {
    Value converted = runtime.make_exception("RuntimeError", "StopIteration interacts badly with generators and cannot be raised into a Future");
    if (!object_set_attr(converted, "__cause__", exception, error) ||
        !object_set_attr(converted, "__context__", exception, error)) return false;
    exception = std::move(converted);
  }
  Value traceback;
  if (!object_get_attr(exception, "__traceback__", traceback, error)) return false;
  fs.exception = std::move(exception);
  fs.exception_traceback = std::move(traceback);
  fs.completion = Completion::Finished;
  if (!schedule_callbacks(runtime, self, fs, error)) return false;
  fs.log_traceback = true;
  return true;
}

bool cancel_future(Runtime& runtime, const Value& self, FutureState& fs,
                    Value message, bool& changed, std::string& error) {
  fs.log_traceback = false;
  changed = fs.completion == Completion::Pending;
  if (!changed) return true;
  Value previous_message = std::move(fs.cancel_message);
  fs.cancel_message = std::move(message);
  fs.completion = Completion::Cancelled;
  return schedule_callbacks(runtime, self, fs, error);
}

bool initialize_future(Runtime& runtime, const Value& self, FutureState& fs,
                        Value loop, std::string& error) {
  // Reinitialization clears Future references but leaves a Task's separate
  // fields intact, matching the common native Future/Task prefix.
  // Retire old owners into locals until the new GC mirror is published. A
  // decref may run a Python finalizer which calls gc.collect() reentrantly;
  // no tracing edge may still point at an already destroyed payload reference.
  Value retired[] = {std::move(fs.loop), std::move(fs.result),
      std::move(fs.exception), std::move(fs.exception_traceback),
      std::move(fs.source_traceback), std::move(fs.cancel_message),
      std::move(fs.cancelled_exception), std::move(fs.awaited_by)};
  Callback retired_first = std::move(fs.first_callback);
  std::vector<Callback> retired_callbacks = std::move(fs.additional_callbacks);
  fs.completion = Completion::Pending;
  fs.loop = Value::none();
  fs.result = fs.exception = fs.exception_traceback = Value::none();
  fs.source_traceback = fs.cancel_message = fs.cancelled_exception = Value::none();
  fs.awaited_by = Value::none();
  fs.awaited_by_is_set = false;
  fs.first_callback = {};
  fs.has_first_callback = false;
  fs.additional_callbacks.clear();
  fs.log_traceback = fs.blocking = false;
  if (!publish_edges(self, fs, error)) return false;
  // Resolve a missing loop after clearing previous initialization, including
  // the failure path, as CPython does.
  if (loop.tag == ValueTag::None && !event_loop(runtime, loop, error)) return false;
  fs.loop = std::move(loop);
  if (!publish_edges(self, fs, error)) return false;
  Value debug;
  if (!call_method(runtime, fs.loop, "get_debug", nullptr, 0, {}, debug, error)) return false;
  bool enabled = false;
  if (!runtime_truthy(runtime, debug, enabled, error)) return false;
  if (enabled && !runtime.finalizing()) {
    Value extract_stack;
    if (!helper(runtime, "traceback", "extract_stack", extract_stack, error)) return false;
    if (!runtime_call_callable(runtime, extract_stack, nullptr, 0, fs.source_traceback, error)) return false;
    if (!publish_edges(self, fs, error)) return false;
  }
  return true;
}

Value iterator_class(Runtime& runtime) {
  const Value* object_base = runtime.find_builtin("object");
  return Value::class_object("FutureIter", {
      {"__module__", Value::string("_asyncio")},
      {"__iter__", runtime.make_native_function("_asyncio.FutureIter.__iter__", iterator_iter)},
      {"__next__", runtime.make_native_function("_asyncio.FutureIter.__next__", iterator_next)},
      {"send", runtime.make_native_function("_asyncio.FutureIter.send", iterator_send)},
      {"throw", runtime.make_native_function("_asyncio.FutureIter.throw", iterator_throw)},
      {"close", runtime.make_native_function("_asyncio.FutureIter.close", iterator_close)}},
      object_base == nullptr ? Value::invalid() : *object_base);
}

Value future_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs{
      {"__module__", Value::string("_asyncio")},
      {"__new__", runtime.make_native_function("_asyncio.Future.__new__", future_new,
          nullptr, nullptr, nullptr, false, future_new_kw)},
      {"__init__", runtime.make_native_function("_asyncio.Future.__init__", future_init,
          nullptr, nullptr, nullptr, false, future_init_kw)},
      {"__repr__", runtime.make_native_function("_asyncio.Future.__repr__", future_repr)},
      {"__del__", runtime.make_native_function("_asyncio.Future.__del__", future_finalize)},
      {"__class_getitem__", Value::class_method(runtime.make_native_function("_asyncio.Future.__class_getitem__", generic_alias))},
      // These native methods are called repeatedly by asyncio.gather (done,
      // cancelled, exception) and Task wakeups (result). CPython exposes them
      // as direct C methods; give XLang's VM the same stack/register argument
      // path so a normal bound-method call does not materialize a Value vector.
      {"get_loop", runtime.make_native_function("_asyncio.Future.get_loop", get_loop,
          nullptr, nullptr, builtin_method_fast_adapter<get_loop, 1>)},
      {"result", runtime.make_native_function("_asyncio.Future.result", get_result,
          nullptr, nullptr, builtin_method_fast_adapter<get_result, 1>)},
      {"exception", runtime.make_native_function("_asyncio.Future.exception", get_exception,
          nullptr, nullptr, builtin_method_fast_adapter<get_exception, 1>)},
      {"done", runtime.make_native_function("_asyncio.Future.done", get_done,
          nullptr, nullptr, builtin_method_fast_adapter<get_done, 1>)},
      {"cancelled", runtime.make_native_function("_asyncio.Future.cancelled", get_cancelled,
          nullptr, nullptr, builtin_method_fast_adapter<get_cancelled, 1>)},
      {"_make_cancelled_error", runtime.make_native_function("_asyncio.Future._make_cancelled_error", make_cancelled_error,
          nullptr, nullptr, builtin_method_fast_adapter<make_cancelled_error, 1>)},
      {"set_result", runtime.make_native_function("_asyncio.Future.set_result", set_result,
          nullptr, nullptr, builtin_method_fast_adapter<set_result, 2>)},
      {"set_exception", runtime.make_native_function("_asyncio.Future.set_exception", set_exception,
          nullptr, nullptr, builtin_method_fast_adapter<set_exception, 2>)},
      {"cancel", runtime.make_native_function("_asyncio.Future.cancel", cancel,
          nullptr, nullptr, builtin_method_fast_adapter<cancel, 2>, false, cancel_kw)},
      {"add_done_callback", runtime.make_native_function("_asyncio.Future.add_done_callback", add_callback,
          nullptr, nullptr, builtin_method_fast_adapter<add_callback, 2>, false, add_callback_kw)},
      {"remove_done_callback", runtime.make_native_function("_asyncio.Future.remove_done_callback", remove_callback,
          nullptr, nullptr, builtin_method_fast_adapter<remove_callback, 2>)},
      {"__await__", runtime.make_native_function("_asyncio.Future.__await__", future_await,
          nullptr, nullptr, builtin_method_fast_adapter<future_await, 1>)},
      {"__iter__", runtime.make_native_function("_asyncio.Future.__iter__", future_await,
          nullptr, nullptr, builtin_method_fast_adapter<future_await, 1>)}};
  for (const auto& [name, field] : std::vector<std::pair<const char*, Field>>{
      {"_state", Field::State}, {"_loop", Field::Loop}, {"_result", Field::Result},
      {"_exception", Field::Exception}, {"_callbacks", Field::Callbacks},
      {"_source_traceback", Field::SourceTraceback}, {"_cancel_message", Field::CancelMessage},
      {"_log_traceback", Field::LogTraceback}, {"_asyncio_future_blocking", Field::Blocking},
      {"_asyncio_awaited_by", Field::AwaitedBy}}) {
    void* data = reinterpret_cast<void*>(static_cast<intptr_t>(field));
    Value get = runtime.make_native_function(std::string("_asyncio.Future.") + name + ".get", field_get, data);
    Value set = field == Field::CancelMessage || field == Field::LogTraceback || field == Field::Blocking
        ? runtime.make_native_function(std::string("_asyncio.Future.") + name + ".set", field_set, data)
        : Value::none();
    attrs.emplace_back(name, Value::property(get, set, Value::none(), Value::none()));
  }
  // `_asyncio.Future` is a native heap type derived from `object` in CPython.
  // Keep that base explicit: pure-Python users such as functools.singledispatch
  // walk `__mro__` to their object fallback, and a one-entry MRO returns no
  // handler for an otherwise valid native Future subclass.
  const Value* object_base = runtime.find_builtin("object");
  Value future = Value::class_object("Future", std::move(attrs),
      object_base == nullptr ? Value::invalid() : *object_base);
  if (auto* future_type = value_as_class(future)) {
    future_type->native_type_constructor = construct_future_direct;
    future_type->native_type_constructor_version = future_type->version;
  }
  // CPython's native Future tp_new checks against its static heap type. Store
  // this per-runtime class identity in the callback context so each native
  // Future construction avoids importing _asyncio and resolving its mutable
  // `Future` module attribute, while saved types keep working after rebinding.
  if (auto* future_class = value_as_class(future)) {
    auto new_it = future_class->attrs.find("__new__");
    if (new_it != future_class->attrs.end()) {
      Value* new_value = &new_it->second;
      if (auto* static_new = value_as_static_method(*new_value)) {
        new_value = &static_new->function;
      }
      if (auto* new_function = value_as_native_function(*new_value)) {
        new_function->user_data = future_class;
      }
    }
  }
  return future;
}

} // namespace xlang3::asyncio_native
