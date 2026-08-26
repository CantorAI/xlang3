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

#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace xlang3 {

namespace {

constexpr const char* kPopenNativeType = "subprocess.Popen";

struct PopenState {
#if defined(_WIN32)
  PROCESS_INFORMATION process{};
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stderr_read = nullptr;
#endif
  Value args;
  bool stdin_pipe = false;
  bool stdout_pipe = false;
  bool stderr_pipe = false;
  bool stderr_to_stdout = false;
  int return_code = -1;
  bool has_return_code = false;
};

struct SubprocessClasses {
  Value completed_process;
  Value called_process_error;
  Value timeout_expired;
};

void popen_cleanup(void* data) {
  auto* state = static_cast<PopenState*>(data);
  if (state == nullptr) {
    return;
  }
#if defined(_WIN32)
  if (state->stdin_write != nullptr) {
    CloseHandle(state->stdin_write);
  }
  if (state->stdout_read != nullptr) {
    CloseHandle(state->stdout_read);
  }
  if (state->stderr_read != nullptr) {
    CloseHandle(state->stderr_read);
  }
  if (state->process.hThread != nullptr) {
    CloseHandle(state->process.hThread);
  }
  if (state->process.hProcess != nullptr) {
    CloseHandle(state->process.hProcess);
  }
#endif
  delete state;
}

bool get_string_value(const Value& value, std::string& out) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  return false;
}

bool truthy_option(const Value& value) {
  if (value.tag == ValueTag::Bool) {
    return value.as.b;
  }
  if (value.tag == ValueTag::None || value.tag == ValueTag::Invalid) {
    return false;
  }
  if (value.tag == ValueTag::Int64) {
    return value.as.i64 != 0;
  }
  return true;
}

bool get_bytes_or_string_value(const Value& value, std::string& out) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_to_string(*bytes);
    return true;
  }
  return false;
}

bool kw_bool(const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name, bool fallback) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string(kwargs[i].name) == name && kwargs[i].value != nullptr) {
      return truthy_option(*kwargs[i].value);
    }
  }
  return fallback;
}

Value kw_value(const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name, Value fallback = Value::none()) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string(kwargs[i].name) == name && kwargs[i].value != nullptr) {
      return *kwargs[i].value;
    }
  }
  return fallback;
}

bool value_to_timeout_ms(const Value& value, uint32_t& timeout_ms, bool& has_timeout, std::string& error) {
  has_timeout = false;
  timeout_ms = UINT32_MAX;
  if (value.tag == ValueTag::None || value.tag == ValueTag::Invalid) {
    return true;
  }
  double seconds = 0.0;
  if (value.tag == ValueTag::Int64) {
    seconds = static_cast<double>(value.as.i64);
  } else if (value.tag == ValueTag::Double) {
    seconds = value.as.f64;
  } else {
    error = "timeout must be int, float, or None";
    return false;
  }
  if (seconds < 0.0) {
    error = "timeout must be non-negative";
    return false;
  }
  has_timeout = true;
  const double millis = seconds * 1000.0;
  timeout_ms = millis > static_cast<double>(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(millis);
  return true;
}

Value make_timeout_expired(const Value& klass, Value cmd, Value timeout, Value output, Value stderr_value) {
  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "cmd", cmd, ignored);
  object_set_attr(instance, "timeout", timeout, ignored);
  object_set_attr(instance, "output", output, ignored);
  object_set_attr(instance, "stdout", output, ignored);
  object_set_attr(instance, "stderr", stderr_value, ignored);
  object_set_attr(instance, "args", Value::tuple({cmd, timeout}), ignored);
  object_set_attr(instance, "message", Value::string("Command timed out"), ignored);
  object_set_attr(instance, "__traceback__", Value::none(), ignored);
  object_set_attr(instance, "__cause__", Value::none(), ignored);
  object_set_attr(instance, "__context__", Value::none(), ignored);
  object_set_attr(instance, "__suppress_context__", Value::boolean(false), ignored);
  return instance;
}

std::string quote_arg(const std::string& arg) {
  if (arg.find_first_of(" \t\"") == std::string::npos) {
    return arg;
  }
  std::string quoted = "\"";
  for (char ch : arg) {
    if (ch == '"') {
      quoted += '\\';
    }
    quoted += ch;
  }
  quoted += '"';
  return quoted;
}

bool build_command_line(const Value& value, std::string& out, std::string& error) {
  if (get_string_value(value, out)) {
    return true;
  }
  std::vector<Value> items;
  if (auto* list = value_as_list(value)) {
    items = list->items;
  } else if (auto* tuple = value_as_tuple(value)) {
    items = tuple->items;
  } else {
    error = "subprocess.Popen args must be str, list, or tuple";
    return false;
  }
  std::string command;
  for (const auto& item : items) {
    std::string part;
    if (!get_string_value(item, part)) {
      error = "subprocess.Popen args entries must be str";
      return false;
    }
    if (!command.empty()) {
      command += ' ';
    }
    command += quote_arg(part);
  }
  out = std::move(command);
  return true;
}

