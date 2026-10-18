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
#include "xlang3/interpreter_events.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "../thread/thread_objects.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <utility>
#include <unordered_map>
#include <vector>
#include <csignal>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <climits>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace xlang3 {

bool signal_dispatch_pending(Runtime& runtime, std::string& error);

namespace {

struct SignalState {
  std::unordered_map<int64_t, Value> handlers;
  Value default_int_handler;
  std::vector<int64_t> signals = {2, 4, 6, 8, 11, 15, 21};
  int64_t wakeup_fd = -1;
  bool warn_on_full_buffer = true;
#ifndef _WIN32
  Runtime* owner = nullptr;
  std::unordered_map<int, struct sigaction> original_actions;
#endif
};

std::atomic<SignalState*> active_signal_state{nullptr};

#ifdef _WIN32
// The console handler runs on a Windows-created thread. It can only record an
// event and wake the interpreter; Python callbacks run on the main VM thread.
std::atomic<uint32_t> pending_console_signals{0};
std::atomic<int64_t> console_wakeup_fd{-1};
std::atomic<int> console_int_mode{0};
std::atomic<int> console_break_mode{0};
std::once_flag console_handler_once;
std::atomic<bool> console_handler_installed{false};

BOOL WINAPI console_control_handler(DWORD event) {
  std::atomic<int>* mode = nullptr;
  uint32_t bit = 0;
  char signum = 0;
  if (event == CTRL_C_EVENT) {
    mode = &console_int_mode;
    bit = 1u;
    signum = 2;
  } else if (event == CTRL_BREAK_EVENT) {
    mode = &console_break_mode;
    bit = 2u;
    signum = 21;
  } else {
    return FALSE;
  }
  const int disposition = mode->load(std::memory_order_relaxed);
  if (disposition == 0) return FALSE;
  if (disposition == 1) return TRUE;  // SIG_IGN
  pending_console_signals.fetch_or(bit, std::memory_order_release);
  interpreter_set_pending_event(kInterpreterEventSignals);
  const int64_t fd = console_wakeup_fd.load(std::memory_order_relaxed);
  if (fd >= 0) (void)::send(static_cast<SOCKET>(fd), &signum, 1, 0);
  return TRUE;
}

void ensure_console_handler() {
  std::call_once(console_handler_once, [] {
    console_handler_installed.store(
        SetConsoleCtrlHandler(console_control_handler, TRUE) != 0,
        std::memory_order_release);
  });
}
#else
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
std::array<std::atomic<bool>, NSIG> pending_posix_signals{};
std::atomic<int> posix_wakeup_fd{-1};
std::atomic<bool> posix_warn_on_full_buffer{true};
std::atomic<bool> posix_wakeup_failed{false};

// The OS handler never touches Python values or SignalState lifetime. All
// callbacks and warning construction run later at a main-VM safepoint.
void posix_signal_handler(int signum) {
  const int saved_errno = errno;
  if (signum > 0 && signum < NSIG) {
    pending_posix_signals[signum].store(true, std::memory_order_release);
    interpreter_set_pending_event(kInterpreterEventSignals);
    const int fd = posix_wakeup_fd.load(std::memory_order_relaxed);
    if (fd >= 0) {
      const unsigned char byte = static_cast<unsigned char>(signum);
      if (::write(fd, &byte, 1) != 1 &&
          posix_warn_on_full_buffer.load(std::memory_order_relaxed)) {
        posix_wakeup_failed.store(true, std::memory_order_release);
      }
    }
  }
  errno = saved_errno;
}
#endif

bool raise_signal_os_error(Runtime& runtime, int number, std::string& error) {
  error = std::strerror(number);
  const Value* klass = runtime.find_builtin("OSError");
  if (klass == nullptr) {
    runtime.raise_class_error("OSError", error);
    return false;
  }
  const Value arguments[] = {Value::int64(number), Value::string(error)};
  Value exception;
  if (!runtime_call_callable(runtime, *klass, arguments, 2, exception, error)) return false;
  runtime.set_pending_exception(exception);
  return false;
}

bool warn_wakeup_fd(Runtime& runtime, std::string& error) {
  Value warnings;
  Value warn;
  const Value* warning_class = runtime.find_builtin("RuntimeWarning");
  if (!runtime.import_module("warnings", warnings, error) ||
      !module_get_attr(warnings, "warn", warn, error) || warning_class == nullptr) return false;
  Value arguments[] = {Value::string("signal wakeup fd buffer is full"), *warning_class};
  Value ignored;
  return runtime_call_callable(runtime, warn, arguments, 2, ignored, error);
}

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

Value default_handler_for(const SignalState& state, int64_t signum) {
  return signum == 2 ? state.default_int_handler : Value::int64(0);
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
  if (xlang_thread_current_ident() != xlang_thread_main_ident()) {
    error = "signal only works in main thread of the main interpreter";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto* state = signal_state(user_data);
  if (!supported_signal(*state, signum)) {
    error = "invalid signal number";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto it = state->handlers.find(signum);
  out = it == state->handlers.end() ? default_handler_for(*state, signum) : it->second;
#ifndef _WIN32
  auto* owner = active_signal_state.load(std::memory_order_acquire);
  if (owner != nullptr && owner != state) {
    error = "OS signal handlers belong to another runtime";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  struct sigaction action {};
  sigemptyset(&action.sa_mask);
  if (!(args[1].tag == ValueTag::Int64 && (args[1].as.i64 == 0 || args[1].as.i64 == 1))) {
    const Value* callable = runtime.find_builtin("callable");
    Value accepted;
    if (callable == nullptr ||
        !runtime_call_callable(runtime, *callable, &args[1], 1, accepted, error)) return false;
    if (!value_truthy(accepted)) {
      error = "signal handler must be SIG_IGN, SIG_DFL, or callable";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  action.sa_handler = args[1].tag == ValueTag::Int64 && args[1].as.i64 == 0
      ? SIG_DFL : args[1].tag == ValueTag::Int64 && args[1].as.i64 == 1
      ? SIG_IGN : posix_signal_handler;
  struct sigaction previous {};
  if (::sigaction(static_cast<int>(signum), &action, &previous) != 0)
    return raise_signal_os_error(runtime, errno, error);
  state->original_actions.emplace(static_cast<int>(signum), previous);
  active_signal_state.store(state, std::memory_order_release);
  posix_warn_on_full_buffer.store(state->warn_on_full_buffer, std::memory_order_release);
  posix_wakeup_fd.store(static_cast<int>(state->wakeup_fd), std::memory_order_release);
#endif
  state->handlers[signum] = args[1];
#ifdef _WIN32
  if (signum == 2 || signum == 21) {
    ensure_console_handler();
    if (!console_handler_installed.load(std::memory_order_acquire)) {
      error = "cannot install Windows console signal handler";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    int disposition = 0;
    if (args[1].tag == ValueTag::Int64 && args[1].as.i64 == 1)
      disposition = 1;
    else if (!(args[1].tag == ValueTag::Int64 && args[1].as.i64 == 0))
      disposition = 2;
    (signum == 2 ? console_int_mode : console_break_mode)
        .store(disposition, std::memory_order_release);
  }
#endif
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
  out = it == state->handlers.end() ? default_handler_for(*state, signum) : it->second;
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

bool invoke_signal_handler(Runtime& runtime, SignalState* state, int64_t signum,
                           Value& out, std::string& error) {
  auto it = state->handlers.find(signum);
  const Value handler = it == state->handlers.end()
      ? default_handler_for(*state, signum) : it->second;
  if (handler.tag == ValueTag::Int64 && handler.as.i64 >= 0 && handler.as.i64 <= 1) {
    value_set_none(out);
    return true;
  }
  Value call_args[2] = {Value::int64(signum), Value::none()};
  Value ignored;
  if (!runtime_call_callable(runtime, handler, call_args, 2, ignored, error))
    return false;
  value_set_none(out);
  return true;
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
    return raise_signal_os_error(runtime, EINVAL, error);
  }
#ifndef _WIN32
  // Preserve the runtime's default KeyboardInterrupt contract without changing
  // an embedding host's process-wide SIGINT disposition during module import.
  if (signum == SIGINT && state->handlers.find(signum) == state->handlers.end()) {
    return invoke_signal_handler(runtime, state, signum, out, error);
  }
  if (::raise(static_cast<int>(signum)) != 0)
    return raise_signal_os_error(runtime, errno, error);
  value_set_none(out);
  return signal_dispatch_pending(runtime, error);
#else
  if (state->wakeup_fd >= 0) {
    const char byte = static_cast<char>(signum & 0xff);
#ifdef _WIN32
    const bool written = ::send(static_cast<SOCKET>(state->wakeup_fd), &byte, 1, 0) == 1;
#else
    const bool written = ::write(static_cast<int>(state->wakeup_fd), &byte, 1) == 1;
#endif
    if (!written && state->warn_on_full_buffer) {
      if (!warn_wakeup_fd(runtime, error)) return false;
    }
  }
  return invoke_signal_handler(runtime, state, signum, out, error);
#endif
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
  if (xlang_thread_current_ident() != xlang_thread_main_ident()) {
    error = "set_wakeup_fd only works in main thread of the main interpreter";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto* state = signal_state(user_data);
#ifndef _WIN32
  auto* owner = active_signal_state.load(std::memory_order_acquire);
  if (owner != nullptr && owner != state) {
    error = "OS signal handlers belong to another runtime";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  const int64_t fd = args[0].as.i64;
  if (fd > INT_MAX) {
    error = "wakeup fd is too large";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  if (fd < -1) {
    error = "invalid wakeup fd";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (fd >= 0) {
    const int flags = ::fcntl(static_cast<int>(fd), F_GETFL);
    if (flags < 0 || (flags & O_NONBLOCK) == 0) {
      error = flags < 0 ? "invalid wakeup fd" : "wakeup fd must be non-blocking";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  }
#endif
  out = Value::int64(state->wakeup_fd);
  state->wakeup_fd = args[0].as.i64;
#ifdef _WIN32
  console_wakeup_fd.store(state->wakeup_fd, std::memory_order_release);
#endif
  state->warn_on_full_buffer = argc == 2 ? value_truthy(args[1]) : true;
#ifndef _WIN32
  if (owner == state) {
    posix_warn_on_full_buffer.store(state->warn_on_full_buffer, std::memory_order_release);
    posix_wakeup_fd.store(static_cast<int>(state->wakeup_fd), std::memory_order_release);
  }
#endif
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
      .value("default_int_handler", state->default_int_handler)
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

bool signal_events_pending() {
  return (interpreter_pending_events() & kInterpreterEventSignals) != 0;
}

bool signal_dispatch_pending(Runtime& runtime, std::string& error) {
#ifdef _WIN32
  if (xlang_thread_current_ident() != xlang_thread_main_ident()) return true;
  auto* state = active_signal_state.load(std::memory_order_acquire);
  if (state == nullptr) return true;
  // Clear before draining. A racing interrupt either enters this exchange or
  // leaves its event bit set for the next safepoint, so it cannot be lost.
  interpreter_clear_pending_event(kInterpreterEventSignals);
  const uint32_t pending = pending_console_signals.exchange(0, std::memory_order_acq_rel);
  for (const auto [bit, signum] : {std::pair{1u, 2}, std::pair{2u, 21}}) {
    if ((pending & bit) == 0) continue;
    Value ignored;
    if (!invoke_signal_handler(runtime, state, signum, ignored, error)) return false;
  }
#else
  if (xlang_thread_current_ident() != xlang_thread_main_ident()) return true;
  auto* state = active_signal_state.load(std::memory_order_acquire);
  if (state == nullptr) {
    interpreter_clear_pending_event(kInterpreterEventSignals);
    return true;
  }
  if (state->owner != &runtime) return true;
  interpreter_clear_pending_event(kInterpreterEventSignals);
  for (int signum = 1; signum < NSIG; ++signum) {
    if (!pending_posix_signals[signum].exchange(false, std::memory_order_acq_rel)) continue;
    Value ignored;
    if (!invoke_signal_handler(runtime, state, signum, ignored, error)) {
      interpreter_set_pending_event(kInterpreterEventSignals);
      return false;
    }
  }
  if (posix_wakeup_failed.exchange(false, std::memory_order_acq_rel) &&
      state->warn_on_full_buffer && !warn_wakeup_fd(runtime, error)) return false;
#endif
  return true;
}

void register_signal_module(Runtime& runtime) {
  auto* state = new SignalState();
#ifndef _WIN32
  state->owner = &runtime;
  state->signals.clear();
  state->signals.reserve(NSIG);
  sigset_t all;
  sigfillset(&all);
  for (int signum = 1; signum < NSIG; ++signum) {
    if (sigismember(&all, signum) == 1) state->signals.push_back(signum);
  }
#endif
  state->default_int_handler = runtime.make_native_function(
      "signal.default_int_handler", default_int_handler);
  runtime.register_native_package_cleanup(state, [](void* data) {
    auto* retiring = static_cast<SignalState*>(data);
#ifdef _WIN32
    SignalState* expected = retiring;
    if (active_signal_state.compare_exchange_strong(expected, nullptr)) {
      console_int_mode.store(0);
      console_break_mode.store(0);
      console_wakeup_fd.store(-1);
      pending_console_signals.store(0);
    }
#endif
#ifndef _WIN32
    SignalState* expected = retiring;
    if (active_signal_state.compare_exchange_strong(expected, nullptr)) {
      posix_wakeup_fd.store(-1, std::memory_order_release);
      for (const auto& entry : retiring->original_actions)
        (void)::sigaction(entry.first, &entry.second, nullptr);
      for (auto& pending : pending_posix_signals) pending.store(false);
      posix_wakeup_failed.store(false);
      interpreter_clear_pending_event(kInterpreterEventSignals);
    }
#endif
    delete retiring;
  });
#ifdef _WIN32
  active_signal_state.store(state, std::memory_order_release);
#endif

  NativeModuleBuilder private_module(runtime, "_signal");
  fill_signal_module(runtime, private_module, state);
  runtime.register_module("_signal", private_module.finish());
}

} // namespace xlang3
