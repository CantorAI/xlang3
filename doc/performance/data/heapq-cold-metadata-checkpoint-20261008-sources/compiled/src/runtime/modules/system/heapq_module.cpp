/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0.
*/
#include "xlang3/builtins.h"

#include "xlang3/builtin_methods.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "../thread/runtime_lock.h"

#include <cstddef>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace xlang3 {
namespace {

enum class HeapOperation { Push, Pop, Replace, PushPop, Heapify };

bool is_heap_list(Runtime& runtime, const Value& value) {
  if (value_as_list(value) != nullptr) return true;
  auto* instance = value_as_instance(value);
  auto* actual = instance == nullptr ? nullptr : value_as_class(instance->klass);
  const Value* builtin = runtime.find_builtin("list");
  auto* expected = builtin == nullptr ? nullptr : value_as_class(*builtin);
  // Backing storage alone is insufficient: the runtime's legacy container
  // traits can allocate List storage for a user class merely named "list".
  // Check canonical builtin identity in the actual MRO once at API entry;
  // renamed real subclasses remain valid and no Python isinstance hook runs.
  return actual != nullptr && expected != nullptr && class_is_subclass(actual, expected) &&
      value_as_mutable_list_storage(value) != nullptr;
}

// Native callbacks may drop their last temporary owner while unwinding.
// Keep the original Python failure owned until all operation-local Values
// have been released; cleanup reentry must not replace its type/identity.
class HeapFailureTransport {
 public:
  explicit HeapFailureTransport(Runtime& runtime) : runtime_(runtime) {}
  ~HeapFailureTransport() {
    if (exception_.tag == ValueTag::Invalid) return;
    Value incidental;
    while (runtime_.take_pending_exception(incidental)) {
      Value retired = std::move(incidental);
      // Retire outside the runtime's pending slot, before publishing the
      // original error. A finalizer can itself leave another incidental error.
    }
    runtime_.set_pending_exception(std::move(exception_));
  }

  void capture(std::string& error) {
    if (exception_.tag != ValueTag::Invalid) return;
    if (!runtime_.take_pending_exception(exception_)) {
      if (error.empty()) error = "heap comparison failed";
      exception_ = runtime_.make_exception("TypeError", error);
    }
  }

  bool raise(const char* type, const char* message, std::string& error) {
    error = message;
    runtime_.raise_class_error(type, error);
    capture(error);
    return false;
  }

 private:
  Runtime& runtime_;
  Value exception_;
};

struct HeapAccess {
  Runtime& runtime;
  HeapFailureTransport& failure;
  Value heap;
  std::string& error;

  ListObject* list() const { return value_as_mutable_list_storage(heap); }

  bool verify_size(size_t expected) {
    auto* current = list();
    if (current == nullptr || current->items.size() != expected)
      return failure.raise("RuntimeError", "list changed size during iteration", error);
    return true;
  }

  bool valid_position(size_t index) {
    auto* current = list();
    if (current == nullptr || index >= current->items.size())
      return failure.raise("IndexError", "index out of range", error);
    return true;
  }

