#pragma once

#include "test_harness.h"
#include "xlang3/ir_codec.h"
#include "executor/xlang_vm/xlang_frame.h"
#include "executor/xlang_vm/ops/xlang_vm_ops_arithmetic.h"

namespace xlang3::test {

inline void check_identity_last_use_cases(CaseResult& result) {
  const std::vector<Value> closure;
  auto make_module = [](std::vector<ir::Instr> code) {
    ir::Module module;
    module.functions.emplace_back();
    module.functions[0].register_count = 4;
    module.functions[0].locals = {"live_alias"};
    module.functions[0].code = std::move(code);
    return module;
  };

  // Keep real namespace/native argument owners; retire only consumed registers.
  {
    auto module = make_module({{ir::Op::Is, 2, 0, 1, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    Value owner = Value::list({});
    frame.regs[0] = owner;
    frame.regs[1] = owner;
    frame.locals[0] = owner;
    frame.native_call_args.push_back(owner);
    const auto refs = owner.as.obj->refcnt.load();
    unsigned refreshes = 0;
    xlang_vm::ops::is_op(module.functions[0].code[0], frame, 0, [&] { ++refreshes; });
    expect_true(result, frame.regs[0].tag == ValueTag::Invalid &&
        frame.regs[1].tag == ValueTag::Invalid && frame.regs[2].tag == ValueTag::Bool &&
        frame.regs[2].as.b && refreshes == 1 &&
        owner.as.obj->refcnt.load() == refs - 2 && value_is(frame.locals[0], owner) &&
        frame.native_call_args.size() == 1 && value_is(frame.native_call_args[0], owner),
        "identity cleanup consumes temporary owners while retaining live aliases and native arguments");
  }
  // A borrowed load is not an owner. A future read forbids consuming its owner.
  {
    auto module = make_module({{ir::Op::Is, 2, 0, 1, 0}, {ir::Op::Move, 3, 0, 0, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    Value owner = Value::list({});
    frame.regs[0] = owner;
    value_borrow_assign_fast(frame.regs[1], owner);
    const auto refs = owner.as.obj->refcnt.load();
    unsigned refreshes = 0;
    xlang_vm::ops::is_op(module.functions[0].code[0], frame, 0, [&] { ++refreshes; });
    expect_true(result, value_is(frame.regs[0], owner) && value_is(frame.regs[1], owner) &&
        (frame.regs[1].flags & kXlangValueBorrowedRefFlag) != 0 &&
        owner.as.obj->refcnt.load() == refs && refreshes == 0,
        "identity cleanup preserves borrowed loads and registers read later");
  }
  // Both output aliases must publish a bool and release each old owner once.
  for (uint32_t output : {0u, 1u}) {
    auto module = make_module({{ir::Op::Is, output, 0, 1, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    Value owner = Value::list({});
    frame.regs[0] = owner;
    frame.regs[1] = owner;
    const auto refs = owner.as.obj->refcnt.load();
    xlang_vm::ops::is_op(module.functions[0].code[0], frame, 0, [] {});
    expect_true(result, frame.regs[output].tag == ValueTag::Bool && frame.regs[output].as.b &&
        frame.regs[1 - output].tag == ValueTag::Invalid &&
        owner.as.obj->refcnt.load() == refs - 2,
        "identity output aliases retain the scalar result and consume exactly two owners");
  }
  {
    auto module = make_module({{ir::Op::Is, 2, 0, 0, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    Value owner = Value::list({});
    frame.regs[0] = owner;
    const auto refs = owner.as.obj->refcnt.load();
    xlang_vm::ops::is_op(module.functions[0].code[0], frame, 0, [] {});
    expect_true(result, frame.regs[2].as.b && frame.regs[0].tag == ValueTag::Invalid &&
        owner.as.obj->refcnt.load() == refs - 1,
        "a duplicated identity operand is consumed exactly once");
  }
  // Linear last use alone cannot prove a loop-carried operand dead.
  {
    auto module = make_module({{ir::Op::Is, 2, 0, 1, 0}, {ir::Op::Jump, 0, 0, 0, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    frame.regs[0] = Value::list({});
    frame.regs[1] = Value::none();
    xlang_vm::ops::is_op(module.functions[0].code[0], frame, 0, [] {});
    expect_true(result, frame.execution_metadata->register_loop_carried[0] &&
        frame.regs[0].tag == ValueTag::Object,
        "ordinary backward edges retain loop-carried identity operands");
  }
  // A loop-local callback result is recreated before each identity read.
  {
    auto module = make_module({{ir::Op::CallGlobal, 0, 0, 0, 0},
        {ir::Op::IsJumpIfFalse, 3, 0, 1, 1}, {ir::Op::Jump, 0, 0, 0, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    frame.regs[0] = Value::list({});
    frame.regs[1] = Value::none();
    size_t ip = 1;
    const auto flow = xlang_vm::ops::is_jump_if_false(module.functions[0].code[1], frame, ip,
        [](int64_t, const Value*) { return true; }, [] {});
    expect_true(result, !frame.execution_metadata->register_loop_carried[0] &&
        frame.regs[0].tag == ValueTag::Invalid && flow == XlangVMOpFlow::Next,
        "recreated callback results retire inside ordinary loops");
  }
  // SDK IR can encode backward fused edges absent from the old loop analysis.
  for (const auto edge : {ir::Op::IsJumpIfFalse, ir::Op::IsNoneJumpIfFalse, ir::Op::CompareJumpIfFalse,
                         ir::Op::MoveJumpIfFalse, ir::Op::MoveJumpIfTrue}) {
    const uint32_t target_field = edge == ir::Op::MoveJumpIfFalse ||
        edge == ir::Op::MoveJumpIfTrue ? 2u : 0u;
    const uint32_t rhs_or_target = edge == ir::Op::MoveJumpIfFalse ||
        edge == ir::Op::MoveJumpIfTrue ? 0u : 3u;
    auto module = make_module({{ir::Op::Is, 2, 0, 1, 0},
        {edge, target_field, 2, rhs_or_target, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    frame.regs[0] = Value::list({});
    frame.regs[1] = Value::none();
    xlang_vm::ops::is_op(module.functions[0].code[0], frame, 0, [] {});
    expect_true(result, !frame.execution_metadata->identity_operand_retirement_safe[0] &&
        frame.regs[0].tag == ValueTag::Object,
        "unsupported fused backedges conservatively retain earlier identity operands");
  }
  {
    auto module = make_module({{ir::Op::IsJumpIfFalse, 0, 0, 1, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    frame.regs[0] = Value::list({});
    frame.regs[1] = Value::none();
    size_t ip = 0;
    const auto flow = xlang_vm::ops::is_jump_if_false(module.functions[0].code[0], frame, ip,
        [](int64_t, const Value*) { return true; }, [] {});
    expect_true(result, !frame.execution_metadata->identity_operand_retirement_safe[0] &&
        frame.regs[0].tag == ValueTag::Object && flow == XlangVMOpFlow::ContinueLoop && ip == 0,
        "a fused identity self-edge preserves its operand on the taken path");
  }
  {
    auto module = make_module({{ir::Op::JumpIfFalse, 2, 3, 0, 0},
        {ir::Op::CallGlobal, 0, 0, 0, 0}, {ir::Op::Is, 2, 0, 1, 0},
        {ir::Op::Jump, 0, 0, 0, 0}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    frame.regs[0] = Value::list({});
    frame.regs[1] = Value::none();
    xlang_vm::ops::is_op(module.functions[0].code[2], frame, 2, [] {});
    expect_true(result, !frame.execution_metadata->register_loop_carried[0] &&
        !frame.execution_metadata->identity_operand_retirement_safe[0] &&
        frame.regs[0].tag == ValueTag::Object,
        "a conditional edge bypassing a loop producer preserves the prior register owner");
  }

  // Native destruction is a bounded way to inspect reentrant frame publication
  // and exact output/right/left cleanup ordering without Python GC ambiguity.
  {
    auto module = make_module({{ir::Op::Is, 2, 0, 1, 0}});
    std::vector<int> order;
    struct Audit { XlangVMFrame* frame; std::vector<int>* order; int label; bool safe = false; };
    Audit left{nullptr, &order, 1}, right{nullptr, &order, 2}, output{nullptr, &order, 3};
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    left.frame = right.frame = output.frame = &frame;
    auto cleanup = [](void* opaque) {
      auto& audit = *static_cast<Audit*>(opaque);
      audit.safe = audit.frame->regs[0].tag == ValueTag::Invalid &&
          audit.frame->regs[1].tag == ValueTag::Invalid &&
          audit.frame->regs[2].tag == ValueTag::Bool && !audit.frame->regs[2].as.b;
      audit.order->push_back(audit.label);
      // Reentry must see no remaining consumed frame owners.
      audit.safe = audit.safe && !audit.frame->release_identity_operands_last_used_at(0, 1, 0, 2);
    };
    frame.regs[0] = Value::native_function(0, "identity_left", nullptr, &left, cleanup);
    frame.regs[1] = Value::native_function(0, "identity_right", nullptr, &right, cleanup);
    frame.regs[2] = Value::native_function(0, "identity_old_output", nullptr, &output, cleanup);
    unsigned refreshes = 0;
    xlang_vm::ops::is_op(module.functions[0].code[0], frame, 0, [&] { ++refreshes; });
    expect_true(result, left.safe && right.safe && output.safe && order == std::vector<int>({3, 2, 1}) &&
        refreshes == 1, "all dead roots unpublish before output/right/left finalizers and monitoring refresh");
    // Failed implementations must also clean their userdata while Audit and
    // frame metadata are alive. Unpublish leftovers before their callbacks.
    Value leftover_left = std::move(frame.regs[0]);
    Value leftover_right = std::move(frame.regs[1]);
    Value leftover_output = std::move(frame.regs[2]);
    value_set_bool(frame.regs[2], false);
    value_set_invalid(leftover_output);
    value_set_invalid(leftover_right);
    value_set_invalid(leftover_left);
  }
  // Branch monitoring errors preserve committed cleanup on both directions.
  for (bool condition : {false, true}) {
    auto module = make_module({{ir::Op::IsJumpIfFalse, 4, 0, 1, condition ? 1u : 0u}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    frame.regs[0] = Value::list({});
    frame.regs[1] = Value::none();
    size_t ip = 0;
    bool refreshed = false;
    bool observed = false;
    const auto flow = xlang_vm::ops::is_jump_if_false(module.functions[0].code[0], frame, ip,
        [&](int64_t event, const Value* destination) {
          observed = refreshed && frame.regs[0].tag == ValueTag::Invalid &&
              event == (condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight) &&
              destination->as.i64 == (condition ? 1 : 4);
          return false;
        }, [&] { refreshed = true; });
    expect_true(result, observed && flow == XlangVMOpFlow::ReturnResult && ip == 0,
        "identity cleanup refreshes before branch callbacks, including callback failure");
  }

  // Pin the real CP phases: only a taken source-RHS-None branch uses its
  // entry RIGHT eligibility; general identity and None fallthrough use the
  // fresh mask. A native cleanup changes that mask during operand disposal.
  for (bool literal_none : {false, true}) {
    for (bool condition : {false, true}) {
      for (int64_t entry : {int64_t{0}, kSysMonitoringEventBranchLeft, kSysMonitoringEventBranchRight}) {
        auto module = make_module({{literal_none ? ir::Op::IsNoneJumpIfFalse : ir::Op::IsJumpIfFalse,
                                    4, 0, 1, condition ? 1u : 0u}});
        struct Enable { XlangVMFrame* frame = nullptr; } enable;
        XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
        enable.frame = &frame;
        frame.monitoring_events = entry;
        frame.regs[0] = Value::native_function(0, "identity_enable_branch", nullptr, &enable,
            [](void* opaque) {
              auto& state = *static_cast<Enable*>(opaque);
              state.frame->monitoring_events = kSysMonitoringEventBranchLeft | kSysMonitoringEventBranchRight;
            });
        frame.regs[1] = Value::none();
        size_t ip = 0;
        bool refreshed = false;
        bool emitted = false;
        const auto flow = xlang_vm::ops::is_jump_if_false(module.functions[0].code[0], frame, ip,
            [&](int64_t event, const Value*) {
              emitted = refreshed && (frame.monitoring_events & event) != 0;
              return true;
            }, [&] { refreshed = true; }, literal_none);
        const bool expected = condition || !literal_none || (entry & kSysMonitoringEventBranchRight) != 0;
        expect_true(result, refreshed && emitted == expected &&
            flow == (condition ? XlangVMOpFlow::Next : XlangVMOpFlow::ContinueLoop) &&
            ip == (condition ? 0u : 4u),
            "identity finalizer branch dispatch matches source shape, direction and entry masks");
        Value leftover = std::move(frame.regs[0]);
        value_set_invalid(leftover);
      }
    }
  }
  for (uint32_t old_negation : {2u, 0x80000000u, UINT32_MAX}) {
    auto module = make_module({{ir::Op::IsJumpIfFalse, 4, 0, 1, old_negation}});
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    frame.regs[0] = Value::list({});
    frame.regs[1] = Value::none();
    size_t ip = 0;
    const auto flow = xlang_vm::ops::is_jump_if_false(module.functions[0].code[0], frame, ip,
        [](int64_t, const Value*) { return true; }, [] {});
    expect_true(result, flow == XlangVMOpFlow::Next,
        "legacy nonzero identity negation values preserve their existing meaning");
  }

  // The real failing loop owns two loaded Item copies, independently of its
  // weakref-result identity. Exercise existing deletion against those owners
  // without an identity opcode, so incoming-edge collection cannot be gated
  // solely on identity instructions.
  {
    ir::Module module;
    module.global_slots = {"item"};
    module.functions.emplace_back();
    auto& function = module.functions[0];
    function.register_count = 8;
    function.names = {"ref", "clear"};
    function.list_items = {{0}};
    function.call_args = {{1}, {}};
    function.code = {{ir::Op::IterNext, 7, 6, 8, 0},
        {ir::Op::LoadModuleSlot, 0, 0, 0, 0}, {ir::Op::MakeList, 2, 0, 0, 0},
        {ir::Op::LoadModuleSlot, 1, 0, 0, 0}, {ir::Op::CallMethod, 3, 4, 0, 0},
        {ir::Op::DeleteModuleSlot, 0, 0, 0, 0}, {ir::Op::CallMethod, 5, 2, 1, 1},
        {ir::Op::Jump, 0, 0, 0, 0}};
    Value globals = Value::module("recreated_module_slot_owner");
    Value owner = Value::list({});
    Value holder = Value::list({owner});
    std::string error;
    expect_true(result, module_set_attr(globals, "item", owner, error),
        "module loop fixture binds its actual namespace owner");
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, globals, {}, 0, false);
    frame.regs[0] = owner;
    frame.regs[1] = owner;
    frame.regs[2] = holder;
    frame.native_call_args.push_back(owner);
    const auto refs = owner.as.obj->refcnt.load();
    RuntimeResult run_result;
    const auto flow = xlang_vm::ops::delete_module_slot(function.code[5], module, globals,
        frame.regs, frame.native_call_args, frame.instr_cache,
        frame.execution_metadata->register_last_use, frame.execution_metadata->register_loop_carried,
        5, run_result, [](const std::string&) { return false; });
    expect_true(result, frame.execution_metadata->identity_operand_retirement_safe.empty() &&
        !frame.execution_metadata->register_loop_carried[0] &&
        !frame.execution_metadata->register_loop_carried[1] &&
        frame.execution_metadata->register_last_use[0] == 2 &&
        frame.execution_metadata->register_last_use[1] == 4 &&
        flow == XlangVMOpFlow::Next && run_result.errors.empty() &&
        frame.regs[0].tag == ValueTag::Invalid && frame.regs[1].tag == ValueTag::Invalid &&
        value_is(frame.regs[2], holder) && frame.native_call_args.empty() &&
        owner.as.obj->refcnt.load() == refs - 4,
        "module deletion retires dominated loop-load copies and native args while preserving the holder");
    value_as_list(holder)->items.clear();
    expect_true(result, owner.as.obj->refcnt.load() == 1,
        "clearing the actual holder leaves no hidden module-loop snapshot owners");
  }
  auto check_module_carried = [&](std::vector<ir::Instr> code, const char* message) {
    ir::Module module;
    module.global_slots = {"item"};
    module.functions.emplace_back();
    auto& function = module.functions[0];
    function.register_count = 5;
    function.list_items = {{0}};
    function.code = std::move(code);
    XlangVMFrame frame(module, 0, CallArgsView{}, closure, Value::none(), {}, 0, false);
    expect_true(result, frame.execution_metadata->register_loop_carried[0] &&
        frame.execution_metadata->register_last_use[0] == SIZE_MAX, message);
  };
  check_module_carried({{ir::Op::JumpIfFalse, 2, 3, 0, 0},
      {ir::Op::LoadModuleSlot, 0, 0, 0, 0}, {ir::Op::MakeList, 2, 0, 0, 0},
      {ir::Op::Jump, 0, 0, 0, 0}},
      "a conditional edge bypassing a module load preserves the carried owner");
  check_module_carried({{ir::Op::MakeList, 2, 0, 0, 0},
      {ir::Op::LoadModuleSlot, 0, 0, 0, 0}, {ir::Op::Jump, 0, 0, 0, 0}},
      "a read before the module load preserves the prior iteration owner");
  check_module_carried({{ir::Op::LoadModuleSlot, 0, 0, 0, 0},
      {ir::Op::MakeList, 2, 0, 0, 0}, {ir::Op::Jump, 1, 0, 0, 0},
      {ir::Op::Jump, 0, 0, 0, 0}},
      "a wider recreated load cannot clear a carried owner established by an inner loop");
  check_module_carried({{ir::Op::LoadModuleSlot, 0, 0, 0, 0},
      {ir::Op::MakeList, 2, 0, 0, 0}, {ir::Op::IsJumpIfFalse, 1, 3, 4, 0},
      {ir::Op::Jump, 0, 0, 0, 0}},
      "a fused backedge entering after the module load preserves its owner");
  check_module_carried({{ir::Op::SetupExcept, 2, 0, 0, 0},
      {ir::Op::LoadModuleSlot, 0, 0, 0, 0}, {ir::Op::MakeList, 2, 0, 0, 0},
      {ir::Op::Jump, 0, 0, 0, 0}},
      "an exception target bypassing the module load preserves its owner");

  const std::string source = R"PY(
none_alias = None
def produce(value):
    return value
def rhs_literal(value):
    if produce(value) is None:
        return 1
    return 0
def reversed_literal(value):
    if None is produce(value):
        return 1
    return 0
def dynamic_none(value):
    if produce(value) is none_alias:
        return 1
    return 0
def ordinary_value(value):
    return produce(value) is None
def local_literal(value):
    if value is None:
        return 1
    return 0
def unary_literal(value):
    if not (produce(value) is not None):
        return 1
    return 0
def multiline_literal(value):
    if (
        produce(value) is None
    ):
        return 1
    return 0
def multiline_not_literal(value):
    if not not (
        produce(value) is None
    ):
        return 1
    return 0
)PY";
  auto parsed = parse_source(source);
  expect_true(result, parsed.errors.empty(), "identity provenance source parses");
  if (!parsed.errors.empty()) return;
  auto lowered = lower_to_ir(parsed.module);
  expect_true(result, lowered.errors.empty(), "identity provenance source lowers");
  if (!lowered.errors.empty()) return;
  for (const auto& function : lowered.module.functions) {
    size_t none_branches = 0, general_branches = 0, local_branches = 0, identities = 0;
    for (size_t ip = 0; ip < function.code.size(); ++ip) {
      const auto& in = function.code[ip];
      if (in.op == ir::Op::IsNoneJumpIfFalse) {
        ++none_branches;
        expect_true(result, in.c == 0 && ip < function.source_lines.size() && function.source_lines[ip] != 0,
            "source-proven RHS-None branch preserves negation and source position");
      }
      general_branches += in.op == ir::Op::IsJumpIfFalse;
      local_branches += in.op == ir::Op::IsLocalConstJumpIfFalse;
      identities += in.op == ir::Op::Is;
    }
    if (function.name == "rhs_literal" || function.name == "unary_literal" ||
        function.name == "multiline_literal" || function.name == "multiline_not_literal")
      expect_true(result, none_branches == 1 && general_branches == 0,
          "literal RHS-None, unary and multiline conditions retain explicit source provenance");
    if (function.name == "reversed_literal" || function.name == "dynamic_none")
      expect_true(result, none_branches == 0 && general_branches == 1,
          "reversed and dynamic None values do not acquire literal-RHS provenance");
    if (function.name == "ordinary_value")
      expect_true(result, none_branches == 0 && identities == 1,
          "ordinary value identity keeps its existing Is layout");
    if (function.name == "local_literal")
      expect_true(result, none_branches == 0 && local_branches == 1,
          "ordinary same-line local-constant identity retains its existing fast opcode");
  }
  constexpr uint64_t source_hash = 0x1de17;
  ir::EncodedModule encoded;
  std::string error;
  const bool encoded_ok = ir::encode_module(lowered.module, source_hash, encoded, error);
  expect_true(result, encoded_ok && encoded.bytes.size() >= 8, "identity provenance IR encodes");
  if (!encoded_ok || encoded.bytes.size() < 8) return;
  ir::Module decoded;
  const bool decoded_ok = ir::decode_module(encoded.bytes.data(), encoded.bytes.size(), source_hash, decoded, error);
  expect_true(result, decoded_ok && ir::dump_module(decoded) == ir::dump_module(lowered.module),
      "source-proven identity opcodes survive the unchanged five-field codec layout");
  auto old_cache = encoded.bytes;
  old_cache[4] = 63;
  old_cache[5] = old_cache[6] = old_cache[7] = 0;
  ir::Module rejected;
  expect_true(result, !ir::decode_module(old_cache.data(), old_cache.size(), source_hash, rejected, error) &&
      error == "IR cache has unsupported format",
      "pre-provenance version63 caches reject so import can recompile their source");
  auto legacy = make_module({{ir::Op::IsJumpIfFalse, 4, 0, 1, 2},
      {ir::Op::IsNoneJumpIfFalse, 4, 0, 1, 0}});
  expect_true(result, ir::encode_module(legacy, source_hash, encoded, error) &&
      ir::decode_module(encoded.bytes.data(), encoded.bytes.size(), source_hash, decoded, error) &&
      decoded.functions[0].code[0].op == ir::Op::IsJumpIfFalse && decoded.functions[0].code[0].c == 2 &&
      decoded.functions[0].code[1].op == ir::Op::IsNoneJumpIfFalse && decoded.functions[0].code[1].c == 0,
      "new provenance opcode roundtrips alongside unchanged legacy nonzero-c instructions");
}

} // namespace xlang3::test
