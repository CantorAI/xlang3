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
#include "xlang3/interpreter_events.h"
#include "xlang3/expression.h"

#include "xlang_frame.h"
#include "xlang_vm_arithmetic.h"
#include "xlang_vm_attr.h"
#include "xlang_vm_inline_call.h"
#include "xlang_vm_op_switch.h"
#include "xlang_vm_property_inline.h"
#include "ops/xlang_vm_ops_arithmetic.h"
#include "ops/xlang_vm_ops_async.h"
#include "ops/xlang_vm_ops_attr.h"
#include "ops/xlang_vm_ops_call.h"
#include "ops/xlang_vm_ops_containers.h"
#include "ops/xlang_vm_ops_control.h"
#include "ops/xlang_vm_ops_construct.h"
#include "ops/xlang_vm_ops_import_raw.h"
#include "ops/xlang_vm_ops_iteration.h"
#include "ops/xlang_vm_ops_variables.h"
#include "ops/xlang_vm_ops_fused.h"
#include "runtime_lock.h"

#include <array>

#include "xlang3/attribute.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/builtins.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/generator.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#ifndef XLANG3_EMBEDDED
#include "task_objects.h"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <string_view>
#include <thread>

#include "xlang_vm_inline_support.h"

namespace xlang3 {

void generator_vm_visit_references(
    const GeneratorObject& generator,
    const std::function<void(const Value&)>& visit) {
  if (generator.vm_state == nullptr) return;
  const auto* state = static_cast<const GeneratorVMState*>(generator.vm_state);
  visit(state->current_exception);
  for (const auto& value : state->previous_exceptions) visit(value);
  const size_t count = std::min(state->frame_count, state->frames.size());
  for (size_t index = 0; index < count; ++index) {
    const auto& frame = state->frames[index];
    for (size_t i = 0; i < frame.locals.size(); ++i) visit(frame.locals[i]);
    for (size_t i = 0; i < frame.cells.size(); ++i) visit(frame.cells[i]);
    for (size_t i = 0; i < frame.regs.size(); ++i) visit(frame.regs[i]);
    for (const auto& value : frame.native_call_args) visit(value);
    if (frame.closure_owner != nullptr)
      for (const auto& value : *frame.closure_owner) visit(value);
    visit(frame.continuation_value);
  }
}

bool generator_vm_frame_snapshot(const GeneratorObject& generator, Value& out) {
  auto snapshot_frame = [&](const VMFrame& frame) {
    if (frame.fn == nullptr || frame.module_owner == nullptr) return false;
    std::vector<std::pair<Value, Value>> entries;
    entries.reserve(frame.fn->locals.size() + frame.fn->free_vars.size());
    for (size_t local_index = 0;
         local_index < frame.fn->locals.size() && local_index < frame.locals.size();
         ++local_index) {
      const auto& name = frame.fn->locals[local_index];
      if (name.empty() || name[0] == '#') continue;
      const Value* local_value = &frame.locals[local_index];
      for (size_t cell_index = 0;
           cell_index < frame.fn->cell_slots.size() && cell_index < frame.cells.size();
           ++cell_index) {
        if (frame.fn->cell_slots[cell_index] == local_index) {
          if (auto* cell = value_as_cell(frame.cells[cell_index])) local_value = &cell->value;
          break;
        }
      }
      if (local_value->tag != ValueTag::Invalid) entries.push_back({Value::string(name), *local_value});
    }
    if (frame.closure != nullptr) {
      for (size_t free_index = 0;
           free_index < frame.fn->free_vars.size() && free_index < frame.closure->size();
           ++free_index) {
        const auto& name = frame.fn->free_vars[free_index];
        if (name.empty() || name[0] == '#') continue;
        const Value* free_value = &(*frame.closure)[free_index];
        if (auto* cell = value_as_cell(*free_value)) free_value = &cell->value;
        if (free_value->tag != ValueTag::Invalid) entries.push_back({Value::string(name), *free_value});
      }
    }
    out = Value::frame(
        frame.module_owner,
        frame.function_id,
        frame.globals_module,
        static_cast<uint32_t>(frame.ip),
        Value::dict(std::move(entries)),
        Value::none(),
        Value::none(),
        frame.activation_id);
    if (generator.runtime != nullptr) frame_set_generator_owner(*generator.runtime, out, generator);
    return true;
  };

  if (generator.vm_state != nullptr) {
    const auto* state = static_cast<const GeneratorVMState*>(generator.vm_state);
    if (state->frame_count == 0 || state->frame_count > state->frames.size()) return false;
    return snapshot_frame(state->frames[state->frame_count - 1]);
  }

  // An inlined coroutine keeps its live frame in the awaiting parent's saved
  // VM stack. Follow the non-owning parent chain to find that stack so cr_frame
  // remains inspectable while the parent is suspended.
  for (auto* parent = generator.inline_parent; parent != nullptr; parent = parent->inline_parent) {
    if (parent->vm_state == nullptr) continue;
    const auto* state = static_cast<const GeneratorVMState*>(parent->vm_state);
    const size_t count = std::min(state->frame_count, state->frames.size());
    for (size_t index = count; index > 0; --index) {
      const auto& frame = state->frames[index - 1];
      if (frame.inline_coroutine_entry && frame.coroutine_owner == &generator)
        return snapshot_frame(frame);
    }
  }

  // While the parent is actively executing, materialize its current stack and
  // locate this coroutine's entry frame by the weak generator reference.
  if (generator.runtime != nullptr && generator.inline_parent != nullptr) {
    Value frame_value = generator.runtime->current_frame_snapshot();
    while (auto* frame = value_as_frame(frame_value)) {
      Value owner;
      if (weakref_get_target(frame->generator_ref, owner) && owner.as.obj == &generator.header) {
        value_assign_fast(out, frame_value);
        return true;
      }
      frame_value = frame->back;
    }
  }
  return false;
}

RuntimeResult Interpreter::run_function(
    const ir::Module& module,
    uint32_t function_id,
    CallArgsView args,
    const std::vector<Value>& fn_obj_closure,
    const std::vector<Value>& fn_obj_defaults,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner,
    GeneratorObject* generator,
    std::shared_ptr<RuntimeDebugPauseState> pause_state) {
  RuntimeResult result;
  if (function_id >= module.functions.size()) {
    result.errors.push_back("invalid function id");
    return result;
  }
  const auto& fn = module.functions[function_id];
  // The CLI changes this diagnostic switch only outside interpreter execution.
  // Snapshot it once so normal opcode dispatch avoids an atomic load per op.
  const bool count_opcode_dispatches = xlang_perf_enabled();
  const bool resuming_pause = pause_state != nullptr;
  struct CurrentGlobalsGuard {
    Runtime& runtime;
    Value previous;

    ~CurrentGlobalsGuard() {
      runtime.set_current_globals_module(previous);
    }
  } current_globals_guard{runtime_, runtime_.current_globals_module()};
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

  auto has_dynamic_positional_defaults = [](const ir::Function& target_fn,
                                             const std::vector<Value>& defaults) -> bool {
    return defaults.size() == target_fn.signature.size() + 1 &&
           !defaults.empty() && defaults.back().tag == ValueTag::Invalid;
  };

  std::function<bool(const std::string&)> bind_error = [&](const std::string& message) -> bool {
    runtime_.set_pending_exception(runtime_.make_exception("TypeError", message));
    result.errors.push_back(message);
    return false;
  };

  auto callable_display_name = [](const ir::Function& target_fn, CallArgsView values) {
    if (values.size() != 0 && target_fn.name.size() >= 4 &&
        target_fn.name[0] == '_' && target_fn.name[1] == '_') {
      if (auto* instance = value_as_instance(values.get(0))) {
        if (auto* klass = value_as_class(instance->klass)) {
          return klass->name + "." + target_fn.name;
        }
      }
    }
    return target_fn.name;
  };

  auto bind_count_error = [&](const ir::Function& target_fn, CallArgsView values) -> bool {
    const size_t expected = target_fn.params.size();
    const size_t provided = values.size();
    const std::string display_name = callable_display_name(target_fn, values);
    if (provided < expected) {
      const size_t missing = expected - provided;
      std::string message = display_name + "() missing " + std::to_string(missing) +
                            " required positional argument" + (missing == 1 ? ": " : "s: ");
      for (size_t i = provided; i < expected; ++i) {
        if (i != provided) {
          message += i + 1 == expected ? " and " : ", ";
        }
        message += "'" + target_fn.params[i] + "'";
      }
      return bind_error(message);
    }
    return bind_error(
        display_name + "() takes " + std::to_string(expected) + " positional argument" +
        (expected == 1 ? "" : "s") + " but " + std::to_string(provided) +
        (provided == 1 ? " was given" : " were given"));
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
    const bool dynamic_positional_defaults = has_dynamic_positional_defaults(target_fn, defaults);
    const Value* live_keyword_defaults = values.live_keyword_defaults;
    const bool dynamic_keyword_defaults = live_keyword_defaults != nullptr &&
        live_keyword_defaults->tag != ValueTag::Invalid;
    if (target_fn.signature.empty() && !dynamic_positional_defaults &&
        !values.has_keywords() && !values.has_expansion()) {
      if (values.size() != target_fn.params.size()) {
        return bind_count_error(target_fn, values);
      }
      return true;
    }

    bound.assign(target_fn.params.size(), Value::invalid());
    // Ordinary positional calls to fixed-arity functions need no keyword
    // matching or expansion. Bind those locals directly and read omitted
    // defaults from the live function defaults so changing __defaults__ keeps
    // taking effect. This avoids the general binder's keyword/overflow scans
    // on hot recursive Python calls such as pickle._Pickler.save().
    if (!values.has_keywords() && !values.has_expansion() &&
        target_fn.params.size() == signature.size() &&
        values.size() <= signature.size()) {
      bool simple_positional_signature = true;
      bool all_omitted_have_defaults = true;
      for (size_t index = 0; index < signature.size(); ++index) {
        const auto& param = signature[index];
        if (param.kind != ir::ParamKind::PosOnly &&
            param.kind != ir::ParamKind::PosOrKeyword) {
          simple_positional_signature = false;
          break;
        }
        if (index >= values.size()) {
          const bool has_default = dynamic_positional_defaults
              ? index < defaults.size() && defaults[index].tag != ValueTag::Invalid
              : param.default_reg != UINT32_MAX && param.default_reg < defaults.size() &&
                    defaults[param.default_reg].tag != ValueTag::Invalid;
          if (!has_default) all_omitted_have_defaults = false;
        }
      }
      if (simple_positional_signature && all_omitted_have_defaults) {
        for (size_t index = 0; index < signature.size(); ++index) {
          if (index < values.size()) {
            bound[index] = values.get(index);
          } else if (dynamic_positional_defaults) {
            value_assign_fast(bound[index], defaults[index]);
          } else {
            value_assign_fast(bound[index], defaults[signature[index].default_reg]);
          }
        }
        return true;
      }
    }
    // A common CALL_FUNCTION_EX-free keyword shape supplies one positional
    // argument and names the second fixed parameter (asyncio.ensure_future's
    // `ensure_future(coro, loop=loop)` is one hot example). Resolve this exact
    // two-slot layout directly; expansions, duplicate/unknown names, and all
    // other signatures still use the general binder so their Python errors
    // and mapping side effects remain unchanged.
    if (!dynamic_positional_defaults && !values.has_expansion() &&
        values.size() == 1 && values.keyword_args != nullptr &&
        values.keyword_args->size() == 1 && target_fn.params.size() == 2 &&
        signature.size() == 2 &&
        (signature[0].kind == ir::ParamKind::PosOnly ||
         signature[0].kind == ir::ParamKind::PosOrKeyword) &&
        (signature[1].kind == ir::ParamKind::PosOrKeyword ||
         signature[1].kind == ir::ParamKind::KeywordOnly) &&
        signature[1].name == values.keyword_args->front().name) {
      value_assign_fast(bound[0], values.get(0));
      value_assign_fast(
          bound[1], values.registers[values.keyword_args->front().value_reg]);
      return true;
    }
    // A fixed positional call to `f(required..., *args, **kwargs)` is a common
    // Python method shape (for example logging.Logger.debug). When the call
    // supplies exactly the required prefix and no expansions, bind its values
    // directly and create the two empty variadics without running the general
    // keyword/overflow binder. Keep both containers fresh per call because
    // Python code can observe or mutate them.
    if (!dynamic_positional_defaults && !values.has_keywords() &&
        !values.has_expansion() && target_fn.params.size() == signature.size() &&
        signature.size() >= 2 &&
        signature[signature.size() - 2].kind == ir::ParamKind::VarArgs &&
        signature.back().kind == ir::ParamKind::KwArgs) {
      const size_t positional_count = signature.size() - 2;
      bool simple_empty_variadics = values.size() == positional_count;
      for (size_t index = 0; simple_empty_variadics && index < positional_count; ++index) {
        const auto& param = signature[index];
        simple_empty_variadics =
            (param.kind == ir::ParamKind::PosOnly ||
             param.kind == ir::ParamKind::PosOrKeyword) &&
            param.default_reg == UINT32_MAX;
      }
      if (simple_empty_variadics) {
        for (size_t index = 0; index < positional_count; ++index)
          bound[index] = values.get(index);
        bound[positional_count] = Value::tuple({});
        bound[positional_count + 1] = Value::dict({});
        return true;
      }
    }
    // `asyncio.gather(*new_list)` expands one temporary exact list into a
    // function shaped as `(*args, keyword_only=default)`. The generic binder
    // first copies list elements into expanded_positional, then copies them
    // again into extra_positional before building args. Preserve CPython's
    // observable tuple identity rules, but build that one final tuple directly
    // and copy the live keyword-only defaults. Subclasses, iterators, multiple
    // stars, keywords, and mutable/invalid defaults retain generic binding.
    if (!dynamic_positional_defaults && !dynamic_keyword_defaults && values.size() == 0 &&
        !values.has_keywords() && values.kw_star_arg == UINT32_MAX &&
        (values.kw_star_args == nullptr || values.kw_star_args->empty()) &&
        target_fn.params.size() == signature.size() && signature.size() >= 2 &&
        signature.front().kind == ir::ParamKind::VarArgs) {
      bool fixed_keyword_only_defaults = true;
      for (size_t index = 1; index < signature.size(); ++index) {
        const auto& param = signature[index];
        fixed_keyword_only_defaults = fixed_keyword_only_defaults &&
            param.kind == ir::ParamKind::KeywordOnly &&
            param.default_reg != UINT32_MAX && param.default_reg < defaults.size() &&
            defaults[param.default_reg].tag != ValueTag::Invalid;
      }
      const bool one_star = values.star_args != nullptr && !values.star_args->empty()
          ? values.star_args->size() == 1
          : values.star_arg != UINT32_MAX;
      if (fixed_keyword_only_defaults && one_star) {
        const uint32_t star_reg = values.star_args != nullptr && !values.star_args->empty()
            ? values.star_args->front() : values.star_arg;
        const Value& star_value = values.registers[star_reg];
        const auto* star_tuple = value_as_tuple(star_value);
        const auto* star_list = value_as_list(star_value);
        if (star_tuple != nullptr || star_list != nullptr) {
          if (star_tuple != nullptr && star_tuple->items.empty()) {
            // CALL_FUNCTION_EX reuses an exact empty tuple as the *args value.
            value_assign_fast(bound[0], star_value);
          } else if (star_tuple != nullptr) {
            std::vector<Value> varargs;
            varargs.reserve(star_tuple->items.size());
            for (const auto& item : star_tuple->items) varargs.push_back(item);
            bound[0] = Value::tuple(std::move(varargs));
          } else {
            std::vector<Value> varargs;
            varargs.reserve(star_list->items.size());
            for (const auto& item : star_list->items) varargs.push_back(item);
            bound[0] = Value::tuple(std::move(varargs));
          }
          for (size_t index = 1; index < signature.size(); ++index) {
            value_assign_fast(bound[index], defaults[signature[index].default_reg]);
          }
          xlang_perf_count_call_ex_varargs_kwonly_binding();
          return true;
        }
      }
    }
    // Read explicit positional arguments directly from the call view. Most
    // calls with keywords have no `*args`; copying their arguments into a
    // temporary vector only adds an allocation and refcount traffic before
    // the bound frame copies them again. Keep storage only for expanded stars.
    auto expand_star_arg = [&](uint32_t star_reg, std::vector<Value>& output) -> bool {
      const Value& star = values.registers[star_reg];
      if (auto* tuple = value_as_tuple(star)) {
        for (const auto& item : tuple->items) output.push_back(item);
      } else if (auto* list = value_as_list(star)) {
        for (const auto& item : list->items) output.push_back(item);
      } else {
        Value iterator;
        std::string iter_error;
        if (!runtime_get_iter(runtime_, star, iterator, iter_error)) {
          return bind_error("function '" + target_fn.name + "' * argument must be iterable");
        }
        while (true) {
          bool done = false;
          Value item;
          if (!sequence_iter_next(iterator, done, item, iter_error)) {
            return bind_error(iter_error.empty() ? "function '" + target_fn.name + "' failed to expand * argument" : iter_error);
          }
          if (done) {
            break;
          }
          output.push_back(std::move(item));
        }
      }
      return true;
    };

    std::vector<Value> expanded_positional;
    if (values.star_args != nullptr && !values.star_args->empty()) {
      for (uint32_t star_reg : *values.star_args) {
        if (!expand_star_arg(star_reg, expanded_positional)) {
          return false;
        }
      }
    } else if (values.star_arg != UINT32_MAX) {
      if (!expand_star_arg(values.star_arg, expanded_positional)) {
        return false;
      }
    }

    int32_t varargs_index = -1;
    int32_t kwargs_index = -1;
    uint32_t empty_tuple_varargs_identity_reg = UINT32_MAX;
    size_t next_positional_param = 0;
    size_t positional_index = 0;
    bool too_many_positional = false;
    size_t keyword_only_given = 0;
    std::vector<Value> extra_positional;
    std::vector<std::pair<Value, Value>> extra_keywords;
    for (size_t i = 0; i < signature.size(); ++i) {
      if (signature[i].kind == ir::ParamKind::VarArgs) {
        varargs_index = static_cast<int32_t>(i);
      } else if (signature[i].kind == ir::ParamKind::KwArgs) {
        kwargs_index = static_cast<int32_t>(i);
      }
    }
    // CPython preserves the exact empty tuple passed to a varargs-only target
    // by CALL_FUNCTION_EX. Keep that observable identity while using the
    // normal binder for every performance-sensitive non-empty expansion.
    const bool defaulted_varargs_signature =
        target_fn.params.size() == signature.size() && !signature.empty() &&
        signature[0].kind == ir::ParamKind::VarArgs;
    if (defaulted_varargs_signature && !dynamic_keyword_defaults) {
      bool only_keyword_only_defaults = true;
      for (size_t i = 1; i < signature.size(); ++i) {
        const uint32_t default_reg = signature[i].default_reg;
        only_keyword_only_defaults = only_keyword_only_defaults &&
            signature[i].kind == ir::ParamKind::KeywordOnly &&
            default_reg != UINT32_MAX && default_reg < defaults.size() &&
            defaults[default_reg].tag != ValueTag::Invalid;
      }
      const bool no_other_operands = values.size() == 0 &&
          !values.has_keywords() && values.kw_star_arg == UINT32_MAX &&
          (values.kw_star_args == nullptr || values.kw_star_args->empty()) &&
          !dynamic_positional_defaults;
      if (only_keyword_only_defaults && no_other_operands) {
        const uint32_t star_reg = values.star_args != nullptr && !values.star_args->empty()
            ? (values.star_args->size() == 1 ? values.star_args->front() : UINT32_MAX)
            : values.star_arg;
        if (star_reg != UINT32_MAX) {
          const auto* tuple = value_as_tuple(values.registers[star_reg]);
          if (tuple != nullptr && tuple->items.empty()) {
            empty_tuple_varargs_identity_reg = star_reg;
          }
        }
      }
    }
    const size_t positional_count = values.size() + expanded_positional.size();
    while (positional_index < positional_count) {
      while (next_positional_param < signature.size() &&
             (signature[next_positional_param].kind == ir::ParamKind::KeywordOnly ||
              signature[next_positional_param].kind == ir::ParamKind::VarArgs ||
              signature[next_positional_param].kind == ir::ParamKind::KwArgs)) {
        ++next_positional_param;
      }
      const Value& positional_value = positional_index < values.size()
          ? values.get(positional_index)
          : expanded_positional[positional_index - values.size()];
      ++positional_index;
      if (next_positional_param < signature.size()) {
        value_assign_fast(bound[next_positional_param], positional_value);
        ++next_positional_param;
      } else if (varargs_index >= 0) {
        extra_positional.push_back(positional_value);
      } else {
        too_many_positional = true;
        break;
      }
    }
    if (varargs_index >= 0) {
      if (empty_tuple_varargs_identity_reg != UINT32_MAX) {
        value_assign_fast(
            bound[static_cast<size_t>(varargs_index)],
            values.registers[empty_tuple_varargs_identity_reg]);
      } else {
        bound[static_cast<size_t>(varargs_index)] = Value::tuple(std::move(extra_positional));
      }
    }

    auto bind_keyword = [&](const std::string& name, const Value& value) -> bool {
      bool matched_positional_only = false;
      for (size_t i = 0; i < signature.size(); ++i) {
        if (signature[i].name != name) continue;
        if (signature[i].kind == ir::ParamKind::PosOnly) {
          matched_positional_only = true;
          continue;
        }
        if (signature[i].kind == ir::ParamKind::VarArgs ||
            signature[i].kind == ir::ParamKind::KwArgs) {
          continue;
        }
        if (bound[i].tag != ValueTag::Invalid) {
          return bind_error(
              callable_display_name(target_fn, values) + "() got multiple values for argument '" + name + "'");
        }
        value_assign_fast(bound[i], value);
        if (signature[i].kind == ir::ParamKind::KeywordOnly) {
          ++keyword_only_given;
        }
        return true;
      }
      if (kwargs_index >= 0) {
        extra_keywords.push_back(std::make_pair(Value::string(name), value));
        return true;
      }
      if (matched_positional_only) {
        return bind_error(callable_display_name(target_fn, values) + "() got positional-only argument as keyword");
      }
      return bind_error(
          callable_display_name(target_fn, values) + "() got an unexpected keyword argument '" + name + "'");
    };
    if (values.keyword_args != nullptr) {
      for (const auto& keyword : *values.keyword_args) {
        if (!bind_keyword(keyword.name, values.registers[keyword.value_reg])) {
          return false;
        }
      }
    }
    auto expand_kw_star_arg = [&](uint32_t kw_star_reg) -> bool {
      const Value& mapping = values.registers[kw_star_reg];
      std::vector<std::pair<Value, Value>> entries;
      if (auto* dict = value_as_dict(mapping)) {
        entries = dict->entries;
      } else {
        Value keys_method;
        Value getitem_method;
        std::string mapping_error;
        if (!object_get_attr(mapping, "keys", keys_method, mapping_error) ||
            !object_get_attr(mapping, "__getitem__", getitem_method, mapping_error)) {
          return bind_error("function '" + target_fn.name + "' ** argument must be a mapping");
        }
        Value keys_result;
        if (!runtime_call_callable(runtime_, keys_method, nullptr, 0, keys_result, mapping_error)) {
          return bind_error(mapping_error.empty()
              ? "function '" + target_fn.name + "' failed to read ** argument keys"
              : mapping_error);
        }
        std::vector<Value> keys;
        if (!runtime_collect_iterable(runtime_, keys_result, keys, mapping_error)) {
          return bind_error(mapping_error.empty()
              ? "function '" + target_fn.name + "' ** argument keys must be iterable"
              : mapping_error);
        }
        entries.reserve(keys.size());
        for (const auto& key : keys) {
          Value item;
          if (!runtime_call_callable(runtime_, getitem_method, &key, 1, item, mapping_error)) {
            return bind_error(mapping_error.empty()
                ? "function '" + target_fn.name + "' failed to read ** argument"
                : mapping_error);
          }
          entries.emplace_back(key, std::move(item));
        }
      }
      for (const auto& entry : entries) {
        auto* key = value_as_string(entry.first);
        if (key == nullptr) {
          return bind_error("function '" + target_fn.name + "' ** argument keys must be strings");
        }
        if (!bind_keyword(string_object_to_string(*key), entry.second)) {
          return false;
        }
      }
      return true;
    };
    if (values.kw_star_args != nullptr && !values.kw_star_args->empty()) {
      for (uint32_t kw_star_reg : *values.kw_star_args) {
        if (!expand_kw_star_arg(kw_star_reg)) {
          return false;
        }
      }
    } else if (values.kw_star_arg != UINT32_MAX) {
      if (!expand_kw_star_arg(values.kw_star_arg)) {
        return false;
      }
    }
    if (kwargs_index >= 0) {
      bound[static_cast<size_t>(kwargs_index)] = Value::dict(std::move(extra_keywords));
    }
    if (too_many_positional) {
      size_t positional_capacity = 0;
      size_t required_positional = 0;
      for (size_t i = 0; i < signature.size(); ++i) {
        const auto& param = signature[i];
        if (param.kind != ir::ParamKind::PosOnly && param.kind != ir::ParamKind::PosOrKeyword) {
          continue;
        }
        ++positional_capacity;
        const bool has_default = dynamic_positional_defaults
            ? i < defaults.size() && defaults[i].tag != ValueTag::Invalid
            : param.default_reg != UINT32_MAX && param.default_reg < defaults.size() &&
                  defaults[param.default_reg].tag != ValueTag::Invalid;
        if (!has_default) {
          ++required_positional;
        }
      }
      const std::string display_name = callable_display_name(target_fn, values);
      std::string expected;
      if (required_positional != positional_capacity) {
        expected = "from " + std::to_string(required_positional) + " to " +
                   std::to_string(positional_capacity) + " positional arguments";
      } else {
        expected = std::to_string(positional_capacity) + " positional argument" +
                   (positional_capacity == 1 ? "" : "s");
      }
      std::string provided = std::to_string(positional_count);
      if (keyword_only_given != 0) {
        provided += " positional argument" + std::string(positional_count == 1 ? "" : "s") +
                    " (and " + std::to_string(keyword_only_given) + " keyword-only argument" +
                    (keyword_only_given == 1 ? "" : "s") + ")";
      }
      return bind_error(
          display_name + "() takes " + expected + " but " + provided +
          (positional_count == 1 && keyword_only_given == 0 ? " was given" : " were given"));
    }
    std::vector<std::string> missing_positional;
    std::vector<std::string> missing_keyword_only;
    for (size_t i = 0; i < signature.size(); ++i) {
      if (bound[i].tag != ValueTag::Invalid) {
        continue;
      }
      // Exposed keyword defaults are a shared mutable Python dictionary.
      // Consult it only for an omitted keyword-only argument; the common
      // unexposed path keeps direct indexed reads with no dict construction.
      if (signature[i].kind == ir::ParamKind::KeywordOnly && dynamic_keyword_defaults) {
        std::string ignored;
        if (live_keyword_defaults->tag != ValueTag::None &&
            mapping_get_string_item(*live_keyword_defaults, signature[i].name, bound[i], ignored)) {
          continue;
        }
        missing_keyword_only.push_back(signature[i].name);
        continue;
      }
      if (dynamic_positional_defaults && i < defaults.size() &&
          defaults[i].tag != ValueTag::Invalid) {
        value_assign_fast(bound[i], defaults[i]);
        continue;
      }
      if (!dynamic_positional_defaults && signature[i].default_reg != UINT32_MAX &&
          signature[i].default_reg < defaults.size() &&
          defaults[signature[i].default_reg].tag != ValueTag::Invalid) {
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
      if (signature[i].kind == ir::ParamKind::KeywordOnly) {
        missing_keyword_only.push_back(signature[i].name);
      } else {
        missing_positional.push_back(signature[i].name);
      }
    }
    auto format_missing_names = [](const std::vector<std::string>& names) {
      std::string text;
      for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
          text += i + 1 == names.size() ? (names.size() == 2 ? " and " : ", and ") : ", ";
        }
        text += "'" + names[i] + "'";
      }
      return text;
    };
    auto report_missing = [&](const std::vector<std::string>& names, const std::string& kind) {
      const std::string display_name = callable_display_name(target_fn, values);
      return bind_error(
          display_name + "() missing " + std::to_string(names.size()) + " required " + kind +
          " argument" + (names.size() == 1 ? ": " : "s: ") + format_missing_names(names));
    };
    if (!missing_positional.empty()) {
      return report_missing(missing_positional, "positional");
    }
    if (!missing_keyword_only.empty()) {
      return report_missing(missing_keyword_only, "keyword-only");
    }
    return true;
  };