std::string apply_shell_command(bool shell, std::string command) {
#if defined(_WIN32)
  if (shell) {
    return "cmd /c " + command;
  }
#else
  if (shell) {
    return "/bin/sh -c " + quote_arg(command);
  }
#endif
  return command;
}

std::wstring widen(const std::string& text) {
#if defined(_WIN32)
  if (text.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
#else
  return std::wstring(text.begin(), text.end());
#endif
}

#if defined(_WIN32)
bool build_environment_block(const Value& value, std::vector<wchar_t>& out, std::string& error) {
  if (value.tag == ValueTag::None || value.tag == ValueTag::Invalid) {
    return true;
  }
  auto* dict = value_as_dict(value);
  if (dict == nullptr) {
    error = "subprocess.Popen env must be dict";
    return false;
  }
  std::wstring block;
  for (const auto& entry : dict->entries) {
    std::string key;
    std::string val;
    if (!get_string_value(entry.first, key) || !get_string_value(entry.second, val)) {
      error = "subprocess.Popen env keys and values must be str";
      return false;
    }
    block += widen(key);
    block += L'=';
    block += widen(val);
    block += L'\0';
  }
  block += L'\0';
  out.assign(block.begin(), block.end());
  return true;
}
#endif

Value output_value(const std::string& output, bool text_mode) {
  return text_mode ? Value::string(output) : Value::bytes(output);
}

Value make_completed_process(const Value& klass, Value args, int return_code, Value stdout_value, Value stderr_value) {
  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "args", args, ignored);
  object_set_attr(instance, "returncode", Value::int64(return_code), ignored);
  object_set_attr(instance, "stdout", stdout_value, ignored);
  object_set_attr(instance, "stderr", stderr_value, ignored);
  return instance;
}

Value make_called_process_error(const Value& klass, Value args, int return_code, Value stdout_value, Value stderr_value) {
  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "returncode", Value::int64(return_code), ignored);
  object_set_attr(instance, "cmd", args, ignored);
  object_set_attr(instance, "output", stdout_value, ignored);
  object_set_attr(instance, "stdout", stdout_value, ignored);
  object_set_attr(instance, "stderr", stderr_value, ignored);
  object_set_attr(instance, "args", Value::tuple({Value::int64(return_code), args}), ignored);
  object_set_attr(
      instance,
      "message",
      Value::string("Command returned non-zero exit status " + std::to_string(return_code)),
      ignored);
  object_set_attr(instance, "__traceback__", Value::none(), ignored);
  object_set_attr(instance, "__cause__", Value::none(), ignored);
  object_set_attr(instance, "__context__", Value::none(), ignored);
  object_set_attr(instance, "__suppress_context__", Value::boolean(false), ignored);
  return instance;
}

#if defined(_WIN32)
struct WinPipe {
  HANDLE read = nullptr;
  HANDLE write = nullptr;
};

void close_handle(HANDLE& handle) {
  if (handle != nullptr) {
    CloseHandle(handle);
    handle = nullptr;
  }
}

bool create_capture_pipe(WinPipe& pipe, std::string& error) {
  SECURITY_ATTRIBUTES attrs{};
  attrs.nLength = sizeof(attrs);
  attrs.bInheritHandle = TRUE;
  if (!CreatePipe(&pipe.read, &pipe.write, &attrs, 0)) {
    error = "subprocess pipe creation failed with Win32 error " + std::to_string(GetLastError());
    return false;
  }
  if (!SetHandleInformation(pipe.read, HANDLE_FLAG_INHERIT, 0)) {
    close_handle(pipe.read);
    close_handle(pipe.write);
    error = "subprocess pipe setup failed with Win32 error " + std::to_string(GetLastError());
    return false;
  }
  return true;
}

bool create_stdin_pipe(WinPipe& pipe, std::string& error) {
  SECURITY_ATTRIBUTES attrs{};
  attrs.nLength = sizeof(attrs);
  attrs.bInheritHandle = TRUE;
  if (!CreatePipe(&pipe.read, &pipe.write, &attrs, 0)) {
    error = "subprocess stdin pipe creation failed with Win32 error " + std::to_string(GetLastError());
    return false;
  }
  if (!SetHandleInformation(pipe.write, HANDLE_FLAG_INHERIT, 0)) {
    close_handle(pipe.read);
    close_handle(pipe.write);
    error = "subprocess stdin pipe setup failed with Win32 error " + std::to_string(GetLastError());
    return false;
  }
  return true;
}

