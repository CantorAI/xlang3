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

Value make_popen_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("subprocess.Popen.__init__", popen_init, nullptr, nullptr, nullptr, false, popen_init_kw)});
  attrs.push_back({"poll", runtime.make_native_function("subprocess.Popen.poll", popen_poll)});
  attrs.push_back({"wait", runtime.make_native_function("subprocess.Popen.wait", popen_wait)});
  attrs.push_back({"terminate", runtime.make_native_function("subprocess.Popen.terminate", popen_terminate)});
  attrs.push_back({"kill", runtime.make_native_function("subprocess.Popen.kill", popen_terminate)});
  return Value::class_object("Popen", std::move(attrs));
}

} // namespace

void register_subprocess_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "subprocess");
  builder.value("PIPE", Value::int64(-1))
      .value("STDOUT", Value::int64(-2))
      .value("DEVNULL", Value::int64(-3))
      .value("Popen", make_popen_class(runtime));
  runtime.register_module("subprocess", builder.finish());
}

} // namespace xlang3
