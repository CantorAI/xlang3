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
#include "xlang3/interpreter.h"
#include "xlang3/runtime.h"
#include "xlang3/value.h"

#include <memory>
#include <sstream>
#include <string>

namespace xlang3::test {
namespace nested_trace_setting_cases {

struct LiveRuntime {
  Runtime* runtime = nullptr;
  bool active = false;
};

struct Audit {
  std::shared_ptr<LiveRuntime> live;
  const ir::Module* expected_module = nullptr;
  unsigned calls = 0;
  unsigned returns = 0;
  unsigned cleanup = 0;
  bool frame_ok = true;
  bool alive_after_change = false;
  bool replace = false;
  bool cleanup_reenters = false;
  bool publication_seen = false;
  bool reentry_ok = false;
  Value expected_publication;
  Value next;
};

inline void destroy_hook(void* pointer) {
  // Never borrow audit storage after deleting a last callable owner.
  auto audit = *static_cast<std::shared_ptr<Audit>*>(pointer);
  delete static_cast<std::shared_ptr<Audit>*>(pointer);
  ++audit->cleanup;

  if (!audit->cleanup_reenters ||
      !audit->live || !audit->live->active) return;

  try {
    Runtime& runtime = *audit->live->runtime;
    audit->publication_seen =
        value_is(runtime.trace_function(), audit->expected_publication);
    runtime.set_trace_function(audit->next);
    audit->reentry_ok = value_is(runtime.trace_function(), audit->next);
  } catch (...) {
    audit->reentry_ok = false;
  }
}

inline bool check_event(const std::shared_ptr<Audit>& audit,
                        const Value* args, uint32_t argc,
                        std::string& event, std::string& error) {
  auto* frame = argc == 3 ? value_as_frame(args[0]) : nullptr;
  auto* text = argc == 3 ? value_as_string(args[1]) : nullptr;
  const bool valid = frame != nullptr && text != nullptr &&
      frame->module.get() == audit->expected_module &&
      frame->function_id == 1;
  audit->frame_ok &= valid;
  if (!valid) {
    error = "trace proof requires its real DLL Python marker frame";
    return false;
  }
  event = std::string(string_object_view(*text));
  return true;
}

inline bool mutating_trace(Runtime& runtime, const Value* args,
                           uint32_t argc, Value& out,
                           std::string& error, void* pointer) {
  // A defective emitter may delete this native context during the setter.
  // Keep the audit independently owned so failure remains reportable.
  auto audit = *static_cast<std::shared_ptr<Audit>*>(pointer);
  std::string event;
  if (!check_event(audit, args, argc, event, error)) return false;
  if (event != "call") {
    error = "mutating trace proof expected the marker call event";
    return false;
  }

  ++audit->calls;
  runtime.set_trace_function(audit->replace ? audit->next : Value::none());
  audit->alive_after_change = audit->cleanup == 0;

  // Do not leave an additional local-trace owner in the marker frame.
  out = Value::none();
  error.clear();
  return true;
}

inline bool recording_trace(Runtime& runtime, const Value* args,
                            uint32_t argc, Value& out,
                            std::string& error, void* pointer) {
  auto audit = *static_cast<std::shared_ptr<Audit>*>(pointer);
  std::string event;
  if (!check_event(audit, args, argc, event, error)) return false;
  if (event == "call") ++audit->calls;
  if (event == "return") ++audit->returns;

  // Select this hook as the frame's local tracer without assuming line counts.
  value_assign_fast(out, runtime.trace_function());
  error.clear();
  return true;
}

inline Value make_hook(const char* name, NativeFunctionCallback callback,
                       const std::shared_ptr<Audit>& audit) {
  auto context = std::make_unique<std::shared_ptr<Audit>>(audit);
  Value hook = Value::native_function(
      0, name, callback, context.get(), destroy_hook);
  context.release();
  return hook;
}

struct ResetView {
  Runtime& runtime;
  std::shared_ptr<LiveRuntime> live;
  unsigned saved = 0;

  void push() {
    runtime.push_current_frame_state();
    ++saved;
  }

  void pop() {
    runtime.pop_current_frame_state();
    --saved;
  }