std::string read_all_from_pipe(HANDLE pipe) {
  std::string output;
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) && read > 0) {
    output.append(buffer, buffer + read);
  }
  return output;
}

bool write_all_to_pipe(HANDLE pipe, const std::string& input, std::string& error) {
  size_t offset = 0;
  while (offset < input.size()) {
    const DWORD chunk = static_cast<DWORD>(std::min<size_t>(input.size() - offset, 4096));
    DWORD written = 0;
    if (!WriteFile(pipe, input.data() + offset, chunk, &written, nullptr)) {
      error = "subprocess stdin write failed with Win32 error " + std::to_string(GetLastError());
      return false;
    }
    offset += written;
  }
  return true;
}
#endif

bool run_process(
    const std::string& command,
    const Value& cwd_value,
    const Value& env_value,
    const Value& timeout_value,
    const Value& input_value,
    bool shell,
    bool capture_stdout,
    bool capture_stderr,
    bool merge_stderr,
    bool devnull_stdin,
    bool devnull_stdout,
    bool devnull_stderr,
    int& return_code,
    std::string& stdout_data,
    std::string& stderr_data,
    bool& timed_out,
    std::string& error) {
#if defined(_WIN32)
  timed_out = false;
  uint32_t timeout_ms = UINT32_MAX;
  bool has_timeout = false;
  if (!value_to_timeout_ms(timeout_value, timeout_ms, has_timeout, error)) {
    return false;
  }
  std::string input_data;
  const bool has_input = input_value.tag != ValueTag::None && input_value.tag != ValueTag::Invalid;
  if (has_input && !get_bytes_or_string_value(input_value, input_data)) {
    error = "subprocess.run input must be str or bytes";
    return false;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  WinPipe stdin_pipe;
  WinPipe stdout_pipe;
  WinPipe stderr_pipe;
  HANDLE devnull_read = nullptr;
  HANDLE devnull_write = nullptr;
  if ((has_input || devnull_stdin) && !create_stdin_pipe(stdin_pipe, error)) {
    return false;
  }
  if (capture_stdout && !create_capture_pipe(stdout_pipe, error)) {
    close_handle(stdin_pipe.read);
    close_handle(stdin_pipe.write);
    return false;
  }
  if (capture_stderr && !merge_stderr && !create_capture_pipe(stderr_pipe, error)) {
    close_handle(stdin_pipe.read);
    close_handle(stdin_pipe.write);
    close_handle(stdout_pipe.read);
    close_handle(stdout_pipe.write);
    return false;
  }
  if (devnull_stdin || devnull_stdout || devnull_stderr) {
    devnull_read = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    devnull_write = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  if (has_input || devnull_stdin || capture_stdout || capture_stderr || merge_stderr || devnull_stdout || devnull_stderr) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdInput = has_input ? stdin_pipe.read : (devnull_stdin ? devnull_read : GetStdHandle(STD_INPUT_HANDLE));
    startup.hStdOutput = capture_stdout ? stdout_pipe.write : (devnull_stdout ? devnull_write : GetStdHandle(STD_OUTPUT_HANDLE));
    startup.hStdError = merge_stderr ? startup.hStdOutput : (capture_stderr ? stderr_pipe.write : (devnull_stderr ? devnull_write : GetStdHandle(STD_ERROR_HANDLE)));
  }

  std::wstring command_w = widen(apply_shell_command(shell, command));
  std::wstring cwd_w;
  const wchar_t* cwd_ptr = nullptr;
  if (cwd_value.tag != ValueTag::None) {
    std::string cwd;
    if (!get_string_value(cwd_value, cwd)) {
      close_handle(stdout_pipe.read);
      close_handle(stdout_pipe.write);
      close_handle(stderr_pipe.read);
      close_handle(stderr_pipe.write);
      close_handle(stdin_pipe.read);
      close_handle(stdin_pipe.write);
      close_handle(devnull_read);
      close_handle(devnull_write);
      error = "subprocess.run cwd must be str";
      return false;
    }
    cwd_w = widen(cwd);
    cwd_ptr = cwd_w.c_str();
  }

  std::vector<wchar_t> env_block;
  if (!build_environment_block(env_value, env_block, error)) {
    close_handle(stdout_pipe.read);
    close_handle(stdout_pipe.write);
    close_handle(stderr_pipe.read);
    close_handle(stderr_pipe.write);
    close_handle(stdin_pipe.read);
    close_handle(stdin_pipe.write);
    close_handle(devnull_read);
    close_handle(devnull_write);
    return false;
  }
  wchar_t* env_ptr = env_block.empty() ? nullptr : env_block.data();

  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(
      nullptr,
      command_w.data(),
      nullptr,
      nullptr,
      TRUE,
      CREATE_UNICODE_ENVIRONMENT,
      env_ptr,
      cwd_ptr,
      &startup,
      &process);
  close_handle(stdin_pipe.read);
  close_handle(stdout_pipe.write);
  close_handle(stderr_pipe.write);
  close_handle(devnull_read);
  close_handle(devnull_write);
  if (!created) {
    close_handle(stdin_pipe.write);
    close_handle(stdout_pipe.read);
    close_handle(stderr_pipe.read);
    error = "subprocess.run failed with Win32 error " + std::to_string(GetLastError());
    return false;
  }

  std::thread stdout_reader;
  std::thread stderr_reader;
  if (capture_stdout) {
    stdout_reader = std::thread([&]() { stdout_data = read_all_from_pipe(stdout_pipe.read); });
  }
  if (capture_stderr && !merge_stderr) {
    stderr_reader = std::thread([&]() { stderr_data = read_all_from_pipe(stderr_pipe.read); });
  }
  if (has_input) {
    if (!write_all_to_pipe(stdin_pipe.write, input_data, error)) {
      close_handle(stdin_pipe.write);
      if (stdout_reader.joinable()) {
        stdout_reader.join();
      }
      if (stderr_reader.joinable()) {
        stderr_reader.join();
      }
      TerminateProcess(process.hProcess, 1);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      return false;
    }
  }
  close_handle(stdin_pipe.write);

  const DWORD wait_result = WaitForSingleObject(process.hProcess, has_timeout ? static_cast<DWORD>(timeout_ms) : INFINITE);
  if (wait_result == WAIT_TIMEOUT) {
    timed_out = true;
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, INFINITE);
  }
  DWORD code = 0;
  GetExitCodeProcess(process.hProcess, &code);
  return_code = static_cast<int>(code);
  if (stdout_reader.joinable()) {
    stdout_reader.join();
  }
  if (stderr_reader.joinable()) {
    stderr_reader.join();
  }
  close_handle(stdout_pipe.read);
  close_handle(stderr_pipe.read);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
#else
  (void)command;
  (void)cwd_value;
  (void)env_value;
  (void)timeout_value;
  (void)input_value;
  (void)shell;
  (void)capture_stdout;
  (void)capture_stderr;
  (void)merge_stderr;
  (void)devnull_stdin;
  (void)devnull_stdout;
  (void)devnull_stderr;
  (void)return_code;
  (void)stdout_data;
  (void)stderr_data;
  (void)timed_out;
  error = "subprocess.run is not implemented on this platform yet";
  return false;
#endif
}

