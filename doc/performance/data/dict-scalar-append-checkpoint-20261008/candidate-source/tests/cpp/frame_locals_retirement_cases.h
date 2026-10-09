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
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"

namespace xlang3::test {
namespace frame_locals_retirement_cases {
inline bool integer(const Value& dictionary, const char* name, int64_t expected) {
  Value value;
  std::string error;
  return mapping_get_item(dictionary, Value::string(name), value, error) &&
      value.tag == ValueTag::Int64 && value.as.i64 == expected;
}
inline bool absent(const Value& dictionary, const char* name) {
  Value value;
  std::string error;
  return !mapping_get_item(dictionary, Value::string(name), value, error);
}
struct CleanupAudit {
  std::shared_ptr<bool> live;
  Runtime* runtime = nullptr;
  FrameObject* retired = nullptr;
  Value nested;
  unsigned calls = 0;
  bool complete = false;
};
inline bool token(Runtime&, const Value*, uint32_t, Value& output,
    std::string& error, void*) {
  output = Value::none(); error.clear(); return true;
}
inline void cleanup(void* context) {
  auto audit = *static_cast<std::shared_ptr<CleanupAudit>*>(context);
  delete static_cast<std::shared_ptr<CleanupAudit>*>(context);
  ++audit->calls;
  if (!*audit->live) return;
  audit->complete = !audit->retired->live &&
      integer(audit->retired->locals, "later", 42) &&
      absent(audit->retired->locals, "removed") &&
      audit->retired->instruction_index == 7;
  // Both public calls acquire the registry mutex: cleanup must be outside it.
  audit->runtime->track_live_frame_snapshot(audit->nested);
  audit->runtime->retire_live_frame_snapshot(
      value_as_frame(audit->nested)->activation_id, 0, nullptr, 0);
}
struct LiveGuard {
  std::shared_ptr<bool> live;
  ~LiveGuard() { *live = false; }
};
} // namespace frame_locals_retirement_cases

inline void check_frame_locals_retirement(CaseResult& result) {
  using namespace frame_locals_retirement_cases;
  auto mutable_module = std::make_shared<ir::Module>();
  mutable_module->entry = 0;
  mutable_module->functions.resize(2);
  mutable_module->functions[0].name = "module";
  mutable_module->functions[1].name = "captured";
  mutable_module->functions[1].locals = {"value", "removed", "later", "#hidden"};
  std::shared_ptr<const ir::Module> module = mutable_module;
  std::ostringstream output;
  Runtime runtime(output);
  auto live = std::make_shared<bool>(false);
  Value globals = Value::dict({{Value::string("module_only"), Value::int64(9)}});
  Value active = runtime.make_exception("LookupError", "outer");
  Value hook = Value::native_function(0, "retirement_hook", token, nullptr);
  runtime.set_active_exception(active);
  runtime.set_pending_exception(active);
  runtime.set_profile_function(hook);
  runtime.set_trace_function(hook);
  runtime.set_profile_dispatch_active(true);
  runtime.set_trace_dispatch_active(true);

  Value eager = Value::frame(module, 1, globals, 1,
      Value::dict({}), Value::none(), Value::dict({}),
      runtime.allocate_frame_activation_id());
  auto audit = std::make_shared<CleanupAudit>();
  audit->live = live;
  audit->runtime = &runtime;
  audit->retired = value_as_frame(eager);
  audit->nested = Value::frame(module, 1, globals, 0,
      Value::dict({}), Value::none(), Value::dict({}),
      runtime.allocate_frame_activation_id());
  Value old = Value::native_function(0, "removed_owner", token,
      new std::shared_ptr<CleanupAudit>(audit), cleanup);
  value_as_frame(eager)->locals = Value::dict({
      {Value::string("value"), Value::int64(1)},
      {Value::string("removed"), old},
      {Value::string("extra"), Value::int64(17)}});
  value_set_invalid(old);
  Value retained_mapping = value_as_frame(eager)->locals;
  // Prime the old indexes. Same-sized replacement must not use their entries.
  Value primed;
  std::string error;
  mapping_get_item(retained_mapping, Value::string("removed"), primed, error);
  value_set_invalid(primed);
  runtime.track_live_frame_snapshot(eager);
  Value final_values[] = {Value::int64(2), Value::invalid(),
      Value::int64(42), Value::int64(99)};
  LiveGuard guard{live};
  *live = true;
  runtime.retire_live_frame_snapshot(value_as_frame(eager)->activation_id,
      7, final_values, 4);
  expect_true(result, value_is(value_as_frame(eager)->locals, retained_mapping) &&
      integer(retained_mapping, "value", 2) && integer(retained_mapping, "later", 42) &&
      integer(retained_mapping, "extra", 17) &&
      absent(retained_mapping, "removed") && absent(retained_mapping, "#hidden"),
      "eager escaped frame and retained mapping expose final locals with fresh indexes");
  expect_true(result, audit->calls == 1 && audit->complete,
      "displaced local owner reenters only after retirement and registry unlock");

  Value lazy = Value::frame(module, 1, globals, 1, Value::invalid(),
      Value::none(), Value::dict({}), runtime.allocate_frame_activation_id());
  value_as_frame(lazy)->has_lazy_locals = true;
  value_as_frame(lazy)->local_snapshot = {Value::int64(1)};
  runtime.track_live_frame_snapshot(lazy);
  runtime.retire_live_frame_snapshot(value_as_frame(lazy)->activation_id,
      8, final_values, 4);
  Value materialized;
  expect_true(result, object_get_attr(lazy, "f_locals", materialized, error) &&
      integer(materialized, "later", 42) && absent(materialized, "#hidden"),
      "unread escaped locals keep existing lazy final snapshot behavior");

  Value entry = Value::frame(module, module->entry, globals, 0, globals,
      Value::none(), Value::dict({}), runtime.allocate_frame_activation_id());
  runtime.track_live_frame_snapshot(entry);
  runtime.retire_live_frame_snapshot(value_as_frame(entry)->activation_id,
      1, final_values, 4);
  expect_true(result, value_is(value_as_frame(entry)->locals, globals) &&
      integer(globals, "module_only", 9) && absent(globals, "later"),
      "module and eval entry namespace aliases are not physical local snapshots");
  Value pending;
  expect_true(result, value_is(runtime.active_exception(), active) &&
      runtime.take_pending_exception(pending) && value_is(pending, active) &&
      value_is(runtime.profile_function(), hook) && value_is(runtime.trace_function(), hook) &&
      runtime.profile_dispatch_active() && runtime.trace_dispatch_active(),
      "retirement preserves handled/pending exception and observer state");

  std::string execution_output;
  auto actual = run_source(
      "import sys\n"
      "saved = []\n"
      "def capture(fail):\n"
      "    removed = 1\n"
      "    frame = sys._getframe()\n"
      "    assert frame.f_locals['removed'] == 1\n"
      "    del removed\n"
      "    later = 42\n"
      "    saved.append(frame)\n"
      "    if fail:\n"
      "        raise LookupError('unwind')\n"
      "    return frame\n"
      "capture(False)\n"
      "try:\n"
      "    capture(True)\n"
      "except LookupError:\n"
      "    pass\n"
      "for frame in saved:\n"
      "    assert frame.f_locals['later'] == 42\n"
      "    assert 'removed' not in frame.f_locals\n"
      "    frame.clear()\n"
      "print('PASS frame-retirement-return-and-unwind')\n", execution_output);
  result.errors.insert(result.errors.end(), actual.errors.begin(), actual.errors.end());
  expect_true(result, actual.ok &&
      execution_output == "PASS frame-retirement-return-and-unwind\n",
      "actual exported Interpreter normal return and unwind refresh eager snapshots");
}
} // namespace xlang3::test