  ~ResetView() {
    // Expire cleanup reentry before unwinding saved views or Runtime teardown.
    live->active = false;
    while (saved != 0) pop();
    runtime.set_trace_function(Value::none());
    runtime.clear_current_frame();
  }
};

inline bool marker_ok(const RuntimeResult& answer) {
  return answer.errors.empty() && !answer.paused &&
      answer.exception.tag == ValueTag::Invalid &&
      answer.value.tag == ValueTag::Int64 &&
      answer.value.as.i64 == 41;
}

} // namespace nested_trace_setting_cases

inline void check_nested_trace_setting_cases(CaseResult& result) {
  using namespace nested_trace_setting_cases;

  std::ostringstream output;
  Runtime runtime(output);

  auto mutable_module = std::make_shared<ir::Module>();
  mutable_module->source_file = "<nested-trace-setting-cpp>";
  mutable_module->functions.resize(2);
  auto& parent = mutable_module->functions[0];
  parent.name = "embedding_parent";
  parent.constants = {Value::none()};
  parent.code = {{ir::Op::ReturnConst, 0, 0, 0, 0}};
  auto& marker_code = mutable_module->functions[1];
  marker_code.name = "nested_trace_marker";
  marker_code.register_count = 1;
  marker_code.constants = {Value::int64(41)};
  marker_code.code = {{ir::Op::ReturnConst, 0, 0, 0, 0}};

  std::shared_ptr<const ir::Module> module = mutable_module;
  Value globals = Value::module("nested_trace_setting_cpp");
  Value marker = Value::function(1, {}, globals, module);
  Interpreter interpreter(runtime);

  auto live = std::make_shared<LiveRuntime>();
  live->runtime = &runtime;
  ResetView reset{runtime, live};
  // These owning locals outlive reset, including construction/test unwind.
  runtime.set_current_frame(&module, 0, &globals, 0);
  live->active = true;

  auto new_audit = [&]() {
    auto audit = std::make_shared<Audit>();
    audit->live = live;
    audit->expected_module = module.get();
    return audit;
  };
  auto invoke = [&]() {
    return interpreter.run_function_value(
        value_as_function(marker), CallArgsView{});
  };

  {
    auto audit = new_audit();
    Value hook = make_hook(
        "last_owner_self_disabling_trace", mutating_trace, audit);
    runtime.set_trace_function(hook);
    value_set_invalid(hook);

    RuntimeResult answer = invoke();
    expect_true(result,
        marker_ok(answer) && audit->frame_ok && audit->calls == 1 &&
        audit->alive_after_change && audit->cleanup == 1 &&
        runtime.trace_function().tag == ValueTag::None &&
        !runtime.trace_dispatch_active(),
        "real DLL trace dispatch pins its last hook through self-disable "
        "and retires it once after invocation");
    runtime.set_trace_function(Value::none());
  }

  {
    auto replacement = new_audit();
    auto old = new_audit();
    old->replace = true;
    old->next = make_hook(
        "replacement_trace", recording_trace, replacement);

    Value hook = make_hook(
        "last_owner_replacing_trace", mutating_trace, old);
    runtime.set_trace_function(hook);
    value_set_invalid(hook);

    RuntimeResult first = invoke();
    expect_true(result,
        marker_ok(first) && old->frame_ok && old->calls == 1 &&
        old->alive_after_change && old->cleanup == 1 &&
        value_is(runtime.trace_function(), old->next),
        "last-owner replacement survives callback and saved-state return "
        "without early cleanup");

    RuntimeResult second = invoke();
    expect_true(result,
        marker_ok(second) && replacement->frame_ok &&
        replacement->calls == 1 && replacement->returns == 1 &&
        old->cleanup == 1 &&
        value_is(runtime.trace_function(), old->next) &&
        !runtime.trace_dispatch_active(),
        "the next real Python marker uses the replacement trace hook "
        "for call and return");

    runtime.set_trace_function(Value::none());
    value_set_invalid(old->next);
  }

  {
    auto published = new_audit();
    auto third = new_audit();
    auto old = new_audit();
    old->cleanup_reenters = true;
    old->expected_publication = make_hook(
        "published_second_trace", recording_trace, published);
    old->next = make_hook(
        "cleanup_selected_third_trace", recording_trace, third);

    Value hook = make_hook(
        "reentrant_retirement_trace", recording_trace, old);
    runtime.set_trace_function(hook);
    value_set_invalid(hook);
    reset.push();
    reset.push();

    runtime.set_trace_function(old->expected_publication);
    expect_true(result,
        old->cleanup == 1 && old->publication_seen && old->reentry_ok &&
        value_is(runtime.trace_function(), old->next),
        "all saved states publish the new hook before old-hook cleanup "
        "reenters and selects a third hook");

    reset.pop();
    const bool first_pop_kept_third =
        value_is(runtime.trace_function(), old->next);
    reset.pop();
    expect_true(result,
        first_pop_kept_third &&
        value_is(runtime.trace_function(), old->next) &&
        old->cleanup == 1,
        "reentrant cleanup's third hook survives both saved-state pops");

    runtime.set_trace_function(Value::none());
    value_set_invalid(old->expected_publication);
    value_set_invalid(old->next);
  }
}

} // namespace xlang3::test