PopenState* popen_state(const Value& self, std::string& error) {
  auto* state = static_cast<PopenState*>(instance_get_native_data(self, kPopenNativeType));
  if (state == nullptr) {
    error = "invalid subprocess.Popen object";
  }
  return state;
}

bool popen_init_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2) {
    error = "subprocess.Popen() expected args";
    return false;
  }

  std::string command;
  if (!build_command_line(args[1], command, error)) {
    return false;
  }

  Value cwd_value = Value::none();
  Value env_value = Value::none();
  Value stdout_kw = Value::none();
  Value stderr_kw = Value::none();
  Value stdin_kw = Value::none();
  bool shell = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name == "cwd" && kwargs[i].value != nullptr) {
      value_assign_fast(cwd_value, *kwargs[i].value);
    } else if (name == "env" && kwargs[i].value != nullptr) {
      value_assign_fast(env_value, *kwargs[i].value);
    } else if (name == "stdout" && kwargs[i].value != nullptr) {
      value_assign_fast(stdout_kw, *kwargs[i].value);
    } else if (name == "stderr" && kwargs[i].value != nullptr) {
      value_assign_fast(stderr_kw, *kwargs[i].value);
    } else if (name == "stdin" && kwargs[i].value != nullptr) {
      value_assign_fast(stdin_kw, *kwargs[i].value);
    } else if (name == "shell" && kwargs[i].value != nullptr) {
      shell = truthy_option(*kwargs[i].value);
    }
  }

  auto* state = new PopenState();
  value_assign_fast(state->args, args[1]);
  state->stdin_pipe = stdin_kw.tag == ValueTag::Int64 && stdin_kw.as.i64 == -1;
  state->stdout_pipe = stdout_kw.tag == ValueTag::Int64 && stdout_kw.as.i64 == -1;
  state->stderr_pipe = stderr_kw.tag == ValueTag::Int64 && stderr_kw.as.i64 == -1;
  state->stderr_to_stdout = stderr_kw.tag == ValueTag::Int64 && stderr_kw.as.i64 == -2;
