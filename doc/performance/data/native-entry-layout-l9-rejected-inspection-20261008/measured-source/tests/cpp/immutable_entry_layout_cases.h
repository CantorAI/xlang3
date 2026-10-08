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
#include "xlang3/object_model.h"

#include <atomic>

namespace xlang3::test {
namespace immutable_entry_layout_cases {

inline bool probe(Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc == 0) { error = "immutable layout probe requires an argument"; return false; }
  if (args[0].tag == ValueTag::Int64) out = Value::int64(args[0].as.i64 + argc);
  else value_assign_fast(out, args[0]);
  error.clear(); return true;
}

inline std::shared_ptr<ir::Module> make_module() {
  auto module = std::make_shared<ir::Module>();
  module->source_file = "immutable_entry_layout_public_api.py";
  module->functions.resize(4);
  for (uint32_t index = 0; index < 3; ++index) {
    auto& fn = module->functions[index];
    fn.name = index == 0 ? "layout_a" : index == 1 ? "layout_zero" : "layout_b";
    fn.params = {"value"}; fn.locals = {"value"}; fn.register_count = 3;
  }
  auto& a = module->functions[0];
  a.constants = {Value::native_function(0, "layout_probe", probe, nullptr)};
  a.call_args = {{0}, {0, 0, 0, 0, 0}};
  a.code = {{ir::Op::LoadLocal, 0, 0, 0, 0},
      {ir::Op::LoadConst, 1, 0, 0, 0}, {ir::Op::Call, 2, 1, 0, 0},
      {ir::Op::Return, 0, 2, 0, 0}};
  // This nontrivial uncached IR must enter a real frame, between two cached
  // functions at the same depth. It is neither a two-op return nor arithmetic.
  auto& zero = module->functions[1];
  zero.code = {{ir::Op::LoadLocal, 0, 0, 0, 0},
      {ir::Op::Move, 1, 0, 0, 0}, {ir::Op::Move, 2, 1, 0, 0},
      {ir::Op::Return, 0, 2, 0, 0}};
  auto& b = module->functions[2];
  b.constants = {Value::native_function(0, "layout_probe", probe, nullptr)};
  b.call_args = {{0, 0, 0}};
  b.code = a.code;
  auto& driver = module->functions[3];
  driver.name = "layout_driver";
  driver.params = {"a", "zero", "b", "value"}; driver.locals = driver.params;
  driver.register_count = 9;
  driver.call_args = {{1}, {2}, {4}, {6}};
  driver.code = {{ir::Op::LoadLocal, 0, 0, 0, 0},
      {ir::Op::LoadLocal, 1, 3, 0, 0}, {ir::Op::Call, 2, 0, 0, 0},
      {ir::Op::LoadLocal, 3, 1, 0, 0}, {ir::Op::Call, 4, 3, 1, 0},
      {ir::Op::LoadLocal, 5, 2, 0, 0}, {ir::Op::Call, 6, 5, 2, 0},
      {ir::Op::LoadLocal, 7, 0, 0, 0}, {ir::Op::Call, 8, 7, 3, 0},
      {ir::Op::Return, 0, 8, 0, 0}};
  return module;
}

inline std::shared_ptr<const ir::FunctionExecutionMetadata> metadata(const ir::Function& fn) {
  return std::atomic_load_explicit(&fn.execution_metadata, std::memory_order_acquire);
}

} // namespace immutable_entry_layout_cases

