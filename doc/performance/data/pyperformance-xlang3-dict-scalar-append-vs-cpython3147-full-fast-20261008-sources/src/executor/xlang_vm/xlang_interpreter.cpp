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
#include "xlang3/interpreter.h"

#include "xlang_frame.h"
#include "xlang_vm_inline_support.h"

#include "xlang3/generator.h"
#include "xlang3/module_object.h"

namespace xlang3 {

// This is the same liveness/ownership proof used by XlangVMFrame. Leave its
// cold construction/CAS retry semantics here and retain the cached fast check
// in the frame header. The shared runtime already compiles this separate TU.
void XlangVMFrame::compute_register_last_use_slow(
    std::shared_ptr<const ir::FunctionExecutionMetadata> prepared) {
  auto computed = std::make_shared<ir::FunctionExecutionMetadata>();
  computed->owner = fn;
  computed->linear_constructor_argument_liveness =
      !fn->is_generator && !fn->is_async && !fn->is_coroutine;
  bool has_identity_instructions = false;
  bool has_module_slot_loads = false;
  bool has_exception_loads = false;
  bool has_ordinary_backedge = false;
  computed->cache_cleanup_instructions.reserve(fn->code.size() / 8);
  for (size_t ip = 0; ip < fn->code.size(); ++ip) {
    if (fn->code[ip].op == ir::Op::Is || fn->code[ip].op == ir::Op::IsJumpIfFalse ||
        fn->code[ip].op == ir::Op::IsNoneJumpIfFalse)
      has_identity_instructions = true;
    if (fn->code[ip].op == ir::Op::LoadModuleSlot) has_module_slot_loads = true;
    if (fn->code[ip].op == ir::Op::LoadException) has_exception_loads = true;
    if ((fn->code[ip].op == ir::Op::Jump || fn->code[ip].op == ir::Op::JumpIfFalse) &&
        fn->code[ip].dst < ip) has_ordinary_backedge = true;
    // Constructor-only owner transfer uses a stricter proof than legacy
    // positional calls. Unknown/fused/handler/iterator control flow retains
    // arguments; cache this once rather than scanning IR at each constructor.
    switch (fn->code[ip].op) {
      case ir::Op::LoadConst: case ir::Op::LoadConstPair:
      case ir::Op::Move: case ir::Op::LoadLocal: case ir::Op::LoadLocalPair:
      case ir::Op::LoadLocalConst: case ir::Op::LoadLocalGlobal:
      case ir::Op::LoadGlobalLocal: case ir::Op::LoadGlobal:
      case ir::Op::LoadModuleSlot: case ir::Op::LoadModuleAttr:
      case ir::Op::StoreLocal: case ir::Op::StoreLocalPair:
      case ir::Op::LoadAttr: case ir::Op::LoadLocalAttr:
      case ir::Op::GetItem: case ir::Op::GetItemConst:
      case ir::Op::MakeTuple: case ir::Op::MakeList: case ir::Op::MakeDict:
      case ir::Op::Call: case ir::Op::CallLocal: case ir::Op::CallGlobal:
      case ir::Op::Return: case ir::Op::ReturnConst: case ir::Op::ReturnLocal:
      case ir::Op::Pop:
        break;
      default:
        computed->linear_constructor_argument_liveness = false;
        break;
    }
    if (instruction_may_own_inline_cache(fn->code[ip].op)) {
      computed->cache_cleanup_instructions.push_back(static_cast<uint32_t>(ip));
    }
  }
  computed->register_last_use.assign(
      fn->register_count, std::numeric_limits<size_t>::max());
  computed->register_loop_carried.assign(fn->register_count, false);
  for (size_t i = 0; i < fn->code.size(); ++i) {
    for_each_register_read(fn->code[i], [&](uint32_t reg) {
      if (reg < computed->register_last_use.size()) {
        computed->register_last_use[reg] = i;
      }
    });
  }
  // Owning module/exception loads need a linear-use snapshot only in loops.
  // Keep this cold proof/storage out of callbacks without either load kind.
  const bool need_module_load_loop_proof =
      (has_module_slot_loads || has_exception_loads) && has_ordinary_backedge;
  std::vector<size_t> linear_register_last_use;
  if (need_module_load_loop_proof) linear_register_last_use = computed->register_last_use;
  // Cache identity-only proofs separately from existing call-transfer
  // liveness. Incoming edges can bypass a loop's temporary producer even
  // though the old loop analysis sees some Call writing that register.
  std::vector<std::pair<size_t, uint32_t>> incoming_edges;
  // Functions without identity instructions allocate no new proof storage.
  if (has_identity_instructions)
    computed->identity_operand_retirement_safe.assign(fn->register_count, true);
  for (size_t source = 0; (has_identity_instructions || need_module_load_loop_proof) && source < fn->code.size(); ++source) {
    const auto& branch = fn->code[source];
    uint32_t target = UINT32_MAX;
    switch (branch.op) {
      case ir::Op::Jump:
      case ir::Op::JumpIfFalse:
      case ir::Op::JumpIfLocalConstFalse:
      case ir::Op::CompareJumpIfFalse:
      case ir::Op::IsJumpIfFalse:
      case ir::Op::IsNoneJumpIfFalse:
      case ir::Op::NotJumpIfFalse:
      case ir::Op::JumpIfLocalLocalFalse:
      case ir::Op::IsLocalConstJumpIfFalse:
      case ir::Op::ForRangeConstLocalNext:
      case ir::Op::ForRangeConstLocalSum:
      case ir::Op::ForLocalMoveAddLoop:
      case ir::Op::ForCallAccumulateLoop:
      case ir::Op::ForConstructMethodAccumulateLoop:
      case ir::Op::ForScalarArithmeticLoop:
      case ir::Op::ForPropertyAccessLoop:
      case ir::Op::SetupExcept:
      case ir::Op::SetupWith:
        target = branch.dst;
        break;
      case ir::Op::MoveJumpIfFalse:
      case ir::Op::MoveJumpIfTrue:
      case ir::Op::JumpIfFalseLoadLocal:
      case ir::Op::IterNext:
      case ir::Op::IterNextLocal:
        target = branch.b;
        break;
      default:
        break;
    }
    if (target == UINT32_MAX) continue;
    incoming_edges.emplace_back(source, target);
    // SDK IR may encode a backward fused/exception/iterator edge outside
    // the ordinary analysis below. Retain only that region's read owners;
    // an unrelated earlier loop must not disable later expression cleanup.
    if (target <= source && branch.op != ir::Op::Jump && branch.op != ir::Op::JumpIfFalse) {
      for (size_t loop_ip = target; loop_ip <= source; ++loop_ip) {
        for_each_register_read(fn->code[loop_ip], [&](uint32_t reg) {
          if (reg < computed->identity_operand_retirement_safe.size())
            computed->identity_operand_retirement_safe[reg] = false;
        });
      }
    }
  }
  // Values read in a loop may be needed again after a backward edge. Keep
  // those registers for the frame lifetime; the linear last-use index alone
  // cannot express loop-carried liveness.
  for (size_t i = 0; i < fn->code.size(); ++i) {
    const auto& instr = fn->code[i];
    if ((instr.op != ir::Op::Jump && instr.op != ir::Op::JumpIfFalse) ||
        instr.dst >= i) {
      continue;
    }
    // Call results and container literals produced inside a loop are
    // overwritten on every iteration, so their registers are not roots
    // carried into the next iteration.
    std::vector<bool> temporary_defined_in_loop(fn->register_count, false);
    std::vector<size_t> temporary_definition_in_loop((has_identity_instructions || need_module_load_loop_proof) ? fn->register_count : 0, SIZE_MAX);
    for (size_t loop_ip = instr.dst; loop_ip <= i; ++loop_ip) {
      const auto& loop_instr = fn->code[loop_ip];
      switch (loop_instr.op) {
        case ir::Op::LoadModuleSlot:
        case ir::Op::LoadException:
          if (need_module_load_loop_proof && loop_instr.dst < temporary_definition_in_loop.size() &&
              !temporary_defined_in_loop[loop_instr.dst] && temporary_definition_in_loop[loop_instr.dst] == SIZE_MAX)
            temporary_definition_in_loop[loop_instr.dst] = loop_ip;
          break;
        case ir::Op::Call:
        case ir::Op::CallEx:
        case ir::Op::CallMethod:
        case ir::Op::CallMethodEx:
        case ir::Op::CallLocal:
        case ir::Op::CallLocalMethod:
        case ir::Op::CallModuleMethod:
        case ir::Op::CallGlobal:
        case ir::Op::MakeDict:
        case ir::Op::MakeList:
        case ir::Op::MakeSet:
        case ir::Op::MakeTuple:
          if (loop_instr.dst < temporary_defined_in_loop.size()) {
            temporary_defined_in_loop[loop_instr.dst] = true;
            if (has_identity_instructions || need_module_load_loop_proof)
              temporary_definition_in_loop[loop_instr.dst] = loop_ip;
          }
          break;
        default:
          break;
      }
    }
    if (need_module_load_loop_proof) {
      // These loads create owning snapshots, unlike borrowed local reads.
      // Prove recreation before every read so StoreLocal can move a handled
      // exception snapshot instead of retaining its traceback/frame graph
      // until function return. Bypass and nested-loop owners remain live.
      for (size_t loop_ip = instr.dst; loop_ip <= i; ++loop_ip) {
        for_each_register_read(fn->code[loop_ip], [&](uint32_t reg) {
          if (reg >= temporary_definition_in_loop.size() || temporary_defined_in_loop[reg]) return;
          const size_t producer = temporary_definition_in_loop[reg];
          if (producer != SIZE_MAX &&
              (fn->code[producer].op == ir::Op::LoadModuleSlot ||
               fn->code[producer].op == ir::Op::LoadException) && loop_ip <= producer)
            temporary_definition_in_loop[reg] = SIZE_MAX;
        });
      }
      for (uint32_t reg = 0; reg < temporary_definition_in_loop.size(); ++reg) {
        const size_t producer = temporary_definition_in_loop[reg];
        if (temporary_defined_in_loop[reg] || producer == SIZE_MAX ||
            (fn->code[producer].op != ir::Op::LoadModuleSlot &&
             fn->code[producer].op != ir::Op::LoadException)) continue;
        const size_t consumer = std::min(linear_register_last_use[reg], i);
        if (consumer <= producer) continue;
        bool dominates_reads = true;
        for (const auto& edge : incoming_edges) {
          // A handler's installation IP is not its throw origin. A later
          // failure can enter after the load even when setup is inside this
          // interval; do not move that exception snapshot out of its register.
          const bool exception_handler_target =
              fn->code[producer].op == ir::Op::LoadException &&
              (fn->code[edge.first].op == ir::Op::SetupExcept ||
               fn->code[edge.first].op == ir::Op::SetupWith);
          if (edge.second > producer && edge.second <= consumer &&
              (exception_handler_target || edge.first < producer || edge.first > consumer)) {
            dominates_reads = false;
            break;
          }
        }
        if (dominates_reads) temporary_defined_in_loop[reg] = true;
        // Never clear a carried flag/MAX established by a narrower loop.
      }
    }
    for (uint32_t reg = 0; has_identity_instructions && reg < computed->register_last_use.size(); ++reg) {
      const size_t consumer = computed->register_last_use[reg];
      if (!temporary_defined_in_loop[reg] || consumer < instr.dst || consumer > i ||
          (fn->code[consumer].op != ir::Op::Is && fn->code[consumer].op != ir::Op::IsJumpIfFalse &&
           fn->code[consumer].op != ir::Op::IsNoneJumpIfFalse)) {
        continue;
      }
      const size_t producer = temporary_definition_in_loop[reg];
      if (producer >= consumer) {
        computed->identity_operand_retirement_safe[reg] = false;
        continue;
      }
      for (const auto& edge : incoming_edges) {
        if (edge.second > producer && edge.second <= consumer &&
            (edge.first < producer || edge.first > consumer)) {
          computed->identity_operand_retirement_safe[reg] = false;
          break;
        }
      }
    }
    for (size_t loop_ip = instr.dst; loop_ip <= i; ++loop_ip) {
      for_each_register_read(fn->code[loop_ip], [&](uint32_t reg) {
        // LoadModuleAttr writes its receiver scratch register from the live
        // module slot before reading it internally. That read is not a root
        // carried from the previous iteration. Keep its linear last-use for
        // cleanup, and let any later external read establish real liveness.
        // Otherwise `del obj` in a loop retains obj in this fused temporary.
        const auto& loop_instr = fn->code[loop_ip];
        if (loop_instr.op == ir::Op::LoadModuleAttr && reg == loop_instr.c) return;
        if (reg < computed->register_last_use.size() &&
            !temporary_defined_in_loop[reg]) {
          computed->register_loop_carried[reg] = true;
          computed->register_last_use[reg] = std::numeric_limits<size_t>::max();
        }
      });
    }
  }
  // Function copies inherit the source's cache pointer. Replace metadata
  // whose owner is a different Function object instead of using stale
  // register indices from that source object.
  std::shared_ptr<const ir::FunctionExecutionMetadata> expected = prepared;
  std::shared_ptr<const ir::FunctionExecutionMetadata> immutable = std::move(computed);
  if (!std::atomic_compare_exchange_strong_explicit(
          &fn->execution_metadata,
          &expected,
          immutable,
          std::memory_order_release,
          std::memory_order_acquire)) {
    if (expected != nullptr && expected->owner == fn) {
      execution_metadata = std::move(expected);
    } else {
      compute_register_last_use();
    }
  } else {
    execution_metadata = std::move(immutable);
  }
}

namespace {
thread_local uint32_t g_nested_interpreter_depth = 0;

struct NestedInterpreterGuard {
  NestedInterpreterGuard() { ++g_nested_interpreter_depth; }
  ~NestedInterpreterGuard() { --g_nested_interpreter_depth; }
};
} // namespace

Interpreter::Interpreter(Runtime& runtime) : runtime_(runtime) {}

bool runtime_call_builtin_constructor(Runtime& runtime, const ClassObject& klass,
    const Value* args, uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& kwargs,
    bool& handled, Value& out, std::string& error) {
  handled = false;
  auto constructor = xlang_vm_find_builtin_constructor(klass.name);
  if (constructor != XlangVMBuiltinConstructor::Unknown &&
      !xlang_vm_class_is_builtin_module_class(klass)) {
    constructor = XlangVMBuiltinConstructor::Unknown;
  }
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    constructor = xlang_vm_find_inherited_builtin_constructor(klass);
  }
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    return true;
  }
  handled = true;
  CallArgsView view;
  view.leading = args;
  view.leading_count = argc;
  std::vector<Value> keyword_values;
  std::vector<ir::CallKeywordArg> keyword_specs;
  keyword_values.reserve(kwargs.size());
  keyword_specs.reserve(kwargs.size());
  for (const auto& item : kwargs) {
    keyword_specs.push_back({item.first, static_cast<uint32_t>(keyword_values.size())});
    keyword_values.push_back(item.second);
  }
  if (!kwargs.empty()) {
    view.registers = keyword_values.data();
    view.keyword_args = &keyword_specs;
  }
  XlangRuntimeExecutionGuard lock;
  XlangVMBuiltinConstructorError detail;
  if (call_builtin_type_constructor(runtime, klass, view, lock, out, detail)) return true;
  error = detail.message.empty() ? "builtin constructor failed" : detail.message;
  Value pending;
  if (runtime.take_pending_exception(pending)) runtime.set_pending_exception(std::move(pending));
  else runtime.raise_class_error(detail.type, error);
  return false;
}