#if defined(_WIN32)
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  WinPipe stdin_pipe;
  WinPipe stdout_pipe;
  WinPipe stderr_pipe;
  if (state->stdin_pipe && !create_stdin_pipe(stdin_pipe, error)) {
    delete state;
    return false;
  }
  if (state->stdout_pipe && !create_capture_pipe(stdout_pipe, error)) {
    close_handle(stdin_pipe.read);
    close_handle(stdin_pipe.write);
    delete state;
    return false;
  }
  if (state->stderr_pipe && !create_capture_pipe(stderr_pipe, error)) {
    close_handle(stdin_pipe.read);
    close_handle(stdin_pipe.write);
    close_handle(stdout_pipe.read);
    close_handle(stdout_pipe.write);
    delete state;
    return false;
  }
  if (state->stdin_pipe || state->stdout_pipe || state->stderr_pipe || state->stderr_to_stdout) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdInput = state->stdin_pipe ? stdin_pipe.read : GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = state->stdout_pipe ? stdout_pipe.write : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = state->stderr_to_stdout ? startup.hStdOutput : (state->stderr_pipe ? stderr_pipe.write : GetStdHandle(STD_ERROR_HANDLE));
  }
  std::wstring command_w = widen(apply_shell_command(shell, command));
  std::wstring cwd_w;
  const wchar_t* cwd_ptr = nullptr;
  if (cwd_value.tag != ValueTag::None) {
    std::string cwd;
    if (!get_string_value(cwd_value, cwd)) {
      close_handle(stdin_pipe.read);
      close_handle(stdin_pipe.write);
      close_handle(stdout_pipe.read);
      close_handle(stdout_pipe.write);
      close_handle(stderr_pipe.read);
      close_handle(stderr_pipe.write);
      delete state;
      error = "subprocess.Popen cwd must be str";
      return false;
    }
    cwd_w = widen(cwd);
    cwd_ptr = cwd_w.c_str();
  }
  std::vector<wchar_t> env_block;
  if (!build_environment_block(env_value, env_block, error)) {
    close_handle(stdin_pipe.read);
    close_handle(stdin_pipe.write);
    close_handle(stdout_pipe.read);
    close_handle(stdout_pipe.write);
    close_handle(stderr_pipe.read);
    close_handle(stderr_pipe.write);
    delete state;
    return false;
  }
  wchar_t* env_ptr = env_block.empty() ? nullptr : env_block.data();
  if (!CreateProcessW(
          nullptr,
          command_w.data(),
          nullptr,
          nullptr,
          TRUE,
          CREATE_UNICODE_ENVIRONMENT,
          env_ptr,
          cwd_ptr,
          &startup,
          &state->process)) {
    const DWORD code = GetLastError();
    close_handle(stdin_pipe.read);
    close_handle(stdin_pipe.write);
    close_handle(stdout_pipe.read);
    close_handle(stdout_pipe.write);
    close_handle(stderr_pipe.read);
    close_handle(stderr_pipe.write);
    delete state;
    error = "subprocess.Popen failed with Win32 error " + std::to_string(code);
    return false;
  }
  close_handle(stdin_pipe.read);
  close_handle(stdout_pipe.write);
  close_handle(stderr_pipe.write);
  state->stdin_write = stdin_pipe.write;
  state->stdout_read = stdout_pipe.read;
  state->stderr_read = stderr_pipe.read;
#else
  delete state;
  error = "subprocess.Popen is not implemented on this platform yet";
  return false;
#endif

  if (!instance_set_native_data(args[0], kPopenNativeType, state, popen_cleanup, error)) {
    popen_cleanup(state);
    return false;
  }
  object_set_attr(const_cast<Value&>(args[0]), "returncode", Value::none(), error);
  object_set_attr(const_cast<Value&>(args[0]), "args", args[1], error);
#if defined(_WIN32)
  object_set_attr(const_cast<Value&>(args[0]), "pid", Value::int64(static_cast<int64_t>(state->process.dwProcessId)), error);
#else
  object_set_attr(const_cast<Value&>(args[0]), "pid", Value::none(), error);
#endif
  value_set_none(out);
  return true;
}

bool popen_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return popen_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool popen_poll(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Popen.poll() expected no arguments";
    return false;
  }
  auto* state = popen_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
#if defined(_WIN32)
  DWORD code = 0;
  if (GetExitCodeProcess(state->process.hProcess, &code) && code != STILL_ACTIVE) {
    state->return_code = static_cast<int>(code);
    state->has_return_code = true;
    object_set_attr(const_cast<Value&>(args[0]), "returncode", Value::int64(state->return_code), error);
    value_set_int64(out, state->return_code);
    return true;
  }
#endif
  value_set_none(out);
  return true;
}

