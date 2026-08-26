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
#include "xlang3/object_model.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

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
  return return_string("3.14.7", args, argc, out, error, "platform.python_version");
}

bool platform_python_build(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "platform.python_build() expected no arguments";
    return false;
  }
  out = Value::tuple({Value::string("xlang3"), Value::string("Aug 2026")});
  return true;
}

bool platform_python_compiler(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
#if defined(_MSC_VER)
  if (argc != 0) {
    error = "platform.python_compiler() expected no arguments";
    return false;
  }
  out = Value::string("MSC v." + std::to_string(_MSC_VER));
  return true;
#elif defined(__clang__)
  return return_string("Clang", args, argc, out, error, "platform.python_compiler");
#elif defined(__GNUC__)
  return return_string("GCC", args, argc, out, error, "platform.python_compiler");
#else
  return return_string("", args, argc, out, error, "platform.python_compiler");
#endif
}

bool platform_python_branch(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return return_string("xlang3", args, argc, out, error, "platform.python_branch");
}

bool platform_python_revision(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return return_string("", args, argc, out, error, "platform.python_revision");
}

bool platform_python_version_tuple(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "platform.python_version_tuple() expected no arguments";
    return false;
  }
  out = Value::tuple({Value::string("3"), Value::string("14"), Value::string("7")});
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
  if (argc != 0) {
    error = "platform.node() expected no arguments";
    return false;
  }
#if defined(_WIN32)
  const char* name = std::getenv("COMPUTERNAME");
#else
  const char* name = std::getenv("HOSTNAME");
#endif
  out = Value::string(name == nullptr ? "" : name);
  return true;
}

Value platform_node_value() {
#if defined(_WIN32)
  const char* name = std::getenv("COMPUTERNAME");
#else
  const char* name = std::getenv("HOSTNAME");
#endif
  return Value::string(name == nullptr ? "" : name);
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

Value platform_system_value() {
#if defined(_WIN32)
  return Value::string("Windows");
#elif defined(__APPLE__)
  return Value::string("Darwin");
#else
  return Value::string("Linux");
#endif
}

Value platform_machine_value() {
#if defined(_M_X64) || defined(__x86_64__)
  return Value::string("AMD64");
#elif defined(_M_ARM64) || defined(__aarch64__)
  return Value::string("ARM64");
#elif defined(_M_IX86) || defined(__i386__)
  return Value::string("x86");
#else
  return Value::string("");
#endif
}

bool platform_uname(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "platform.uname() expected no arguments";
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("platform")});
  attrs.push_back({"system", platform_system_value()});
  attrs.push_back({"node", platform_node_value()});
  attrs.push_back({"release", Value::string("")});
  attrs.push_back({"version", Value::string("")});
  attrs.push_back({"machine", platform_machine_value()});
  attrs.push_back({"processor", platform_machine_value()});
  Value instance = Value::instance(Value::class_object("uname_result", std::move(attrs)));
  std::string ignored;
  object_set_attr(
      instance,
      "_tuple",
      Value::tuple({
          platform_system_value(),
          platform_node_value(),
          Value::string(""),
          Value::string(""),
          platform_machine_value(),
          platform_machine_value(),
      }),
      ignored);
  out = std::move(instance);
  return true;
}

bool platform_libc_ver(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "platform.libc_ver() expected optional executable/lib/version";
    return false;
  }
#if defined(_WIN32)
  out = Value::tuple({Value::string(""), Value::string("")});
#elif defined(__GLIBC__)
  out = Value::tuple({Value::string("glibc"), Value::string("")});
#else
  out = Value::tuple({Value::string(""), Value::string("")});
#endif
  return true;
}

bool platform_win32_ver(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 4) {
    error = "platform.win32_ver() expected optional release/version/csd/ptype";
    return false;
  }
#if defined(_WIN32)
  out = Value::tuple({
      Value::string(""),
      Value::string(""),
      Value::string(""),
      Value::string(""),
  });
#else
  out = Value::tuple({
      Value::string(""),
      Value::string(""),
      Value::string(""),
      Value::string(""),
  });
#endif
  return true;
}

bool platform_mac_ver(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "platform.mac_ver() expected optional release";
    return false;
  }
#if defined(__APPLE__)
  out = Value::tuple({Value::string(""), Value::tuple({Value::string(""), Value::string(""), Value::string("")}), platform_machine_value()});
#else
  out = Value::tuple({Value::string(""), Value::tuple({Value::string(""), Value::string(""), Value::string("")}), Value::string("")});
#endif
  return true;
}

bool platform_java_ver(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 4) {
    error = "platform.java_ver() expected optional release/vendor/vminfo/osinfo";
    return false;
  }
  out = Value::tuple({
      Value::string(""),
      Value::string(""),
      Value::tuple({Value::string(""), Value::string(""), Value::string("")}),
      Value::tuple({Value::string(""), Value::string(""), Value::string("")}),
  });
  return true;
}

bool platform_system_alias(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "platform.system_alias() expected system, release, version";
    return false;
  }
  auto* system = value_as_string(args[0]);
  auto* release = value_as_string(args[1]);
  auto* version = value_as_string(args[2]);
  if (system == nullptr || release == nullptr || version == nullptr) {
    error = "platform.system_alias() arguments must be str";
    return false;
  }
  std::string system_text = string_object_to_string(*system);
  std::string release_text = string_object_to_string(*release);
  const std::string version_text = string_object_to_string(*version);
  if (system_text == "SunOS") {
    system_text = "Solaris";
    if (release_text.rfind("5.", 0) == 0) {
      release_text = "2." + release_text.substr(2);
    }
  }
  out = Value::tuple({Value::string(std::move(system_text)), Value::string(std::move(release_text)), Value::string(version_text)});
  return true;
}

bool platform_sys_version(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "platform._sys_version() expected no arguments";
    return false;
  }
  out = Value::tuple({
      Value::string("CPython"),
      Value::string("3.14.7"),
      Value::string("xlang3"),
      Value::string(""),
      Value::string(""),
      Value::string(""),
      Value::string(""),
  });
  return true;
}

bool platform_freedesktop_os_release(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "platform.freedesktop_os_release() expected no arguments";
    return false;
  }
#if defined(__linux__)
  out = Value::dict({
      {Value::string("NAME"), Value::string("Linux")},
      {Value::string("ID"), Value::string("linux")},
      {Value::string("PRETTY_NAME"), Value::string("Linux")},
  });
#else
  out = Value::dict({});
#endif
  return true;
}

} // namespace

void register_platform_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "platform");
  builder.function("python_implementation", platform_python_implementation)
      .function("python_version", platform_python_version)
      .function("python_build", platform_python_build)
      .function("python_compiler", platform_python_compiler)
      .function("python_branch", platform_python_branch)
      .function("python_revision", platform_python_revision)
      .function("python_version_tuple", platform_python_version_tuple)
      .function("system", platform_system)
      .function("machine", platform_machine)
      .function("release", platform_release)
      .function("version", platform_version)
      .function("processor", platform_processor)
      .function("node", platform_node)
      .function("platform", platform_platform)
      .function("architecture", platform_architecture)
      .function("uname", platform_uname)
      .function("libc_ver", platform_libc_ver)
      .function("win32_ver", platform_win32_ver)
      .function("mac_ver", platform_mac_ver)
      .function("java_ver", platform_java_ver)
      .function("system_alias", platform_system_alias)
      .function("_sys_version", platform_sys_version)
      .function("freedesktop_os_release", platform_freedesktop_os_release);
  runtime.register_module("platform", builder.finish());
}

} // namespace xlang3