  std::vector<Value> entry_bound_args;
  CallArgsView entry_args = args;
  if (resuming_pause) {
    entry_args = {};
  } else if (generator != nullptr && generator->args_bound) {
    entry_args = args;
  } else if (!simple_signature(fn) || has_dynamic_positional_defaults(fn, fn_obj_defaults) ||
             args.has_keywords() || args.has_expansion()) {
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
    bind_count_error(fn, args);
    return result;
  }

  // Eager asyncio tasks recursively enter the interpreter and usually finish
  // synchronously. Reuse the cleared frame vector at each C++ interpreter
  // nesting depth so every task does not reallocate its VMFrame stack and the
  // per-frame locals/register/cache buffers. The pool is thread-local (never
  // shared by concurrent interpreters), bounded to shallow nesting, and only
  // retained when this invocation completes with no live frames; suspended,
  // paused, or exceptional frames continue to use their existing ownership.
  struct FrameVectorPool {
    std::array<std::vector<VMFrame>, 32> frames;
    size_t active_depth = 0;
  };
  thread_local FrameVectorPool frame_pool;
  const size_t pool_depth = frame_pool.active_depth++;
  std::vector<VMFrame> overflow_frames;
  std::vector<VMFrame>* frames_ptr = pool_depth < frame_pool.frames.size()
      ? &frame_pool.frames[pool_depth]
      : &overflow_frames;
  struct FrameVectorPoolGuard {
    FrameVectorPool& pool;
    std::vector<VMFrame>& frames;
    bool retain = false;
    ~FrameVectorPoolGuard() {
      if (retain) {
        // Keep only reusable storage: a pooled slot must not pin a code
        // module, closure, adaptive-cache object, or function from a finished
        // interpreter activation. VMFrame::reset rebuilds its function-bound
        // state on the next use while preserving the backing allocations.
        for (auto& frame : frames) {
          frame.closure_owner.reset();
          frame.closure = nullptr;
          frame.module_owner.reset();
          frame.execution_metadata.reset();
          frame.prepared_functions.clear();
          frame.module = nullptr;
          frame.fn = nullptr;
        }
      } else {
        frames.clear();
      }
      --pool.active_depth;
    }
  } frame_pool_guard{frame_pool, *frames_ptr};
  std::vector<VMFrame>& frames = *frames_ptr;
  // Most generator resumes publish only their own frame. Keep those borrowed
  // runtime views in stack storage so a short-lived vector allocation is not
  // paid once per yielded item; retain the general vector path for deep stacks.
  std::array<RuntimeFrameView, 8> inline_runtime_frame_views{};
  std::vector<RuntimeFrameView> overflow_runtime_frame_views;
  bool published_overflow_frame_views = false;
  uint64_t frame_stack_generation = 1;
  uint64_t published_frame_stack_generation = 0;
  struct CurrentFrameGuard {
    Runtime& runtime;