inline void check_immutable_entry_layout(CaseResult& result) {
  using namespace immutable_entry_layout_cases;
  std::ostringstream output;
  Runtime runtime(output);
  auto module = make_module();
  Value a = Value::function(0, {}, Value::none(), module);
  Value zero = Value::function(1, {}, Value::none(), module);
  Value b = Value::function(2, {}, Value::none(), module);
  Value driver = Value::function(3, {}, Value::none(), module);
  Value args[4] = {a, zero, b, Value::int64(10)};
  std::string error;
  bool entries_ok = true;
  for (unsigned i = 0; i < 32; ++i) {
    Value out;
    const bool called = runtime_call_callable(runtime, driver, args, 4, out, error);
    entries_ok &= called && out.tag == ValueTag::Int64 && out.as.i64 == 15;
    if (!called) break;
  }
  expect_true(result, entries_ok, "32 real native entries execute A/uncached/B/A prepared-frame restores");
  const auto a_metadata = metadata(module->functions[0]);
  const auto zero_metadata = metadata(module->functions[1]);
  const auto b_metadata = metadata(module->functions[2]);
  const auto driver_metadata = metadata(module->functions[3]);
  expect_true(result, a_metadata != nullptr && zero_metadata != nullptr &&
      b_metadata != nullptr && driver_metadata != nullptr,
      "every cached and uncached function actually entered the public DLL frame path");
  if (!a_metadata || !zero_metadata || !b_metadata || !driver_metadata) return;
  const std::vector<uint32_t> expected_layout = {UINT32_MAX, UINT32_MAX, 0, UINT32_MAX};
  expect_true(result, a_metadata->owner == &module->functions[0] &&
      a_metadata->cache_slot_by_instruction == expected_layout &&
      a_metadata->cache_cleanup_instructions == std::vector<uint32_t>{2} &&
      a_metadata->max_native_call_arg_count == 5 &&
      b_metadata->cache_slot_by_instruction == expected_layout &&
      b_metadata->max_native_call_arg_count == 3 &&
      zero_metadata->cache_slot_by_instruction.empty() &&
      zero_metadata->cache_cleanup_instructions.empty() &&
      zero_metadata->max_native_call_arg_count == 0,
      "actual prepared metadata provides exact immutable slots/capacity and an empty uncached layout");
  const auto* layout_data = a_metadata->cache_slot_by_instruction.data();
  const auto driver_refs = driver.as.obj->refcnt.load();
  Value token = Value::list({Value::int64(42)});
  args[3] = token;
  const auto token_refs = token.as.obj->refcnt.load();
  bool owners_ok = true;
  for (unsigned i = 0; i < 32; ++i) {
    {
      Value out;
      owners_ok &= runtime_call_callable(runtime, driver, args, 4, out, error) && value_is(out, token);
    }
    owners_ok &= token.as.obj->refcnt.load() == token_refs;
  }
  expect_true(result, owners_ok && driver.as.obj->refcnt.load() == driver_refs &&
      metadata(module->functions[0]).get() == a_metadata.get() &&
      a_metadata->cache_slot_by_instruction.data() == layout_data,
      "repeated public entries reuse only immutable layout and release all argument/function cache owners");

  Value replacement_code = Value::code(module, 2);
  error.clear();
  const bool replaced = object_set_attr(a, "__code__", replacement_code, error);
  args[3] = Value::int64(10);
  Value replaced_out;
  const bool replaced_called = replaced && runtime_call_callable(runtime, driver, args, 4, replaced_out, error);
  expect_true(result, replaced_called && replaced_out.tag == ValueTag::Int64 &&
      replaced_out.as.i64 == 19 && value_as_function(a)->function_id == 2 &&
      metadata(module->functions[2]).get() == b_metadata.get(),
      "live code replacement binds the current function layout/capacity without a stale saved association");

  // An IR copy inherits the old metadata pointer. Existing owner validation
  // must rebuild its immutable layout for the actual copied Function object.
  auto copy = std::make_shared<ir::Module>(*module);
  Value copied_a = Value::function(0, {}, Value::none(), copy);
  Value input = Value::int64(3), copied_out;
  const bool copied_called = runtime_call_callable(runtime, copied_a, &input, 1, copied_out, error);
  const auto copied_metadata = metadata(copy->functions[0]);
  expect_true(result, copied_called && copied_out.tag == ValueTag::Int64 && copied_out.as.i64 == 4 &&
      copied_metadata != nullptr && copied_metadata->owner == &copy->functions[0] &&
      copied_metadata.get() != a_metadata.get() &&
      copied_metadata->cache_slot_by_instruction == expected_layout &&
      copied_metadata->max_native_call_arg_count == 5,
      "copied code rejects inherited metadata ownership and builds its own current immutable layout");
}

} // namespace xlang3::test
