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

#include <string>
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
#endif
  int return_code = -1;
  bool has_return_code = false;
};

struct SubprocessClasses {
  Value completed_process;
  Value called_process_error;
};

void popen_cleanup(void* data) {
  auto* state = static_cast<PopenState*>(data);
  if (state == nullptr) {
    return;
  }
#if defined(_WIN32)
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

std::string read_all_from_pipe(HANDLE pipe) {
  std::string output;
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) && read > 0) {
    output.append(buffer, buffer + read);
  }
  return output;
}
#endif

bool run_process(
    const std::string& command,
    const Value& cwd_value,
    const Value& env_value,
    bool capture_stdout,
    bool capture_stderr,
    int& return_code,
    std::string& stdout_data,
    std::string& stderr_data,
    std::string& error) {
#if defined(_WIN32)
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  WinPipe stdout_pipe;
  WinPipe stderr_pipe;
  if (capture_stdout && !create_capture_pipe(stdout_pipe, error)) {
    return false;
  }
  if (capture_stderr && !create_capture_pipe(stderr_pipe, error)) {
    close_handle(stdout_pipe.read);
    close_handle(stdout_pipe.write);
    return false;
  }
  if (capture_stdout || capture_stderr) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = capture_stdout ? stdout_pipe.write : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = capture_stderr ? stderr_pipe.write : GetStdHandle(STD_ERROR_HANDLE);
  }

  std::wstring command_w = widen(command);
  std::wstring cwd_w;
  const wchar_t* cwd_ptr = nullptr;
  if (cwd_value.tag != ValueTag::None) {
    std::string cwd;
    if (!get_string_value(cwd_value, cwd)) {
      close_handle(stdout_pipe.read);
      close_handle(stdout_pipe.write);
      close_handle(stderr_pipe.read);
      close_handle(stderr_pipe.write);
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
  close_handle(stdout_pipe.write);
  close_handle(stderr_pipe.write);
  if (!created) {
    close_handle(stdout_pipe.read);
    close_handle(stderr_pipe.read);
    error = "subprocess.run failed with Win32 error " + std::to_string(GetLastError());
    return false;
  }

  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(process.hProcess, &code);
  return_code = static_cast<int>(code);
  if (capture_stdout) {
    stdout_data = read_all_from_pipe(stdout_pipe.read);
  }
  if (capture_stderr) {
    stderr_data = read_all_from_pipe(stderr_pipe.read);
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
  (void)capture_stdout;
  (void)capture_stderr;
  (void)return_code;
  (void)stdout_data;
  (void)stderr_data;
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
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name == "cwd" && kwargs[i].value != nullptr) {
      value_assign_fast(cwd_value, *kwargs[i].value);
    } else if (name == "env" && kwargs[i].value != nullptr) {
      value_assign_fast(env_value, *kwargs[i].value);
    }
  }

  auto* state = new PopenState();
#if defined(_WIN32)
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  std::wstring command_w = widen(command);
  std::wstring cwd_w;
  const wchar_t* cwd_ptr = nullptr;
  if (cwd_value.tag != ValueTag::None) {
    std::string cwd;
    if (!get_string_value(cwd_value, cwd)) {
      delete state;
      error = "subprocess.Popen cwd must be str";
      return false;
    }
    cwd_w = widen(cwd);
    cwd_ptr = cwd_w.c_str();
  }
  std::vector<wchar_t> env_block;
  if (!build_environment_block(env_value, env_block, error)) {
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
    delete state;
    error = "subprocess.Popen failed with Win32 error " + std::to_string(code);
    return false;
  }
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
  WaitForSingleObject(state->process.hProcess, INFINITE);
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
  const Value stdout_kw = kw_value(kwargs, kwargc, "stdout");
  const Value stderr_kw = kw_value(kwargs, kwargc, "stderr");
  const bool capture_stdout = capture_output || (stdout_kw.tag == ValueTag::Int64 && stdout_kw.as.i64 == -1);
  const bool capture_stderr = capture_output || (stderr_kw.tag == ValueTag::Int64 && stderr_kw.as.i64 == -1);

  int return_code = 0;
  std::string stdout_data;
  std::string stderr_data;
  if (!run_process(
          command,
          kw_value(kwargs, kwargc, "cwd"),
          kw_value(kwargs, kwargc, "env"),
          capture_stdout,
          capture_stderr,
          return_code,
          stdout_data,
          stderr_data,
          error)) {
    return false;
  }

  Value stdout_value = capture_stdout ? output_value(stdout_data, text_mode) : Value::none();
  Value stderr_value = capture_stderr ? output_value(stderr_data, text_mode) : Value::none();
  auto* classes = static_cast<SubprocessClasses*>(user_data);
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

Value make_popen_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("subprocess.Popen.__init__", popen_init, nullptr, nullptr, nullptr, false, popen_init_kw)});
  attrs.push_back({"poll", runtime.make_native_function("subprocess.Popen.poll", popen_poll)});
  attrs.push_back({"wait", runtime.make_native_function("subprocess.Popen.wait", popen_wait)});
  attrs.push_back({"terminate", runtime.make_native_function("subprocess.Popen.terminate", popen_terminate)});
  attrs.push_back({"kill", runtime.make_native_function("subprocess.Popen.kill", popen_terminate)});
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

} // namespace

void register_subprocess_module(Runtime& runtime) {
  auto* classes = new SubprocessClasses();
  classes->called_process_error = make_called_process_error_class(runtime);
  classes->completed_process = make_completed_process_class(runtime, classes);

  NativeModuleBuilder builder(runtime, "subprocess");
  builder.value("PIPE", Value::int64(-1))
      .value("STDOUT", Value::int64(-2))
      .value("DEVNULL", Value::int64(-3))
      .value("Popen", make_popen_class(runtime))
      .value("CompletedProcess", classes->completed_process)
      .value("CalledProcessError", classes->called_process_error)
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