bool popen_wait(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "Popen.wait() expected optional timeout";
    return false;
  }
  auto* state = popen_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
#if defined(_WIN32)
  uint32_t timeout_ms = UINT32_MAX;
  bool has_timeout = false;
  if (argc == 2 && !value_to_timeout_ms(args[1], timeout_ms, has_timeout, error)) {
    return false;
  }
  const DWORD wait_result = WaitForSingleObject(state->process.hProcess, has_timeout ? static_cast<DWORD>(timeout_ms) : INFINITE);
  if (wait_result == WAIT_TIMEOUT) {
    auto* classes = static_cast<SubprocessClasses*>(user_data);
    runtime.set_pending_exception(make_timeout_expired(
        classes->timeout_expired,
        state->args,
        args[1],
        Value::none(),
        Value::none()));
    return false;
  }
#endif
  return popen_poll(runtime, args, 1, out, error, user_data);
}

bool popen_terminate(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Popen.terminate() expected no arguments";
    return false;
  }
  auto* state = popen_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
#if defined(_WIN32)
  TerminateProcess(state->process.hProcess, 1);
#endif
  value_set_none(out);
  return true;
}

bool popen_communicate_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "Popen.communicate() expected optional input";
    return false;
  }
  auto* state = popen_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  Value input_value = argc >= 2 ? args[1] : kw_value(kwargs, kwargc, "input");
  Value timeout_value = kw_value(kwargs, kwargc, "timeout");
#if defined(_WIN32)
  uint32_t timeout_ms = UINT32_MAX;
  bool has_timeout = false;
  if (!value_to_timeout_ms(timeout_value, timeout_ms, has_timeout, error)) {
    return false;
  }
  std::string input_data;
  if (input_value.tag != ValueTag::None && input_value.tag != ValueTag::Invalid) {
    if (!get_bytes_or_string_value(input_value, input_data)) {
      error = "Popen.communicate() input must be str or bytes";
      return false;
    }
    if (state->stdin_write == nullptr) {
      error = "Popen.communicate() input requires stdin=PIPE";
      return false;
    }
  }

  std::string stdout_data;
  std::string stderr_data;
  std::thread stdout_reader;
  std::thread stderr_reader;
  if (state->stdout_read != nullptr) {
    HANDLE read = state->stdout_read;
    state->stdout_read = nullptr;
    stdout_reader = std::thread([read, &stdout_data]() mutable {
      stdout_data = read_all_from_pipe(read);
      CloseHandle(read);
    });
  }
  if (state->stderr_read != nullptr) {
    HANDLE read = state->stderr_read;
    state->stderr_read = nullptr;
    stderr_reader = std::thread([read, &stderr_data]() mutable {
      stderr_data = read_all_from_pipe(read);
      CloseHandle(read);
    });
  }
  if (!input_data.empty() && state->stdin_write != nullptr) {
    if (!write_all_to_pipe(state->stdin_write, input_data, error)) {
      close_handle(state->stdin_write);
      if (stdout_reader.joinable()) {
        stdout_reader.join();
      }
      if (stderr_reader.joinable()) {
        stderr_reader.join();
      }
      return false;
    }
  }
  close_handle(state->stdin_write);

  const DWORD wait_result = WaitForSingleObject(state->process.hProcess, has_timeout ? static_cast<DWORD>(timeout_ms) : INFINITE);
  if (wait_result == WAIT_TIMEOUT) {
    TerminateProcess(state->process.hProcess, 1);
    WaitForSingleObject(state->process.hProcess, INFINITE);
    if (stdout_reader.joinable()) {
      stdout_reader.join();
    }
    if (stderr_reader.joinable()) {
      stderr_reader.join();
    }
    auto* classes = static_cast<SubprocessClasses*>(user_data);
    runtime.set_pending_exception(make_timeout_expired(
        classes->timeout_expired,
        state->args,
        timeout_value,
        output_value(stdout_data, false),
        output_value(stderr_data, false)));
    return false;
  }
  if (stdout_reader.joinable()) {
    stdout_reader.join();
  }
  if (stderr_reader.joinable()) {
    stderr_reader.join();
  }
  DWORD code = 0;
  GetExitCodeProcess(state->process.hProcess, &code);
  state->return_code = static_cast<int>(code);
  state->has_return_code = true;
  object_set_attr(const_cast<Value&>(args[0]), "returncode", Value::int64(state->return_code), error);
  out = Value::tuple({
      state->stdout_pipe ? Value::bytes(stdout_data) : Value::none(),
      state->stderr_pipe ? Value::bytes(stderr_data) : Value::none(),
  });
  return true;
