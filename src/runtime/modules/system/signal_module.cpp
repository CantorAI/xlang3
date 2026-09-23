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

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <csignal>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace xlang3 {

namespace {

struct SignalState {
  std::unordered_map<int64_t, Value> handlers;
  std::vector<int64_t> signals = {2, 4, 6, 8, 11, 15, 21};
  int64_t wakeup_fd = -1;
  bool warn_on_full_buffer = true;
};

SignalState* signal_state(void* user_data) {
  return static_cast<SignalState*>(user_data);
}

bool supported_signal(const SignalState& state, int64_t signum) {
  return std::find(state.signals.begin(), state.signals.end(), signum) != state.signals.end();
}

bool signal_number(const Value& value, int64_t& out, std::string& error) {
  if (!value_int_like_to_i64(value, out)) {
    error = "signal number must be int";
    return false;
  }
  return true;
}

Value default_handler_for(int64_t signum) {
  (void)signum;
  return Value::int64(0);
}

bool signal_signal(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "signal.signal() expected signal number and handler";
    return false;
  }
  int64_t signum = 0;
  if (!signal_number(args[0], signum, error)) {
    return false;
  }
  auto* state = signal_state(user_data);
  if (!supported_signal(*state, signum)) {
    error = "invalid signal number";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto it = state->handlers.find(signum);
  out = it == state->handlers.end() ? default_handler_for(signum) : it->second;
  state->handlers[signum] = args[1];
  return true;
}

bool getsignal(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "signal.getsignal() expected signal number";
    return false;
  }
  int64_t signum = 0;
  if (!signal_number(args[0], signum, error)) {
    return false;
  }
  auto* state = signal_state(user_data);
  if (!supported_signal(*state, signum)) {
    error = "invalid signal number";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
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
  if (!supported_signal(*state, signum)) {
    error = "invalid signal number";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (state->wakeup_fd >= 0) {
    const char byte = static_cast<char>(signum & 0xff);
#ifdef _WIN32
    const bool written = ::send(static_cast<SOCKET>(state->wakeup_fd), &byte, 1, 0) == 1;
#else
    const bool written = ::write(static_cast<int>(state->wakeup_fd), &byte, 1) == 1;
#endif
    if (!written && state->warn_on_full_buffer) {
      Value warnings;
      Value warn;
      const Value* warning_class = runtime.find_builtin("RuntimeWarning");
      if (!runtime.import_module("warnings", warnings, error) ||
          !module_get_attr(warnings, "warn", warn, error) ||
          warning_class == nullptr) return false;
      Value warning_args[] = {
          Value::string("signal wakeup fd buffer is full"), *warning_class};
      Value ignored;
      if (!runtime_call_callable(runtime, warn, warning_args, 2, ignored, error))
        return false;
    }
  }
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

bool set_wakeup_fd(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "signal.set_wakeup_fd() expected fd and optional warn_on_full_buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].tag != ValueTag::Int64) {
    error = "signal.set_wakeup_fd() fd must be int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = signal_state(user_data);
  out = Value::int64(state->wakeup_fd);
  state->wakeup_fd = args[0].as.i64;
  state->warn_on_full_buffer = argc == 2 ? value_truthy(args[1]) : true;
  return true;
}

bool set_wakeup_fd_kw(Runtime& runtime, const Value* args, uint32_t argc,
                      const NativeKeywordArg* kwargs, uint32_t kwargc,
                      Value& out, std::string& error, void* user_data) {
  if (argc != 1 || kwargc != 1 || kwargs[0].name == nullptr ||
      std::string(kwargs[0].name) != "warn_on_full_buffer" || kwargs[0].value == nullptr) {
    error = "signal.set_wakeup_fd() expected fd and warn_on_full_buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value positional[] = {args[0], *kwargs[0].value};
  return set_wakeup_fd(runtime, positional, 2, out, error, user_data);
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
      .value("set_wakeup_fd", runtime.make_native_function(
          "signal.set_wakeup_fd", set_wakeup_fd, state, nullptr, nullptr,
          false, set_wakeup_fd_kw))
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
#if defined(_WIN32)
      .value("SIGBREAK", Value::int64(21))
      .value("NSIG", Value::int64(23))
      .value("CTRL_C_EVENT", Value::int64(0))
      .value("CTRL_BREAK_EVENT", Value::int64(1));
#else
      .value("NSIG", Value::int64(NSIG))
      .value("SIGKILL", Value::int64(SIGKILL))
      .value("SIGSTOP", Value::int64(SIGSTOP))
      .value("SIGCONT", Value::int64(SIGCONT))
      .value("SIGCHLD", Value::int64(SIGCHLD))
      .value("SIGPIPE", Value::int64(SIGPIPE))
      .value("SIGHUP", Value::int64(SIGHUP))
      .value("SIGQUIT", Value::int64(SIGQUIT))
      .value("SIGALRM", Value::int64(SIGALRM))
      .value("SIGUSR1", Value::int64(SIGUSR1))
      .value("SIGUSR2", Value::int64(SIGUSR2));
#endif
}

} // namespace

void register_signal_module(Runtime& runtime) {
  auto* state = new SignalState();
  runtime.register_native_package_cleanup(state, [](void* data) { delete static_cast<SignalState*>(data); });

  NativeModuleBuilder private_module(runtime, "_signal");
  fill_signal_module(runtime, private_module, state);
  runtime.register_module("_signal", private_module.finish());
}

} // namespace xlang3
