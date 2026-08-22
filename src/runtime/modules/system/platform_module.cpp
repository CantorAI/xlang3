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

namespace xlang3 {

namespace {

bool return_string(const char* value, const Value*, uint32_t argc, Value& out, std::string& error, const char* name) {
  if (argc != 0) {
    error = std::string(name) + "() expected no arguments";
    return false;
  }
  out = Value::string(value);
  return true;
}

bool platform_python_implementation(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return return_string("CPython", args, argc, out, error, "platform.python_implementation");
}

bool platform_python_version(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return return_string("3.14.0", args, argc, out, error, "platform.python_version");
}

bool platform_python_version_tuple(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "platform.python_version_tuple() expected no arguments";
    return false;
  }
  out = Value::tuple({Value::string("3"), Value::string("14"), Value::string("0")});
  return true;
}

bool platform_system(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
#if defined(_WIN32)
  return return_string("Windows", args, argc, out, error, "platform.system");
#elif defined(__APPLE__)
  return return_string("Darwin", args, argc, out, error, "platform.system");
#else
  return return_string("Linux", args, argc, out, error, "platform.system");
#endif
}

bool platform_machine(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
#if defined(_M_X64) || defined(__x86_64__)
  return return_string("AMD64", args, argc, out, error, "platform.machine");
#elif defined(_M_ARM64) || defined(__aarch64__)
  return return_string("ARM64", args, argc, out, error, "platform.machine");
#elif defined(_M_IX86) || defined(__i386__)
  return return_string("x86", args, argc, out, error, "platform.machine");
#else
  return return_string("", args, argc, out, error, "platform.machine");
#endif
}

bool platform_release(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return return_string("", args, argc, out, error, "platform.release");
}

bool platform_version(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return return_string("", args, argc, out, error, "platform.version");
}

bool platform_processor(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
#if defined(_M_X64) || defined(__x86_64__)
  return return_string("AMD64", args, argc, out, error, "platform.processor");
#elif defined(_M_ARM64) || defined(__aarch64__)
  return return_string("ARM64", args, argc, out, error, "platform.processor");
#elif defined(_M_IX86) || defined(__i386__)
  return return_string("x86", args, argc, out, error, "platform.processor");
#else
  return return_string("", args, argc, out, error, "platform.processor");
#endif
}

bool platform_node(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return return_string("", args, argc, out, error, "platform.node");
}

bool platform_platform(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "platform.platform() expected at most 2 arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::string("Windows");
#elif defined(__APPLE__)
  out = Value::string("Darwin");
#else
  out = Value::string("Linux");
#endif
  return true;
}

bool platform_architecture(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "platform.architecture() expected at most 1 argument";
    return false;
  }
  out = Value::tuple({Value::string(sizeof(void*) == 8 ? "64bit" : "32bit"), Value::string("")});
  return true;
}

} // namespace

void register_platform_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "platform");
  builder.function("python_implementation", platform_python_implementation)
      .function("python_version", platform_python_version)
      .function("python_version_tuple", platform_python_version_tuple)
      .function("system", platform_system)
      .function("machine", platform_machine)
      .function("release", platform_release)
      .function("version", platform_version)
      .function("processor", platform_processor)
      .function("node", platform_node)
      .function("platform", platform_platform)
      .function("architecture", platform_architecture);
  runtime.register_module("platform", builder.finish());
}

} // namespace xlang3
