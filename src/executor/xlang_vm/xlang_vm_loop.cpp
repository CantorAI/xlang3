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
#include "xlang_vm_arithmetic.h"
#include "xlang_vm_attr.h"
#include "xlang_vm_inline_call.h"
#include "runtime_lock.h"

#include "xlang3/attribute.h"
#include "xlang3/generator.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#ifndef XLANG3_EMBEDDED
#include "task_objects.h"
#endif

#include <cstddef>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>

namespace xlang3 {

namespace {

struct GeneratorVMState {
  std::vector<VMFrame> frames;
  size_t frame_count = 0;
};

void destroy_generator_vm_state(void* state) {
  delete static_cast<GeneratorVMState*>(state);
}

} // namespace

RuntimeResult Interpreter::run_function(
    const ir::Module& module,
    uint32_t function_id,
    CallArgsView args,
    const std::vector<Value>& fn_obj_closure,
    const std::vector<Value>& fn_obj_defaults,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner,
    GeneratorObject* generator) {
  RuntimeResult result;
  if (function_id >= module.functions.size()) {
    result.errors.push_back("invalid function id");
    return result;
  }
  const auto& fn = module.functions[function_id];
  auto simple_signature = [](const ir::Function& candidate) -> bool {
    if (candidate.signature.empty()) {
      return true;
    }
    for (const auto& param : candidate.signature) {
      if (param.kind != ir::ParamKind::PosOrKeyword || param.default_reg != UINT32_MAX) {
        return false;
      }
    }
    return true;
  };

  auto bind_args = [&](const ir::Function& target_fn,
                       CallArgsView values,
                       const std::vector<Value>& defaults,
                       std::vector<Value>& bound) -> bool {
    std::vector<ir::Param> synthetic_signature;
    const std::vector<ir::Param>* signature_ptr = &target_fn.signature;
    if (target_fn.signature.empty()) {
      synthetic_signature.reserve(target_fn.params.size());
      for (const auto& name : target_fn.params) {
        synthetic_signature.push_back(ir::Param{name, ir::ParamKind::PosOrKeyword, UINT32_MAX});
      }
      signature_ptr = &synthetic_signature;
    }
    const auto& signature = *signature_ptr;
    if (target_fn.signature.empty() && !values.has_keywords() && !values.has_expansion()) {
      if (values.size() != target_fn.params.size()) {
        result.errors.push_back("function '" + target_fn.name + "' expected " + std::to_string(target_fn.params.size()) +
                                " arguments, got " + std::to_string(values.size()));
        return false;
      }
      return true;
    }

    bound.assign(target_fn.params.size(), Value::invalid());
    std::vector<Value> positional;
    positional.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      positional.push_back(values.get(i));
    }
    if (values.star_arg != UINT32_MAX) {
      const Value& star = values.registers[values.star_arg];
      if (auto* tuple = value_as_tuple(star)) {
        for (const auto& item : tuple->items) positional.push_back(item);
      } else if (auto* list = value_as_list(star)) {
        for (const auto& item : list->items) positional.push_back(item);
      } else {
        result.errors.push_back("function '" + target_fn.name + "' * argument must be tuple or list");
        return false;
      }
    }

    int32_t varargs_index = -1;
    int32_t kwargs_index = -1;
    size_t next_positional_param = 0;
    size_t positional_index = 0;
    std::vector<Value> extra_positional;
    std::vector<std::pair<Value, Value>> extra_keywords;
    for (size_t i = 0; i < signature.size(); ++i) {
      if (signature[i].kind == ir::ParamKind::VarArgs) {
        varargs_index = static_cast<int32_t>(i);
      } else if (signature[i].kind == ir::ParamKind::KwArgs) {
        kwargs_index = static_cast<int32_t>(i);
      }
    }
    while (positional_index < positional.size()) {
      while (next_positional_param < signature.size() &&
             (signature[next_positional_param].kind == ir::ParamKind::KeywordOnly ||
              signature[next_positional_param].kind == ir::ParamKind::VarArgs ||
              signature[next_positional_param].kind == ir::ParamKind::KwArgs)) {
        ++next_positional_param;
      }
      if (next_positional_param < signature.size()) {
        value_assign_fast(bound[next_positional_param], positional[positional_index++]);
        ++next_positional_param;
      } else if (varargs_index >= 0) {
        extra_positional.push_back(positional[positional_index++]);
      } else {
        result.errors.push_back("function '" + target_fn.name + "' got too many positional arguments");
        return false;
      }
    }
    if (varargs_index >= 0) {
      bound[static_cast<size_t>(varargs_index)] = Value::tuple(std::move(extra_positional));
    }

    auto bind_keyword = [&](const std::string& name, const Value& value) -> bool {
      for (size_t i = 0; i < signature.size(); ++i) {
        if (signature[i].name != name) continue;
        if (signature[i].kind == ir::ParamKind::PosOnly) {
          result.errors.push_back("function '" + target_fn.name + "' got positional-only argument as keyword");
          return false;
        }
        if (bound[i].tag != ValueTag::Invalid) {
          result.errors.push_back("function '" + target_fn.name + "' got multiple values for argument '" + name + "'");
          return false;
        }
        value_assign_fast(bound[i], value);
        return true;
      }
      if (kwargs_index >= 0) {
        extra_keywords.push_back(std::make_pair(Value::string(name), value));
        return true;
      }
      result.errors.push_back("function '" + target_fn.name + "' got unexpected keyword argument '" + name + "'");
      return false;
    };
    if (values.keyword_args != nullptr) {
      for (const auto& keyword : *values.keyword_args) {
        if (!bind_keyword(keyword.name, values.registers[keyword.value_reg])) {
          return false;
        }
      }
    }
    if (values.kw_star_arg != UINT32_MAX) {
      auto* dict = value_as_dict(values.registers[values.kw_star_arg]);
      if (dict == nullptr) {
        result.errors.push_back("function '" + target_fn.name + "' ** argument must be dict");
        return false;
      }
      for (const auto& entry : dict->entries) {
        auto* key = value_as_string(entry.first);
        if (key == nullptr) {
          result.errors.push_back("function '" + target_fn.name + "' ** argument keys must be strings");
          return false;
        }
        if (!bind_keyword(key->value, entry.second)) {
          return false;
        }
      }
    }
    if (kwargs_index >= 0) {
      bound[static_cast<size_t>(kwargs_index)] = Value::dict(std::move(extra_keywords));
    }
    for (size_t i = 0; i < signature.size(); ++i) {
      if (bound[i].tag != ValueTag::Invalid) {
        continue;
      }
      if (signature[i].default_reg != UINT32_MAX && signature[i].default_reg < defaults.size()) {
        value_assign_fast(bound[i], defaults[signature[i].default_reg]);
        continue;
      }
      if (signature[i].kind == ir::ParamKind::VarArgs) {
        bound[i] = Value::tuple({});
        continue;
      }
      if (signature[i].kind == ir::ParamKind::KwArgs) {
        bound[i] = Value::dict({});
        continue;
      }
      result.errors.push_back("function '" + target_fn.name + "' missing required argument '" + signature[i].name + "'");
      return false;
    }
    return true;
  };

  std::vector<Value> entry_bound_args;
  CallArgsView entry_args = args;
  if (!simple_signature(fn) || args.has_keywords() || args.has_expansion()) {
    if (!bind_args(fn, args, fn_obj_defaults, entry_bound_args)) {
      return result;
    }
    entry_args.leading = entry_bound_args.data();
    entry_args.leading_count = static_cast<uint32_t>(entry_bound_args.size());
    entry_args.registers = nullptr;
    entry_args.register_args = nullptr;
    entry_args.keyword_args = nullptr;
    entry_args.star_arg = UINT32_MAX;
    entry_args.kw_star_arg = UINT32_MAX;
  } else if (args.size() != fn.params.size()) {
    result.errors.push_back("function '" + fn.name + "' expected " + std::to_string(fn.params.size()) +
                            " arguments, got " + std::to_string(args.size()));
    return result;
  }

  std::vector<VMFrame> frames;
  size_t frame_count = 0;
  bool resumed_generator = false;
  if (generator != nullptr && generator->vm_state != nullptr) {
    auto* state = static_cast<GeneratorVMState*>(generator->vm_state);
    frames = std::move(state->frames);
    frame_count = state->frame_count;
    delete state;
    generator->vm_state = nullptr;
    generator->vm_state_cleanup = nullptr;
    resumed_generator = true;
  } else {
    frames.reserve(64);
    frames.emplace_back(
        module, function_id, entry_args, fn_obj_closure, std::move(globals_module), std::move(module_owner), 0, false);
    frame_count = 1;
  }

  auto make_generator_if_needed = [&](FunctionObject* fn_obj, CallArgsView call_args, Value& out, bool& made) -> bool {
    made = false;
    if (fn_obj == nullptr) {
      return true;
    }
    const ir::Module* call_module = &module;
    if (fn_obj->module != nullptr) {
      call_module = fn_obj->module.get();
    }
    if (fn_obj->function_id >= call_module->functions.size()) {
      result.errors.push_back("invalid function id");
      return false;
    }
    const auto& call_fn = call_module->functions[fn_obj->function_id];
    if (!call_fn.is_generator) {
      return true;
    }

    std::vector<Value> args_for_generator;
    if (!simple_signature(call_fn) || call_args.has_keywords() || call_args.has_expansion()) {
      if (!bind_args(call_fn, call_args, fn_obj->defaults, args_for_generator)) {
        return false;
      }
    } else {
      if (call_args.size() != call_fn.params.size()) {
        result.errors.push_back("function '" + call_fn.name + "' expected " + std::to_string(call_fn.params.size()) +
                                " arguments, got " + std::to_string(call_args.size()));
        return false;
      }
      args_for_generator.reserve(call_args.size());
      for (size_t i = 0; i < call_args.size(); ++i) {
        args_for_generator.push_back(call_args.get(i));
      }
    }

    Value function_value = Value::function(
        fn_obj->function_id,
        fn_obj->closure,
        fn_obj->globals_module,
        fn_obj->module != nullptr ? fn_obj->module : module_owner,
        fn_obj->defaults);
    out = Value::generator(&runtime_, std::move(function_value), std::move(args_for_generator));
    made = true;
    return true;
  };