  // No vector pointer, reference, or compared item remains borrowed across a
  // Python comparison. Reload list storage after comparison/truth conversion
  // and owner retirement: same-size replacement is permitted by _heapq.
  bool less(Value left, Value right, bool retire_right_first, bool& result) {
    Value comparison;
    const bool ok = runtime_value_compare(runtime, "<", left, right, comparison, error) &&
        runtime_truthy(runtime, comparison, result, error);
    if (!ok) failure.capture(error);
    comparison = Value::invalid();
    if (retire_right_first) {
      right = Value::invalid();
      left = Value::invalid();
    } else {
      left = Value::invalid();
      right = Value::invalid();
    }
    return ok;
  }
};

template <bool MaxHeap>
bool move_toward_root(HeapAccess& heap, size_t stop, size_t position) {
  if (!heap.valid_position(position)) return false;
  const size_t expected = heap.list()->items.size();
  while (position > stop) {
    const size_t parent = (position - 1) / 2;
    auto* current = heap.list();
    Value moving = current->items[position];
    Value above = current->items[parent];
    bool exchange = false;
    // Both min/max variants use __lt__. Reversing the operands, rather than
    // using >, preserves reflected comparison and callback order.
    const bool ok = MaxHeap
        ? heap.less(std::move(above), std::move(moving), false, exchange)
        : heap.less(std::move(moving), std::move(above), true, exchange);
    if (!ok || !heap.verify_size(expected)) return false;
    if (!exchange) break;
    current = heap.list();
    std::swap(current->items[parent], current->items[position]);
    position = parent;
  }
  return true;
}

template <bool MaxHeap>
bool move_toward_leaf(HeapAccess& heap, size_t position) {
  if (!heap.valid_position(position)) return false;
  const size_t initial = position;
  const size_t expected = heap.list()->items.size();
  const size_t first_leaf = expected / 2;
  while (position < first_leaf) {
    size_t child = position * 2 + 1;
    if (child + 1 < expected) {
      auto* current = heap.list();
      Value left = current->items[child];
      Value right = current->items[child + 1];
      bool choose_left = false;
      const bool ok = MaxHeap
          ? heap.less(std::move(right), std::move(left), false, choose_left)
          : heap.less(std::move(left), std::move(right), false, choose_left);
      if (!ok || !heap.verify_size(expected)) return false;
      // Equal children select the right child in both reference variants.
      if (!choose_left) ++child;
    }
    auto* current = heap.list();
    std::swap(current->items[position], current->items[child]);
    position = child;
  }
  return move_toward_root<MaxHeap>(heap, initial, position);
}

template <bool MaxHeap>
bool build_heap(HeapAccess& heap) {
  const size_t size = heap.list()->items.size();
  const size_t parents = size / 2;
  if (size <= 2500) {
    for (size_t next = parents; next != 0;) {
      if (!move_toward_leaf<MaxHeap>(heap, --next)) return false;
    }
    return true;
  }

  // Match the native accelerator's large-heap traversal as well as its result.
  // Callback comparison order is observable. Visiting a parent immediately
  // after its sibling subtrees also avoids revisiting cold child storage.
  size_t power = 1;
  while (power <= (parents + 1) / 2) power *= 2;
  const size_t boundary = power - 1;
  const size_t lower = parents / 2;
  auto visit_parent_chain = [&](size_t node) {
    for (;;) {
      if (!move_toward_leaf<MaxHeap>(heap, node)) return false;
      if ((node & 1) == 0) return true;
      node /= 2;
    }
  };
  for (size_t next = boundary; next > lower;) {
    if (!visit_parent_chain(--next)) return false;
  }
  for (size_t next = parents; next > boundary;) {
    if (!visit_parent_chain(--next)) return false;
  }
  return true;
}

template <HeapOperation Operation, bool MaxHeap>
const char* operation_name() {
  if constexpr (Operation == HeapOperation::Push) return MaxHeap ? "heappush_max" : "heappush";
  if constexpr (Operation == HeapOperation::Pop) return MaxHeap ? "heappop_max" : "heappop";
  if constexpr (Operation == HeapOperation::Replace) return MaxHeap ? "heapreplace_max" : "heapreplace";
  if constexpr (Operation == HeapOperation::PushPop) return MaxHeap ? "heappushpop_max" : "heappushpop";
  return MaxHeap ? "heapify_max" : "heapify";
}

template <HeapOperation Operation, bool MaxHeap>
bool heap_operation(Runtime& runtime, const Value* args, uint32_t argc,
                    Value& out, std::string& error, void*) {
  XlangRuntimeExecutionGuard execution;
  HeapFailureTransport failure(runtime);
  constexpr uint32_t expected = (Operation == HeapOperation::Pop || Operation == HeapOperation::Heapify) ? 1 : 2;
  const char* name = operation_name<Operation, MaxHeap>();
  if (argc != expected) {
    if constexpr (expected == 1) {
      error = std::string(name) + "() takes exactly one argument (" + std::to_string(argc) + " given)";
    } else {
      error = std::string(name) + " expected 2 arguments, got " + std::to_string(argc);
    }
    runtime.raise_class_error("TypeError", error);
    failure.capture(error);
    return false;
  }
  if (!is_heap_list(runtime, args[0])) {
    error = std::string(name) + (expected == 1 ? "() argument" : "() argument 1") +
        " must be list, not " + value_binary_type_name(args[0]);
    runtime.raise_class_error("TypeError", error);
    failure.capture(error);
    return false;
  }
  HeapAccess heap{runtime, failure, args[0], error};
  Value item;
  if constexpr (expected == 2) item = args[1];
  Value result;
  try {
    if constexpr (Operation == HeapOperation::Push) {
      heap.list()->items.push_back(item);
      if (!move_toward_root<MaxHeap>(heap, 0, heap.list()->items.size() - 1)) return false;
      result = Value::none();
    } else if constexpr (Operation == HeapOperation::Pop) {
      auto* current = heap.list();
      if (current->items.empty()) return failure.raise("IndexError", "index out of range", error);
      Value last = std::move(current->items.back());
      current->items.pop_back();
      if (current->items.empty()) {
        result = std::move(last);
      } else {
        result = std::move(current->items[0]);
        current->items[0] = std::move(last);
        if (!move_toward_leaf<MaxHeap>(heap, 0)) return false;
      }
    } else if constexpr (Operation == HeapOperation::Replace) {
      auto* current = heap.list();
      if (current->items.empty()) return failure.raise("IndexError", "index out of range", error);
      result = std::move(current->items[0]);
      current->items[0] = item;
      if (!move_toward_leaf<MaxHeap>(heap, 0)) return false;
    } else if constexpr (Operation == HeapOperation::PushPop) {
      if (heap.list()->items.empty()) {
        result = item;
      } else {
        Value top = heap.list()->items[0];
        bool replace = false;
        const bool ok = MaxHeap
            ? heap.less(item, std::move(top), true, replace)
            : heap.less(std::move(top), item, false, replace);
        if (!ok) return false;
        if (!replace) {
          result = item;
        } else {
          // pushpop intentionally does not require the pre-comparison size:
          // a callback may mutate the heap. Only a true comparison followed
          // by an empty heap raises; false returns the input item unchanged.
          if (!heap.valid_position(0)) return false;
          auto* current = heap.list();
          result = std::move(current->items[0]);
          current->items[0] = item;
          if (!move_toward_leaf<MaxHeap>(heap, 0)) return false;
        }
      }
    } else {
      if (!build_heap<MaxHeap>(heap)) return false;
      result = Value::none();
    }
    out = std::move(result);
    return true;
  } catch (const std::bad_alloc&) {
    return failure.raise("MemoryError", "heap allocation failed", error);
  } catch (const std::length_error&) {
    return failure.raise("MemoryError", "heap allocation failed", error);
  }
}

template <HeapOperation Operation, bool MaxHeap>
bool heap_operation_kw(Runtime& runtime, const Value* args, uint32_t argc,
                       const NativeKeywordArg*, uint32_t kwargc,
                       Value& out, std::string& error, void* userdata) {
  if (kwargc != 0) {
    XlangRuntimeExecutionGuard execution;
    error = std::string(operation_name<Operation, MaxHeap>()) + "() takes no keyword arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return heap_operation<Operation, MaxHeap>(runtime, args, argc, out, error, userdata);
}

template <HeapOperation Operation, bool MaxHeap>
void add_operation(NativeModuleBuilder& module) {
  constexpr uint32_t argc = (Operation == HeapOperation::Pop || Operation == HeapOperation::Heapify) ? 1 : 2;
  module.function(operation_name<Operation, MaxHeap>(),
      heap_operation<Operation, MaxHeap>,
      builtin_fast_adapter<heap_operation<Operation, MaxHeap>, argc>,
      false, heap_operation_kw<Operation, MaxHeap>);
}

} // namespace

void register_heapq_module(Runtime& runtime) {
  NativeModuleBuilder module(runtime, "_heapq");
  add_operation<HeapOperation::Push, false>(module);
  add_operation<HeapOperation::Pop, false>(module);
  add_operation<HeapOperation::Replace, false>(module);
  add_operation<HeapOperation::PushPop, false>(module);
  add_operation<HeapOperation::Heapify, false>(module);
  add_operation<HeapOperation::Push, true>(module);
  add_operation<HeapOperation::Pop, true>(module);
  add_operation<HeapOperation::Replace, true>(module);
  add_operation<HeapOperation::PushPop, true>(module);
  add_operation<HeapOperation::Heapify, true>(module);
  runtime.register_module("_heapq", module.finish());
}
} // namespace xlang3
