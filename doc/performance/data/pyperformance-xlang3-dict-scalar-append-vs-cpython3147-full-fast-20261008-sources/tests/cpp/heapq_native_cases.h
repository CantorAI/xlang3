#pragma once
#include "test_harness.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include <memory>

namespace xlang3::test {
namespace heapq_native_cases {
struct State {
  Runtime* runtime = nullptr;
  ListObject* heap = nullptr; // Borrowed only during a callback with an owned heap.
  Value original;
  Value incidental;
  uint32_t calls = 0;
  uint32_t cleanups = 0;
  bool clear_and_fail = false;
  bool inject_cleanup_pending = false;
};
inline void destroy_context(void* context) {
  delete static_cast<std::shared_ptr<State>*>(context);
}
inline void destroy_operand(void* context) {
  auto state = *static_cast<std::shared_ptr<State>*>(context);
  ++state->cleanups;
  if (state->inject_cleanup_pending && state->runtime != nullptr)
    state->runtime->set_pending_exception(state->incidental);
  destroy_context(context);
}
inline bool less(Runtime& runtime, const Value*, uint32_t argc,
    Value& out, std::string& error, void* context) {
  auto& state = **static_cast<std::shared_ptr<State>*>(context);
  if (argc != 2 || state.heap == nullptr) { error = "heap callback arity/storage"; return false; }
  ++state.calls;
  if (state.clear_and_fail) {
    state.heap->items.clear();
    runtime.set_pending_exception(state.original);
    error = "original heap marker";
    return false;
  }
  // Reallocate and restore size, then replace BOTH compared positions.
  // A native implementation must swap the reloaded actual entries rather
  // than the objects whose __lt__ it just invoked.
  const size_t size = state.heap->items.size();
  for (uint32_t i = 0; i < 4096; ++i) state.heap->items.push_back(Value::int64(i));
  state.heap->items.resize(size);
  state.heap->items[0] = Value::int64(222);
  state.heap->items[1] = Value::int64(111);
  out = Value::boolean(true);
  error.clear();
  return true;
}
inline Value make_less(Runtime& runtime, const std::shared_ptr<State>& state) {
  auto context = std::make_unique<std::shared_ptr<State>>(state);
  Value function = runtime.make_native_function("heapq_native_proof.__lt__", less,
      context.get(), destroy_context);
  context.release();
  return function;
}
inline void attach_cleanup(CaseResult& result, Value& value, const std::shared_ptr<State>& state) {
  auto context = std::make_unique<std::shared_ptr<State>>(state);
  std::string error;
  const bool ok = instance_set_native_owner(value, "heapq_native_proof", context.get(),
      context.get(), destroy_operand, error);
  expect_true(result, ok, "heap operand owns shared lifetime state");
  if (ok) context.release();
}
struct StopCleanupReentry {
  std::shared_ptr<State> state;
  ~StopCleanupReentry() { state->inject_cleanup_pending = false; state->runtime = nullptr; }
};
} // namespace heapq_native_cases

inline void check_heapq_native_cases(CaseResult& result) {
  std::ostringstream output;
  Runtime runtime(output);
  Value module;
  std::string error;
  if (!runtime.import_module("_heapq", module, error)) {
    expect_true(result, false, "registered own _heapq must import");
    return;
  }
  for (const char* name : {"heappush", "heappop", "heapreplace", "heappushpop", "heapify",
      "heappush_max", "heappop_max", "heapreplace_max", "heappushpop_max", "heapify_max"}) {
    Value function;
    const bool found = module_get_attr(module, name, function, error);
    auto* native = found ? value_as_native_function(function) : nullptr;
    expect_true(result, native != nullptr && native->fast_callback != nullptr &&
        !native->fast_releases_vm_lock && !native->bind_as_descriptor,
        std::string("own _heapq exports stack fastcall for ") + name);
  }

  // This source-backed representation deliberately has List storage solely
  // because of its name. Native admission must still reject its actual MRO.
  Value fake = Value::instance(Value::class_object("list", {}));
  expect_true(result, value_as_mutable_list_storage(fake) != nullptr,
      "fake-name admission proof requires the legacy List backing storage");
  for (const char* name : {"heappush", "heappop", "heapreplace", "heappushpop", "heapify",
      "heappush_max", "heappop_max", "heapreplace_max", "heappushpop_max", "heapify_max"}) {
    Value function;
    if (!module_get_attr(module, name, function, error)) continue;
    auto* native = value_as_native_function(function);
    if (native == nullptr || native->fast_callback == nullptr) continue;
    const std::string operation(name);
    const uint32_t argc = operation.rfind("heappop", 0) == 0 || operation.rfind("heapify", 0) == 0 ? 1 : 2;
    Value args[] = {fake, Value::int64(31)};
    const uint32_t indices[] = {0, 1};
    for (bool fast : {false, true}) {
      Value out = Value::int64(701);
      error.clear();
      const bool ok = fast
          ? native->fast_callback(runtime, nullptr, 0, args, indices, argc, out, error, native->user_data)
          : runtime_call_callable(runtime, function, args, argc, out, error);
      Value pending;
      expect_true(result, !ok && runtime.take_pending_exception(pending) &&
          value_is(runtime.exception_type(pending), *runtime.find_builtin("TypeError")) &&
          out.tag == ValueTag::Int64 && out.as.i64 == 701,
          "native heap rejects fake list ancestry before any heap mutation");
    }
  }
  const Value* builtin_list = runtime.find_builtin("list");
  if (builtin_list != nullptr) {
    Value klass = Value::class_object("HeapRealListSubclass", {}, *builtin_list);
    expect_true(result, object_set_attr(klass, "__name__", Value::string("renamed_heap_list"), error),
        "rename real list subclass without changing builtin ancestry");
    Value receiver = Value::instance(klass);
    auto* storage = value_as_mutable_list_storage(receiver);
    expect_true(result, storage != nullptr, "real renamed list subclass retains List storage");
    if (storage != nullptr) {
      storage->items.push_back(Value::int64(43));
      Value function;
      Value out;
      error.clear();
      const bool ok = module_get_attr(module, "heappop", function, error) &&
          runtime_call_callable(runtime, function, &receiver, 1, out, error);
      expect_true(result, ok && out.tag == ValueTag::Int64 && out.as.i64 == 43,
          "native heap accepts renamed real list subclass by canonical MRO identity");
    }
  }

  for (const char* name : {"heappush", "heappush_max"}) {
    Value function;
    if (!module_get_attr(module, name, function, error)) {
      expect_true(result, false, "heap push lookup"); continue;
    }
    auto* native = value_as_native_function(function);
    if (native == nullptr || native->fast_callback == nullptr) continue;
    for (bool fast : {false, true}) {
      auto state = std::make_shared<heapq_native_cases::State>();
      state->runtime = &runtime;
      state->original = runtime.make_exception("LookupError", "original heap marker");
      state->incidental = runtime.make_exception("RuntimeError", "cleanup heap marker");
      // Declared before every callback-owned Value: on C++ unwind it disables
      // runtime reentry before Runtime destruction, even if pending traceback
      // retains an operand or its function. Counters remain shared-owned.
      heapq_native_cases::StopCleanupReentry stop{state};
      Value method = heapq_native_cases::make_less(runtime, state);
      Value klass = Value::class_object("HeapProofOperand", {{"__lt__", method}});
      Value first = Value::instance(klass);
      Value second = Value::instance(klass);
      heapq_native_cases::attach_cleanup(result, first, state);
      heapq_native_cases::attach_cleanup(result, second, state);
      Value heap = Value::list({first});
      state->heap = value_as_list(heap);
      Value args[] = {heap, second};
      first = Value::invalid();
      second = Value::invalid();
      state->clear_and_fail = true;
      state->inject_cleanup_pending = true;
      const auto initial_refs = state->original.as.obj->refcnt.load();
      Value out = Value::int64(701);
      error.clear();
      const uint32_t indices[] = {0, 1};
      const bool ok = fast
          ? native->fast_callback(runtime, nullptr, 0, args, indices, 2, out, error, native->user_data)
          : runtime_call_callable(runtime, function, args, 2, out, error);
      Value pending;
      expect_true(result, !ok && runtime.take_pending_exception(pending) &&
          value_is(pending, state->original), "heap error preserves original pending across operand cleanup");
      expect_true(result, state->calls == 1 && state->cleanups == 1 &&
          value_as_list(heap)->items.empty(), "failed heap comparison pins both erased operands");
      expect_true(result, out.tag == ValueTag::Int64 && out.as.i64 == 701 &&
          error == "original heap marker", "heap error leaves output and diagnostic unchanged");
      pending = Value::invalid();
      expect_true(result, state->original.as.obj->refcnt.load() == initial_refs,
          "heap failure transport has no leaked exception owner");
      Value extra;
      expect_true(result, !runtime.take_pending_exception(extra), "heap failure leaves no incidental pending owner");
      state->inject_cleanup_pending = false;
      args[1] = Value::invalid();
      expect_true(result, state->cleanups == 2, "heap operand cleanup happens exactly once");

      state->clear_and_fail = false;
      state->calls = 0;
      Value a = Value::instance(klass);
      Value b = Value::instance(klass);
      heap = Value::list({a});
      state->heap = value_as_list(heap);
      args[0] = heap;
      args[1] = b;
      a = Value::invalid();
      b = Value::invalid();
      error.clear();
      const bool changed_ok = fast
          ? native->fast_callback(runtime, nullptr, 0, args, indices, 2, out, error, native->user_data)
          : runtime_call_callable(runtime, function, args, 2, out, error);
      const auto* current = value_as_list(heap);
      expect_true(result, changed_ok && state->calls == 1 && current->items.size() == 2 &&
          current->items[0].tag == ValueTag::Int64 && current->items[0].as.i64 == 111 &&
          current->items[1].tag == ValueTag::Int64 && current->items[1].as.i64 == 222,
          "heap swaps current storage after same-size callback mutation and vector reallocation");
    }
  }

  // Fast-call outputs may be one of the borrowed argument registers. The
  // operation must retain its heap/item independently until publication.
  for (const char* name : {"heappop", "heappop_max"}) {
    Value function;
    if (!module_get_attr(module, name, function, error)) continue;
    auto* native = value_as_native_function(function);
    if (native == nullptr || native->fast_callback == nullptr) continue;
    Value registers[] = {Value::list({Value::int64(43)})};
    const uint32_t indices[] = {0};
    error.clear();
    const bool ok = native->fast_callback(runtime, nullptr, 0, registers, indices, 1,
        registers[0], error, native->user_data);
    expect_true(result, ok && registers[0].tag == ValueTag::Int64 && registers[0].as.i64 == 43,
        "heap fast pop may publish into its only heap argument owner");
  }
  for (const char* name : {"heappush", "heappush_max"}) {
    Value function;
    if (!module_get_attr(module, name, function, error)) continue;
    auto* native = value_as_native_function(function);
    if (native == nullptr || native->fast_callback == nullptr) continue;
    Value registers[] = {Value::list({Value::int64(43)}), Value::int64(17)};
    const uint32_t indices[] = {0, 1};
    error.clear();
    const bool ok = native->fast_callback(runtime, nullptr, 0, registers, indices, 2,
        registers[1], error, native->user_data);
    expect_true(result, ok && registers[1].tag == ValueTag::None &&
        value_as_list(registers[0])->items.size() == 2,
        "heap fast push may publish into its item argument register");
  }
}
} // namespace xlang3::test