  auto push_frame = [&](const ir::Module& call_module,
                        uint32_t call_function_id,
                        CallArgsView call_args,
                        const std::vector<Value>& closure,
                        const std::vector<Value>& defaults,
                        Value call_globals_module,
                        std::shared_ptr<const ir::Module> call_module_owner,
                        uint32_t return_dst,
                        FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue,
                        Value continuation_value = Value::invalid()) -> bool {
    if (call_function_id >= call_module.functions.size()) {
      result.errors.push_back("invalid function id");
      return false;
    }
    const auto& call_fn = call_module.functions[call_function_id];
    std::vector<Value> bound_args;
    CallArgsView frame_args = call_args;
    if (!simple_signature(call_fn) || call_args.has_keywords() || call_args.has_expansion()) {
      if (!bind_args(call_fn, call_args, defaults, bound_args)) {
        return false;
      }
      frame_args.leading = bound_args.data();
      frame_args.leading_count = static_cast<uint32_t>(bound_args.size());
      frame_args.registers = nullptr;
      frame_args.register_args = nullptr;
      frame_args.keyword_args = nullptr;
      frame_args.star_arg = UINT32_MAX;
      frame_args.kw_star_arg = UINT32_MAX;
    } else if (call_args.size() != call_fn.params.size()) {
      result.errors.push_back("function '" + call_fn.name + "' expected " + std::to_string(call_fn.params.size()) +
                              " arguments, got " + std::to_string(call_args.size()));
      return false;
    }
    if (frame_count < frames.size()) {
      frames[frame_count].reset(call_module, call_function_id, frame_args, closure, std::move(call_globals_module),
                                std::move(call_module_owner), return_dst, true, return_mode,
                                std::move(continuation_value));
    } else {
      frames.emplace_back(call_module, call_function_id, frame_args, closure, std::move(call_globals_module),
                          std::move(call_module_owner), return_dst, true, return_mode,
                          std::move(continuation_value));
    }
    auto& pushed = frames[frame_count];
    ++frame_count;
    for (size_t i = 0; i < pushed.fn->cell_slots.size(); ++i) {
      if (pushed.fn->cell_slots[i] >= pushed.locals.size()) {
        result.errors.push_back("invalid cell local slot");
        --frame_count;
        return false;
      }
      pushed.cells[i] = Value::cell(pushed.locals[pushed.fn->cell_slots[i]]);
    }
    return true;
  };

  auto finish_frame = [&](const Value& return_value) -> bool {
    VMFrame& finished = frames[frame_count - 1];
    const uint32_t return_dst = finished.return_dst;
    const bool has_caller = finished.has_caller;
    const FrameReturnMode return_mode = finished.return_mode;
    if (!has_caller) {
      value_assign_fast(result.value, return_value);
      --frame_count;
      return false;
    }
    --frame_count;
    Value& target = frames[frame_count - 1].regs[return_dst];
    if (return_mode == FrameReturnMode::StoreConstructedInstance) {
      value_assign_fast(target, finished.continuation_value);
    } else {
      value_assign_fast(target, return_value);
    }
    return true;
  };

  Value current_exception;

  auto normalize_exception = [&](const Value& value) -> Value {
    if (auto* klass = value_as_class(value)) {
      (void)klass;
      return Value::instance(value);
    }
    if (value_as_instance(value) != nullptr) {
      return value;
    }
    return runtime_.make_exception("RuntimeError", value_to_string(value));
  };

  auto dispatch_exception = [&](Value exception) -> bool {
    value_assign_fast(current_exception, exception);
    while (frame_count != 0) {
      auto& handlers = frames[frame_count - 1].exception_handlers;
      if (!handlers.empty()) {
        const auto handler = handlers.back();
        handlers.pop_back();
        frames[frame_count - 1].ip = handler.ip;
        return true;
      }
      --frame_count;
    }
    result.errors.push_back("uncaught exception: " + value_to_string(current_exception));
    return false;
  };

  auto exception_matches = [&](const Value& handler_type) -> bool {
    auto* handler_class = value_as_class(handler_type);
    if (handler_class == nullptr) {
      return false;
    }
    Value exception_type = runtime_.exception_type(current_exception);
    auto* raised_class = value_as_class(exception_type);
    return raised_class != nullptr && class_is_subclass(raised_class, handler_class);
  };

  if (!resumed_generator) {
    for (size_t i = 0; i < frames[frame_count - 1].fn->cell_slots.size(); ++i) {
      if (frames[frame_count - 1].fn->cell_slots[i] >= frames[frame_count - 1].locals.size()) {
        result.errors.push_back("invalid cell local slot");
        return result;
      }
      frames[frame_count - 1].cells[i] =
          Value::cell(frames[frame_count - 1].locals[frames[frame_count - 1].fn->cell_slots[i]]);
    }
  }

