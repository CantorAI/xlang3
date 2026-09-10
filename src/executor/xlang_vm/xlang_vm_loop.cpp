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
#include "runtime_lock.h"

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

#include <array>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <new>
#include <sstream>
#include <string_view>
#include <thread>

#include "xlang_vm_inline_support.h"

namespace xlang3 {

bool generator_vm_frame_snapshot(const GeneratorObject& generator, Value& out) {
  if (generator.vm_state == nullptr) {
    return false;
  }
  const auto* state = static_cast<const GeneratorVMState*>(generator.vm_state);
  if (state->frame_count == 0 || state->frame_count > state->frames.size()) {
    return false;
  }
  const auto& frame = state->frames[state->frame_count - 1];
  if (frame.fn == nullptr || frame.module_owner == nullptr) {
    return false;
  }
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(frame.fn->locals.size() + frame.fn->free_vars.size());
  for (size_t local_index = 0; local_index < frame.fn->locals.size() && local_index < frame.locals.size(); ++local_index) {
    const auto& name = frame.fn->locals[local_index];
    if (name.empty() || name[0] == '#') {
      continue;
    }
    const Value* local_value = &frame.locals[local_index];
    for (size_t cell_index = 0; cell_index < frame.fn->cell_slots.size() && cell_index < frame.cells.size(); ++cell_index) {
      if (frame.fn->cell_slots[cell_index] == local_index) {
        if (auto* cell = value_as_cell(frame.cells[cell_index])) {
          local_value = &cell->value;
        }
        break;
      }
    }
    if (local_value->tag != ValueTag::Invalid) {
      entries.push_back({Value::string(name), *local_value});
    }
  }
  if (frame.closure != nullptr) {
    for (size_t free_index = 0;
         free_index < frame.fn->free_vars.size() && free_index < frame.closure->size();
         ++free_index) {
      const auto& name = frame.fn->free_vars[free_index];
      if (name.empty() || name[0] == '#') {
        continue;
      }
      const Value* free_value = &(*frame.closure)[free_index];
      if (auto* cell = value_as_cell(*free_value)) {
        free_value = &cell->value;
      }
      if (free_value->tag != ValueTag::Invalid) {
        entries.push_back({Value::string(name), *free_value});
      }
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
  return true;
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
  const bool resuming_pause = pause_state != nullptr;
  struct CurrentFrameGuard {
    Runtime& runtime;

    explicit CurrentFrameGuard(Runtime& target_runtime) : runtime(target_runtime) {
      runtime.push_current_frame_state();
    }

    ~CurrentFrameGuard() {
      runtime.pop_current_frame_state();
    }
  } current_frame_guard(runtime_);
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
    if (target_fn.signature.empty() && !dynamic_positional_defaults &&
        !values.has_keywords() && !values.has_expansion()) {
      if (values.size() != target_fn.params.size()) {
        return bind_count_error(target_fn, values);
      }
      return true;
    }

    bound.assign(target_fn.params.size(), Value::invalid());
    std::vector<Value> positional;
    positional.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      positional.push_back(values.get(i));
    }
    auto expand_star_arg = [&](uint32_t star_reg) -> bool {
      const Value& star = values.registers[star_reg];
      if (auto* tuple = value_as_tuple(star)) {
        for (const auto& item : tuple->items) positional.push_back(item);
      } else if (auto* list = value_as_list(star)) {
        for (const auto& item : list->items) positional.push_back(item);
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
          positional.push_back(std::move(item));
        }
      }
      return true;
    };
    if (values.star_args != nullptr && !values.star_args->empty()) {
      for (uint32_t star_reg : *values.star_args) {
        if (!expand_star_arg(star_reg)) {
          return false;
        }
      }
    } else if (values.star_arg != UINT32_MAX) {
      if (!expand_star_arg(values.star_arg)) {
        return false;
      }
    }

    int32_t varargs_index = -1;
    int32_t kwargs_index = -1;
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
        too_many_positional = true;
        break;
      }
    }
    if (varargs_index >= 0) {
      bound[static_cast<size_t>(varargs_index)] = Value::tuple(std::move(extra_positional));
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
      std::string provided = std::to_string(positional.size());
      if (keyword_only_given != 0) {
        provided += " positional argument" + std::string(positional.size() == 1 ? "" : "s") +
                    " (and " + std::to_string(keyword_only_given) + " keyword-only argument" +
                    (keyword_only_given == 1 ? "" : "s") + ")";
      }
      return bind_error(
          display_name + "() takes " + expected + " but " + provided +
          (positional.size() == 1 && keyword_only_given == 0 ? " was given" : " were given"));
    }
    std::vector<std::string> missing_positional;
    std::vector<std::string> missing_keyword_only;
    for (size_t i = 0; i < signature.size(); ++i) {
      if (bound[i].tag != ValueTag::Invalid) {
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

  std::vector<VMFrame> frames;
  std::vector<RuntimeFrameView> runtime_frame_views;
  size_t frame_count = 0;
  bool resumed_generator = false;
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
    delete state;
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
    frames.reserve(64);
    frames.emplace_back(
        module, function_id, entry_args, fn_obj_closure, std::move(globals_module), std::move(module_owner), 0, false);
    frames.back().activation_id = runtime_.allocate_frame_activation_id();
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

    Value function_value = Value::function(
        fn_obj->function_id,
        fn_obj->closure,
        fn_obj->globals_module,
        fn_obj->module != nullptr ? fn_obj->module : module_owner,
        fn_obj->defaults);
    if (auto* generated_function = value_as_function(function_value)) {
      value_assign_fast(generated_function->attrs_dict, fn_obj->attrs_dict);
      value_assign_fast(generated_function->globals_dict, fn_obj->globals_dict);
      generated_function->positional_defaults = fn_obj->positional_defaults;
      generated_function->kwdefaults = fn_obj->kwdefaults;
      generated_function->qualname = fn_obj->qualname;
    }
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
    // Some source-backed operations still use recursive native helper paths
    // while Python frames are active.  Keep a conservative host-stack ceiling
    // in addition to the user-visible recursion limit so recursive logging and
    // traceback inspection raise Python's RecursionError before Windows can
    // raise an access violation.
    constexpr size_t kSafeHostFrameLimit = 1024;
    const size_t effective_recursion_limit = std::min(
        static_cast<size_t>(runtime_.recursion_limit()), kSafeHostFrameLimit);
    if (frame_count >= effective_recursion_limit) {
      deferred_frame_exception = runtime_.make_exception(
          "RecursionError", "maximum recursion depth exceeded");
      return false;
    }
    if (call_function_id >= call_module.functions.size()) {
      result.errors.push_back("invalid function id");
      return false;
    }
    const auto& call_fn = call_module.functions[call_function_id];
    std::vector<Value> bound_args;
    CallArgsView frame_args = call_args;
    if (!simple_signature(call_fn) || has_dynamic_positional_defaults(call_fn, defaults) ||
        call_args.has_keywords() || call_args.has_expansion()) {
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
      frame_args.star_args = nullptr;
      frame_args.kw_star_args = nullptr;
    } else if (call_args.size() != call_fn.params.size()) {
      return bind_count_error(call_fn, call_args);
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
    pushed.activation_id = runtime_.allocate_frame_activation_id();
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

  auto source_line_for_frame = [](const VMFrame& trace_frame) -> uint32_t {
    if (trace_frame.fn != nullptr && trace_frame.ip < trace_frame.fn->source_lines.size()) {
      return trace_frame.fn->source_lines[trace_frame.ip];
    }
    return 0;
  };

  auto refresh_runtime_frame_views = [&]() {
    runtime_frame_views.clear();
    runtime_frame_views.reserve(frame_count);
    for (size_t i = 0; i < frame_count; ++i) {
      auto& view_frame = frames[i];
      runtime_frame_views.push_back(RuntimeFrameView{
          &view_frame.module_owner,
          &view_frame.globals_module,
          &view_frame.fn->locals,
          view_frame.locals.value_data(),
          &view_frame.ip,
          view_frame.locals.size(),
          view_frame.function_id,
          view_frame.activation_id,
          view_frame.regs.value_data(),
          &view_frame.register_last_use,
          view_frame.regs.size(),
          &view_frame.native_call_args,
      });
    }
    runtime_.set_current_frame_stack(runtime_frame_views.data(), runtime_frame_views.size());
  };

  auto emit_trace_event = [&](VMFrame& trace_frame, const char* event_name, const Value& arg) -> bool {
    const bool is_call_event = std::string_view(event_name) == std::string_view("call");
    const Value& hook = is_call_event ? runtime_.trace_function() : trace_frame.trace_function;
    auto* hook_fn = value_as_function(hook);
    if (hook_fn == nullptr || runtime_.trace_dispatch_active()) {
      return true;
    }
    runtime_.set_current_frame(
        &trace_frame.module_owner,
        trace_frame.function_id,
        &trace_frame.globals_module,
        static_cast<uint32_t>(trace_frame.ip));
    runtime_.set_current_globals_module(trace_frame.globals_module);
    runtime_.set_current_frame_locals(&trace_frame.fn->locals, trace_frame.locals.value_data(), trace_frame.locals.size());

    Value trace_args_storage[3] = {
        runtime_.current_frame_snapshot(),
        Value::string(event_name),
        arg,
    };
    CallArgsView trace_args;
    trace_args.leading = trace_args_storage;
    trace_args.leading_count = 3;

    runtime_.set_trace_dispatch_active(true);
    Interpreter trace_interpreter(runtime_);
    RuntimeResult trace_result = trace_interpreter.run_function_value(hook_fn, trace_args);
    runtime_.set_trace_dispatch_active(false);
    if (!trace_result.errors.empty()) {
      result.errors.insert(result.errors.end(), trace_result.errors.begin(), trace_result.errors.end());
      return false;
    }
    if (auto* returned_trace = value_as_function(trace_result.value)) {
      (void)returned_trace;
      value_assign_fast(trace_frame.trace_function, trace_result.value);
    } else if (trace_result.value.tag == ValueTag::None || trace_result.value.tag == ValueTag::Invalid) {
      value_set_invalid(trace_frame.trace_function);
    }
    return true;
  };

  auto emit_profile_event = [&](VMFrame& profile_frame, const char* event_name, const Value& arg) -> bool {
    const Value& hook = runtime_.profile_function();
    if (hook.tag == ValueTag::Invalid || hook.tag == ValueTag::None || runtime_.profile_dispatch_active()) {
      return true;
    }
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

    runtime_.set_profile_dispatch_active(true);
    struct ProfileDispatchGuard {
      Runtime& runtime;
      ~ProfileDispatchGuard() { runtime.set_profile_dispatch_active(false); }
    } profile_guard{runtime_};
    Value ignored;
    std::string profile_error;
    if (!runtime_call_callable(runtime_, hook, profile_args_storage, 3, ignored, profile_error)) {
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
    CallArgsView debug_args;
    debug_args.leading = debug_args_storage;
    debug_args.leading_count = 2;

    runtime_.set_debug_dispatch_active(true);
    Interpreter debug_interpreter(runtime_);
    RuntimeResult debug_result = debug_interpreter.run_function_value(hook_fn, debug_args);
    runtime_.set_debug_dispatch_active(false);
    if (!debug_result.errors.empty()) {
      result.errors.insert(result.errors.end(), debug_result.errors.begin(), debug_result.errors.end());
      return false;
    }
    return true;
  };

  auto emit_monitoring_event = [&](VMFrame& monitoring_frame, int64_t event, const Value* arg) -> bool {
    if (!sys_monitoring_event_may_dispatch(event)) {
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

    Value code = monitoring_frame.module_owner != nullptr
        ? Value::code(monitoring_frame.module_owner, monitoring_frame.function_id)
        : Value::none();

    std::string monitoring_error;
    if (!sys_monitoring_dispatch_event(
            runtime_,
            event,
            code,
            static_cast<int64_t>(monitoring_frame.ip),
            arg,
            monitoring_error)) {
      result.errors.push_back(monitoring_error);
      return false;
    }
    return true;
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
  Value pending_exception_cause;
  bool pending_exception_explicit_cause = false;

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

  auto finish_frame = [&](const Value& return_value) -> bool {
    runtime_.refresh_live_frame_snapshots();
    Value owned_return_value;
    value_assign_fast(owned_return_value, return_value);
    VMFrame& finished = frames[frame_count - 1];
    if (!emit_monitoring_event(finished, kSysMonitoringEventPyReturn, &owned_return_value)) {
      return false;
    }
    if (!emit_trace_event(finished, "return", owned_return_value)) {
      return false;
    }
    if (!emit_profile_event(finished, "return", owned_return_value)) {
      return false;
    }
    const uint32_t return_dst = finished.return_dst;
    const bool has_caller = finished.has_caller;
    const FrameReturnMode return_mode = finished.return_mode;
    Value continuation_value;
    if (return_mode == FrameReturnMode::StoreConstructedInstance) {
      value_assign_fast(continuation_value, finished.continuation_value);
    }
    while (!active_exception_handler_frames.empty() && active_exception_handler_frames.back() == frame_count) {
      restore_active_exception_context();
    }
    if (!has_caller) {
      value_assign_fast(result.value, owned_return_value);
      finished.clear_for_pop();
      --frame_count;
      return false;
    }
    finished.clear_for_pop();
    --frame_count;
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

  auto make_traceback_from_frames = [&](bool track_live_frames = false) -> Value {
    Value builtins = Value::dict({});
    Value builtins_module;
    std::string builtins_error;
    if (runtime_.import_module("builtins", builtins_module, builtins_error)) {
      if (auto* module_object = value_as_module(builtins_module)) {
        std::vector<std::pair<Value, Value>> entries;
        entries.reserve(module_object->name_to_slot.size());
        for (const auto& attr : module_object->name_to_slot) {
          if (attr.second < module_object->slots.size() &&
              module_object->slots[attr.second].tag != ValueTag::Invalid) {
            entries.push_back({Value::string(attr.first), module_object->slots[attr.second]});
          }
        }
        builtins = Value::dict(std::move(entries));
      }
    }
    Value next = Value::none();
    for (size_t index = frame_count; index > 0; --index) {
      const auto& captured = frames[index - 1];
      // A caller is suspended after its call instruction, while the active
      // frame still points at the instruction that raised.  Tracebacks report
      // the call site for suspended callers.
      const uint32_t traceback_ip = index < frame_count && captured.ip > 0
          ? captured.ip - 1
          : captured.ip;
      std::vector<std::pair<Value, Value>> local_entries;
      if (captured.fn != nullptr) {
        local_entries.reserve(captured.fn->locals.size() + captured.fn->free_vars.size());
        for (size_t local_index = 0;
             local_index < captured.fn->locals.size() && local_index < captured.locals.size();
             ++local_index) {
          const auto& name = captured.fn->locals[local_index];
          if (name.empty() || name[0] == '#') {
            continue;
          }
          const Value* local_value = &captured.locals[local_index];
          for (size_t cell_index = 0;
               cell_index < captured.fn->cell_slots.size() && cell_index < captured.cells.size();
               ++cell_index) {
            if (captured.fn->cell_slots[cell_index] == local_index) {
              if (auto* cell = value_as_cell(captured.cells[cell_index])) {
                local_value = &cell->value;
              }
              break;
            }
          }
          if (local_value->tag != ValueTag::Invalid) {
            local_entries.push_back({Value::string(name), *local_value});
          }
        }
        if (captured.closure != nullptr) {
          for (size_t free_index = 0;
               free_index < captured.fn->free_vars.size() && free_index < captured.closure->size();
               ++free_index) {
            const auto& name = captured.fn->free_vars[free_index];
            if (name.empty() || name[0] == '#') {
              continue;
            }
            const Value* free_value = &(*captured.closure)[free_index];
            if (auto* cell = value_as_cell(*free_value)) {
              free_value = &cell->value;
            }
            if (free_value->tag != ValueTag::Invalid) {
              local_entries.push_back({Value::string(name), *free_value});
            }
          }
        }
      }
      Value frame_object = Value::frame(
          captured.module_owner,
          captured.function_id,
          captured.globals_module,
          traceback_ip,
          Value::dict(std::move(local_entries)),
          Value::invalid(),
          builtins,
          captured.activation_id);
      if (track_live_frames) {
        runtime_.track_live_frame_snapshot(frame_object);
      }
      int64_t source_line = static_cast<int64_t>(traceback_ip);
      if (captured.fn != nullptr && traceback_ip < captured.fn->source_lines.size() &&
          captured.fn->source_lines[traceback_ip] != 0) {
        source_line = static_cast<int64_t>(captured.fn->source_lines[traceback_ip]);
      }
      next = Value::traceback(std::move(frame_object), std::move(next), source_line);
    }
    return next;
  };

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

  auto dispatch_exception = [&](Value exception,
                                bool preserve_reraised_traceback = false,
                                bool deduplicate_bare_reraise_frame = false) -> bool {
    runtime_.refresh_live_frame_snapshots();
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
      Value traceback = make_traceback_from_frames(true);
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
    if (frame_count != 0) {
      Value exception_type = runtime_.exception_type(exception);
      Value traceback = make_traceback_from_frames();
      Value event_arg = Value::tuple({exception_type, exception, traceback});
      if (!emit_trace_event(frames[frame_count - 1], "exception", event_arg)) {
        return false;
      }
      if (!emit_profile_event(frames[frame_count - 1], "exception", event_arg)) {
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
        if (value_as_instance(current_exception) != nullptr) {
          Value traceback;
          std::string ignored;
          if (object_get_attr(current_exception, "__traceback__", traceback, ignored)) {
            for (size_t outer_index = 1; outer_index < frame_count; ++outer_index) {
              auto* traceback_object = value_as_traceback(traceback);
              if (traceback_object == nullptr) {
                break;
              }
              value_assign_fast(traceback, traceback_object->next);
            }
            object_set_attr(current_exception, "__traceback__", traceback, ignored);
          }
        }
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
      frames[frame_count - 1].clear_for_pop();
      --frame_count;
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
    if (!dispatch_exception(std::move(generator_resume_exception))) {
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

  while (frame_count != 0) {
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

    runtime_.set_current_globals_module(globals_module);
    runtime_.set_current_frame_locals(&fn.locals, locals.value_data(), locals.size());

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

    XlangRuntimeExecutionGuard execution_lock;
    uint32_t execution_lock_ticks = 0;

    try {
    for (;;) {
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

      runtime_.set_current_frame(&module_owner, frame.function_id, &globals_module, ip);
      if (XLANG3_UNLIKELY(runtime_.debug_poll_needed())) {
        if (!poll_debug_event(frame)) {
          return result;
        }
      }
      if (!runtime_.trace_dispatch_active()) {
        if (!frame.trace_call_emitted) {
          frame.trace_call_emitted = true;
          if (!emit_monitoring_event(frame, kSysMonitoringEventPyStart, nullptr)) {
            return result;
          }
          if (!emit_trace_event(frame, "call", Value::none())) {
            return result;
          }
          if (!emit_profile_event(frame, "call", Value::none())) {
            return result;
          }
        }
        if (resumed_generator) {
          resumed_generator = false;
          if (!emit_monitoring_event(frame, kSysMonitoringEventPyResume, nullptr)) {
            return result;
          }
        }
        const uint32_t source_line = source_line_for_frame(frame);
        if (source_line != 0 && source_line != frame.last_monitoring_line) {
          frame.last_monitoring_line = source_line;
          if (!emit_monitoring_event(frame, kSysMonitoringEventLine, nullptr)) {
            return result;
          }
        }
        if (value_as_function(frame.trace_function) != nullptr && source_line != 0 && source_line != frame.last_trace_line) {
          frame.last_trace_line = source_line;
          if (!emit_trace_event(frame, "line", Value::none())) {
            return result;
          }
        }
      }
      const auto& in = fn.code[ip];
      if (!runtime_.trace_dispatch_active()) {
        if (!emit_monitoring_event(frame, kSysMonitoringEventInstruction, nullptr)) {
          return result;
        }
      }
      if ((++execution_lock_ticks & 0x3ffu) == 0) {
        execution_lock.unlock();
        std::this_thread::yield();
        execution_lock.lock();
      }
      switch (in.op) {
#include "xlang_vm_op_rows.h"
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