RuntimeResult Interpreter::run(const ir::Module& module) {
  auto globals_module = Value::module("__main__");
  std::string ignored;
  module_set_attr(globals_module, "__spec__", Value::none(), ignored);
  return run_module(module, std::move(globals_module), nullptr);
}

RuntimeResult Interpreter::run(std::shared_ptr<const ir::Module> module) {
  RuntimeResult result;
  if (module == nullptr) {
    result.errors.push_back("invalid module");
    return result;
  }
  auto module_owner = std::move(module);
  auto globals_module = Value::module("__main__");
  std::string ignored;
  module_set_attr(globals_module, "__spec__", Value::none(), ignored);
  return run_module(*module_owner, std::move(globals_module), module_owner);
}

RuntimeResult Interpreter::run_module(const ir::Module& module, Value globals_module) {
  return run_module(module, std::move(globals_module), nullptr);
}

RuntimeResult Interpreter::run_module(
    const ir::Module& module,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner) {
  return run_module(module, std::move(globals_module), std::move(module_owner), true);
}

RuntimeResult Interpreter::run_module(
    const ir::Module& module,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner,
    bool register_in_runtime) {
  RuntimeResult result;
  // Calls made by native helpers re-enter the interpreter recursively and
  // therefore consume the host C stack.  Keep that path below Windows' stack
  // ceiling even when Python temporarily raises its visible recursion limit.
  constexpr uint32_t kSafeNestedInterpreterLimit = 256;
  const uint32_t effective_recursion_limit = std::min(
      static_cast<uint32_t>(runtime_.recursion_limit()), kSafeNestedInterpreterLimit);
  if (g_nested_interpreter_depth >= effective_recursion_limit) {
    result.exception = runtime_.make_exception(
        "RecursionError", "maximum recursion depth exceeded");
    result.errors.push_back("maximum recursion depth exceeded");
    return result;
  }
  NestedInterpreterGuard nested_guard;
  if (auto* globals = value_as_module(globals_module)) {
    std::string error;
    if (!module_ensure_attr_slots(globals_module, module.global_slots, error)) {
      result.errors.push_back(error);
      return result;
    }
    Value existing;
    auto name = globals->name.empty() ? "__main__" : globals->name;
    const auto name_slot = globals->name_to_slot.find("__name__");
    const bool has_name = name_slot != globals->name_to_slot.end() &&
        name_slot->second < globals->slots.size() && globals->slots[name_slot->second].tag != ValueTag::Invalid;
    if (register_in_runtime || globals->implicit_name || has_name) {
      if (!module_set_attr(globals_module, "__name__", Value::string(name), error)) {
        result.errors.push_back(error);
        return result;
      }
      const Value module_doc =
        module.entry < module.functions.size() && !module.functions[module.entry].doc.empty()
            ? Value::string(module.functions[module.entry].doc)
            : Value::none();
      if (!module_set_attr(globals_module, "__doc__", module_doc, error)) {
        result.errors.push_back(error);
        return result;
      }
      const bool synthetic_source_file =
        module.source_file.size() >= 2 && module.source_file.front() == '<' &&
        module.source_file.back() == '>';
      if (!module.source_file.empty() && !synthetic_source_file &&
          (!module_get_attr(globals_module, "__file__", existing, error) || existing.tag == ValueTag::Invalid)) {
        error.clear();
        if (!module_set_attr(globals_module, "__file__", Value::string(module.source_file), error)) {
          result.errors.push_back(error);
          return result;
        }
      }
      if (!module_get_attr(globals_module, "__package__", existing, error) || existing.tag == ValueTag::Invalid) {
        error.clear();
        if (!module_set_attr(globals_module, "__package__", Value::string(""), error)) {
          result.errors.push_back(error);
          return result;
        }
      }
      if (!module_get_attr(globals_module, "__annotations__", existing, error) || existing.tag == ValueTag::Invalid) {
        error.clear();
        if (!module_set_attr(globals_module, "__annotations__", Value::dict({}), error)) {
          result.errors.push_back(error);
          return result;
        }
      }
    }
    if (!module_get_attr(globals_module, "__builtins__", existing, error) || existing.tag == ValueTag::Invalid) {
      error.clear();
      Value builtins;
      if (mapping_get_item(
              runtime_.module_registry_dict(), Value::string("builtins"), builtins, error)) {
        if (!module_set_attr(globals_module, "__builtins__", builtins, error)) {
          result.errors.push_back(error);
          return result;
        }
      } else {
        error.clear();
      }
    }
    if (register_in_runtime) {
      runtime_.register_module(name, globals_module);
    }
  }
  static const std::vector<Value> empty_closure;
  static const std::vector<Value> empty_defaults;
  return run_function(
      module,
      module.entry,
      {},
      empty_closure,
      empty_defaults,
      std::move(globals_module),
      std::move(module_owner),
      nullptr);
}

