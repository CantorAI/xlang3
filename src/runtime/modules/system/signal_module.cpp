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
#include "xlang3/builtins.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <unordered_map>
#include <vector>

namespace xlang3 {

namespace {

struct SignalState {
  std::unordered_map<int64_t, Value> handlers;
  std::vector<int64_t> signals = {2, 4, 6, 8, 11, 15};
};

SignalState* signal_state(void* user_data) {
  return static_cast<SignalState*>(user_data);
}

bool signal_number(const Value& value, int64_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "signal number must be int";
    return false;
  }
  out = value.as.i64;
  return true;
}

Value default_handler_for(int64_t signum) {
  (void)signum;
  return Value::int64(0);
}

bool signal_signal(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "signal.signal() expected signal number and handler";
    return false;
  }
  int64_t signum = 0;
  if (!signal_number(args[0], signum, error)) {
    return false;
  }
  auto* state = signal_state(user_data);
  auto it = state->handlers.find(signum);
  out = it == state->handlers.end() ? default_handler_for(signum) : it->second;
  state->handlers[signum] = args[1];
  return true;
}

bool getsignal(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "signal.getsignal() expected signal number";
    return false;
  }
  int64_t signum = 0;
  if (!signal_number(args[0], signum, error)) {
    return false;
  }
  auto* state = signal_state(user_data);
  auto it = state->handlers.find(signum);
  out = it == state->handlers.end() ? default_handler_for(signum) : it->second;
  return true;
}

bool default_int_handler(Runtime& runtime, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  if (argc > 2) {
    error = "default_int_handler() expected optional signum and frame";
    return false;
  }
  runtime.raise_class_error("KeyboardInterrupt", "");
  return false;
}

bool raise_signal(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "signal.raise_signal() expected signal number";
    return false;
  }
  int64_t signum = 0;
  if (!signal_number(args[0], signum, error)) {
    return false;
  }
  auto* state = signal_state(user_data);
  auto it = state->handlers.find(signum);
  if (it == state->handlers.end() || (it->second.tag == ValueTag::Int64 && it->second.as.i64 >= 0 && it->second.as.i64 <= 1)) {
    value_set_none(out);
    return true;
  }
  Value call_args[2] = {Value::int64(signum), Value::none()};
  Value ignored;
  if (!runtime_call_callable(runtime, it->second, call_args, 2, ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool valid_signals(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) {
    error = "signal.valid_signals() expected no arguments";
    return false;
  }
  auto* state = signal_state(user_data);
  std::vector<Value> values;
  values.reserve(state->signals.size());
  for (int64_t signum : state->signals) {
    values.push_back(Value::int64(signum));
  }
  out = Value::set(std::move(values));
  return true;
}

bool strsignal(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "signal.strsignal() expected signal number";
    return false;
  }
  int64_t signum = 0;
  if (!signal_number(args[0], signum, error)) {
    return false;
  }
  switch (signum) {
  case 2:
    out = Value::string("Interrupt");
    return true;
  case 4:
    out = Value::string("Illegal instruction");
    return true;
  case 6:
    out = Value::string("Aborted");
    return true;
  case 8:
    out = Value::string("Floating point exception");
    return true;
  case 11:
    out = Value::string("Segmentation fault");
    return true;
  case 15:
    out = Value::string("Terminated");
    return true;
  default:
    value_set_none(out);
    return true;
  }
}

void fill_signal_module(Runtime& runtime, NativeModuleBuilder& builder, SignalState* state) {
  builder.value("signal", runtime.make_native_function("signal.signal", signal_signal, state))
      .value("getsignal", runtime.make_native_function("signal.getsignal", getsignal, state))
      .value("raise_signal", runtime.make_native_function("signal.raise_signal", raise_signal, state))
      .value("valid_signals", runtime.make_native_function("signal.valid_signals", valid_signals, state))
      .function("strsignal", strsignal)
      .function("default_int_handler", default_int_handler)
      .value("SIG_DFL", Value::int64(0))
      .value("SIG_IGN", Value::int64(1))
      .value("SIGINT", Value::int64(2))
      .value("SIGILL", Value::int64(4))
      .value("SIGABRT", Value::int64(6))
      .value("SIGFPE", Value::int64(8))
      .value("SIGSEGV", Value::int64(11))
      .value("SIGTERM", Value::int64(15))
      .value("NSIG", Value::int64(23))
      .value("CTRL_C_EVENT", Value::int64(0))
      .value("CTRL_BREAK_EVENT", Value::int64(1));
}

} // namespace

void register_signal_module(Runtime& runtime) {
  auto* state = new SignalState();
  runtime.register_native_package_cleanup(state, [](void* data) { delete static_cast<SignalState*>(data); });

  NativeModuleBuilder public_module(runtime, "signal");
  fill_signal_module(runtime, public_module, state);
  runtime.register_module("signal", public_module.finish());

  NativeModuleBuilder private_module(runtime, "_signal");
  fill_signal_module(runtime, private_module, state);
  runtime.register_module("_signal", private_module.finish());
}

} // namespace xlang3