    explicit CurrentFrameGuard(Runtime& target_runtime) : runtime(target_runtime) {
      runtime.push_current_frame_state();
    }

    ~CurrentFrameGuard() {
      runtime.pop_current_frame_state();
    }
  } current_frame_guard(runtime_);
  struct ActiveExceptionGuard {
    Runtime& runtime;
    Value previous;

    explicit ActiveExceptionGuard(Runtime& target_runtime) : runtime(target_runtime) {
      value_assign_fast(previous, runtime.active_exception());
    }

    ~ActiveExceptionGuard() {
      if (previous.tag == ValueTag::Invalid) {
        runtime.clear_active_exception();
      } else {
        runtime.set_active_exception(previous);
      }
    }
  } active_exception_guard(runtime_);
  size_t frame_count = 0;
  bool resumed_generator = false;
  Value resumed_current_exception;
  std::vector<Value> resumed_previous_exceptions;
  std::vector<size_t> resumed_exception_handler_depths;
  std::vector<size_t> resumed_exception_handler_frames;
  Value generator_resume_exception;
  bool has_generator_resume_exception = false;
  if (pause_state != nullptr) {
    frames = std::move(pause_state->frames);
    frame_count = pause_state->frame_count;
  } else if (generator != nullptr && generator->vm_state != nullptr) {
    auto* state = static_cast<GeneratorVMState*>(generator->vm_state);
    const uint32_t send_target = state->send_target;
    frames = std::move(state->frames);
    frame_count = state->frame_count;
    value_assign_fast(resumed_current_exception, state->current_exception);
    resumed_previous_exceptions = std::move(state->previous_exceptions);
    resumed_exception_handler_depths = std::move(state->active_exception_handler_depths);
    resumed_exception_handler_frames = std::move(state->active_exception_handler_frames);
    // Retain the continuation shell during execution. A yield can move the
    // frame vectors back into it, amortizing state allocation across every
    // item produced by generator-heavy Python code.
    generator->vm_state_reuse = state;
    generator->vm_state_reuse_cleanup = generator->vm_state_cleanup;
    generator->vm_state = nullptr;
    generator->vm_state_cleanup = nullptr;
    resumed_generator = true;
    if (generator->has_pending_send && send_target != UINT32_MAX && frame_count > 0) {
      auto& resumed_frame = frames[frame_count - 1];
      if (send_target < resumed_frame.regs.size()) {
        value_assign_fast(resumed_frame.regs[send_target], generator->pending_send);
      }
      value_set_invalid(generator->pending_send);
      generator->has_pending_send = false;
    }
    if (generator->has_pending_throw) {
      value_assign_fast(generator_resume_exception, generator->pending_throw);
      value_set_invalid(generator->pending_throw);
      generator->has_pending_throw = false;
      has_generator_resume_exception = true;
    }
  } else {
    // Native callbacks frequently re-enter the interpreter for short Python
    // helpers (including sys.monitoring callbacks). Keep their initial frame
    // storage small and let genuinely deep Python call chains grow on demand.
    frames.reserve(8);
    if (frames.empty()) {
      frames.emplace_back(
          module, function_id, entry_args, fn_obj_closure, std::move(globals_module), std::move(module_owner), 0, false);
    } else {
      frames[0].reset(
          module, function_id, entry_args, fn_obj_closure, std::move(globals_module), std::move(module_owner), 0, false);
    }
    frames.back().activation_id = runtime_.allocate_frame_activation_id();
    frame_count = 1;
  }
  if (generator != nullptr && frame_count != 0) {
    if (frames[0].coroutine_owner == nullptr) frames[0].coroutine_owner = generator;
    for (size_t index = 0; index < frame_count; ++index) {
      auto* owner = frames[index].coroutine_owner;
      if (owner != nullptr && owner != generator && owner->inline_parent != nullptr)
        owner->running = true;
    }
  }

  auto make_generator_if_needed = [&](FunctionObject* fn_obj, CallArgsView call_args, Value& out, bool& made) -> bool {
    made = false;
    if (fn_obj == nullptr) {
      return true;
    }
    call_args = call_args.with_keyword_defaults(*fn_obj);
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
    if (!simple_signature(call_fn) || has_dynamic_positional_defaults(call_fn, fn_obj->defaults) ||
        call_args.has_keywords() || call_args.has_expansion()) {
      if (!bind_args(call_fn, call_args, fn_obj->defaults, args_for_generator)) {
        return false;
      }
    } else {
      if (call_args.size() != call_fn.params.size()) {
        return bind_count_error(call_fn, call_args);
      }
      args_for_generator.reserve(call_args.size());
      for (size_t i = 0; i < call_args.size(); ++i) {
        args_for_generator.push_back(call_args.get(i));
      }
    }

    // A generator captures the callable that was invoked. Keep that function
    // alive directly instead of cloning its object, closure, defaults, and
    // metadata for every coroutine/generator creation. Arguments have already
    // been bound into args_for_generator, so later __defaults__ changes cannot
    // affect this suspended call.
    Value function_value;
    function_value.tag = ValueTag::Object;
    function_value.as.obj = &fn_obj->header;
    retain(function_value);
    out = Value::generator(
        &runtime_,
        std::move(function_value),
        std::move(args_for_generator),
        call_fn.is_async,
        call_fn.is_coroutine,
        true);
    made = true;
    return true;
  };