#else
  (void)runtime;
  (void)kwargs;
  (void)kwargc;
  (void)user_data;
  error = "Popen.communicate is not implemented on this platform yet";
  return false;
#endif
}

bool popen_communicate(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return popen_communicate_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool popen_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Popen.__enter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool popen_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 4) {
    error = "Popen.__exit__() expected exc_type, exc, traceback";
    return false;
  }
  Value ignored_out;
  if (!popen_wait(runtime, args, 1, ignored_out, error, user_data)) {
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool completed_process_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 5) {
    error = "CompletedProcess() expected args, returncode, optional stdout, stderr";
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  object_set_attr(self, "args", args[1], error);
  object_set_attr(self, "returncode", args[2], error);
  object_set_attr(self, "stdout", argc >= 4 ? args[3] : Value::none(), error);
  object_set_attr(self, "stderr", argc >= 5 ? args[4] : Value::none(), error);
  value_set_none(out);
  return true;
}

bool completed_process_check_returncode(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1) {
    error = "CompletedProcess.check_returncode() expected no arguments";
    return false;
  }
  Value return_code;
  if (!object_get_attr(args[0], "returncode", return_code, error)) {
    return false;
  }
  if (return_code.tag == ValueTag::Int64 && return_code.as.i64 != 0) {
    auto* classes = static_cast<SubprocessClasses*>(user_data);
    Value cmd;
    Value stdout_value;
    Value stderr_value;
    std::string ignored;
    object_get_attr(args[0], "args", cmd, ignored);
    object_get_attr(args[0], "stdout", stdout_value, ignored);
    object_get_attr(args[0], "stderr", stderr_value, ignored);
    runtime.set_pending_exception(make_called_process_error(
        classes->called_process_error,
        cmd,
        static_cast<int>(return_code.as.i64),
        stdout_value,
        stderr_value));
    return false;
  }
  value_set_none(out);
  return true;
}

bool called_process_error_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 5) {
    error = "CalledProcessError() expected returncode, cmd, optional output, stderr";
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  Value output = argc >= 4 ? args[3] : Value::none();
  Value stderr_value = argc >= 5 ? args[4] : Value::none();
  object_set_attr(self, "returncode", args[1], error);
  object_set_attr(self, "cmd", args[2], error);
  object_set_attr(self, "output", output, error);
  object_set_attr(self, "stdout", output, error);
  object_set_attr(self, "stderr", stderr_value, error);
  value_set_none(out);
  return true;
}

bool timeout_expired_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 5) {
    error = "TimeoutExpired() expected cmd, timeout, optional output, stderr";
    return false;
  }
  Value& self = const_cast<Value&>(args[0]);
  Value output_value = argc >= 4 ? args[3] : Value::none();
  Value stderr_value = argc >= 5 ? args[4] : Value::none();
  object_set_attr(self, "cmd", args[1], error);
  object_set_attr(self, "timeout", args[2], error);
  object_set_attr(self, "output", output_value, error);
  object_set_attr(self, "stdout", output_value, error);
  object_set_attr(self, "stderr", stderr_value, error);
  value_set_none(out);
  return true;
}

bool subprocess_run_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1) {
    error = "subprocess.run() expected args";
    return false;
  }
  std::string command;
  if (!build_command_line(args[0], command, error)) {
    return false;
  }

  const bool capture_output = kw_bool(kwargs, kwargc, "capture_output", false);
  const bool text_mode = kw_bool(kwargs, kwargc, "text", kw_bool(kwargs, kwargc, "universal_newlines", false));
  const bool check = kw_bool(kwargs, kwargc, "check", false);
  const bool shell = kw_bool(kwargs, kwargc, "shell", false);
  const Value stdout_kw = kw_value(kwargs, kwargc, "stdout");
  const Value stderr_kw = kw_value(kwargs, kwargc, "stderr");
  const Value stdin_kw = kw_value(kwargs, kwargc, "stdin");
  const Value input_kw = kw_value(kwargs, kwargc, "input");
  const Value timeout_kw = kw_value(kwargs, kwargc, "timeout");
  const bool capture_stdout = capture_output || (stdout_kw.tag == ValueTag::Int64 && stdout_kw.as.i64 == -1);
  const bool capture_stderr = capture_output || (stderr_kw.tag == ValueTag::Int64 && stderr_kw.as.i64 == -1);
  const bool merge_stderr = stderr_kw.tag == ValueTag::Int64 && stderr_kw.as.i64 == -2;
  const bool devnull_stdin = stdin_kw.tag == ValueTag::Int64 && stdin_kw.as.i64 == -3;
  const bool devnull_stdout = stdout_kw.tag == ValueTag::Int64 && stdout_kw.as.i64 == -3;
  const bool devnull_stderr = stderr_kw.tag == ValueTag::Int64 && stderr_kw.as.i64 == -3;

  int return_code = 0;
  std::string stdout_data;
  std::string stderr_data;
  bool timed_out = false;
  if (!run_process(
          command,
          kw_value(kwargs, kwargc, "cwd"),
          kw_value(kwargs, kwargc, "env"),
          timeout_kw,
          input_kw,
          shell,
          capture_stdout,
          capture_stderr,
          merge_stderr,
          devnull_stdin,
          devnull_stdout,
          devnull_stderr,
          return_code,
          stdout_data,
          stderr_data,
          timed_out,
          error)) {
    return false;
  }

  Value stdout_value = capture_stdout ? output_value(stdout_data, text_mode) : Value::none();
  Value stderr_value = capture_stderr ? output_value(stderr_data, text_mode) : Value::none();
  auto* classes = static_cast<SubprocessClasses*>(user_data);
  if (timed_out) {
    runtime.set_pending_exception(make_timeout_expired(classes->timeout_expired, args[0], timeout_kw, stdout_value, stderr_value));
    return false;
  }
  if (check && return_code != 0) {
    runtime.set_pending_exception(make_called_process_error(
        classes->called_process_error,
        args[0],
        return_code,
        stdout_value,
        stderr_value));
    return false;
  }
  out = make_completed_process(classes->completed_process, args[0], return_code, stdout_value, stderr_value);
  return true;
}

