#pragma once
#include "test_harness.h"

namespace xlang3::test {
inline void check_class_method_annotation_capture(CaseResult& result) {
  auto parsed = parse_source(
      "def holder():\n"
      "    target = 11\n"
      "    class C:\n"
      "        target = 22\n"
      "        def method(self, arg: target) -> target:\n"
      "            return target\n"
      "    return C\n");
  expect_true(result, parsed.errors.empty(), "annotation capture proof parses");
  if (!parsed.errors.empty()) return;
  auto lowered = lower_to_ir(parsed.module);
  expect_true(result, lowered.errors.empty(), "annotation capture proof lowers");
  if (!lowered.errors.empty()) return;
  const ir::Function* parent = nullptr;
  uint32_t method_id = UINT32_MAX, annotation_id = UINT32_MAX;
  for (uint32_t id = 0; id < lowered.module.functions.size(); ++id) {
    const auto& function = lowered.module.functions[id];
    if (function.name == "holder") parent = &function;
    if (function.name == "method") method_id = id;
    if (function.name == "__annotate__") annotation_id = id;
  }
  expect_true(result, parent != nullptr && method_id != UINT32_MAX &&
      annotation_id != UINT32_MAX, "distinct method and annotation helpers exist");
  if (parent == nullptr || method_id == UINT32_MAX || annotation_id == UINT32_MAX) return;
  auto cell_for_name = [&](const std::string& name) -> uint32_t {
    for (uint32_t cell = 0; cell < parent->cell_slots.size(); ++cell) {
      const auto local = parent->cell_slots[cell];
      if (local < parent->locals.size() && parent->locals[local] == name) return cell;
    }
    return UINT32_MAX;
  };
  const uint32_t outer_cell = cell_for_name("target");
  const uint32_t class_cell = cell_for_name("#class.C.target");
  auto supplied_cell = [&](uint32_t child_id) -> uint32_t {
    for (size_t ip = 0; ip < parent->code.size(); ++ip) {
      const auto& instruction = parent->code[ip];
      if (instruction.op != ir::Op::MakeFunction || instruction.a != child_id ||
          instruction.b >= parent->function_closures.size()) continue;
      const auto& registers = parent->function_closures[instruction.b];
      if (registers.size() != 1) return UINT32_MAX;
      for (size_t producer = ip; producer != 0;) {
        const auto& load = parent->code[--producer];
        if (load.dst == registers[0] && load.op == ir::Op::LoadCellObject)
          return load.a;
      }
    }
    return UINT32_MAX;
  };
  expect_true(result,
      outer_cell != UINT32_MAX && class_cell != UINT32_MAX &&
          outer_cell != class_cell && supplied_cell(method_id) == outer_cell &&
          supplied_cell(annotation_id) == class_cell,
      "method body captures enclosing target while lazy annotations capture class target");
  const auto& method = lowered.module.functions[method_id];
  expect_true(result, method.free_vars == std::vector<std::string>({"target"}) &&
      method.code.size() == 3 && method.code[0].op == ir::Op::LoadFree &&
      method.code[1].op == ir::Op::Return &&
      method.code[2].op == ir::Op::ReturnConst &&
      method.code[2].a < method.constants.size() &&
      method.constants[method.code[2].a].tag == ValueTag::None,
      "annotation-only scope correction preserves the original method-body IR shape");
}
} // namespace xlang3::test