  Value deferred_frame_exception;
  size_t active_dispatch_ip = std::numeric_limits<size_t>::max();
  bool allow_call_argument_transfer = false;
  auto push_frame = [&](const ir::Module& call_module,
                        uint32_t call_function_id,
                        CallArgsView call_args,
                        const std::vector<Value>& closure,
                        const std::vector<Value>& defaults,
                        Value call_globals_module,
                        std::shared_ptr<const ir::Module> call_module_owner,
                        uint32_t return_dst,
                        FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue,
                        Value continuation_value = Value::invalid(),
                        bool call_args_are_bound = false,
                        uint32_t leading_argument_register = UINT32_MAX) -> bool {
    // Keep ordinary Python calls on this VM frame stack and return to the same
    // dispatch loop. CPython 3.14's CALL_PY_EXACT_ARGS follows the same shape:
    // transfer positional arguments into a compact interpreter frame, then
    // switch the active frame without recursively entering another evaluator.
    // See doc/performance/cpython314-vm-comparison-20260930.md before changing
    // this path; frame switching is a measured hot cost in pure-Python pickle.
    // Some source-backed operations still use recursive native helper paths
    // while Python frames are active.  Keep a conservative host-stack ceiling
    // in addition to the user-visible recursion limit so recursive logging and
    // traceback inspection raise Python's RecursionError before Windows can
    // raise an access violation.
    constexpr size_t kSafeHostFrameLimit = 1024;
    const size_t effective_recursion_limit = std::min(
        static_cast<size_t>(runtime_.recursion_limit()), kSafeHostFrameLimit);
    if (frame_count + runtime_.saved_python_frame_depth() >= effective_recursion_limit) {
      deferred_frame_exception = runtime_.make_exception(
          "RecursionError", "maximum recursion depth exceeded");
      return false;
    }
    if (call_function_id >= call_module.functions.size()) {
      result.errors.push_back("invalid function id");
      return false;
    }
    const auto& call_fn = call_module.functions[call_function_id];
    GeneratorObject* inherited_coroutine_owner = frame_count == 0
        ? generator : frames[frame_count - 1].coroutine_owner;
    std::vector<Value>* bound_args = nullptr;
    CallArgsView frame_args = call_args;
    if (call_args_are_bound) {
      if (call_args.has_keywords() || call_args.has_expansion() ||
          call_args.size() != call_fn.params.size()) {
        result.errors.push_back("invalid bound coroutine arguments");
        return false;
      }
    } else if (!simple_signature(call_fn) || has_dynamic_positional_defaults(call_fn, defaults) ||
        call_args.has_keywords() || call_args.has_expansion()) {
      // Binding temporaries belong to the active caller frame. Reusing this
      // vector avoids allocating a fresh bound-argument array on each keyword
      // call; frame initialization copies the values before we clear it.
      bound_args = &frames[frame_count - 1].call_binding_scratch;
      if (!bind_args(call_fn, call_args, defaults, *bound_args)) {
        bound_args->clear();
        return false;
      }
      frame_args.leading = bound_args->data();
      frame_args.leading_count = static_cast<uint32_t>(bound_args->size());
      frame_args.registers = nullptr;
      frame_args.register_args = nullptr;
      frame_args.keyword_args = nullptr;
      frame_args.star_arg = UINT32_MAX;
      frame_args.kw_star_arg = UINT32_MAX;
      frame_args.star_args = nullptr;
      frame_args.kw_star_args = nullptr;
    } else if (call_args.size() != call_fn.params.size()) {
      return bind_count_error(call_fn, call_args);
    }

    // An exact positional Python call consumes each input register here. If
    // every source register is dead at this opcode and none is loop-carried,
    // move those owned references into the new frame instead of retaining
    // them in the caller and copying them into callee locals. Exact cached
    // CallMethod calls include `self` as a leading register; its explicit id
    // lets this path transfer receiver and arguments with the same liveness
    // proof. Debug/monitoring frames and unusual argument layouts keep copies.
    std::array<Value, 8> moved_call_args;
    bool moved_call_arguments = false;
    const bool can_transfer_unbound_register_args = !call_args_are_bound &&
        call_args.leading_count == 0;
    const bool can_transfer_bound_method_args = call_args_are_bound &&
        call_args.leading_count == 1 && leading_argument_register != UINT32_MAX;
    const bool can_transfer_constructor_args = !call_args_are_bound &&
        return_mode == FrameReturnMode::StoreConstructedInstance &&
        call_args.leading_count == 1 && call_args.leading != nullptr &&
        leading_argument_register == UINT32_MAX &&
        !call_fn.is_generator && !call_fn.is_async && !call_fn.is_coroutine &&
        frame_count != 0 && frames[frame_count - 1].execution_metadata != nullptr &&
        frames[frame_count - 1].execution_metadata->linear_constructor_argument_liveness &&
        xlang3::xlang_vm::ops::inline_calls_allowed(runtime_);
    if (allow_call_argument_transfer &&
        (can_transfer_unbound_register_args || can_transfer_bound_method_args || can_transfer_constructor_args) &&
        !call_args.has_keywords() && !call_args.has_expansion() &&
        call_args.register_args != nullptr && call_args.registers != nullptr &&
        call_args.size() != 0 &&
        call_args.size() <= moved_call_args.size() &&
        call_args.size() == call_fn.params.size() && simple_signature(call_fn) &&
        !has_dynamic_positional_defaults(call_fn, defaults) && frame_count != 0) {
      auto& caller = frames[frame_count - 1];
      const auto& last_use = caller.execution_metadata->register_last_use;
      const auto& loop_carried = caller.execution_metadata->register_loop_carried;
      bool can_transfer = call_args.registers == caller.regs.value_data() &&
          call_args.register_args->size() + call_args.leading_count == call_args.size() &&
          active_dispatch_ip != std::numeric_limits<size_t>::max();
      std::array<uint32_t, 8> argument_registers{};
      size_t explicit_index = 0;
      uint32_t constructor_callee = UINT32_MAX;
      if (can_transfer_constructor_args) {
        can_transfer = can_transfer && active_dispatch_ip < caller.fn->code.size();
        if (can_transfer) {
          const auto& origin = caller.fn->code[active_dispatch_ip];
          constructor_callee = origin.op == ir::Op::Call ? origin.a : origin.dst;
          explicit_index = 1;
          argument_registers[0] = UINT32_MAX;
        }
      }
      if (can_transfer_bound_method_args) {
        // CallMethod's leading self is known to be a caller register. Keep its
        // register id explicitly instead of reverse-mapping a borrowed Value*
        // through the register buffer. The callsite's exact-arity guard makes
        // this receiver plus the explicit register arguments the full frame.
        argument_registers[0] = leading_argument_register;
        can_transfer = leading_argument_register < caller.regs.size() &&
            leading_argument_register < last_use.size() &&
            last_use[leading_argument_register] == active_dispatch_ip &&
            (leading_argument_register >= loop_carried.size() ||
             !loop_carried[leading_argument_register]);
        explicit_index = 1;
      }
      for (size_t index = explicit_index;
           can_transfer && index < call_args.size(); ++index) {
        const size_t source_index = index - explicit_index;
        const uint32_t reg = (*call_args.register_args)[source_index];
        if (can_transfer_bound_method_args || can_transfer_constructor_args) argument_registers[index] = reg;
        can_transfer = reg < caller.regs.size() && reg < last_use.size() &&
            last_use[reg] == active_dispatch_ip &&
            (reg >= loop_carried.size() || !loop_carried[reg]);
        if (can_transfer_constructor_args && can_transfer) {
          const auto& argument = caller.regs[reg];
          can_transfer = argument.tag != ValueTag::Invalid &&
              (argument.flags & kXlangValueBorrowedRefFlag) == 0 &&
              reg != return_dst && reg != constructor_callee &&
              &argument != call_args.leading &&
              !value_is(argument, call_args.leading[0]) &&
              (constructor_callee >= caller.regs.size() ||
               !value_is(argument, caller.regs[constructor_callee]));
        }
        if (can_transfer_bound_method_args || can_transfer_constructor_args) {
          for (size_t prior = 0; can_transfer && prior < index; ++prior) {
            if (argument_registers[prior] == reg) can_transfer = false;
          }
        } else {
          for (size_t prior = 0; can_transfer && prior < index; ++prior) {
            if ((*call_args.register_args)[prior] == reg) can_transfer = false;
          }
        }
      }
      if (can_transfer) {
        // The fresh constructor self is not a caller register. Own it locally
        // while moving only proven dead explicit arguments into init's frame.
        // This removes otherwise-hidden roots without changing ordinary call
        // transfer, and preserves exceptions/cleanup at frame completion.
        if (can_transfer_constructor_args)
          value_assign_fast(moved_call_args[0], call_args.leading[0]);
        for (size_t index = can_transfer_constructor_args ? 1 : 0; index < call_args.size(); ++index) {
          const uint32_t reg = (can_transfer_bound_method_args || can_transfer_constructor_args)
              ? argument_registers[index]
              : (*call_args.register_args)[index];
          value_move_assign_fast(moved_call_args[index], caller.regs[reg]);
        }
        xlang_perf_count_call_argument_transfer();
        frame_args.leading = moved_call_args.data();
        frame_args.leading_count = static_cast<uint32_t>(call_args.size());
        frame_args.registers = nullptr;
        frame_args.register_args = nullptr;
        moved_call_arguments = true;
      }
    }

    if (frame_count < frames.size()) {
      frames[frame_count].reset(call_module, call_function_id, frame_args, closure, std::move(call_globals_module),
                                std::move(call_module_owner), return_dst, true, return_mode,
                                std::move(continuation_value), moved_call_arguments);
    } else {
      frames.emplace_back(call_module, call_function_id, frame_args, closure, std::move(call_globals_module),
                          std::move(call_module_owner), return_dst, true, return_mode,
                          std::move(continuation_value), moved_call_arguments);
    }
    if (bound_args != nullptr) {
      // emplace_back may relocate the frame vector, so reacquire the caller by
      // index. The bound values now live in the callee's locals.
      frames[frame_count - 1].call_binding_scratch.clear();
    }
    auto& pushed = frames[frame_count];
    pushed.activation_id = runtime_.allocate_frame_activation_id();
    pushed.coroutine_owner = inherited_coroutine_owner;
    ++frame_count;
    ++frame_stack_generation;
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

  auto source_line_for_frame = [](const VMFrame& trace_frame) -> uint32_t {
    if (trace_frame.fn != nullptr && trace_frame.ip < trace_frame.fn->source_lines.size()) {
      return trace_frame.fn->source_lines[trace_frame.ip];
    }
    return 0;
  };

  const VMFrame* published_frames_data = nullptr;
  size_t published_frame_count = 0;
  auto refresh_runtime_frame_views = [&]() {
    // Keep previously published elements alive until this invocation returns.
    // A cross-thread reader can briefly retain an older logical count, so the
    // inline array and overflow vector both remain alive for the whole resume.
    const bool use_overflow = frames.size() > inline_runtime_frame_views.size();
    RuntimeFrameView* views = inline_runtime_frame_views.data();
    if (use_overflow) {
      if (overflow_runtime_frame_views.size() < frames.size()) {
        overflow_runtime_frame_views.resize(frames.size());
      }
      views = overflow_runtime_frame_views.data();
    }
    const bool frame_storage_moved = published_frames_data != frames.data();
    const bool view_storage_moved = published_overflow_frame_views != use_overflow;
    auto update_view = [&](size_t i) {
      auto& view_frame = frames[i];
      GeneratorObject* frame_generator_owner = i == 0 ? generator : nullptr;
      if (i < frame_count && view_frame.inline_coroutine_entry)
        frame_generator_owner = view_frame.coroutine_owner;
      if (i >= frame_count || view_frame.fn == nullptr) {
        views[i] = RuntimeFrameView{
            &view_frame.module_owner,
            &view_frame.globals_module,
            nullptr,
            nullptr,
            &view_frame.ip,
            0,
            view_frame.function_id,
            view_frame.activation_id,
            nullptr,
            nullptr,
            0,
            nullptr,
            view_frame.closure,
            frame_generator_owner,
        };
        return;
      }
      views[i] = RuntimeFrameView{
          &view_frame.module_owner,
          &view_frame.globals_module,
          &view_frame.fn->locals,
          view_frame.locals.value_data(),
          &view_frame.ip,
          view_frame.locals.size(),
          view_frame.function_id,
          view_frame.activation_id,
          view_frame.regs.value_data(),
          &view_frame.execution_metadata->register_last_use,
          view_frame.regs.size(),
          &view_frame.native_call_args,
          view_frame.closure,
          frame_generator_owner,
          view_frame.cells.value_data(),
          view_frame.cells.size(),
          &view_frame.captured_builtins,
      };
    };
    if (frame_storage_moved || view_storage_moved) {
      for (size_t i = 0; i < frames.size(); ++i) update_view(i);
      published_frames_data = frames.data();
    } else if (frame_count > published_frame_count && frame_count != 0) {
      // A push either initializes a new slot or resets an inactive one. Lower
      // frame views retain pointers into unchanged frame-owned storage.
      update_view(frame_count - 1);
    }
    runtime_.set_current_frame_stack(views, frame_count);
    published_overflow_frame_views = use_overflow;
    published_frame_count = frame_count;
    published_frame_stack_generation = frame_stack_generation;
  };
  uint64_t inspection_publish_generation = static_cast<uint64_t>(-1);
  uint64_t suspension_count = 0;
  XlangRuntimeSuspensionCallbackGuard suspension_callback([&]() {
    // A blocking operation exposes this stack to sys._current_frames().
    // Publish immediately after a frame push/pop, and periodically refresh the
    // instruction within a long-running frame. The runtime copies only stable
    // frame metadata, so the snapshot remains safe after this VM resumes
    // without retaining arbitrary local objects.
    if (inspection_publish_generation != frame_stack_generation ||
        ((++suspension_count & 0xfffu) == 0)) {
      refresh_runtime_frame_views();
      runtime_.publish_current_frame_for_thread_inspection();
      inspection_publish_generation = frame_stack_generation;
    }
  });

  auto emit_trace_event = [&](VMFrame& trace_frame, const char* event_name, const Value& arg) -> bool {
    const bool is_call_event = std::string_view(event_name) == std::string_view("call");
    const Value& hook = is_call_event ? runtime_.trace_function() : trace_frame.trace_function;
    if (hook.tag == ValueTag::Invalid || hook.tag == ValueTag::None || runtime_.trace_dispatch_active()) {
      return true;
    }
    runtime_.set_current_frame(
        &trace_frame.module_owner,
        trace_frame.function_id,
        &trace_frame.globals_module,
        static_cast<uint32_t>(trace_frame.ip));
    runtime_.set_current_globals_module(trace_frame.globals_module);
    runtime_.set_current_frame_locals(&trace_frame.fn->locals, trace_frame.locals.value_data(), trace_frame.locals.size());

    auto trace_locals = [&]() {
      std::vector<std::pair<Value, Value>> local_entries;
      local_entries.reserve(trace_frame.fn->locals.size());
      for (size_t local_index = 0;
           local_index < trace_frame.fn->locals.size() && local_index < trace_frame.locals.size();
           ++local_index) {
        const auto& local_name = trace_frame.fn->locals[local_index];
        if (local_name.empty() || local_name[0] == '#') continue;
        const Value* local_value = &trace_frame.locals[local_index];
        for (size_t cell_index = 0;
             cell_index < trace_frame.fn->cell_slots.size() && cell_index < trace_frame.cells.size();
             ++cell_index) {
          if (trace_frame.fn->cell_slots[cell_index] == local_index) {
            if (auto* cell = value_as_cell(trace_frame.cells[cell_index])) local_value = &cell->value;
            break;
          }
        }
        if (local_value->tag != ValueTag::Invalid) {
          local_entries.push_back({Value::string(local_name), *local_value});
        }
      }
      return Value::dict(std::move(local_entries));
    };

    Value visible_frame_value;
    auto* cached_frame = value_as_frame(trace_frame.trace_frame_object);
    if (cached_frame != nullptr && cached_frame->module.get() == trace_frame.module_owner.get() &&
        cached_frame->function_id == trace_frame.function_id) {
      cached_frame->instruction_index = static_cast<uint32_t>(trace_frame.ip);
      Value refreshed_locals = trace_locals();
      value_move_assign_fast(cached_frame->locals, refreshed_locals);
      value_assign_fast(visible_frame_value, trace_frame.trace_frame_object);
    } else {
      Value back = Value::none();
      for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        if (&frames[frame_index] != &trace_frame) continue;
        if (frame_index != 0 && value_as_frame(frames[frame_index - 1].trace_frame_object) != nullptr) {
          value_assign_fast(back, frames[frame_index - 1].trace_frame_object);
        }
        break;
      }
      visible_frame_value = Value::frame(
          trace_frame.module_owner,
          trace_frame.function_id,
          trace_frame.globals_module,
          static_cast<uint32_t>(trace_frame.ip),
          trace_locals(),
          std::move(back),
          Value::dict({}),
          trace_frame.activation_id);
      value_assign_fast(trace_frame.trace_frame_object, visible_frame_value);
    }
    Value trace_args_storage[3] = {
        visible_frame_value, Value::string(event_name), arg,
    };
    CallArgsView trace_args;
    trace_args.leading = trace_args_storage;
    trace_args.leading_count = 3;

    auto* visible_frame = value_as_frame(trace_args_storage[0]);
    if (visible_frame != nullptr) {
      visible_frame->allow_line_jump = true;
      visible_frame->source_line_is_current = true;
    }
    runtime_.set_trace_dispatch_active(true);
    struct TraceDispatchGuard {
      Runtime& runtime;
      ~TraceDispatchGuard() { runtime.set_trace_dispatch_active(false); }
    } trace_guard{runtime_};
    Value trace_result;
    std::string trace_error;
    const bool trace_ok = runtime_call_callable(
        runtime_, hook, trace_args.leading, trace_args.leading_count, trace_result, trace_error);
    if (visible_frame != nullptr) {
      visible_frame->allow_line_jump = false;
      visible_frame->source_line_is_current = false;
    }
    if (!trace_ok) {
      result.errors.push_back(trace_error.empty() ? "trace callback failed" : trace_error);
      return false;
    }
    if (visible_frame != nullptr) {
      trace_frame.trace_lines = visible_frame->trace_lines;
      trace_frame.trace_opcodes = visible_frame->trace_opcodes;
      trace_frame.ip = visible_frame->instruction_index;
    }
    if (trace_result.tag == ValueTag::None || trace_result.tag == ValueTag::Invalid) {
      value_set_invalid(trace_frame.trace_function);
    } else {
      value_assign_fast(trace_frame.trace_function, trace_result);
    }
    return true;
  };