RuntimeResult Interpreter::run_function_value(FunctionObject* function, CallArgsView args) {
  RuntimeResult result;
  if (function == nullptr || function->module == nullptr) {
    result.errors.push_back("function has no module");
    return result;
  }
  result = run_function(
      *function->module,
      function->function_id,
      args.with_keyword_defaults(*function),
      function->closure,
      function->defaults,
      function->globals_module,
      function->module,
      nullptr);
  // A Python callee may have taken a temporary sys._getframe() snapshot.
  // Reclaim registry-only frames after its locals are gone, so those frames
  // do not keep unrelated caller locals alive beyond their deletion.
  runtime_.refresh_live_frame_snapshots(false, true);
  if (!result.errors.empty() && result.exception.tag == ValueTag::Invalid) {
    Value pending;
    if (runtime_.take_pending_exception(pending)) {
      value_assign_fast(result.exception, pending);
      runtime_.set_pending_exception(std::move(pending));
    } else if (runtime_.active_exception().tag != ValueTag::Invalid) {
      value_assign_fast(result.exception, runtime_.active_exception());
    }
  }
  return result;
}

RuntimeResult Interpreter::resume_paused(std::shared_ptr<RuntimeDebugPauseState> pause_state) {
  RuntimeResult result;
  if (pause_state == nullptr || pause_state->frame_count == 0 || pause_state->frames.empty()) {
    result.errors.push_back("invalid paused debug state");
    return result;
  }
  const auto& entry = pause_state->frames[0];
  if (entry.module == nullptr || entry.fn == nullptr) {
    result.errors.push_back("invalid paused debug frame");
    return result;
  }
  static const std::vector<Value> empty_closure;
  static const std::vector<Value> empty_defaults;
  return run_function(
      *entry.module,
      entry.function_id,
      {},
      entry.closure == nullptr ? empty_closure : *entry.closure,
      empty_defaults,
      entry.globals_module,
      entry.module_owner,
      nullptr,
      std::move(pause_state));
}

RuntimeResult Interpreter::resume_generator(GeneratorObject& generator, Value& out, bool& done) {
  RuntimeResult result;
  auto* function = value_as_function(generator.function);
  if (function == nullptr || function->module == nullptr) {
    result.errors.push_back("function has no module");
    return result;
  }
  CallArgsView args;
  args.leading = generator.args.data();
  args.leading_count = static_cast<uint32_t>(generator.args.size());
  result = run_function(
      *function->module,
      function->function_id,
      args.with_keyword_defaults(*function),
      function->closure,
      function->defaults,
      function->globals_module,
      function->module,
      &generator);
  if (!result.errors.empty()) {
    return result;
  }
  value_assign_fast(out, result.value);
  done = generator.done;
  return result;
}

} // namespace xlang3
