#pragma once
#include "test_harness.h"
#include "xlang3/builtins.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include "xlang3/module_object.h"
#include <sstream>

namespace xlang3::test {

struct NativeBoundZeroAudit {
  int calls = 0;
  int self_cleanup = 0;
  int function_cleanup = 0;
  bool cleanup_early = false;
  bool argument_survived = true;
  bool self_released_before_function = true;
  bool fail = false;
  uint32_t expected_argc = 1;
};

inline void native_bound_zero_self_cleanup(void* context) {
  auto* audit = static_cast<NativeBoundZeroAudit*>(context);
  ++audit->self_cleanup;
}

inline void native_bound_zero_function_cleanup(void* context) {
  auto* audit = static_cast<NativeBoundZeroAudit*>(context);
  audit->self_released_before_function &= audit->self_cleanup == 1;
  ++audit->function_cleanup;
}

inline bool native_bound_zero_nested(
    Runtime&, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "invalid nested bound callback arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

inline bool native_bound_zero_callback(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* context) {
  auto* audit = static_cast<NativeBoundZeroAudit*>(context);
  ++audit->calls;
  if (argc != audit->expected_argc) {
    error = "unexpected explicit argument count";
    return false;
  }
  // In the zero-argument test, this drops the LAST bound-method owner and
  // would invalidate a borrowed &bound->self or &bound->function argument.
  out = Value::none();
  audit->cleanup_early |= audit->self_cleanup != 0 || audit->function_cleanup != 0;
  // Exercise the registered API used by Python and native clients; the
  // collector implementation is private to the shared runtime library.
  Value gc_module;
  Value collect;
  Value collected;
  if (!runtime.import_module("gc", gc_module, error) ||
      !module_get_attr(gc_module, "collect", collect, error) ||
      !runtime_call_callable(runtime, collect, nullptr, 0, collected, error)) {
    return false;
  }
  audit->argument_survived &= instance_get_native_data(args[0], "NativeBoundZeroAudit") == audit;
  Value nested_function = runtime.make_native_function("native_bound_zero_nested", native_bound_zero_nested);
  Value nested_method = Value::bound_method(Value::int64(7), std::move(nested_function));
  Value nested_out;
  const bool nested_ok = runtime_call_callable(runtime, nested_method, nullptr, 0, nested_out, error);
  audit->argument_survived &= nested_ok && nested_out.tag == ValueTag::Int64 && nested_out.as.i64 == 7;
  audit->argument_survived &= instance_get_native_data(args[0], "NativeBoundZeroAudit") == audit;
  audit->cleanup_early |= audit->self_cleanup != 0 || audit->function_cleanup != 0;
  if (argc == 2) {
    audit->argument_survived &= args[1].tag == ValueTag::Int64 && args[1].as.i64 == 9;
  }
  if (audit->fail) {
    error = "native bound callback failed after reentry";
    return false;
  }
  out = Value::int64(41);
  return true;
}

inline void check_native_bound_zero_args(CaseResult& result) {
  std::ostringstream output;
  Runtime runtime(output);
  for (const bool fail : {false, true}) {
    NativeBoundZeroAudit audit;
    audit.fail = fail;
    Value klass = Value::class_object("NativeBoundZeroReceiver", {});
    Value self = Value::instance(klass);
    std::string error;
    expect_true(result, instance_set_native_data(self, "NativeBoundZeroAudit", &audit,
        native_bound_zero_self_cleanup, error), "install native argument lifetime audit");
    Value function = runtime.make_native_function("native_bound_zero_callback", native_bound_zero_callback,
        &audit, native_bound_zero_function_cleanup);
    Value callable = Value::bound_method(std::move(self), std::move(function));
    const bool ok = runtime_call_callable(runtime, callable, nullptr, 0, callable, error);
    expect_true(result, ok == !fail && audit.calls == 1 && audit.argument_survived && !audit.cleanup_early,
        "aliased output must not release self/function before callback rereads arguments, GC and reentry");
    expect_true(result, audit.self_cleanup == 1 && audit.function_cleanup == 1 &&
        audit.self_released_before_function,
        "zero-argument callback must release self before function after success or failure without retaining owners");
    expect_true(result, fail ? error == "native bound callback failed after reentry" &&
        callable.tag == ValueTag::None : callable.tag == ValueTag::Int64 && callable.as.i64 == 41,
        "zero-argument stack dispatch must preserve native output and failure");
  }
  // The nonzero path remains the existing vector path. Keep its target alive
  // externally and verify explicit argument order independently of output alias.
  {
    NativeBoundZeroAudit audit;
    audit.expected_argc = 2;
    Value klass = Value::class_object("NativeBoundArgumentReceiver", {});
    Value self = Value::instance(klass);
    std::string error;
    expect_true(result, instance_set_native_data(self, "NativeBoundZeroAudit", &audit,
        native_bound_zero_self_cleanup, error), "install explicit argument lifetime audit");
    Value function = runtime.make_native_function("native_bound_argument_callback", native_bound_zero_callback,
        &audit, native_bound_zero_function_cleanup);
    Value callable = Value::bound_method(std::move(self), function);
    Value explicit_arg = Value::int64(9);
    Value out;
    expect_true(result, runtime_call_callable(runtime, callable, &explicit_arg, 1, out, error) &&
        out.tag == ValueTag::Int64 && out.as.i64 == 41 && audit.calls == 1 &&
        audit.argument_survived && !audit.cleanup_early,
        "existing nonzero bound vector dispatch must preserve self then explicit arguments");
    value_set_invalid(callable);
    value_set_invalid(function);
    expect_true(result, audit.self_cleanup == 1 && audit.function_cleanup == 1 &&
        audit.self_released_before_function, "explicit path must release its original owners");
  }
}
} // namespace xlang3::test