  auto emit_profile_event = [&](VMFrame& profile_frame, const char* event_name, const Value& arg) -> bool {
    const Value& borrowed_hook = runtime_.profile_function();
    if (borrowed_hook.tag == ValueTag::Invalid || borrowed_hook.tag == ValueTag::None || runtime_.profile_dispatch_active()) {
      return true;
    }
    // Pin before frame snapshots/argument allocation can reenter. A hook may
    // replace or disable itself; its executing callable still owns this call.
    Value hook;
    value_assign_fast(hook, borrowed_hook);
    runtime_.set_current_frame(
        &profile_frame.module_owner,
        profile_frame.function_id,
        &profile_frame.globals_module,
        static_cast<uint32_t>(profile_frame.ip));
    runtime_.set_current_globals_module(profile_frame.globals_module);
    runtime_.set_current_frame_locals(&profile_frame.fn->locals, profile_frame.locals.value_data(), profile_frame.locals.size());

    Value profile_args_storage[3] = {
        runtime_.current_frame_snapshot(),
        Value::string(event_name),
        arg,
    };
    if (auto* current = value_as_frame(profile_args_storage[0])) {
      current->source_line_is_current = true;
    }

    runtime_.set_profile_dispatch_active(true);
    struct ProfileDispatchGuard {
      Runtime& runtime;
      ~ProfileDispatchGuard() { runtime.set_profile_dispatch_active(false); }
    } profile_guard{runtime_};
    Value ignored;
    std::string profile_error;
    const bool profile_ok = runtime_call_callable(runtime_, hook, profile_args_storage, 3, ignored, profile_error);
    if (auto* current = value_as_frame(profile_args_storage[0])) {
      current->source_line_is_current = false;
    }
    if (!profile_ok) {
      result.errors.push_back(profile_error.empty() ? "profile callback failed" : profile_error);
      return false;
    }
    return true;
  };

  auto emit_debug_event = [&](VMFrame& debug_frame, const char* event_name) -> bool {
    auto* hook_fn = value_as_function(runtime_.debug_hook());
    if (hook_fn == nullptr || runtime_.debug_dispatch_active()) {
      return true;
    }
    runtime_.set_current_frame(
        &debug_frame.module_owner,
        debug_frame.function_id,
        &debug_frame.globals_module,
        static_cast<uint32_t>(debug_frame.ip));
    runtime_.set_current_globals_module(debug_frame.globals_module);
    runtime_.set_current_frame_locals(
        &debug_frame.fn->locals,
        debug_frame.locals.value_data(),
        debug_frame.locals.size());

    Value debug_args_storage[2] = {
        runtime_.current_frame_snapshot(),
        Value::string(event_name),
    };
    if (auto* current = value_as_frame(debug_args_storage[0])) {
      current->source_line_is_current = true;
    }
    CallArgsView debug_args;
    debug_args.leading = debug_args_storage;
    debug_args.leading_count = 2;

    runtime_.set_debug_dispatch_active(true);
    Interpreter debug_interpreter(runtime_);
    RuntimeResult debug_result = debug_interpreter.run_function_value(hook_fn, debug_args);
    if (auto* current = value_as_frame(debug_args_storage[0])) {
      current->source_line_is_current = false;
    }
    runtime_.set_debug_dispatch_active(false);
    if (!debug_result.errors.empty()) {
      result.errors.insert(result.errors.end(), debug_result.errors.begin(), debug_result.errors.end());
      return false;
    }
    return true;
  };

  auto refresh_monitoring_configuration = [&](VMFrame& monitoring_frame) {
    const uint64_t generation = sys_monitoring_configuration_generation();
    if (monitoring_frame.monitoring_configuration_generation == generation) return;
    monitoring_frame.monitoring_configuration_generation = generation;
    monitoring_frame.monitoring_events =
        sys_monitoring_code_events(monitoring_frame.module, monitoring_frame.function_id);
  };

  auto emit_monitoring_event = [&](VMFrame& monitoring_frame, int64_t event, const Value* arg) -> bool {
    // Branch op handlers reach this helper from the hot dispatch loop even
    // when monitoring is off. Keep this frame-local mask check first so the
    // disabled path avoids instruction-cache and runtime-frame work.
    if ((monitoring_frame.monitoring_events & event) == 0) {
      return true;
    }
    const int64_t monitoring_location = event == kSysMonitoringEventLine
        ? static_cast<int64_t>(source_line_for_frame(monitoring_frame))
        : static_cast<int64_t>(monitoring_frame.ip);
    XlangVMMonitoringSiteState* monitoring_cache = monitoring_frame.ip < monitoring_frame.instr_cache.size()
        ? &monitoring_frame.instr_cache.monitoring_at(monitoring_frame.ip)
        : nullptr;
    if (monitoring_cache != nullptr) monitoring_frame.monitoring_cache_touched = true;
    const uint64_t monitoring_generation = sys_monitoring_configuration_generation();
    if (monitoring_cache != nullptr) {
      if (monitoring_cache->generation != monitoring_generation) {
        monitoring_cache->generation = monitoring_generation;
        monitoring_cache->disabled_events = 0;
      } else if ((monitoring_cache->disabled_events & event) != 0) {
        return true;
      }
    }
    if (!sys_monitoring_location_may_dispatch(
            monitoring_frame.module,
            monitoring_frame.function_id,
            event,
            monitoring_location)) {
      if (monitoring_cache != nullptr) monitoring_cache->disabled_events |= event;
      return true;
    }

    runtime_.set_current_frame(
        &monitoring_frame.module_owner,
        monitoring_frame.function_id,
        &monitoring_frame.globals_module,
        static_cast<uint32_t>(monitoring_frame.ip));
    runtime_.set_current_globals_module(monitoring_frame.globals_module);
    runtime_.set_current_frame_locals(
        &monitoring_frame.fn->locals,
        monitoring_frame.locals.value_data(),
        monitoring_frame.locals.size());

    if (monitoring_frame.monitoring_code.tag == ValueTag::Invalid) {
      monitoring_frame.monitoring_code = monitoring_frame.module_owner != nullptr
          ? runtime_.code_object(monitoring_frame.module_owner, monitoring_frame.function_id)
          : Value::none();
    }
    const Value& code = monitoring_frame.monitoring_code;

    std::string monitoring_error;
    if (!sys_monitoring_dispatch_event(
            runtime_,
            event,
            code,
            monitoring_location,
            arg,
            monitoring_error)) {
      result.errors.push_back(monitoring_error);
      return false;
    }
    const uint64_t current_monitoring_generation = sys_monitoring_configuration_generation();
    if (monitoring_generation != current_monitoring_generation) {
      monitoring_frame.monitoring_configuration_generation = current_monitoring_generation;
      monitoring_frame.monitoring_events =
          sys_monitoring_code_events(monitoring_frame.module, monitoring_frame.function_id);
    } else if (monitoring_cache != nullptr &&
               !sys_monitoring_location_may_dispatch(
                   monitoring_frame.module,
                   monitoring_frame.function_id,
                   event,
                   monitoring_location)) {
        monitoring_cache->disabled_events |= event;
        if (event == kSysMonitoringEventPyStart) {
          monitoring_frame.monitoring_events &= ~event;
        }
      }
    return true;
  };

