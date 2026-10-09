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
#pragma once
#include "test_harness.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/sequence.h"

namespace xlang3::test {
namespace generic_gc_cases {
inline Value objects(Runtime& runtime) {
  Value module, function, out;
  std::string error;
  if (!runtime.import_module("gc", module, error) ||
      !module_get_attr(module, "get_objects", function, error) ||
      !runtime_call_callable(runtime, function, nullptr, 0, out, error)) return Value::invalid();
  return out;
}
inline bool contains(Runtime& runtime, Object* pointer, CaseResult& result) {
  // Inspect through the real registered module ABI, not an unexported tracker
  // helper. The returned list is allocated after its snapshot, so it cannot
  // mistake its own reused allocation address for a reclaimed list object.
  Value snapshot = objects(runtime);
  auto* list = value_as_list(snapshot);
  expect_true(result, list != nullptr, "registered gc.get_objects supplies lifetime evidence");
  if (list == nullptr) return true; // Fail closed when inspection is unavailable.
  for (const auto& value : list->items) if (value.as.obj == pointer) return true;
  return false;
}
inline bool collect(Runtime& runtime, uint64_t& count) {
  Value module, function, out;
  std::string error;
  if (!runtime.import_module("gc", module, error) ||
      !module_get_attr(module, "collect", function, error) ||
      !runtime_call_callable(runtime, function, nullptr, 0, out, error) ||
      out.tag != ValueTag::Int64 || out.as.i64 < 0) return false;
  count = static_cast<uint64_t>(out.as.i64);
  return true;
}
inline bool noop(Runtime&, const Value*, uint32_t, Value& out, std::string& error, void*) {
  out = Value::none(); error.clear(); return true;
}
inline void cleanup(void* context) { ++*static_cast<unsigned*>(context); }
}

inline void check_generic_gc_cases(CaseResult& result) {
  using namespace generic_gc_cases;
  std::ostringstream output;
  Runtime runtime(output);
  uint64_t count = 0;
  Value self = Value::list({});
  auto* object = self.as.obj;
  value_as_list(self)->items.push_back(self);
  value_set_invalid(self);
  expect_true(result, collect(runtime, count) && count >= 1 && !contains(runtime, object, result),
      "registered gc.collect discovers and releases an unseeded list self-cycle");

  Value first = Value::list({}), second = Value::list({});
  auto* first_object = first.as.obj;
  auto* second_object = second.as.obj;
  value_as_list(first)->items = {second, second, second};
  value_as_list(second)->items = {first};
  value_set_invalid(first); value_set_invalid(second);
  expect_true(result, collect(runtime, count) && count >= 2 &&
      !contains(runtime, first_object, result) && !contains(runtime, second_object, result),
      "duplicate owning edges are subtracted separately from the snapshot-pinned refcount");

  // Exercise full packed blocks, short tails and mismatching borrowed/scalar
  // lanes. Reclamation proves multiplicity; a native root proves cached edges
  // still preserve the component before that root is removed.
  for (size_t copies : {size_t{4}, size_t{5}, size_t{8}, size_t{9}, size_t{17}}) {
    for (bool mixed : {false, true}) {
      Value left = Value::list({}), right = Value::list({left});
      auto* left_object = left.as.obj;
      auto* right_object = right.as.obj;
      auto& items = value_as_list(left)->items;
      items.assign(copies, right);
      if (mixed) {
        Value borrowed;
        borrowed.tag = ValueTag::Object; borrowed.flags = kXlangValueBorrowedRefFlag;
        borrowed.as.obj = right_object;
        items.insert(items.begin() + copies / 2, std::move(borrowed));
        items.insert(items.begin() + copies / 2, Value::none());
      }
      value_set_invalid(right);
      expect_true(result, collect(runtime, count) && items.size() == copies + (mixed ? 2 : 0) &&
          value_as_list(items.front()) != nullptr && value_as_list(items.front())->items.size() == 1 &&
          value_is(value_as_list(items.front())->items.front(), left),
          "packed owning runs preserve descendants reached only through the native root");
      value_set_invalid(left);
      expect_true(result, collect(runtime, count) && count >= 2 &&
          !contains(runtime, left_object, result) && !contains(runtime, right_object, result),
          "packed owning runs and scalar tails count every owner and exclude borrowed lanes");
    }
  }
  for (size_t copies : {size_t{5}, size_t{17}}) {
    Value list = Value::list({});
    Value tuple = Value::tuple(std::vector<Value>(copies, list));
    auto* list_object = list.as.obj;
    auto* tuple_object = tuple.as.obj;
    value_as_list(list)->items.push_back(tuple);
    value_set_invalid(tuple); value_set_invalid(list);
    expect_true(result, collect(runtime, count) && count >= 2 &&
        !contains(runtime, list_object, result) && !contains(runtime, tuple_object, result),
        "packed tuple runs retain exact incoming counts and reclaim a mixed list/tuple cycle");
  }

  Value borrowed_self = Value::list({});
  auto* borrowed_object = borrowed_self.as.obj;
  value_as_list(borrowed_self)->items.push_back(borrowed_self);
  Value borrowed;
  borrowed.tag = ValueTag::Object; borrowed.flags = kXlangValueBorrowedRefFlag;
  borrowed.as.obj = borrowed_object;
  value_as_list(borrowed_self)->items.push_back(std::move(borrowed));
  value_set_invalid(borrowed_self);
  expect_true(result, collect(runtime, count) && count >= 1 && !contains(runtime, borrowed_object, result),
      "borrowed storage edges do not pretend to own a reference or underflow cycle counts");

  Value rooted = Value::list({}), child = Value::list({});
  value_as_list(rooted)->items.push_back(child);
  value_as_list(child)->items.push_back(rooted);
  value_set_invalid(child);
  expect_true(result, collect(runtime, count) && value_as_list(rooted)->items.size() == 1 &&
      value_is(value_as_list(value_as_list(rooted)->items[0])->items[0], rooted),
      "external native owner protects a complete reachable cyclic component");
  auto* rooted_object = rooted.as.obj;
  value_set_invalid(rooted);
  expect_true(result, collect(runtime, count) && count >= 2 && !contains(runtime, rooted_object, result),
      "removing the native root makes the same component genuinely collectible");

  Value cell = Value::cell(Value::none());
  auto* cell_object = cell.as.obj;
  value_as_cell(cell)->value = cell;
  value_set_invalid(cell);
  expect_true(result, collect(runtime, count) && count >= 1 && !contains(runtime, cell_object, result),
      "cell storage participates in ordinary owning-edge discovery");

  unsigned cleanups = 0;
  Value opaque_cycle = Value::list({});
  auto* opaque_object = opaque_cycle.as.obj;
  value_as_list(opaque_cycle)->items.push_back(opaque_cycle);
  value_as_list(opaque_cycle)->items.push_back(
      Value::native_function(0, "generic_gc_opaque_owner", noop, &cleanups, cleanup));
  value_set_invalid(opaque_cycle);
  expect_true(result, collect(runtime, count) && contains(runtime, opaque_object, result) && cleanups == 0,
      "opaque cleanup owners remain intact instead of observing a partly cleared graph");
  {
    Value snapshot = objects(runtime);
    auto* list = value_as_list(snapshot);
    expect_true(result, list != nullptr, "registered inspection permits explicit opaque-owner cleanup");
    if (list != nullptr) for (const auto& value : list->items)
      if (value.as.obj == opaque_object) value_as_list(value)->items.clear();
  }
  expect_true(result, cleanups == 1 && !contains(runtime, opaque_object, result),
      "opaque exclusion does not prevent ordinary explicit release and cleanup");

  Value pending = runtime.make_exception("LookupError", "generic gc pending identity");
  runtime.set_pending_exception(pending);
  expect_true(result, collect(runtime, count), "registered collector remains callable with an existing pending owner");
  Value restored;
  expect_true(result, runtime.take_pending_exception(restored) && value_is(restored, pending),
      "plain collection preserves a preexisting pending exception's identity");
}
} // namespace xlang3::test
