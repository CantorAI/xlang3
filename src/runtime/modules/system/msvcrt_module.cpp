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

#include "xlang3/module_object.h"
#include "xlang3/runtime.h"

#if defined(_WIN32)
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace xlang3 {

namespace {

bool get_int_arg(const Value& value, const char* name, int64_t& out, std::string& error) {
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  error = std::string(name) + " must be an integer";
  return false;
}

bool msvcrt_get_osfhandle(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "get_osfhandle() expected fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t fd_value = 0;
  if (!get_int_arg(args[0], "fd", fd_value, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const intptr_t handle = _get_osfhandle(static_cast<int>(fd_value));
  if (handle == -1) {
    error = "invalid file descriptor";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(handle));
#else
  out = Value::int64(fd_value);
#endif
  return true;
}

bool msvcrt_open_osfhandle(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "open_osfhandle() expected handle and optional flags";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t handle_value = 0;
  int64_t flags_value = 0;
  if (!get_int_arg(args[0], "handle", handle_value, error) ||
      (argc == 2 && !get_int_arg(args[1], "flags", flags_value, error))) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int fd = _open_osfhandle(static_cast<intptr_t>(handle_value), static_cast<int>(flags_value));
  if (fd == -1) {
    error = "invalid OS handle";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::int64(fd);
#else
  out = Value::int64(handle_value);
#endif
  return true;
}

bool msvcrt_setmode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "setmode() expected fd and mode";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t fd_value = 0;
  int64_t mode_value = 0;
  if (!get_int_arg(args[0], "fd", fd_value, error) || !get_int_arg(args[1], "mode", mode_value, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int previous = _setmode(static_cast<int>(fd_value), static_cast<int>(mode_value));
  if (previous == -1) {
    error = "invalid file descriptor";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::int64(previous);
#else
  out = Value::int64(0);
#endif
  return true;
}

bool msvcrt_kbhit(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "kbhit() takes no arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::boolean(_kbhit() != 0);
#else
  out = Value::boolean(false);
#endif
  return true;
}

bool msvcrt_getch(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "getch() takes no arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::bytes(std::string(1, static_cast<char>(_getch())));
#else
  out = Value::bytes(std::string());
#endif
  return true;
}

bool msvcrt_getwch(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "getwch() takes no arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::string(std::string(1, static_cast<char>(_getwch())));
#else
  out = Value::string(std::string());
#endif
  return true;
}

bool msvcrt_putwch(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "putwch() expected character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string text = value_to_string(args[0]);
#if defined(_WIN32)
  if (!text.empty()) {
    _putwch(static_cast<wchar_t>(static_cast<unsigned char>(text.front())));
  }
#endif
  value_set_none(out);
  return true;
}

} // namespace

void register_msvcrt_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "msvcrt");
  builder.function("get_osfhandle", msvcrt_get_osfhandle)
      .function("open_osfhandle", msvcrt_open_osfhandle)
      .function("setmode", msvcrt_setmode)
      .function("kbhit", msvcrt_kbhit)
      .function("getch", msvcrt_getch)
      .function("getwch", msvcrt_getwch)
      .function("putwch", msvcrt_putwch);
  runtime.register_module("msvcrt", builder.finish());
}

} // namespace xlang3