  auto hook_is_active = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };

  auto pause_debug_execution = [&](RuntimePauseReason reason, uint32_t source_line) -> bool {
    refresh_runtime_frame_views();
    auto& paused_frame = frames[frame_count - 1];
    result.paused = true;
    result.pause_reason = reason;
    result.pause_line = source_line;
    result.selected_frame = static_cast<uint32_t>(frame_count - 1);
    result.pause_file = paused_frame.module != nullptr ? paused_frame.module->source_file : std::string();
    result.pause_frame = runtime_.current_frame_snapshot();
    if (auto* current = value_as_frame(result.pause_frame)) {
      current->source_line_is_current = true;
    }

    auto state = std::make_shared<RuntimeDebugPauseState>();
    state->reason = reason;
    state->frame_count = frame_count;
    state->frames = std::move(frames);
    result.pause_state = std::move(state);
    return false;
  };

  auto poll_debug_event = [&](VMFrame& debug_frame) -> bool {
    const uint32_t source_line = source_line_for_frame(debug_frame);
    if (source_line == 0) {
      return true;
    }
    if (source_line == debug_frame.last_debug_line) {
      return true;
    }
    debug_frame.last_debug_line = source_line;
    const std::string_view source_file =
        debug_frame.module != nullptr ? std::string_view(debug_frame.module->source_file) : std::string_view();
    if (!runtime_.debug_skip_breakpoint_at_step_origin(frame_count, source_line) &&
        runtime_.debug_breakpoint_matches(source_file, source_line)) {
      if (!emit_debug_event(debug_frame, "breakpoint")) {
        return false;
      }
      return runtime_.debug_pause_on_hit() ? pause_debug_execution(RuntimePauseReason::Breakpoint, source_line) : true;
    }
    const RuntimePauseReason step_reason = runtime_.debug_step_pause_reason(frame_count, source_line);
    if (step_reason != RuntimePauseReason::None) {
      if (!emit_debug_event(debug_frame, "step")) {
        return false;
      }
      return runtime_.debug_pause_on_hit() ? pause_debug_execution(step_reason, source_line) : true;
    }
    return true;
  };

  Value current_exception;
  std::vector<Value> previous_exceptions;
  std::vector<size_t> active_exception_handler_depths;
  std::vector<size_t> active_exception_handler_frames;
  if (resumed_generator) {
    value_assign_fast(current_exception, resumed_current_exception);
    previous_exceptions = std::move(resumed_previous_exceptions);
    active_exception_handler_depths = std::move(resumed_exception_handler_depths);
    active_exception_handler_frames = std::move(resumed_exception_handler_frames);
    if (current_exception.tag != ValueTag::Invalid) {
      runtime_.set_active_exception(current_exception);
    }
  } else if (generator == nullptr && !resuming_pause &&
             XLANG3_UNLIKELY(active_exception_guard.previous.tag != ValueTag::Invalid &&
                            active_exception_guard.previous.tag != ValueTag::None)) {
    // Normal embedding/native callbacks inherit the caller's handled state.
    // Seed once from the guard's existing owner so an inner except records and
    // restores that state after ClearException, rather than losing it until
    // the final entry guard runs. No additional work belongs in opcode paths;
    // generator and paused entries retain their existing restoration policy.
    value_assign_fast(current_exception, active_exception_guard.previous);
  }
  auto save_generator_exception_context = [&]() {
    if (generator == nullptr || generator->vm_state == nullptr) return;
    auto* state = static_cast<GeneratorVMState*>(generator->vm_state);
    value_assign_fast(state->current_exception, current_exception);
    state->previous_exceptions = std::move(previous_exceptions);
    state->active_exception_handler_depths = std::move(active_exception_handler_depths);
    state->active_exception_handler_frames = std::move(active_exception_handler_frames);
  };
  Value pending_exception_cause;
  bool pending_exception_explicit_cause = false;
  Value traceback_builtins;

  auto restore_active_exception_context = [&]() {
    if (!previous_exceptions.empty()) {
      value_assign_fast(current_exception, previous_exceptions.back());
      previous_exceptions.pop_back();
      active_exception_handler_depths.pop_back();
      active_exception_handler_frames.pop_back();
    } else {
      value_set_invalid(current_exception);
    }
    if (current_exception.tag == ValueTag::Invalid) {
      runtime_.clear_active_exception();
    } else {
      runtime_.set_active_exception(current_exception);
    }
  };

  auto complete_inlined_coroutine = [&](VMFrame& finished) {
    if (!finished.inline_coroutine_entry || finished.coroutine_owner == nullptr) return;
    GeneratorObject* coroutine = finished.coroutine_owner;
    GeneratorObject* parent = finished.inline_coroutine_parent;
    coroutine->running = false;
    coroutine->done = true;
    coroutine->has_pending_send = false;
    coroutine->has_pending_throw = false;
    coroutine->delegated_result_ready = false;
    coroutine->has_active_suspended_exception_handlers = false;
    value_set_invalid(coroutine->pending_send);
    value_set_invalid(coroutine->pending_throw);
    value_set_invalid(coroutine->awaiting);
    value_set_none(coroutine->return_value);
    coroutine->args.clear();
    coroutine->inline_parent = nullptr;
    if (parent != nullptr && parent->awaiting.tag == ValueTag::Object &&
        parent->awaiting.as.obj == &coroutine->header) {
      value_set_invalid(parent->awaiting);
    }
  };

  auto make_traceback_from_frames = [&](bool track_live_frames = false,
                                        size_t lowest_frame = 0) -> Value {
    if (traceback_builtins.tag == ValueTag::Invalid) {
      traceback_builtins = Value::dict({});
      Value builtins_module;
      std::string builtins_error;
      if (runtime_.import_module("builtins", builtins_module, builtins_error) &&
          value_as_module(builtins_module) != nullptr) {
        traceback_builtins = module_namespace_dict(builtins_module);
      }
    }
    Value next = Value::none();
    Value inner_frame = Value::none();
    for (size_t index = frame_count; index > lowest_frame; --index) {
      const auto& captured = frames[index - 1];
      // A caller is suspended after its call instruction, while the active
      // frame still points at the instruction that raised.  Tracebacks report
      // the call site for suspended callers.
      const uint32_t traceback_ip = index < frame_count && captured.ip > 0
          ? captured.ip - 1
          : captured.ip;
      Value frame_object = Value::frame(
          captured.module_owner,
          captured.function_id,
          captured.globals_module,
          traceback_ip,
          Value::invalid(),
          Value::invalid(),
          traceback_builtins,
          captured.activation_id);
      if (auto* traceback_frame = value_as_frame(frame_object)) {
        // The activation is still represented by the VM frame and the live
        // frame registry. Copy locals only when f_locals is requested or when
        // the activation retires, rather than for every caught exception.
        traceback_frame->has_lazy_locals = true;
      }
      if (track_live_frames) {
        frame_object = runtime_.track_live_frame_snapshot(std::move(frame_object));
      }
      if (auto* inner = value_as_frame(inner_frame)) {
        value_assign_fast(inner->back, frame_object);
      }
      value_assign_fast(inner_frame, frame_object);
      int64_t source_line = static_cast<int64_t>(traceback_ip);
      if (captured.fn != nullptr && traceback_ip < captured.fn->source_lines.size() &&
          captured.fn->source_lines[traceback_ip] != 0) {
        source_line = static_cast<int64_t>(captured.fn->source_lines[traceback_ip]);
      }
      next = Value::traceback(
          std::move(frame_object), std::move(next), source_line,
          static_cast<int64_t>((traceback_ip + 1) * 2));
    }
    return next;
  };

  auto normalize_exception = [&](const Value& value) -> Value {
    if (auto* klass = value_as_class(value)) {
      const Value* base_value = runtime_.find_builtin("BaseException");
      auto* base = base_value == nullptr ? nullptr : value_as_class(*base_value);
      if (base == nullptr || !class_is_subclass(klass, base)) {
        return runtime_.make_exception("TypeError", "exceptions must derive from BaseException");
      }
      Value instance;
      std::string init_error;
      if (runtime_call_callable(runtime_, value, nullptr, 0, instance, init_error)) {
        return instance;
      }
      Value pending;
      if (runtime_.take_pending_exception(pending)) return pending;
      return runtime_.make_exception("RuntimeError", init_error);
    }
    if (auto* instance = value_as_instance(value)) {
      auto* klass = value_as_class(instance->klass);
      const Value* base_value = runtime_.find_builtin("BaseException");
      auto* base = base_value == nullptr ? nullptr : value_as_class(*base_value);
      if (klass != nullptr && base != nullptr && class_is_subclass(klass, base)) {
        return value;
      }
    }
    return runtime_.make_exception("TypeError", "exceptions must derive from BaseException");
  };

  auto dispatch_exception = [&](Value exception,
                                bool preserve_reraised_traceback = false,
                                bool deduplicate_bare_reraise_frame = false) -> bool {
    runtime_.refresh_live_frame_snapshots();
    struct ClassNewUnwindOwners {
      Runtime& runtime;
      std::vector<VMFrame>& frames;
      size_t& count;
      decltype(refresh_runtime_frame_views)& refresh;
      const Value& previous_globals;
      std::vector<Value> owners;

      ~ClassNewUnwindOwners() noexcept(false) {
        if (owners.empty()) return;
        // Selected constructor callables/arguments must outlive frame cleanup
        // and exception transport. Repair the active inspection views before
        // these owners retire, including an early observer failure or unwind.
        refresh();
        if (count == 0) {
          runtime.clear_current_frame();
          runtime.set_current_globals_module(previous_globals);
        } else {
          auto& active = frames[count - 1];
          runtime.set_current_globals_module(active.globals_module);
          runtime.set_current_frame_locals(
              &active.fn->locals, active.locals.value_data(), active.locals.size());
          runtime.set_current_frame(
              &active.module_owner, active.function_id, &active.globals_module, active.ip);
        }
      }
    } class_new_unwind_owners{
        runtime_, frames, frame_count, refresh_runtime_frame_views,
        current_globals_guard.previous, {}};
    Value previous_exception;
    value_assign_fast(previous_exception, current_exception);
    if (value_as_instance(exception) != nullptr) {
      std::string ignored;
      if (current_exception.tag != ValueTag::Invalid &&
          !value_is(exception, current_exception)) {
        Value existing_context;
        if (!object_get_attr(exception, "__context__", existing_context, ignored) ||
            existing_context.tag == ValueTag::None ||
            existing_context.tag == ValueTag::Invalid) {
          object_set_attr(exception, "__context__", current_exception, ignored);
        }
      }
      // Match CPython's propagation model: a caught exception records the
      // raising frame and each frame it crosses through the nearest handler.
      // Callers above that handler are not part of this traceback.
      size_t lowest_traceback_frame = 0;
      for (size_t index = frame_count; index > 0; --index) {
        if (!frames[index - 1].exception_handlers.empty()) {
          lowest_traceback_frame = index - 1;
          break;
        }
      }
      Value traceback = make_traceback_from_frames(true, lowest_traceback_frame);
      if (preserve_reraised_traceback) {
        Value existing_traceback;
        if (object_get_attr(exception, "__traceback__", existing_traceback, ignored) &&
            value_as_traceback(existing_traceback) != nullptr) {
          auto* cursor = value_as_traceback(traceback);
          TracebackObject* parent = nullptr;
          while (cursor != nullptr && value_as_traceback(cursor->next) != nullptr) {
            parent = cursor;
            cursor = value_as_traceback(cursor->next);
          }
          if (cursor == nullptr) {
            value_assign_fast(traceback, existing_traceback);
          } else {
            bool same_frame = false;
            if (deduplicate_bare_reraise_frame) {
              auto* current_frame = value_as_frame(cursor->frame);
              auto* existing = value_as_traceback(existing_traceback);
              auto* existing_frame = existing == nullptr ? nullptr : value_as_frame(existing->frame);
              same_frame = current_frame != nullptr && existing_frame != nullptr &&
                           current_frame->activation_id == existing_frame->activation_id;
            }
            if (same_frame) {
              if (parent == nullptr) {
                value_assign_fast(traceback, existing_traceback);
              } else {
                value_assign_fast(parent->next, existing_traceback);
              }
            } else {
              value_assign_fast(cursor->next, existing_traceback);
            }
          }
        }
      }
      object_set_attr(exception, "__traceback__", traceback, ignored);
    }
    if (frame_count != 0) {
      if (!emit_monitoring_event(frames[frame_count - 1], kSysMonitoringEventRaise, &exception)) {
        return false;
      }
    }
    if (frame_count != 0 &&
        frames[frame_count - 1].trace_function.tag != ValueTag::Invalid &&
        frames[frame_count - 1].trace_function.tag != ValueTag::None &&
        !runtime_.trace_dispatch_active()) {
      Value exception_type = runtime_.exception_type(exception);
      Value traceback = Value::none();
      std::string ignored;
      (void)object_get_attr(exception, "__traceback__", traceback, ignored);
      Value event_arg = Value::tuple({exception_type, exception, traceback});
      if (!emit_trace_event(frames[frame_count - 1], "exception", event_arg)) {
        return false;
      }
    }
    value_assign_fast(current_exception, exception);
    runtime_.set_active_exception(current_exception);
    std::string failing_function;
    std::string failing_location;
    if (frame_count != 0 && frames[frame_count - 1].fn != nullptr) {
      failing_function = frames[frame_count - 1].fn->name;
      if (frames[frame_count - 1].module != nullptr) {
        failing_location = frames[frame_count - 1].module->source_file + ":" +
                           std::to_string(frames[frame_count - 1].fn->first_line);
      }
    }
    while (frame_count != 0) {
      auto& handlers = frames[frame_count - 1].exception_handlers;
      if (!handlers.empty()) {
        const size_t handler_depth_before_pop = handlers.size();
        while (!active_exception_handler_depths.empty() &&
               active_exception_handler_frames.back() == frame_count &&
               handler_depth_before_pop <= active_exception_handler_depths.back()) {
          value_assign_fast(previous_exception, previous_exceptions.back());
          previous_exceptions.pop_back();
          active_exception_handler_depths.pop_back();
          active_exception_handler_frames.pop_back();
        }
        const auto handler = handlers.back();
        handlers.pop_back();
        if (!emit_monitoring_event(
                frames[frame_count - 1],
                kSysMonitoringEventExceptionHandled,
                &current_exception)) {
          return false;
        }
        previous_exceptions.push_back(previous_exception);
        active_exception_handler_depths.push_back(handlers.size());
        active_exception_handler_frames.push_back(frame_count);
        frames[frame_count - 1].ip = handler.ip;
        return true;
      }
      if (!emit_monitoring_event(frames[frame_count - 1], kSysMonitoringEventPyUnwind, &current_exception)) {
        return false;
      }
      while (!active_exception_handler_frames.empty() && active_exception_handler_frames.back() == frame_count) {
        value_assign_fast(previous_exception, previous_exceptions.back());
        previous_exceptions.pop_back();
        active_exception_handler_depths.pop_back();
        active_exception_handler_frames.pop_back();
      }
      runtime_.retire_live_frame_snapshot(
          frames[frame_count - 1].activation_id,
          static_cast<uint32_t>(frames[frame_count - 1].ip),
          frames[frame_count - 1].locals.value_data(),
          frames[frame_count - 1].locals.size());
      if (frames[frame_count - 1].return_mode == FrameReturnMode::ContinuePythonClassNew ||
          frames[frame_count - 1].return_mode == FrameReturnMode::StorePythonClassNewInstance) {
        class_new_unwind_owners.owners.push_back(frames[frame_count - 1].continuation_value);
      }
      complete_inlined_coroutine(frames[frame_count - 1]);
      frames[frame_count - 1].clear_for_pop();
      --frame_count;
      ++frame_stack_generation;
    }
    const std::string exception_text = value_to_string(current_exception);
    const std::string exception_type_text = value_to_string(runtime_.exception_type(current_exception));
    const std::string exception_summary =
        exception_text.empty() ? exception_type_text : exception_type_text + ": " + exception_text;
    value_assign_fast(result.exception, current_exception);
    runtime_.set_pending_exception(current_exception);
    std::string frame_summary = failing_function;
    if (!failing_location.empty()) {
      frame_summary += " (" + failing_location + ")";
    }
    result.errors.push_back(
        frame_summary.empty()
            ? "uncaught exception: " + exception_summary
            : "uncaught exception in " + frame_summary + ": " + exception_summary);
    return false;
  };

  auto finish_frame = [&](const Value& return_value) -> bool {
    Value owned_return_value;
    value_assign_fast(owned_return_value, return_value);
    VMFrame& finished = frames[frame_count - 1];
    runtime_.retire_live_frame_snapshot(
        finished.activation_id, static_cast<uint32_t>(finished.ip),
        finished.locals.value_data(), finished.locals.size());
    if ((finished.monitoring_events & kSysMonitoringEventPyReturn) != 0) {
      if (!emit_monitoring_event(finished, kSysMonitoringEventPyReturn, &owned_return_value)) {
        return false;
      }
    }
    if (hook_is_active(finished.trace_function)) {
      if (!emit_trace_event(finished, "return", owned_return_value)) {
        return false;
      }
    }
    if (runtime_.profile_event_may_dispatch() &&
        hook_is_active(runtime_.profile_function()) && !runtime_.profile_dispatch_active()) {
      if (!emit_profile_event(finished, "return", owned_return_value)) {
        return false;
      }
    }
    const uint32_t return_dst = finished.return_dst;
    const bool has_caller = finished.has_caller;
    const FrameReturnMode return_mode = finished.return_mode;
    Value continuation_value;
    if (return_mode == FrameReturnMode::StoreConstructedInstance ||
        return_mode == FrameReturnMode::ContinuePythonClassNew ||
        return_mode == FrameReturnMode::StorePythonClassNewInstance) {
      value_assign_fast(continuation_value, finished.continuation_value);
    }
    while (!active_exception_handler_frames.empty() && active_exception_handler_frames.back() == frame_count) {
      restore_active_exception_context();
    }
    complete_inlined_coroutine(finished);
    if (!has_caller) {
      value_assign_fast(result.value, owned_return_value);
      finished.clear_for_pop();
      --frame_count;
      ++frame_stack_generation;
      return false;
    }
    finished.clear_for_pop();
    --frame_count;
    ++frame_stack_generation;
    // Subscription assignment invokes a normal Python frame but ignores its
    // result. Never overwrite the container register with __setitem__'s return.
    if (return_mode == FrameReturnMode::DiscardReturnValue) {
      auto& caller = frames[frame_count - 1];
      // The pushed frame skips the opcode loop's normal post-instruction
      // retirement. Release consumed temporary exports after the setter has
      // returned, before the caller attempts a resize in its next statement.
      if (caller.ip != 0) caller.release_memoryviews_last_used_at(caller.ip - 1);
      return true;
    }
    if (return_mode == FrameReturnMode::ContinuePythonClassNew) {
      const auto* context = value_as_tuple(continuation_value);
      if (context == nullptr || context->items.size() < 2) {
        return dispatch_exception(runtime_.make_exception("RuntimeError", "invalid __new__ continuation"));
      }
      const size_t caller_index = frame_count - 1;
      // call_user_function advanced the suspended caller. This continuation is
      // still executing that Call: expose its callsite during fresh lookup and
      // synchronous callbacks, and advance only on success or an init push.
      if (frames[caller_index].ip != 0) --frames[caller_index].ip;
      refresh_runtime_frame_views();
      {
        auto& caller = frames[caller_index];
        runtime_.set_current_globals_module(caller.globals_module);
        runtime_.set_current_frame_locals(
            &caller.fn->locals, caller.locals.value_data(), caller.locals.size());
        runtime_.set_current_frame(
            &caller.module_owner, caller.function_id, &caller.globals_module, caller.ip);
      }
      auto raise_constructor_error = [&](const std::string& message) -> bool {
        Value pending;
        if (!runtime_.take_pending_exception(pending))
          pending = runtime_.make_exception("RuntimeError", message);
        return dispatch_exception(std::move(pending), true);
      };
      // Frame-local cleanup and __new__ return observers have completed. Do
      // not carry an initializer/MRO proof across them: use the current result
      // type and resolve __init__ afresh, just as the synchronous helper does.
      auto* called_class = value_as_class(context->items[1]);
      auto* instance = value_as_instance(owned_return_value);
      auto* module_instance = value_as_module(owned_return_value);
      auto* result_class = instance != nullptr ? value_as_class(instance->klass)
          : module_instance != nullptr ? value_as_class(module_instance->klass) : nullptr;
      if (result_class != nullptr && called_class != nullptr &&
          class_is_subclass(result_class, called_class)) {
        Value init;
        std::string error;
        const bool found = module_instance != nullptr
            ? module_get_attr(owned_return_value, "__init__", init, error)
            : object_get_attr(owned_return_value, "__init__", init, error);
        if (found && init.tag != ValueTag::Invalid &&
            !xlang_vm::ops::xlang_vm_is_default_object_hook(init, "object.__init__")) {
          auto* bound = value_as_bound_method(init);
          auto* function = bound == nullptr ? nullptr : value_as_function(bound->function);
          if (bound != nullptr && value_is(bound->self, owned_return_value) &&
              xlang_vm::ops::xlang_vm_plain_sync_function(function)) {
            std::vector<Value> init_args;
            init_args.reserve(context->items.size() - 1);
            init_args.push_back(bound->self);
            for (size_t i = 2; i < context->items.size(); ++i)
              init_args.push_back(context->items[i]);
            CallArgsView view;
            view.leading = init_args.data();
            view.leading_count = static_cast<uint32_t>(init_args.size());
            // Keep both selected functions and original inputs alive through
            // init's body, frame cleanup, and replacement of the old output.
            Value completion = Value::tuple({owned_return_value, continuation_value, init});
            auto init_module_owner = function->module;
            if (!push_frame(
                    *init_module_owner, function->function_id,
                    view.with_keyword_defaults(*function), function->closure,
                    function->defaults, function->globals_module, init_module_owner,
                    return_dst, FrameReturnMode::StorePythonClassNewInstance,
                    std::move(completion))) {
              return result.errors.empty() && frame_count != 0;
            }
            // push_frame can relocate frame storage. No borrowed caller or
            // selected-function fields are read after it has taken ownership.
            ++frames[caller_index].ip;
            return true;
          }
          Value ignored;
          const std::vector<std::pair<std::string, Value>> no_keywords;
          if (!runtime_call_callable_kw(
                  runtime_, init, context->items.begin() + 2,
                  static_cast<uint32_t>(context->items.size() - 2),
                  no_keywords, ignored, error)) {
            return raise_constructor_error(error.empty() ? "__init__ failed" : error);
          }
        }
      }
      value_assign_fast(frames[caller_index].regs[return_dst], owned_return_value);
      ++frames[caller_index].ip;
      return true;
    }
    if (return_mode == FrameReturnMode::StorePythonClassNewInstance) {
      // init is retired. Both old destination cleanup and the last context
      // owners can reenter Python, so expose the live caller before either.
      refresh_runtime_frame_views();
      {
        auto& caller = frames[frame_count - 1];
        runtime_.set_current_globals_module(caller.globals_module);
        runtime_.set_current_frame_locals(
            &caller.fn->locals, caller.locals.value_data(), caller.locals.size());
        runtime_.set_current_frame(
            &caller.module_owner, caller.function_id, &caller.globals_module, caller.ip);
      }
      const auto* completion = value_as_tuple(continuation_value);
      if (completion == nullptr || completion->items.size() != 3) {
        return dispatch_exception(runtime_.make_exception("RuntimeError", "invalid __init__ continuation"));
      }
      // The current custom-new helper ignores __init__'s return value. Keep
      // that policy and its selected-owner lifetime; no callable-policy change.
      value_assign_fast(frames[frame_count - 1].regs[return_dst], completion->items[0]);
      return true;
    }
    Value& target = frames[frame_count - 1].regs[return_dst];
    if (return_mode == FrameReturnMode::StoreConstructedInstance) {
      value_assign_fast(target, continuation_value);
    } else if (return_mode == FrameReturnMode::StoreBoolean ||
               return_mode == FrameReturnMode::StoreNegatedBoolean) {
      value_set_bool(target, value_truthy(owned_return_value) != (return_mode == FrameReturnMode::StoreNegatedBoolean));
    } else {
      value_assign_fast(target, owned_return_value);
    }
    return true;
  };

  std::function<bool(const Value&)> exception_matches = [&](const Value& handler_type) -> bool {
    if (auto* tuple = value_as_tuple(handler_type)) {
      for (const auto& item : tuple->items) {
        if (exception_matches(item)) {
          return true;
        }
      }
      return false;
    }
    auto* handler_class = value_as_class(handler_type);
    if (handler_class == nullptr) {
      return false;
    }
    Value exception_type = runtime_.exception_type(current_exception);
    auto* raised_class = value_as_class(exception_type);
    return raised_class != nullptr && class_is_subclass(raised_class, handler_class);
  };

  bind_error = [&](const std::string& message) -> bool {
    Value exception = runtime_.make_exception("TypeError", message);
    const size_t source_frame = frame_count;
    if (!dispatch_exception(std::move(exception))) {
      return false;
    }
    if (frame_count != source_frame) {
      throw VMUnwind{};
    }
    return false;
  };

  if (has_generator_resume_exception) {
    // ``throw()`` resumes a suspended generator with an exception that can
    // already carry frames from the awaited operation (for example a Future
    // completed by a worker thread).  Keep that traceback tail while adding
    // the frames crossed by the resumed await chain.
    if (!dispatch_exception(std::move(generator_resume_exception), true)) {
      if (generator != nullptr) {
        generator->done = true;
      }
      return result;
    }
  }

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

  // Protect frame storage and its published inspection views across frame
  // switches as well as opcode dispatch. Other threads may inspect these
  // views whenever a blocking native operation releases this lock.
  XlangRuntimeExecutionGuard execution_lock;
  uint32_t execution_lock_ticks = 0;

  while (frame_count != 0) {
    // Every push or pop exits the opcode loop through switch_frame, so publish
    // the changed stack once here instead of comparing generations per opcode.
    refresh_runtime_frame_views();
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
    auto& instr_cache = frame.instr_cache;
    auto& native_call_args = frame.native_call_args;

    // CPython updates monitoring instrumentation at evaluator resume points.
    // Do the equivalent when an XLang frame becomes current, rather than
    // loading a global atomic generation for every instruction.
    refresh_monitoring_configuration(frame);

    if (frame.monitoring_code.tag == ValueTag::Invalid &&
        sys_monitoring_event_may_dispatch(kSysMonitoringEventAll)) {
      frame.monitoring_code = module_owner != nullptr
          ? runtime_.code_object(module_owner, frame.function_id)
          : Value::none();
    }

    // The globals module is stable for this frame. Update it at the frame
    // boundary; doing a Value assignment in the opcode loop retains/releases
    // the module on every instruction and measurably slows tight loops.
    runtime_.set_current_globals_module(globals_module);
    runtime_.set_current_frame_locals(&fn.locals, locals.value_data(), locals.size());
    // The live frame-stack view owns the changing instruction pointer. Update
    // the legacy single-frame identity once when execution switches frames.
    runtime_.set_current_frame(&module_owner, frame.function_id, &globals_module, ip);

    auto raise_exception_value = [&](Value exception) -> bool {
      const size_t source_frame = frame_count;
      // Exceptions propagated through a native/runtime call may already carry
      // traceback frames from a nested interpreter (for example exec(code)).
      // Raising an existing exception also retains its prior traceback in
      // CPython, with the current frame prepended.
      if (!dispatch_exception(std::move(exception), true)) {
        return false;
      }
      if (frame_count != source_frame) {
        throw VMUnwind{};
      }
      return true;
    };

    auto reraise_exception_value = [&](Value exception) -> bool {
      const size_t source_frame = frame_count;
      if (!dispatch_exception(std::move(exception), true, true)) {
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

    auto raise_name_error = [&](const std::string& message) -> bool {
      Value exception = runtime_.make_exception("NameError", message);
      const size_t name_start = message.find('\'');
      const size_t name_end = name_start == std::string::npos
          ? std::string::npos
          : message.find('\'', name_start + 1);
      if (name_start != std::string::npos && name_end != std::string::npos) {
        std::string ignored;
        object_set_attr(
            exception,
            "name",
            Value::string(message.substr(name_start + 1, name_end - name_start - 1)),
            ignored);
      }
      return raise_exception_value(std::move(exception));
    };

    auto raise_unbound_local_error = [&](const std::string& message) -> bool {
      return raise_exception_value(runtime_.make_exception("UnboundLocalError", message));
    };

    auto raise_import_error = [&](
        const std::string& message,
        bool module_not_found = false,
        const std::string& name = std::string(),
        const std::string& name_from = std::string()) -> bool {
      Value pending;
      if (runtime_.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending));
      }
      Value exception = runtime_.make_exception(
          module_not_found ? "ModuleNotFoundError" : "ImportError", message);
      std::string ignored;
      if (!name.empty()) {
        object_set_attr(exception, "name", Value::string(name), ignored);
      }
      if (!name_from.empty()) {
        ignored.clear();
        object_set_attr(exception, "name_from", Value::string(name_from), ignored);
      }
      return raise_exception_value(std::move(exception));
    };

    try {
    for (;;) {
      // CPython 3.14 places eval-breaker handling in periodic operations such
      // as CALL and backward jumps. XLang also bounds cross-thread event
      // delivery to 64 IR instructions, so the ordinary per-op path must stay
      // a TLS countdown; the helper performs the global acquire poll only at
      // that interval. Keep event handling here at the dispatch boundary.
      const uint32_t pending_events = interpreter_poll_pending_events();
      if (XLANG3_UNLIKELY(pending_events != 0)) {
        if ((pending_events & kInterpreterEventWeakrefCallbacks) != 0) {
          weakref_dispatch_callbacks(runtime_);
        }
        if ((pending_events & kInterpreterEventSignals) != 0) {
          std::string signal_error;
          if (!signal_dispatch_pending(runtime_, signal_error)) {
            Value exception;
            if (!runtime_.take_pending_exception(exception)) {
              exception = runtime_.make_exception("RuntimeError", signal_error);
            }
            if (!dispatch_exception(std::move(exception))) return result;
            goto switch_frame;
          }
        }
      }
      if (deferred_frame_exception.tag != ValueTag::Invalid) {
        Value exception = std::move(deferred_frame_exception);
        value_set_invalid(deferred_frame_exception);
        if (!dispatch_exception(std::move(exception))) {
          return result;
        }
        goto switch_frame;
      }
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

      const bool debug_poll_active = runtime_.debug_poll_needed();
      if (XLANG3_UNLIKELY(debug_poll_active)) {
        if (!poll_debug_event(frame)) {
          return result;
        }
      }
      // Once the frame's call event has run, an ordinary unmonitored frame has
      // no per-opcode tracing work. Entry, return, branch, exception, and yield
      // events are handled at their corresponding control-flow points; only
      // line and instruction monitoring require this check on every opcode.
      bool trace_dispatch_active = false;
      constexpr int64_t kPerInstructionMonitoringEvents =
          kSysMonitoringEventLine | kSysMonitoringEventInstruction;
      const bool frame_observability_active =
          !frame.trace_call_emitted || resumed_generator ||
          (frame.monitoring_events & kPerInstructionMonitoringEvents) != 0 ||
          hook_is_active(frame.trace_function);
      active_dispatch_ip = ip;
      allow_call_argument_transfer = !frame_observability_active &&
          !debug_poll_active && !resumed_generator;
      if (XLANG3_UNLIKELY(frame_observability_active)) {
        // A frame without an installed local trace function cannot be executing
        // recursively inside that trace function. Avoid a TLS runtime-state
        // lookup on every ordinary opcode.
        trace_dispatch_active =
            hook_is_active(frame.trace_function) && runtime_.trace_dispatch_active();
      }
      if (XLANG3_UNLIKELY(frame_observability_active && !trace_dispatch_active)) {
        if (!frame.trace_call_emitted) {
          frame.trace_call_emitted = true;
          if ((frame.monitoring_events & kSysMonitoringEventPyStart) != 0) {
            if (!emit_monitoring_event(frame, kSysMonitoringEventPyStart, nullptr)) {
              return result;
            }
          }
          if (runtime_.trace_event_may_dispatch() &&
              hook_is_active(runtime_.trace_function())) {
            if (!emit_trace_event(frame, "call", Value::none())) {
              return result;
            }
          }
          if (runtime_.profile_event_may_dispatch() &&
              hook_is_active(runtime_.profile_function()) && !runtime_.profile_dispatch_active()) {
            if (!emit_profile_event(frame, "call", Value::none())) {
              return result;
            }
          }
        }
        if (resumed_generator) {
          resumed_generator = false;
          if (!emit_monitoring_event(frame, kSysMonitoringEventPyResume, nullptr)) {
            return result;
          }
          if ((runtime_.trace_event_may_dispatch() &&
               !emit_trace_event(frame, "call", Value::none())) ||
              (runtime_.profile_event_may_dispatch() &&
               !emit_profile_event(frame, "call", Value::none()))) {
            return result;
          }
        }
        uint32_t source_line = 0;
        const bool trace_lines_active = frame.trace_function.tag != ValueTag::Invalid &&
            frame.trace_function.tag != ValueTag::None && frame.trace_lines;
        if ((frame.monitoring_events & kSysMonitoringEventLine) != 0 || trace_lines_active) {
          source_line = source_line_for_frame(frame);
        }
        if ((frame.monitoring_events & kSysMonitoringEventLine) != 0 &&
            source_line != 0 && source_line != frame.last_monitoring_line) {
          frame.last_monitoring_line = source_line;
          if (!emit_monitoring_event(frame, kSysMonitoringEventLine, nullptr)) {
            return result;
          }
        }
        if (trace_lines_active && source_line != 0 && source_line != frame.last_trace_line) {
          frame.last_trace_line = source_line;
          if (!emit_trace_event(frame, "line", Value::none())) {
            return result;
          }
        }
      }
      const auto& in = fn.code[ip];
      active_dispatch_ip = ip;
      if (count_opcode_dispatches) {
        xlang_perf_count_opcode_enabled(static_cast<uint16_t>(in.op));
      }
      if (!trace_dispatch_active &&
          XLANG3_UNLIKELY((frame.monitoring_events & kSysMonitoringEventInstruction) != 0)) {
        if (!emit_monitoring_event(frame, kSysMonitoringEventInstruction, nullptr)) {
          return result;
        }
      }
      ++execution_lock_ticks;
      if ((execution_lock_ticks & 0xfffu) == 0 && xlang_runtime_execution_should_yield()) {
        execution_lock.unlock();
        std::this_thread::yield();
        execution_lock.lock();
        refresh_monitoring_configuration(frame);
      }
      switch (in.op) {
#include "xlang_vm_op_rows.h"
      }
      frame.release_memoryviews_last_used_at(ip);
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
  // All successful returns have passed through finish_frame(), which releases
  // Python references while leaving reusable storage in each VMFrame slot.
  frame_pool_guard.retain = frame_count == 0;
  return result;
}

} // namespace xlang3