  while (frame_count != 0) {
    auto& frame = frames[frame_count - 1];
    const auto& module = *frame.module;
    const auto& fn = *frame.fn;
    auto& fn_obj_closure = *frame.closure;
    auto& globals_module = frame.globals_module;
    auto& module_owner = frame.module_owner;
    auto& locals = frame.locals;
    auto& cells = frame.cells;
    auto& regs = frame.regs;
    auto& ip = frame.ip;
    auto& exception_handlers = frame.exception_handlers;
    auto& global_value_cache = frame.global_value_cache;
    auto& global_slot_cache = frame.global_slot_cache;
    auto& global_cache_versions = frame.global_cache_versions;
    auto& global_cache_kind = frame.global_cache_kind;
    auto& call_site_cache = frame.call_site_cache;
    auto& attr_site_cache = frame.attr_site_cache;
    auto& native_call_args = frame.native_call_args;

    auto raise_exception_value = [&](Value exception) -> bool {
      const size_t source_frame = frame_count;
      if (!dispatch_exception(std::move(exception))) {
        return false;
      }
      if (frame_count != source_frame) {
        throw VMUnwind{};
      }
      return true;
    };

    auto raise_runtime_error = [&](const std::string& message) -> bool {
      return raise_exception_value(runtime_.make_exception("RuntimeError", message));
    };

    XlangRuntimeExecutionGuard execution_lock;
    uint32_t execution_lock_ticks = 0;

    try {
    for (;;) {
      if (ip >= fn.code.size()) {
        Value none = Value::none();
        if (!finish_frame(none)) {
          if (generator != nullptr) {
            generator->done = true;
            value_set_none(result.value);
          }
          return result;
        }
        goto switch_frame;
      }

      const auto& in = fn.code[ip];
      if ((++execution_lock_ticks & 0x3ffu) == 0) {
        execution_lock.unlock();
        std::this_thread::yield();
        execution_lock.lock();
      }
      switch (in.op) {
      case ir::Op::LoadConst:
        if (in.a >= fn.constants.size()) {
          result.errors.push_back("invalid constant index");
          return result;
        }
        value_assign_fast(regs[in.dst], fn.constants[in.a]);
        break;
      case ir::Op::Move:
        if (in.dst >= regs.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid register move");
          return result;
        }
        value_assign_fast(regs[in.dst], regs[in.a]);
        break;
      case ir::Op::LoadLocal:
        if (in.a >= locals.size()) {
          result.errors.push_back("invalid local slot");
          return result;
        }
        if (locals[in.a].tag == ValueTag::Invalid) {
          if (raise_runtime_error("local variable is not defined")) continue;
          return result;
        }
        value_assign_fast(regs[in.dst], locals[in.a]);
        break;
      case ir::Op::StoreLocal:
        if (in.dst >= locals.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid local store");
          return result;
        }
        value_assign_fast(locals[in.dst], regs[in.a]);
        break;
      case ir::Op::MoveLocal:
        if (in.dst >= locals.size() || in.a >= locals.size()) {
          result.errors.push_back("invalid local move");
          return result;
        }
        value_assign_fast(locals[in.dst], locals[in.a]);
        break;
      case ir::Op::AddLocalConst: {
        if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= fn.constants.size()) {
          result.errors.push_back("invalid local const add");
          return result;
        }
        const auto& lhs = locals[in.a];
        const auto& rhs = fn.constants[in.b];
        if (!fast_add(lhs, rhs, locals[in.dst])) {
          std::string error;
          if (!value_add(lhs, rhs, locals[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::AddLocalLocal: {
        if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size()) {
          result.errors.push_back("invalid local local add");
          return result;
        }
        const auto& lhs = locals[in.a];
        const auto& rhs = locals[in.b];
        if (!fast_add(lhs, rhs, locals[in.dst])) {
          std::string error;
          if (!value_add(lhs, rhs, locals[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::LoadCell: {
        if (in.a >= cells.size()) {
          result.errors.push_back("invalid cell slot");
          return result;
        }
        auto* cell = value_as_cell(cells[in.a]);
        if (cell == nullptr) {
          result.errors.push_back("invalid cell object");
          return result;
        }
        value_assign_fast(regs[in.dst], cell->value);
        break;
      }
      case ir::Op::StoreCell: {
        if (in.dst >= cells.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid cell store");
          return result;
        }
        auto* cell = value_as_cell(cells[in.dst]);
        if (cell == nullptr) {
          result.errors.push_back("invalid cell object");
          return result;
        }
        value_assign_fast(cell->value, regs[in.a]);
        value_assign_fast(locals[fn.cell_slots[in.dst]], regs[in.a]);
        break;
      }
      case ir::Op::LoadCellObject:
        if (in.a >= cells.size()) {
          result.errors.push_back("invalid cell object slot");
          return result;
        }
        value_assign_fast(regs[in.dst], cells[in.a]);
        break;
      case ir::Op::LoadFree: {
        if (in.a >= fn_obj_closure.size()) {
          result.errors.push_back("invalid free slot");
          return result;
        }
        auto* cell = value_as_cell(fn_obj_closure[in.a]);
        if (cell == nullptr) {
          result.errors.push_back("invalid free cell");
          return result;
        }
        value_assign_fast(regs[in.dst], cell->value);
        break;
      }
      case ir::Op::StoreFree: {
        if (in.dst >= fn_obj_closure.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid free store");
          return result;
        }
        auto* cell = value_as_cell(fn_obj_closure[in.dst]);
        if (cell == nullptr) {
          result.errors.push_back("invalid free cell");
          return result;
        }
        value_assign_fast(cell->value, regs[in.a]);
        break;
      }
      case ir::Op::LoadFreeObject:
        if (in.a >= fn_obj_closure.size()) {
          result.errors.push_back("invalid free object slot");
          return result;
        }
        value_assign_fast(regs[in.dst], fn_obj_closure[in.a]);
        break;
      case ir::Op::LoadModuleSlot: {
        if (in.a >= module.global_slots.size()) {
          result.errors.push_back("invalid module slot");
          return result;
        }
        const auto& name = module.global_slots[in.a];
        auto* globals_module_obj = value_as_module(globals_module);
        if (globals_module_obj != nullptr) {
          if (in.a < globals_module_obj->slots.size() && globals_module_obj->slots[in.a].tag != ValueTag::Invalid) {
            value_assign_fast(regs[in.dst], globals_module_obj->slots[in.a]);
            break;
          }
          if (const auto* builtin = runtime_.find_builtin(name)) {
            value_assign_fast(regs[in.dst], *builtin);
            break;
          }
          if (raise_runtime_error("name '" + name + "' is not defined")) continue;
          return result;
        }
        if (auto it = globals_.find(name); it != globals_.end()) {
          value_assign_fast(regs[in.dst], it->second);
        } else if (const auto* builtin = runtime_.find_builtin(name)) {
          value_assign_fast(regs[in.dst], *builtin);
        } else {
          if (raise_runtime_error("name '" + name + "' is not defined")) continue;
          return result;
        }
        break;
      }
      case ir::Op::StoreModuleSlot: {
        if (in.dst >= module.global_slots.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid module slot store");
          return result;
        }
        auto* globals_module_obj = value_as_module(globals_module);
        if (globals_module_obj != nullptr) {
          if (in.dst >= globals_module_obj->slots.size()) {
            result.errors.push_back("module slot is not bound");
            return result;
          }
          value_assign_fast(globals_module_obj->slots[in.dst], regs[in.a]);
          ++globals_module_obj->version;
        } else {
          value_assign_fast(globals_[module.global_slots[in.dst]], regs[in.a]);
          ++globals_version_;
        }
        break;
      }
      case ir::Op::LoadGlobal: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid global name");
          return result;
        }
        auto* globals_module_obj = value_as_module(globals_module);
        const uint64_t globals_version = globals_module_obj != nullptr ? globals_module_obj->version : globals_version_;
        if (global_cache_kind[in.a] != 0) {
          if (globals_module_obj != nullptr && global_cache_kind[in.a] == 1) {
            const auto slot = global_slot_cache[in.a];
            if (slot < globals_module_obj->slots.size()) {
              value_assign_fast(regs[in.dst], globals_module_obj->slots[slot]);
              break;
            }
          } else if (global_cache_kind[in.a] == 2 && global_cache_versions[in.a] == globals_version) {
            value_assign_fast(regs[in.dst], global_value_cache[in.a]);
            break;
          }
        }
        const auto& name = fn.names[in.a];
        if (globals_module_obj != nullptr) {
          std::string error;
          uint32_t slot = 0;
          if (module_find_attr_slot(globals_module, name, slot, error)) {
            value_assign_fast(regs[in.dst], globals_module_obj->slots[slot]);
            global_slot_cache[in.a] = slot;
            global_cache_kind[in.a] = 1;
            break;
          }
          if (const auto* builtin = runtime_.find_builtin(name)) {
            value_assign_fast(regs[in.dst], *builtin);
            value_assign_fast(global_value_cache[in.a], regs[in.dst]);
            global_cache_versions[in.a] = globals_module_obj->version;
            global_cache_kind[in.a] = 2;
          } else {
            if (raise_runtime_error("name '" + name + "' is not defined")) continue;
            return result;
          }
        } else if (auto it = globals_.find(name); it != globals_.end()) {
          value_assign_fast(regs[in.dst], it->second);
          value_assign_fast(global_value_cache[in.a], regs[in.dst]);
          global_cache_versions[in.a] = globals_version_;
          global_cache_kind[in.a] = 2;
        } else if (const auto* builtin = runtime_.find_builtin(name)) {
          value_assign_fast(regs[in.dst], *builtin);
          value_assign_fast(global_value_cache[in.a], regs[in.dst]);
          global_cache_versions[in.a] = globals_version_;
          global_cache_kind[in.a] = 2;
        } else {
          if (raise_runtime_error("name '" + name + "' is not defined")) continue;
          return result;
        }
        break;
      }
      case ir::Op::StoreGlobal:
        if (in.dst >= fn.names.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid global store");
          return result;
        }
        if (value_as_module(globals_module) != nullptr) {
          std::string error;
          if (!module_set_attr(globals_module, fn.names[in.dst], regs[in.a], error)) {
            result.errors.push_back(error);
            return result;
          }
        } else {
          value_assign_fast(globals_[fn.names[in.dst]], regs[in.a]);
          ++globals_version_;
        }
        if (auto* globals_module_obj = value_as_module(globals_module)) {
          std::string error;
          uint32_t slot = 0;
          if (module_find_attr_slot(globals_module, fn.names[in.dst], slot, error)) {
            global_slot_cache[in.dst] = slot;
            global_cache_kind[in.dst] = 1;
          }
          global_cache_versions[in.dst] = globals_module_obj->version;
        } else {
          value_assign_fast(global_value_cache[in.dst], regs[in.a]);
          global_cache_versions[in.dst] = globals_version_;
          global_cache_kind[in.dst] = 2;
        }
        break;
      case ir::Op::DeleteLocal:
        if (in.dst >= locals.size()) {
          result.errors.push_back("invalid local delete");
          return result;
        }
        value_set_invalid(locals[in.dst]);
        break;
      case ir::Op::DeleteGlobal:
        if (in.dst >= fn.names.size()) {
          result.errors.push_back("invalid global delete");
          return result;
        }
        globals_.erase(fn.names[in.dst]);
        ++globals_version_;
        break;
      case ir::Op::DeleteModuleSlot: {
        if (in.dst >= module.global_slots.size()) {
          result.errors.push_back("invalid module slot delete");
          return result;
        }
        auto* globals_module_obj = value_as_module(globals_module);
        if (globals_module_obj == nullptr || in.dst >= globals_module_obj->slots.size()) {
          if (raise_runtime_error("module slot is not bound")) continue;
          return result;
        }
        value_set_invalid(globals_module_obj->slots[in.dst]);
        ++globals_module_obj->version;
        break;
      }
      case ir::Op::ImportModule: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid module name");
          return result;
        }
        std::string error;
        if (!runtime_.import_module(fn.names[in.a], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::ImportFrom: {
        if (in.a >= fn.names.size() || in.b >= fn.names.size()) {
          result.errors.push_back("invalid from import");
          return result;
        }
        std::string error;
        if (!runtime_.import_from(fn.names[in.a], fn.names[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::ImportStar: {
        if (in.dst >= fn.names.size()) {
          result.errors.push_back("invalid star import module name");
          return result;
        }
        std::string error;
        if (!runtime_.import_star(fn.names[in.dst], globals_module, error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::RawBlock: {
        if (in.dst >= fn.raw_blocks.size()) {
          result.errors.push_back("invalid raw block index");
          return result;
        }
        const auto& raw = fn.raw_blocks[in.dst];
        RawBlockContext context;
        context.get_var = [&](const std::string& name, Value& out, std::string& error) -> bool {
          for (size_t i = 0; i < fn.locals.size(); ++i) {
            if (fn.locals[i] == name && i < locals.size()) {
              value_assign_fast(out, locals[i]);
              return true;
            }
          }
          for (size_t i = 0; i < fn.free_vars.size(); ++i) {
            if (fn.free_vars[i] == name && i < fn_obj_closure.size()) {
              auto* cell = value_as_cell(fn_obj_closure[i]);
              if (cell != nullptr) {
                value_assign_fast(out, cell->value);
                return true;
              }
            }
          }
          if (value_as_module(globals_module) != nullptr) {
            if (module_get_attr(globals_module, name, out, error)) {
              return true;
            }
          } else if (auto it = globals_.find(name); it != globals_.end()) {
            value_assign_fast(out, it->second);
            return true;
          }
          if (const auto* builtin = runtime_.find_builtin(name)) {
            value_assign_fast(out, *builtin);
            return true;
          }
          error = "name '" + name + "' is not defined";
          return false;
        };
        context.set_var = [&](const std::string& name, const Value& value, std::string& error) -> bool {
          for (size_t i = 0; i < fn.locals.size(); ++i) {
            if (fn.locals[i] == name && i < locals.size()) {
              value_assign_fast(locals[i], value);
              return true;
            }
          }
          if (value_as_module(globals_module) != nullptr) {
            return module_set_attr(globals_module, name, value, error);
          }
          value_assign_fast(globals_[name], value);
          ++globals_version_;
          return true;
        };
        std::string error;
        if (!runtime_.execute_raw_block(context, raw.language, raw.provider, raw.body, error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::LoadAttr: {
        if (in.b >= fn.names.size()) {
          result.errors.push_back("invalid attribute name");
          return result;
        }
        std::string error;
        if (!load_attr_cached(regs[in.a], fn.names[in.b], attr_site_cache[ip], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::StoreAttr: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid attribute name");
          return result;
        }
        std::string error;
        if (!store_attr_cached(regs[in.dst], fn.names[in.a], regs[in.b], attr_site_cache[ip], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::DeleteAttr: {
        if (in.dst >= regs.size() || in.a >= fn.names.size()) {
          result.errors.push_back("invalid attr delete");
          return result;
        }
        std::string error;
        if (value_as_module(regs[in.dst]) != nullptr) {
          if (!module_set_attr(regs[in.dst], fn.names[in.a], Value::invalid(), error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        } else if (!object_delete_attr(regs[in.dst], fn.names[in.a], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::LoadInstanceSlot: {
        auto* instance = value_as_instance(regs[in.a]);
        if (instance == nullptr || in.b >= instance_slot_count(instance)) {
          if (raise_runtime_error("invalid instance slot load")) continue;
          return result;
        }
        const auto& slot_value = instance_slot_at(instance, in.b);
        if (slot_value.tag == ValueTag::Invalid) {
          if (raise_runtime_error("object has no attribute")) continue;
          return result;
        }
        value_assign_fast(regs[in.dst], slot_value);
        break;
      }
      case ir::Op::StoreInstanceSlot: {
        auto* instance = value_as_instance(regs[in.dst]);
        if (instance == nullptr || in.a >= instance_slot_count(instance)) {
          if (raise_runtime_error("invalid instance slot store")) continue;
          return result;
        }
        value_assign_fast(instance_slot_at(instance, in.a), regs[in.b]);
        break;
      }
      case ir::Op::MakeClass: {
        if (in.a >= fn.names.size() || in.b >= fn.class_attrs.size() || in.c >= fn.class_instance_slots.size()) {
          result.errors.push_back("invalid class data");
          return result;
        }
        std::vector<std::pair<std::string, Value>> attrs;
        attrs.reserve(fn.class_attrs[in.b].size());
        for (const auto& attr : fn.class_attrs[in.b]) {
          if (attr.second >= regs.size()) {
            result.errors.push_back("invalid class attr register");
            return result;
          }
          attrs.push_back(std::make_pair(attr.first, regs[attr.second]));
        }
        regs[in.dst] =
            Value::class_object(fn.names[in.a], std::move(attrs), Value::invalid(), fn.class_instance_slots[in.c]);
        break;
      }
      case ir::Op::MakeFunction: {
        if (in.b >= fn.function_closures.size()) {
          result.errors.push_back("invalid function closure list");
          return result;
        }
        std::vector<Value> closure;
        closure.reserve(fn.function_closures[in.b].size());
        for (const auto reg : fn.function_closures[in.b]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid closure register");
            return result;
          }
          closure.push_back(regs[reg]);
        }
        std::vector<Value> defaults;
        if (in.c != UINT32_MAX) {
          if (in.c >= fn.function_defaults.size()) {
            result.errors.push_back("invalid function defaults list");
            return result;
          }
          defaults.reserve(fn.function_defaults[in.c].size());
          for (const auto reg : fn.function_defaults[in.c]) {
            if (reg >= regs.size()) {
              result.errors.push_back("invalid default register");
              return result;
            }
            defaults.push_back(regs[reg]);
          }
        }
        regs[in.dst] = Value::function(in.a, std::move(closure), globals_module, module_owner, std::move(defaults));
        break;
      }
      case ir::Op::SetFunctionAnnotations: {
        if (in.a >= fn.function_annotations.size()) {
          result.errors.push_back("invalid function annotations list");
          return result;
        }
        auto* function = value_as_function(regs[in.dst]);
        if (function == nullptr) {
          result.errors.push_back("invalid function annotations target");
          return result;
        }
        std::vector<std::pair<Value, Value>> entries;
        entries.reserve(fn.function_annotations[in.a].size());
        for (const auto& annotation : fn.function_annotations[in.a]) {
          if (annotation.second >= regs.size()) {
            result.errors.push_back("invalid annotation register");
            return result;
          }
          entries.push_back(std::make_pair(Value::string(annotation.first), regs[annotation.second]));
        }
        value_assign_fast(function->annotations, Value::dict(std::move(entries)));
        break;
      }
      case ir::Op::SetClassBase: {
        std::string error;
        if (!class_set_base(regs[in.dst], regs[in.a], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::MakeTuple: {
        if (in.a >= fn.tuple_items.size()) {
          result.errors.push_back("invalid tuple item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.tuple_items[in.a].size());
        for (const auto reg : fn.tuple_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid tuple item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::tuple(std::move(items));
        break;
      }
      case ir::Op::MakeList: {
        if (in.a >= fn.list_items.size()) {
          result.errors.push_back("invalid list item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.list_items[in.a].size());
        for (const auto reg : fn.list_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid list item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::list(std::move(items));
        break;
      }
      case ir::Op::MakeDict: {
        if (in.a >= fn.dict_items.size()) {
          result.errors.push_back("invalid dict item list");
          return result;
        }
        std::vector<std::pair<Value, Value>> items;
        items.reserve(fn.dict_items[in.a].size());
        for (const auto& pair : fn.dict_items[in.a]) {
          if (pair.first >= regs.size() || pair.second >= regs.size()) {
            result.errors.push_back("invalid dict item register");
            return result;
          }
          items.push_back(std::make_pair(regs[pair.first], regs[pair.second]));
        }
        regs[in.dst] = Value::dict(std::move(items));
        break;
      }
      case ir::Op::MakeSet: {
        if (in.a >= fn.set_items.size()) {
          result.errors.push_back("invalid set item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.set_items[in.a].size());
        for (const auto reg : fn.set_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid set item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::set(std::move(items));
        break;
      }
      case ir::Op::ListAppend: {
        std::string error;
        if (!sequence_list_append(regs[in.dst], regs[in.a], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::GetItem: {
        std::string error;
        if (!sequence_get_item(regs[in.a], regs[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::SetItem: {
        std::string error;
        if (!sequence_set_item(regs[in.dst], regs[in.a], regs[in.b], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::DeleteItem: {
        std::string error;
        if (!sequence_delete_item(regs[in.dst], regs[in.a], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::GetIter: {
        std::string error;
        if (!sequence_get_iter(regs[in.a], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::IterNext: {
        std::string error;
        bool done = false;
        if (!sequence_iter_next(regs[in.a], done, regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        if (done) {
          ip = in.b;
          continue;
        }
        break;
      }
      case ir::Op::ForRangeConstLocalNext: {
        if (in.a >= locals.size() || in.b >= locals.size() || in.c >= fn.range_specs.size()) {
          result.errors.push_back("invalid fused range loop");
          return result;
        }
        auto& current = locals[in.b];
        const auto& spec = fn.range_specs[in.c];
        if (current.tag != ValueTag::Int64 ||
            spec.first >= fn.constants.size() ||
            spec.second >= fn.constants.size() ||
            fn.constants[spec.first].tag != ValueTag::Int64 ||
            fn.constants[spec.second].tag != ValueTag::Int64) {
          result.errors.push_back("invalid fused range state");
          return result;
        }
        const int64_t value = current.as.i64;
        const int64_t stop = fn.constants[spec.first].as.i64;
        const int64_t step = fn.constants[spec.second].as.i64;
        const bool done = step > 0 ? value >= stop : value <= stop;
        if (done) {
          ip = in.dst;
          continue;
        }
        value_set_int64(locals[in.a], value);
        value_set_int64(current, value + step);
        break;
      }
      case ir::Op::Add: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_add(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_add(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Sub: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_sub(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_sub(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Mul: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_mul(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_mul(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Div: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool divide_by_zero = false;
        if (!fast_div(lhs, rhs, regs[in.dst], divide_by_zero)) {
          if (divide_by_zero) {
            if (raise_runtime_error("division by zero")) continue;
            return result;
          }
          std::string error;
          if (!value_div(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::FloorDiv: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool divide_by_zero = false;
        if (!fast_floor_div(lhs, rhs, regs[in.dst], divide_by_zero)) {
          if (divide_by_zero) {
            if (raise_runtime_error("division by zero")) continue;
            return result;
          }
          std::string error;
          if (!value_floor_div(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Mod: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool modulo_by_zero = false;
        if (!fast_mod(lhs, rhs, regs[in.dst], modulo_by_zero)) {
          if (modulo_by_zero) {
            if (raise_runtime_error("integer modulo by zero")) continue;
            return result;
          }
          std::string error;
          if (!value_mod(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Pow: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_pow(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_pow(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::BitAnd: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_bit_and(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_bit_and(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::BitOr: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_bit_or(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_bit_or(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::BitXor: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_bit_xor(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_bit_xor(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Shl: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool bad_shift = false;
        if (!fast_shift_left(lhs, rhs, regs[in.dst], bad_shift)) {
          if (bad_shift) {
            if (raise_runtime_error(rhs.tag == ValueTag::Int64 && rhs.as.i64 < 0 ? "negative shift count" : "shift count too large for int64")) continue;
            return result;
          }
          std::string error;
          if (!value_shift_left(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Shr: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool negative_shift = false;
        if (!fast_shift_right(lhs, rhs, regs[in.dst], negative_shift)) {
          if (negative_shift) {
            if (raise_runtime_error("negative shift count")) continue;
            return result;
          }
          std::string error;
          if (!value_shift_right(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::BoolAnd:
        value_set_bool(regs[in.dst], value_truthy(regs[in.a]) && value_truthy(regs[in.b]));
        break;
      case ir::Op::BoolOr:
        value_set_bool(regs[in.dst], value_truthy(regs[in.a]) || value_truthy(regs[in.b]));
        break;
      case ir::Op::Compare: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        const auto op = static_cast<ir::CompareOp>(in.c);
        if (!fast_compare(op, lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_compare(compare_name(op), lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Is:
        value_set_bool(regs[in.dst], value_is(regs[in.a], regs[in.b]) != (in.c != 0));
        break;
      case ir::Op::Contains: {
        bool contains = false;
        std::string error;
        if (!value_contains(regs[in.b], regs[in.a], contains, error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        value_set_bool(regs[in.dst], contains != (in.c != 0));
        break;
      }
      case ir::Op::Not:
        value_set_bool(regs[in.dst], !value_truthy(regs[in.a]));
        break;
      case ir::Op::Neg:
        if (regs[in.a].tag == ValueTag::Int64) {
          value_set_int64(regs[in.dst], -regs[in.a].as.i64);
        } else if (regs[in.a].tag == ValueTag::Double) {
          value_set_number(regs[in.dst], -regs[in.a].as.f64);
        } else {
          if (raise_runtime_error("unsupported operand for unary -")) continue;
          return result;
        }
        break;
      case ir::Op::Invert: {
        std::string error;
        if (!value_invert(regs[in.a], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::Jump:
        ip = in.dst;
        continue;
      case ir::Op::JumpIfFalse:
        if (!value_truthy(regs[in.a])) {
          ip = in.dst;
          continue;
        }
        break;
      case ir::Op::JumpIfLocalConstFalse: {
        if (in.a >= locals.size() || in.b >= fn.constants.size()) {
          result.errors.push_back("invalid local const jump");
          return result;
        }
        Value compare_result;
        const auto op = static_cast<ir::CompareOp>(in.c);
        if (!fast_compare(op, locals[in.a], fn.constants[in.b], compare_result)) {
          std::string error;
          if (!value_compare(compare_name(op), locals[in.a], fn.constants[in.b], compare_result, error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        if (!value_truthy(compare_result)) {
          ip = in.dst;
          continue;
        }
        break;
      }
      case ir::Op::SetupExcept:
        exception_handlers.push_back({in.dst, ExceptionHandlerKind::Except, 0});
        break;
      case ir::Op::SetupWith:
        exception_handlers.push_back({in.dst, ExceptionHandlerKind::With, in.a});
        break;
      case ir::Op::PopExcept:
        if (exception_handlers.empty()) {
          result.errors.push_back("invalid exception handler pop");
          return result;
        }
        exception_handlers.pop_back();
        break;
      case ir::Op::Raise:
        if (in.a >= regs.size()) {
          result.errors.push_back("invalid raise value");
          return result;
        }
        if (!raise_exception_value(normalize_exception(regs[in.a]))) return result;
        continue;
      case ir::Op::Reraise:
        if (current_exception.tag == ValueTag::Invalid) {
          if (raise_runtime_error("No active exception to reraise")) continue;
          return result;
        }
        if (!raise_exception_value(current_exception)) return result;
        continue;
      case ir::Op::ClearException:
        value_set_invalid(current_exception);
        break;
      case ir::Op::LoadException:
        value_assign_fast(regs[in.dst], current_exception);
        break;
      case ir::Op::LoadExceptionType:
        regs[in.dst] = runtime_.exception_type(current_exception);
        break;
      case ir::Op::MatchException:
        value_set_bool(regs[in.dst], exception_matches(regs[in.a]));
        break;
      case ir::Op::CallModuleMethod: {
        if (in.a >= module.global_slots.size() || in.b >= fn.names.size() || in.c >= fn.call_args.size()) {
          result.errors.push_back("invalid module method call");
          return result;
        }
        auto* globals_module_obj = value_as_module(globals_module);
        if (globals_module_obj == nullptr || in.a >= globals_module_obj->slots.size()) {
          if (raise_runtime_error("module slot is not bound")) continue;
          return result;
        }
        const auto& module_value = globals_module_obj->slots[in.a];
        auto* module_object = value_as_module(module_value);
        if (module_object == nullptr) {
          if (raise_runtime_error("imported module binding is not a module")) continue;
          return result;
        }

        const auto& call_arg_regs = fn.call_args[in.c];
        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &call_arg_regs;

        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };

        auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          const bool use_fast = native->fast_callback != nullptr;
          const Value* native_args = nullptr;
          if (!use_fast) {
            native_args = materialize_native_args(values);
          }
          bool ok = false;
          if (use_fast && !native->fast_releases_vm_lock) {
            ok = native->fast_callback(
                runtime_,
                values.leading,
                values.leading_count,
                values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result,
                error,
                native->user_data);
          } else {
            execution_lock.unlock();
            ok = use_fast
                ? native->fast_callback(
                      runtime_,
                      values.leading,
                      values.leading_count,
                      values.registers,
                      values.register_args == nullptr ? nullptr : values.register_args->data(),
                      values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                      native_result,
                      error,
                      native->user_data)
                : native->callback != nullptr &&
                      native->callback(
                          runtime_,
                          native_args,
                          static_cast<uint32_t>(values.size()),
                          native_result,
                          error,
                          native->user_data);
            execution_lock.lock();
          }
          if (!ok) {
            Value pending;
            if (runtime_.take_pending_exception(pending)) {
              if (raise_exception_value(std::move(pending))) return false;
              return false;
            }
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };

        auto call_cached_fast_function = [&](const CallSiteCache& cache, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          bool ok = false;
          if (!cache.fast_releases_vm_lock) {
            ok = cache.fast_callback(
                runtime_,
                values.leading,
                values.leading_count,
                values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result,
                error,
                cache.native_user_data);
          } else {
            execution_lock.unlock();
            ok = cache.fast_callback(
                runtime_,
                values.leading,
                values.leading_count,
                values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result,
                error,
                cache.native_user_data);
            execution_lock.lock();
          }
          if (!ok) {
            Value pending;
            if (runtime_.take_pending_exception(pending)) {
              if (raise_exception_value(std::move(pending))) return false;
              return false;
            }
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };

        if (!call_site_cache.empty()) {
          auto& cache = call_site_cache[ip];
          if (cache.callee_object == module_value.as.obj &&
              cache.class_version == module_object->version &&
              cache.kind == CallSiteKind::NativeFunction) {
            if (cache.fast_callback != nullptr) {
              if (!call_cached_fast_function(cache, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
            } else if (!call_native_function(cache.native, call_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
            break;
          }
        }

        std::string module_error;
        uint32_t module_slot = 0;
        const auto& name = fn.names[in.b];
        if (!module_find_attr_slot(module_value, name, module_slot, module_error) ||
            module_slot >= module_object->slots.size()) {
          if (raise_runtime_error(module_error.empty() ? "module method not found" : module_error)) continue;
          return result;
        }
        auto* native = value_as_native_function(module_object->slots[module_slot]);
        if (native == nullptr) {
          if (raise_runtime_error("module method is not a native function")) continue;
          return result;
        }
        if (!call_site_cache.empty()) {
          auto& cache = call_site_cache[ip];
          cache.callee_object = module_value.as.obj;
          cache.kind = CallSiteKind::NativeFunction;
          cache.function = nullptr;
          cache.native = native;
          cache.fast_callback = native->fast_callback;
          cache.native_user_data = native->user_data;
          cache.fast_releases_vm_lock = native->fast_releases_vm_lock;
          cache.class_version = module_object->version;
        }
        if (!call_native_function(native, call_args, regs[in.dst])) {
          if (!result.errors.empty()) return result;
          continue;
        }
        break;
      }
      case ir::Op::CallEx: {
        if (in.a >= regs.size() || in.b >= fn.call_specs.size()) {
          result.errors.push_back("invalid extended call");
          return result;
        }
        const auto& spec = fn.call_specs[in.b];
        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &spec.positional;
        call_args.keyword_args = &spec.keywords;
        call_args.star_arg = spec.star_arg;
        call_args.kw_star_arg = spec.kw_star_arg;
        const auto& callee = regs[in.a];
        bool pushed_frame = false;

        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };

        std::vector<NativeKeywordArg> native_keyword_args;
        auto materialize_native_call_ex =
            [&](CallArgsView values, bool& has_keywords, std::string& error) -> const Value* {
          native_call_args.clear();
          native_keyword_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          if (values.star_arg != UINT32_MAX) {
            const Value& star = values.registers[values.star_arg];
            if (auto* tuple = value_as_tuple(star)) {
              for (const auto& item : tuple->items) native_call_args.push_back(item);
            } else if (auto* list = value_as_list(star)) {
              for (const auto& item : list->items) native_call_args.push_back(item);
            } else {
              error = "* argument must be tuple or list";
              return nullptr;
            }
          }
          auto add_keyword = [&](const char* name, const Value* value) -> bool {
            for (const auto& existing : native_keyword_args) {
              if (std::string(existing.name) == name) {
                error = std::string("got multiple values for keyword argument '") + name + "'";
                return false;
              }
            }
            native_keyword_args.push_back(NativeKeywordArg{name, value});
            return true;
          };
          if (values.keyword_args != nullptr) {
            for (const auto& keyword : *values.keyword_args) {
              if (!add_keyword(keyword.name.c_str(), &values.registers[keyword.value_reg])) {
                return nullptr;
              }
            }
          }
          if (values.kw_star_arg != UINT32_MAX) {
            auto* dict = value_as_dict(values.registers[values.kw_star_arg]);
            if (dict == nullptr) {
              error = "** argument must be dict";
              return nullptr;
            }
            for (const auto& entry : dict->entries) {
              auto* key = value_as_string(entry.first);
              if (key == nullptr) {
                error = "** argument keys must be strings";
                return nullptr;
              }
              if (!add_keyword(key->value.c_str(), &entry.second)) {
                return nullptr;
              }
            }
          }
          has_keywords = !native_keyword_args.empty();
          return native_call_args.data();
        };

        auto call_native_function_ex = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          bool has_materialized_keywords = false;
          const bool needs_materialized_ex = values.has_keywords() || values.has_expansion();
          const bool use_fast = native->fast_callback != nullptr && !needs_materialized_ex;
          const Value* native_args = nullptr;
          if (needs_materialized_ex) {
            native_args = materialize_native_call_ex(values, has_materialized_keywords, error);
            if (native_args == nullptr && !error.empty()) {
              if (raise_runtime_error(error)) return false;
              return false;
            }
            if (has_materialized_keywords && native->keyword_callback == nullptr) {
              if (raise_runtime_error("native function does not accept keyword arguments")) return false;
              return false;
            }
          } else if (!use_fast) {
            native_args = materialize_native_args(values);
          }
          bool ok = false;
          if (use_fast && !native->fast_releases_vm_lock) {
            ok = native->fast_callback(
                runtime_,
                values.leading,
                values.leading_count,
                values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result,
                error,
                native->user_data);
          } else {
            execution_lock.unlock();
            if (use_fast) {
              ok = native->fast_callback(
                  runtime_,
                  values.leading,
                  values.leading_count,
                  values.registers,
                  values.register_args == nullptr ? nullptr : values.register_args->data(),
                  values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                  native_result,
                  error,
                  native->user_data);
            } else if (has_materialized_keywords) {
              ok = native->keyword_callback != nullptr &&
                   native->keyword_callback(
                       runtime_,
                       native_args,
                       static_cast<uint32_t>(native_call_args.size()),
                       native_keyword_args.data(),
                       static_cast<uint32_t>(native_keyword_args.size()),
                       native_result,
                       error,
                       native->user_data);
            } else {
              ok = native->callback != nullptr &&
                   native->callback(
                       runtime_,
                       native_args,
                       static_cast<uint32_t>(needs_materialized_ex ? native_call_args.size() : values.size()),
                       native_result,
                       error,
                       native->user_data);
            }
            execution_lock.lock();
          }
          if (!ok) {
            Value pending;
            if (runtime_.take_pending_exception(pending)) {
              if (raise_exception_value(std::move(pending))) return false;
              return false;
            }
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };

        auto call_user_function_ex = [&](FunctionObject* fn_obj, CallArgsView values, Value& out) -> bool {
          bool made_generator = false;
          if (!make_generator_if_needed(fn_obj, values, out, made_generator)) {
            return false;
          }
          if (made_generator) {
            return true;
          }
          const ir::Module* call_module = &module;
          auto call_module_owner = module_owner;
          if (fn_obj->module != nullptr) {
            call_module = fn_obj->module.get();
            call_module_owner = fn_obj->module;
          }
          ++ip;
          if (!push_frame(*call_module, fn_obj->function_id, values, fn_obj->closure, fn_obj->defaults,
                          fn_obj->globals_module, std::move(call_module_owner), in.dst)) {
            return false;
          }
          pushed_frame = true;
          return true;
        };

        auto call_callable_ex = [&](const Value& function_value, CallArgsView values, Value& out) -> bool {
          if (auto* native = value_as_native_function(function_value)) {
            return call_native_function_ex(native, values, out);
          }
          if (auto* fn_obj = value_as_function(function_value)) {
            return call_user_function_ex(fn_obj, values, out);
          }
          if (raise_runtime_error("object is not callable")) return false;
          return false;
        };

        if (auto* bound = value_as_bound_method(callee)) {
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
          if (!call_callable_ex(bound->function, bound_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* fn_obj = value_as_function(callee)) {
          if (!call_user_function_ex(fn_obj, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* native = value_as_native_function(callee)) {
          if (!call_native_function_ex(native, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (auto* klass = value_as_class(callee)) {
          (void)klass;
          Value instance = Value::instance(callee);
          CallArgsView init_args = call_args;
          init_args.leading = &instance;
          init_args.leading_count = 1;
          Value init_value;
          std::string init_error;
          if (object_get_attr(callee, "__init__", init_value, init_error)) {
            if (!call_callable_ex(init_value, init_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
            if (pushed_frame) {
              frames[frame_count - 1].return_mode = FrameReturnMode::StoreConstructedInstance;
              frames[frame_count - 1].continuation_value = instance;
              goto switch_frame;
            }
            value_assign_fast(regs[in.dst], instance);
          } else {
            value_assign_fast(regs[in.dst], instance);
          }
        } else {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        }
        break;
      }
      case ir::Op::CallMethod: {
        if (in.b >= fn.names.size() || in.c >= fn.call_args.size()) {
          result.errors.push_back("invalid method call");
          return result;
        }
        const auto& name = fn.names[in.b];
        const auto& call_arg_regs = fn.call_args[in.c];
        if (name == "append" && call_arg_regs.size() == 1) {
          if (auto* list = value_as_list(regs[in.a])) {
            list->items.push_back(regs[call_arg_regs[0]]);
            value_set_none(regs[in.dst]);
            break;
          }
        }

        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &call_arg_regs;
        bool pushed_frame = false;
        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };
        auto call_user_function = [&](FunctionObject* fn_obj, CallArgsView values, Value& out) -> bool {
          bool made_generator = false;
          if (!make_generator_if_needed(fn_obj, values, out, made_generator)) {
            return false;
          }
          if (made_generator) {
            return true;
          }
          const ir::Module* call_module = &module;
          auto call_module_owner = module_owner;
          if (fn_obj->module != nullptr) {
            call_module = fn_obj->module.get();
            call_module_owner = fn_obj->module;
          }
          ++ip;
          if (!push_frame(*call_module, fn_obj->function_id, values, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst)) {
            return false;
          }
          pushed_frame = true;
          return true;
        };
        auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          const bool use_fast = native->fast_callback != nullptr;
          const Value* native_args = nullptr;
          if (!use_fast) {
            native_args = materialize_native_args(values);
          }
          bool ok = false;
          if (use_fast && !native->fast_releases_vm_lock) {
            ok = native->fast_callback(
                runtime_,
                values.leading,
                values.leading_count,
                values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result,
                error,
                native->user_data);
          } else {
            execution_lock.unlock();
            ok = use_fast
                ? native->fast_callback(
                    runtime_,
                    values.leading,
                    values.leading_count,
                    values.registers,
                    values.register_args == nullptr ? nullptr : values.register_args->data(),
                    values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                    native_result,
                    error,
                    native->user_data)
                : native->callback != nullptr &&
                      native->callback(
                          runtime_,
                          native_args,
                          static_cast<uint32_t>(values.size()),
                          native_result,
                          error,
                      native->user_data);
            execution_lock.lock();
          }
          if (!ok) {
            Value pending;
            if (runtime_.take_pending_exception(pending)) {
              if (raise_exception_value(std::move(pending))) return false;
              return false;
            }
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };

        if (auto* instance = value_as_instance(regs[in.a])) {
          if (auto* klass = value_as_class(instance->klass)) {
            CallArgsView method_args = call_args;
            method_args.leading = &regs[in.a];
            method_args.leading_count = 1;
            if (!call_site_cache.empty()) {
              auto& cache = call_site_cache[ip];
              if (cache.callee_object == &klass->header && cache.class_version == klass->version) {
                if (cache.kind == CallSiteKind::UserFunction) {
                  if (!call_user_function(cache.function, method_args, regs[in.dst])) {
                    if (!result.errors.empty()) return result;
                    continue;
                  }
                  if (pushed_frame) goto switch_frame;
                  break;
                }
                if (cache.kind == CallSiteKind::NativeFunction) {
                  if (!call_native_function(cache.native, method_args, regs[in.dst])) {
                    if (!result.errors.empty()) return result;
                    continue;
                  }
                  break;
                }
                if (cache.kind == CallSiteKind::InlineSelfBinaryMethod && call_arg_regs.empty()) {
                  SelfBinaryMethodSpec spec;
                  spec.lhs_slot = cache.lhs_slot;
                  spec.rhs_slot = cache.rhs_slot;
                  spec.op = cache.inline_op;
                  std::string error;
                  if (!execute_self_binary_method(*instance, spec, regs[in.dst], error)) {
                    if (raise_runtime_error(error)) continue;
                    return result;
                  }
                  break;
                }
              }
            }
            auto method_it = klass->attrs.find(name);
            if (method_it != klass->attrs.end()) {
              if (auto* native = value_as_native_function(method_it->second)) {
                if (!call_site_cache.empty()) {
                  auto& cache = call_site_cache[ip];
                  cache.callee_object = &klass->header;
                  cache.kind = CallSiteKind::NativeFunction;
                  cache.function = nullptr;
                  cache.native = native;
                  cache.class_version = klass->version;
                }
                if (!call_native_function(native, method_args, regs[in.dst])) {
                  if (!result.errors.empty()) return result;
                  continue;
                }
                break;
              }
              if (auto* fn_obj = value_as_function(method_it->second)) {
                SelfBinaryMethodSpec inline_spec;
                if (call_arg_regs.empty() && analyze_self_binary_method(module, *fn_obj, inline_spec)) {
                  if (!call_site_cache.empty()) {
                    auto& cache = call_site_cache[ip];
                    cache.callee_object = &klass->header;
                    cache.kind = CallSiteKind::InlineSelfBinaryMethod;
                    cache.function = fn_obj;
                    cache.native = nullptr;
                    cache.class_version = klass->version;
                    cache.lhs_slot = inline_spec.lhs_slot;
                    cache.rhs_slot = inline_spec.rhs_slot;
                    cache.inline_op = inline_spec.op;
                  }
                  std::string error;
                  if (!execute_self_binary_method(*instance, inline_spec, regs[in.dst], error)) {
                    if (raise_runtime_error(error)) continue;
                    return result;
                  }
                  break;
                }
                if (!call_site_cache.empty()) {
                  auto& cache = call_site_cache[ip];
                  cache.callee_object = &klass->header;
                  cache.kind = CallSiteKind::UserFunction;
                  cache.function = fn_obj;
                  cache.native = nullptr;
                  cache.class_version = klass->version;
                }
                if (!call_user_function(fn_obj, method_args, regs[in.dst])) {
                  if (!result.errors.empty()) return result;
                  continue;
                }
                if (pushed_frame) goto switch_frame;
                break;
              }
            }
          }
        }

        if (auto* module_object = value_as_module(regs[in.a])) {
          if (!call_site_cache.empty()) {
            auto& cache = call_site_cache[ip];
            if (cache.callee_object == regs[in.a].as.obj &&
                cache.class_version == module_object->version &&
                cache.kind == CallSiteKind::NativeFunction) {
              if (!call_native_function(cache.native, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              break;
            }
          }

          std::string module_error;
          uint32_t module_slot = 0;
          if (module_find_attr_slot(regs[in.a], name, module_slot, module_error) &&
              module_slot < module_object->slots.size()) {
            const Value& module_attr = module_object->slots[module_slot];
            if (auto* native = value_as_native_function(module_attr)) {
              if (!call_site_cache.empty()) {
                auto& cache = call_site_cache[ip];
                cache.callee_object = regs[in.a].as.obj;
                cache.kind = CallSiteKind::NativeFunction;
                cache.function = nullptr;
                cache.native = native;
                cache.class_version = module_object->version;
              }
              if (!call_native_function(native, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              break;
            }
          }
        }

        Value method;
        std::string attr_error;
        if (!attribute_get(regs[in.a], name, method, attr_error)) {
          if (raise_runtime_error(attr_error)) continue;
          return result;
        }

        if (auto* bound = value_as_bound_method(method)) {
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
          if (auto* native = value_as_native_function(bound->function)) {
            if (!call_native_function(native, bound_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
          } else if (auto* fn_obj = value_as_function(bound->function)) {
            if (!call_user_function(fn_obj, bound_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
            if (pushed_frame) goto switch_frame;
          } else {
            if (raise_runtime_error("object is not callable")) continue;
            return result;
          }
        } else if (auto* native = value_as_native_function(method)) {
          if (!call_native_function(native, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (auto* fn_obj = value_as_function(method)) {
          if (!call_user_function(fn_obj, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* klass = value_as_class(method)) {
          Value instance = Value::instance(method);
          CallArgsView init_args = call_args;
          init_args.leading = &instance;
          init_args.leading_count = 1;
          Value init_value;
          std::string init_error;
          if (object_get_attr(method, "__init__", init_value, init_error)) {
            if (auto* native = value_as_native_function(init_value)) {
              Value ignored;
              if (!call_native_function(native, init_args, ignored)) {
                if (!result.errors.empty()) return result;
                continue;
              }
              value_assign_fast(regs[in.dst], instance);
            } else if (auto* init_fn = value_as_function(init_value)) {
              const ir::Module* call_module = &module;
              auto call_module_owner = module_owner;
              if (init_fn->module != nullptr) {
                call_module = init_fn->module.get();
                call_module_owner = init_fn->module;
              }
              Value constructed_instance;
              value_assign_fast(constructed_instance, instance);
              ++ip;
              if (!push_frame(*call_module, init_fn->function_id, init_args, init_fn->closure, init_fn->defaults, init_fn->globals_module,
                              std::move(call_module_owner), in.dst,
                              FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                return result;
              }
              goto switch_frame;
            } else {
              if (raise_runtime_error("__init__ is not callable")) continue;
              return result;
            }
          } else {
            if (call_args.size() != 0) {
              if (raise_runtime_error("class construction expected no arguments")) continue;
              return result;
            }
            value_assign_fast(regs[in.dst], instance);
          }
        } else {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        }
        break;
      }
      case ir::Op::Call: {
        if (in.b >= fn.call_args.size()) {
          result.errors.push_back("invalid call arg list");
          return result;
        }
        const auto& call_arg_regs = fn.call_args[in.b];
        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &call_arg_regs;
        if (in.a < regs.size() && regs[in.a].tag == ValueTag::Invalid) {
          result.errors.push_back("invalid callee");
          return result;
        }
        const auto& callee = regs[in.a];
        bool pushed_frame = false;
        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };
        auto call_user_function = [&](FunctionObject* fn_obj, CallArgsView values, Value& out) -> bool {
          bool made_generator = false;
          if (!make_generator_if_needed(fn_obj, values, out, made_generator)) {
            return false;
          }
          if (made_generator) {
            return true;
          }
          const ir::Module* call_module = &module;
          auto call_module_owner = module_owner;
          if (fn_obj->module != nullptr) {
            call_module = fn_obj->module.get();
            call_module_owner = fn_obj->module;
          }
          ++ip;
          if (!push_frame(*call_module, fn_obj->function_id, values, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                          std::move(call_module_owner), in.dst)) {
            return false;
          }
          pushed_frame = true;
          return true;
        };
        auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          const bool use_fast = native->fast_callback != nullptr;
          const Value* native_args = nullptr;
          if (!use_fast) {
            native_args = materialize_native_args(values);
          }
          bool ok = false;
          if (use_fast && !native->fast_releases_vm_lock) {
            ok = native->fast_callback(
                runtime_,
                values.leading,
                values.leading_count,
                values.registers,
                values.register_args == nullptr ? nullptr : values.register_args->data(),
                values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                native_result,
                error,
                native->user_data);
          } else {
            execution_lock.unlock();
            ok = use_fast
                ? native->fast_callback(
                    runtime_,
                    values.leading,
                    values.leading_count,
                    values.registers,
                    values.register_args == nullptr ? nullptr : values.register_args->data(),
                    values.register_args == nullptr ? 0 : static_cast<uint32_t>(values.register_args->size()),
                    native_result,
                    error,
                    native->user_data)
                : native->callback != nullptr &&
                      native->callback(
                          runtime_,
                          native_args,
                          static_cast<uint32_t>(values.size()),
                          native_result,
                          error,
                      native->user_data);
            execution_lock.lock();
          }
          if (!ok) {
            Value pending;
            if (runtime_.take_pending_exception(pending)) {
              if (raise_exception_value(std::move(pending))) return false;
              return false;
            }
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };
        if (!call_site_cache.empty() && callee.tag == ValueTag::Object && callee.as.obj != nullptr) {
          auto& cache = call_site_cache[ip];
            if (cache.callee_object == callee.as.obj) {
            if (cache.kind == CallSiteKind::UserFunction) {
              if (!call_user_function(cache.function, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              if (pushed_frame) goto switch_frame;
              break;
            }
            if (cache.kind == CallSiteKind::NativeFunction) {
              if (!call_native_function(cache.native, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              break;
            }
            if (cache.kind == CallSiteKind::InlineArgBinaryFunction) {
              ArgBinaryFunctionSpec spec;
              spec.lhs_arg = cache.lhs_slot;
              spec.rhs_arg = cache.rhs_slot;
              spec.op = cache.inline_op;
              spec.next_arg = cache.next_arg;
              spec.next_op = cache.next_op;
              spec.has_next = cache.has_next;
              std::string error;
              if (!execute_arg_binary_function(call_args, spec, regs[in.dst], error)) {
                if (raise_runtime_error(error)) continue;
                return result;
              }
              break;
            }
            if (cache.kind == CallSiteKind::UserConstructor || cache.kind == CallSiteKind::NativeConstructor ||
                cache.kind == CallSiteKind::InlineSlotConstructor) {
              auto* cached_class = value_as_class(callee);
              if (cached_class == nullptr || cache.class_version != cached_class->version) {
                cache.kind = CallSiteKind::Empty;
              } else {
              if (cache.kind == CallSiteKind::InlineSlotConstructor) {
                std::string error;
                if (!execute_slot_constructor(callee, call_args, cache.slot_constructor_args, regs[in.dst], error)) {
                  if (raise_runtime_error(error)) continue;
                  return result;
                }
                break;
              }
              Value instance = Value::instance(callee);
              CallArgsView init_args = call_args;
              init_args.leading = &instance;
              init_args.leading_count = 1;
              if (cache.kind == CallSiteKind::UserConstructor) {
                auto* fn_obj = cache.function;
                const ir::Module* call_module = &module;
                auto call_module_owner = module_owner;
                if (fn_obj->module != nullptr) {
                  call_module = fn_obj->module.get();
                  call_module_owner = fn_obj->module;
                }
                Value constructed_instance;
                value_assign_fast(constructed_instance, instance);
                ++ip;
                if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                                std::move(call_module_owner), in.dst,
                                FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                  return result;
                }
                goto switch_frame;
              }
              Value ignored;
              if (!call_native_function(cache.native, init_args, ignored)) {
                if (!result.errors.empty()) return result;
                continue;
              }
              value_assign_fast(regs[in.dst], instance);
              break;
              }
            }
          }
        }
        auto call_callable_value = [&](const Value& function_value, CallArgsView values, Value& out) -> bool {
          if (auto* native = value_as_native_function(function_value)) {
            return call_native_function(native, values, out);
          }

          auto* fn_obj = value_as_function(function_value);
          if (fn_obj == nullptr) {
            if (raise_runtime_error("object is not callable")) return false;
            return false;
          }
          return call_user_function(fn_obj, values, out);
        };
        if (auto* fn_obj = value_as_function(callee)) {
          ArgBinaryFunctionSpec inline_spec;
          if (analyze_arg_binary_function(module, *fn_obj, static_cast<uint32_t>(call_args.size()), inline_spec)) {
            if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
              auto& cache = call_site_cache[ip];
              cache.callee_object = callee.as.obj;
              cache.kind = CallSiteKind::InlineArgBinaryFunction;
              cache.function = fn_obj;
              cache.native = nullptr;
              cache.class_version = 0;
              cache.lhs_slot = inline_spec.lhs_arg;
              cache.rhs_slot = inline_spec.rhs_arg;
              cache.inline_op = inline_spec.op;
              cache.next_arg = inline_spec.next_arg;
              cache.next_op = inline_spec.next_op;
              cache.has_next = inline_spec.has_next;
            }
            std::string error;
            if (!execute_arg_binary_function(call_args, inline_spec, regs[in.dst], error)) {
              if (raise_runtime_error(error)) continue;
              return result;
            }
            break;
          }
          if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
            auto& cache = call_site_cache[ip];
            cache.callee_object = callee.as.obj;
            cache.kind = CallSiteKind::UserFunction;
            cache.function = fn_obj;
            cache.native = nullptr;
            cache.class_version = 0;
          }
          if (!call_user_function(fn_obj, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* bound = value_as_bound_method(callee)) {
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
          if (!call_callable_value(bound->function, bound_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
          if (pushed_frame) goto switch_frame;
        } else if (auto* klass = value_as_class(callee)) {
          Value instance = Value::instance(callee);
          CallArgsView init_args = call_args;
          init_args.leading = &instance;
          init_args.leading_count = 1;
          if (!call_site_cache.empty()) {
            auto& cache = call_site_cache[ip];
            if (cache.callee_object == callee.as.obj && cache.class_version == klass->version) {
              if (cache.kind == CallSiteKind::InlineSlotConstructor) {
                std::string error;
                if (!execute_slot_constructor(callee, call_args, cache.slot_constructor_args, regs[in.dst], error)) {
                  if (raise_runtime_error(error)) continue;
                  return result;
                }
                break;
              }
              if (cache.kind == CallSiteKind::UserConstructor) {
                const auto* fn_obj = cache.function;
                const ir::Module* call_module = &module;
                auto call_module_owner = module_owner;
                if (fn_obj->module != nullptr) {
                  call_module = fn_obj->module.get();
                  call_module_owner = fn_obj->module;
                }
                Value constructed_instance;
                value_assign_fast(constructed_instance, instance);
                ++ip;
                if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                                std::move(call_module_owner), in.dst,
                                FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                  return result;
                }
                goto switch_frame;
              }
              if (cache.kind == CallSiteKind::NativeConstructor) {
                Value ignored;
                if (!call_native_function(cache.native, init_args, ignored)) {
                  if (!result.errors.empty()) return result;
                  continue;
                }
                value_assign_fast(regs[in.dst], instance);
                break;
              }
            }
          }
          Value init_value;
          std::string init_error;
          if (object_get_attr(callee, "__init__", init_value, init_error)) {
            if (auto* native = value_as_native_function(init_value)) {
              if (!call_site_cache.empty()) {
                auto& cache = call_site_cache[ip];
                cache.callee_object = callee.as.obj;
                cache.kind = CallSiteKind::NativeConstructor;
                cache.function = nullptr;
                cache.native = native;
                cache.class_version = klass->version;
              }
              Value ignored;
              if (!call_native_function(native, init_args, ignored)) {
                if (!result.errors.empty()) return result;
                continue;
              }
              value_assign_fast(regs[in.dst], instance);
            } else if (auto* fn_obj = value_as_function(init_value)) {
              SlotConstructorSpec slot_constructor_spec;
              if (!call_args.has_keywords() && !call_args.has_expansion() &&
                  analyze_slot_constructor(module, *fn_obj, slot_constructor_spec)) {
                if (!call_site_cache.empty()) {
                  auto& cache = call_site_cache[ip];
                  cache.callee_object = callee.as.obj;
                  cache.kind = CallSiteKind::InlineSlotConstructor;
                  cache.function = fn_obj;
                  cache.native = nullptr;
                  cache.class_version = klass->version;
                  cache.slot_constructor_args = slot_constructor_spec;
                }
                std::string error;
                if (!execute_slot_constructor(callee, call_args, slot_constructor_spec, regs[in.dst], error)) {
                  if (raise_runtime_error(error)) continue;
                  return result;
                }
                break;
              }
              if (!call_site_cache.empty()) {
                auto& cache = call_site_cache[ip];
                cache.callee_object = callee.as.obj;
                cache.kind = CallSiteKind::UserConstructor;
                cache.function = fn_obj;
                cache.native = nullptr;
                cache.class_version = klass->version;
              }
              const ir::Module* call_module = &module;
              auto call_module_owner = module_owner;
              if (fn_obj->module != nullptr) {
                call_module = fn_obj->module.get();
                call_module_owner = fn_obj->module;
              }
              Value constructed_instance;
              value_assign_fast(constructed_instance, instance);
              ++ip;
              if (!push_frame(*call_module, fn_obj->function_id, init_args, fn_obj->closure, fn_obj->defaults, fn_obj->globals_module,
                              std::move(call_module_owner), in.dst,
                              FrameReturnMode::StoreConstructedInstance, std::move(constructed_instance))) {
                return result;
              }
              goto switch_frame;
            } else {
              if (raise_runtime_error("__init__ is not callable")) continue;
              return result;
            }
          } else {
            if (call_args.size() != 0) {
              if (raise_runtime_error("class construction expected no arguments")) continue;
              return result;
            }
            value_assign_fast(regs[in.dst], instance);
          }
        } else if (auto* native = value_as_native_function(callee)) {
          if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
            auto& cache = call_site_cache[ip];
            cache.callee_object = callee.as.obj;
            cache.kind = CallSiteKind::NativeFunction;
            cache.function = nullptr;
            cache.native = native;
            cache.class_version = 0;
          }
          if (!call_native_function(native, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (callee.tag == ValueTag::Object) {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        } else if (callee.tag == ValueTag::Invalid) {
          if (raise_runtime_error("invalid callee")) continue;
          return result;
        } else {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        }
        break;
      }
      case ir::Op::Await: {
#ifndef XLANG3_EMBEDDED
        std::string await_error;
        if (!xlang_task_await_value(regs[in.a], regs[in.dst], await_error)) {
          if (raise_runtime_error(await_error.empty() ? "await failed" : await_error)) continue;
          return result;
        }
#else
        value_assign_fast(regs[in.dst], regs[in.a]);
#endif
        break;
      }
      case ir::Op::Yield:
        if (generator == nullptr) {
          if (raise_runtime_error("yield used outside generator")) continue;
          return result;
        } else {
          Value yielded_value;
          value_assign_fast(yielded_value, regs[in.a]);
          ++ip;
          auto* state = new GeneratorVMState();
          state->frames = std::move(frames);
          state->frame_count = frame_count;
          if (generator->vm_state_cleanup != nullptr && generator->vm_state != nullptr) {
            generator->vm_state_cleanup(generator->vm_state);
          }
          generator->vm_state = state;
          generator->vm_state_cleanup = destroy_generator_vm_state;
          generator->done = false;
          value_assign_fast(result.value, yielded_value);
          return result;
        }
        break;
      case ir::Op::YieldFrom:
        if (raise_runtime_error("internal yield from was not lowered")) continue;
        return result;
      case ir::Op::Pop:
        break;
      case ir::Op::Return:
        {
          Value return_value;
          value_assign_fast(return_value, regs[in.a]);
          if (!finish_frame(return_value)) {
            if (generator != nullptr) {
              generator->done = true;
              value_set_none(result.value);
            }
            return result;
          }
        }
        goto switch_frame;
      }
      ++ip;
    }
    } catch (const VMUnwind&) {
      goto switch_frame;
    }
switch_frame:
    continue;
  }

  value_set_none(result.value);
  if (generator != nullptr) {
    generator->done = true;
  }
  return result;
}

} // namespace xlang3