bool subprocess_run(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return subprocess_run_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

Value make_popen_class(Runtime& runtime, SubprocessClasses* classes) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("subprocess.Popen.__init__", popen_init, nullptr, nullptr, nullptr, false, popen_init_kw)});
  attrs.push_back({"poll", runtime.make_native_function("subprocess.Popen.poll", popen_poll)});
  attrs.push_back({"wait", runtime.make_native_function("subprocess.Popen.wait", popen_wait, classes)});
  attrs.push_back({"terminate", runtime.make_native_function("subprocess.Popen.terminate", popen_terminate)});
  attrs.push_back({"kill", runtime.make_native_function("subprocess.Popen.kill", popen_terminate)});
  attrs.push_back({"communicate", runtime.make_native_function("subprocess.Popen.communicate", popen_communicate, classes, nullptr, nullptr, false, popen_communicate_kw)});
  attrs.push_back({"__enter__", runtime.make_native_function("subprocess.Popen.__enter__", popen_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("subprocess.Popen.__exit__", popen_exit, classes)});
  return Value::class_object("Popen", std::move(attrs));
}

Value make_completed_process_class(Runtime& runtime, SubprocessClasses* classes) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("subprocess.CompletedProcess.__init__", completed_process_init)});
  attrs.push_back(
      {"check_returncode",
       runtime.make_native_function(
           "subprocess.CompletedProcess.check_returncode",
           completed_process_check_returncode,
           classes)});
  return Value::class_object("CompletedProcess", std::move(attrs));
}

Value make_called_process_error_class(Runtime& runtime) {
  const Value* exception_base = runtime.find_builtin("Exception");
  Value base = exception_base == nullptr ? Value::none() : *exception_base;
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("subprocess.CalledProcessError.__init__", called_process_error_init)});
  return Value::class_object("CalledProcessError", std::move(attrs), std::move(base));
}

Value make_timeout_expired_class(Runtime& runtime) {
  const Value* exception_base = runtime.find_builtin("Exception");
  Value base = exception_base == nullptr ? Value::none() : *exception_base;
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("subprocess.TimeoutExpired.__init__", timeout_expired_init)});
  return Value::class_object("TimeoutExpired", std::move(attrs), std::move(base));
}

} // namespace

void register_subprocess_module(Runtime& runtime) {
  auto* classes = new SubprocessClasses();
  classes->called_process_error = make_called_process_error_class(runtime);
  classes->timeout_expired = make_timeout_expired_class(runtime);
  classes->completed_process = make_completed_process_class(runtime, classes);

  NativeModuleBuilder builder(runtime, "subprocess");
  builder.value("PIPE", Value::int64(-1))
      .value("STDOUT", Value::int64(-2))
      .value("DEVNULL", Value::int64(-3))
      .value("Popen", make_popen_class(runtime, classes))
      .value("CompletedProcess", classes->completed_process)
      .value("CalledProcessError", classes->called_process_error)
      .value("TimeoutExpired", classes->timeout_expired)
      .value(
          "run",
          runtime.make_native_function(
              "subprocess.run",
              subprocess_run,
              classes,
              [](void* data) { delete static_cast<SubprocessClasses*>(data); },
              nullptr,
              false,
              subprocess_run_kw));
  runtime.register_module("subprocess", builder.finish());
}

} // namespace xlang3
