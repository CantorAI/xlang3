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
#include "xlang3/config.h"
#include "xlang3/builtins.h"
#include "xlang3/dap_session.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/sema.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <crtdbg.h>
#endif

namespace {

bool g_had_system_exit = false;
int g_system_exit_code = 0;
bool g_had_keyboard_interrupt = false;

int keyboard_interrupt_exit_code() {
#if defined(_WIN32)
  return static_cast<int>(0xC000013Au);
#else
  return 130;
#endif
}

std::string path_to_utf8(const std::filesystem::path& path) {
#if defined(_WIN32)
  return path.u8string();
#else
  return path.string();
#endif
}

void configure_no_popup_error_mode() {
#if defined(_WIN32)
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

bool is_system_exit_exception(const xlang3::Value& exception) {
  auto* instance = xlang3::value_as_instance(exception);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = xlang3::value_as_class(instance->klass);
  return klass != nullptr &&
         (klass->name == "SystemExit" || xlang3::class_has_builtin_base_name(klass, "SystemExit"));
}

int system_exit_code_from_exception(xlang3::Runtime& runtime, const xlang3::Value& exception) {
  xlang3::Value code;
  std::string ignored;
  if (!xlang3::object_get_attr(exception, "code", code, ignored) || code.tag == xlang3::ValueTag::None) {
    return 0;
  }
  if (code.tag == xlang3::ValueTag::Bool) {
    return code.as.b ? 1 : 0;
  }
  if (code.tag == xlang3::ValueTag::Int64) {
    return static_cast<int>(code.as.i64);
  }
  if (xlang3::value_as_bigint(code) != nullptr) {
    int64_t integer_code = 0;
    if (xlang3::value_bigint_to_i64(code, integer_code)) {
      return static_cast<int>(integer_code);
    }
    return -1;
  }
  xlang3::Value text;
  if (xlang3::builtin_str_from_value(runtime, code, text, ignored)) {
    if (auto* string = xlang3::value_as_string(text)) {
      std::string encoding = "utf-8";
      if (const char* configured = std::getenv("PYTHONIOENCODING")) {
        encoding = configured;
        if (const auto colon = encoding.find(':'); colon != std::string::npos) {
          encoding.resize(colon);
        }
      }
      xlang3::Value codecs;
      xlang3::Value encode;
      xlang3::Value encoded;
      const xlang3::Value encode_args[] = {
          text,
          xlang3::Value::string(encoding),
          xlang3::Value::string("backslashreplace"),
      };
      if (runtime.import_module("codecs", codecs, ignored) &&
          xlang3::module_get_attr(codecs, "encode", encode, ignored) &&
          xlang3::runtime_call_callable(runtime, encode, encode_args, 3, encoded, ignored)) {
        if (auto* bytes = xlang3::value_as_bytes(encoded)) {
          const auto output = xlang3::bytes_object_view(*bytes);
          std::cerr.write(output.data(), static_cast<std::streamsize>(output.size()));
          std::cerr << "\n";
          return 1;
        }
      }
      std::cerr << xlang3::string_object_to_string(*string) << "\n";
      return 1;
    }
  }
  std::cerr << xlang3::object_model_to_string(code) << "\n";
  return 1;
}

bool is_keyboard_interrupt_exception(const xlang3::Value& exception) {
  auto* instance = xlang3::value_as_instance(exception);
  auto* klass = instance == nullptr ? nullptr : xlang3::value_as_class(instance->klass);
  return klass != nullptr &&
         (klass->name == "KeyboardInterrupt" ||
          xlang3::class_has_builtin_base_name(klass, "KeyboardInterrupt"));
}

bool consume_system_exit_result(xlang3::Runtime& runtime, const xlang3::RuntimeResult& result) {
  if (!is_system_exit_exception(result.exception)) {
    return false;
  }
  g_had_system_exit = true;
  g_system_exit_code = system_exit_code_from_exception(runtime, result.exception);
  return true;
}

void print_usage() {
  std::cerr << "usage: xlang3 [--dap-stdio] [--dump-ir] [--debug-dir <folder>] [--perf-counters] "
               "[-c code | -m module | file.py] [args...]\n";
}

bool parse_args(int argc, char** argv, xlang3::RunConfig& config) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--dump-ir") {
      config.debug.dump_ir = true;
      continue;
    }
    if (arg == "--perf-counters") {
      config.perf_counters = true;
      continue;
    }
    if (arg == "--debug-dir") {
      if (i + 1 >= argc) {
        std::cerr << "--debug-dir requires a folder\n";
        return false;
      }
      config.debug.output_dir = std::filesystem::u8path(argv[++i]);
      continue;
    }
    if (arg == "-X") {
      if (i + 1 >= argc) {
        std::cerr << "-X requires an option value\n";
        return false;
      }
      const std::string option = argv[++i];
      if (option == "no_debug_ranges") {
        config.no_debug_ranges = true;
      } else if (option == "warn_default_encoding") {
        config.warn_default_encoding = true;
      }
      continue;
    }
    if (arg.rfind("-X", 0) == 0) {
      if (arg.substr(2) == "no_debug_ranges") {
        config.no_debug_ranges = true;
      } else if (arg.substr(2) == "warn_default_encoding") {
        config.warn_default_encoding = true;
      }
      continue;
    }
    if (arg == "-B") {
      config.dont_write_bytecode = true;
      continue;
    }
    if (arg == "-E") {
      config.ignore_environment = true;
      continue;
    }
    if (arg == "-I") {
      config.isolated = true;
      config.ignore_environment = true;
      config.no_user_site = true;
      continue;
    }
    if (arg == "-s") {
      config.no_user_site = true;
      continue;
    }
    if (arg == "-S") {
      config.no_site = true;
      continue;
    }
    if (arg == "-u") {
      continue;
    }
    if (arg == "-v") {
      config.verbose = true;
      continue;
    }
    if (arg == "-b" || arg == "-bb") {
      config.bytes_warning = arg == "-bb" ? 2 : (config.bytes_warning < 1 ? 1 : config.bytes_warning);
      continue;
    }
    if (arg == "-i") {
      config.launch_mode = xlang3::RunConfig::LaunchMode::Repl;
      continue;
    }
    bool combined_command_option = arg.size() >= 3 && arg.front() == '-' && arg.back() == 'c';
    if (combined_command_option) {
      for (size_t option_index = 1; option_index + 1 < arg.size(); ++option_index) {
        if (std::string_view("uBEIsS").find(arg[option_index]) == std::string_view::npos) {
          combined_command_option = false;
          break;
        }
        switch (arg[option_index]) {
        case 'B': config.dont_write_bytecode = true; break;
        case 'E': config.ignore_environment = true; break;
        case 'I':
          config.isolated = true;
          config.ignore_environment = true;
          config.no_user_site = true;
          break;
        case 's': config.no_user_site = true; break;
        case 'S': config.no_site = true; break;
        default: break;
        }
      }
    }
    if (arg == "-c" || combined_command_option) {
      if (i + 1 >= argc) {
        std::cerr << "-c requires code\n";
        return false;
      }
      config.launch_mode = xlang3::RunConfig::LaunchMode::Command;
      config.command = argv[++i];
      config.argv.push_back("-c");
      for (++i; i < argc; ++i) {
        config.argv.push_back(argv[i]);
      }
      return true;
    }
    if (arg == "-m") {
      if (i + 1 >= argc) {
        std::cerr << "-m requires a module name\n";
        return false;
      }
      config.launch_mode = xlang3::RunConfig::LaunchMode::Module;
      config.module_name = argv[++i];
      config.argv.push_back(config.module_name);
      for (++i; i < argc; ++i) {
        config.argv.push_back(argv[i]);
      }
      return true;
    }
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option: " << arg << "\n";
      return false;
    }
    if (!config.source_path.empty()) {
      std::cerr << "only one source file is supported\n";
      return false;
    }
    config.source_path = std::filesystem::u8path(arg);
    if (std::filesystem::is_directory(config.source_path)) {
      config.source_file_path = config.source_path / "__main__.py";
    } else {
      config.source_file_path = config.source_path;
    }
    config.launch_mode = xlang3::RunConfig::LaunchMode::Script;
    config.argv.push_back(arg);
    for (++i; i < argc; ++i) {
      config.argv.push_back(argv[i]);
    }
    return true;
  }
  return true;
}

std::filesystem::path running_executable_path(int argc, char** argv) {
  std::filesystem::path executable;
#if defined(_WIN32)
  std::vector<wchar_t> buffer(32768);
  const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size != 0 && size < buffer.size()) {
    executable = std::filesystem::path(buffer.data(), buffer.data() + size);
  }
#endif
  if (executable.empty() && argc > 0 && argv != nullptr && argv[0] != nullptr) {
    executable = std::filesystem::u8path(argv[0]);
  }
  std::error_code ec;
  auto absolute = std::filesystem::absolute(executable, ec);
  return ec ? executable : absolute;
}

bool load_pth_configuration(
    int argc,
    char** argv,
    xlang3::RunConfig& config,
    std::string& error) {
  const auto executable = running_executable_path(argc, argv);
  auto executable_pth = executable;
  executable_pth.replace_extension("._pth");
  auto runtime_pth = executable.parent_path() / "xlang3_runtime._pth";
  std::filesystem::path pth_file;
  std::error_code ec;
  if (std::filesystem::is_regular_file(executable_pth, ec)) {
    pth_file = std::move(executable_pth);
  } else {
    ec.clear();
    if (std::filesystem::is_regular_file(runtime_pth, ec)) {
      pth_file = std::move(runtime_pth);
    }
  }
  if (pth_file.empty()) return true;

  std::ifstream input(pth_file, std::ios::binary);
  if (!input) {
    error = "cannot read path configuration file: " + path_to_utf8(pth_file);
    return false;
  }
  config.pth_mode = true;
  config.isolated = true;
  config.ignore_environment = true;
  config.no_user_site = true;
  config.no_site = true;
  const auto base = executable.parent_path();
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') continue;
    const auto last = line.find_last_not_of(" \t");
    const std::string entry = line.substr(first, last - first + 1);
    if (entry == "import site") {
      config.no_site = false;
      continue;
    }
    if (entry.rfind("import ", 0) == 0) {
      error = "unsupported command in path configuration file: " + entry;
      return false;
    }
    std::filesystem::path path = std::filesystem::u8path(entry);
    if (path.is_relative()) path = base / path;
    config.pth_paths.push_back(std::filesystem::absolute(path, ec).lexically_normal());
    ec.clear();
  }
  return true;
}

std::filesystem::path default_debug_dir(const std::filesystem::path& source_path) {
  return source_path.parent_path() / ".xlang3" / "ir";
}

std::filesystem::path source_file_for_run(const xlang3::RunConfig& config) {
  if (!config.source_file_path.empty()) {
    return config.source_file_path;
  }
  return config.source_path;
}

bool dump_ir_file(const xlang3::RunConfig& config, const xlang3::ir::Module& module) {
  auto output_dir = config.debug.output_dir.empty() ? default_debug_dir(config.source_path) : config.debug.output_dir;
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::cerr << "debug: cannot create " << output_dir.string() << ": " << ec.message() << "\n";
    return false;
  }

  auto output_path = output_dir / (config.source_path.stem().string() + ".ir.txt");
  std::ofstream out(output_path, std::ios::binary);
  if (!out) {
    std::cerr << "debug: cannot write " << output_path.string() << "\n";
    return false;
  }
  out << xlang3::ir::dump_module(module);
  std::cerr << "debug: wrote IR " << output_path.string() << "\n";
  return true;
}

bool frontend_timings_enabled() {
  return std::getenv("XLANG3_FRONTEND_TIMINGS") != nullptr;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration<double>(elapsed).count();
}

void trace_frontend_timing(const char* phase, std::chrono::steady_clock::time_point start) {
  if (!frontend_timings_enabled()) {
    return;
  }
  std::cerr << "xlang3 frontend timing: " << phase << " " << seconds_since(start) << "s\n";
}

bool publish_process_sys_attrs(
    xlang3::Runtime& runtime,
    const xlang3::RunConfig& config,
    int argc,
    char** argv,
    std::string& error) {
  xlang3::Value sys;
  if (!runtime.import_module("sys", sys, error)) {
    return false;
  }
  std::filesystem::path executable;
#if defined(_WIN32)
  std::vector<wchar_t> executable_buffer(32768);
  const DWORD executable_size = GetModuleFileNameW(
      nullptr, executable_buffer.data(), static_cast<DWORD>(executable_buffer.size()));
  if (executable_size != 0 && executable_size < executable_buffer.size()) {
    executable = std::filesystem::path(
        executable_buffer.data(), executable_buffer.data() + executable_size);
  }
#endif
  if (executable.empty() && argc > 0 && argv != nullptr && argv[0] != nullptr) {
    executable = std::filesystem::u8path(argv[0]);
  }
  std::error_code ec;
  auto absolute = std::filesystem::absolute(executable, ec);
  if (!ec) {
    executable = std::move(absolute);
  }
  const std::string executable_utf8 = path_to_utf8(executable);
  if (!xlang3::module_set_attr(sys, "executable", xlang3::Value::string(executable_utf8), error)) {
    return false;
  }
  if (!xlang3::module_set_attr(sys, "_base_executable", xlang3::Value::string(executable_utf8), error)) {
    return false;
  }
  std::vector<xlang3::Value> original_argv;
  if (argc > 0 && argv != nullptr) {
    original_argv.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
      original_argv.push_back(xlang3::Value::string(argv[i] == nullptr ? "" : argv[i]));
    }
  }
  if (!xlang3::module_set_attr(sys, "orig_argv", xlang3::Value::list(std::move(original_argv)), error)) {
    return false;
  }
  const auto prefix = path_to_utf8(executable.parent_path());
  if (!xlang3::module_set_attr(sys, "prefix", xlang3::Value::string(prefix), error)) {
    return false;
  }
  if (!xlang3::module_set_attr(sys, "base_prefix", xlang3::Value::string(prefix), error)) {
    return false;
  }
  if (!xlang3::module_set_attr(sys, "exec_prefix", xlang3::Value::string(prefix), error)) {
    return false;
  }
  if (!xlang3::module_set_attr(sys, "base_exec_prefix", xlang3::Value::string(prefix), error)) {
    return false;
  }
  if (config.warn_default_encoding) {
    xlang3::Value flags;
    if (!xlang3::module_get_attr(sys, "flags", flags, error) ||
        !xlang3::object_set_attr(
            flags,
            "warn_default_encoding",
            xlang3::Value::int64(1),
            error)) {
      return false;
    }
  }
  xlang3::Value flags;
  if (!xlang3::module_get_attr(sys, "flags", flags, error)) {
    return false;
  }
  const std::pair<const char*, int64_t> flag_values[] = {
      {"dont_write_bytecode", config.dont_write_bytecode ? 1 : 0},
      {"no_user_site", config.no_user_site ? 1 : 0},
      {"no_site", config.no_site ? 1 : 0},
      {"ignore_environment", config.ignore_environment ? 1 : 0},
      {"isolated", config.isolated ? 1 : 0},
      {"safe_path", config.isolated ? 1 : 0},
      {"verbose", config.verbose ? 1 : 0},
      {"bytes_warning", config.bytes_warning},
  };
  for (const auto& [name, value] : flag_values) {
    if (!xlang3::object_set_attr(flags, name, xlang3::Value::int64(value), error)) {
      return false;
    }
  }
  if (!xlang3::module_set_attr(
          sys, "dont_write_bytecode", xlang3::Value::boolean(config.dont_write_bytecode), error)) {
    return false;
  }
  if (config.no_debug_ranges || config.warn_default_encoding) {
    std::vector<std::pair<xlang3::Value, xlang3::Value>> xoptions;
    if (config.no_debug_ranges) {
      xoptions.push_back({
          xlang3::Value::string("no_debug_ranges"),
          xlang3::Value::boolean(true),
      });
    }
    if (config.warn_default_encoding) {
      xoptions.push_back({
          xlang3::Value::string("warn_default_encoding"),
          xlang3::Value::boolean(true),
      });
    }
    if (!xlang3::module_set_attr(sys, "_xoptions", xlang3::Value::dict(std::move(xoptions)), error)) {
      return false;
    }
  }
  return true;
}

bool report_uncaught_exception(
    xlang3::Runtime& runtime,
    const xlang3::RuntimeResult& result,
    std::string& error) {
  if (result.exception.tag == xlang3::ValueTag::None) {
    return false;
  }
  if (is_keyboard_interrupt_exception(result.exception)) {
    g_had_keyboard_interrupt = true;
  }
  xlang3::Value pending_exception;
  runtime.take_pending_exception(pending_exception);
  runtime.clear_active_exception();
  xlang3::Value sys;
  xlang3::Value excepthook;
  xlang3::Value traceback = xlang3::Value::none();
  std::string ignored;
  if (!runtime.import_module("sys", sys, error) ||
      !xlang3::module_get_attr(sys, "excepthook", excepthook, error)) {
    return false;
  }
  xlang3::object_get_attr(result.exception, "__traceback__", traceback, ignored);
  xlang3::Value arguments[] = {
      runtime.exception_type(result.exception),
      result.exception,
      traceback,
  };
  xlang3::Value hook_result;
  return xlang3::runtime_call_callable(runtime, excepthook, arguments, 3, hook_result, error);
}

bool publish_command_sys_path(xlang3::Runtime& runtime, const xlang3::RunConfig& config, std::string& error) {
  if (!runtime.publish_sys_path(error)) {
    return false;
  }
  if (config.launch_mode != xlang3::RunConfig::LaunchMode::Command) {
    return true;
  }
  xlang3::Value sys;
  if (!runtime.import_module("sys", sys, error)) {
    return false;
  }
  const auto& roots = runtime.import_roots();
  std::vector<xlang3::Value> values;
  if (config.pth_mode) {
    values.reserve(config.pth_paths.size());
    for (const auto& path : config.pth_paths) {
      values.push_back(xlang3::Value::string(path_to_utf8(path)));
    }
    return xlang3::module_set_attr(sys, "path", xlang3::Value::list(std::move(values)), error);
  }
  values.reserve(roots.empty() ? 1 : roots.size());
  if (!config.isolated) {
    values.push_back(xlang3::Value::string(""));
  }
  const size_t first_root = config.isolated && !roots.empty() ? 1 : 0;
  for (size_t i = first_root; i < roots.size(); ++i) {
    values.push_back(xlang3::Value::string(path_to_utf8(roots[i])));
  }
  return xlang3::module_set_attr(sys, "path", xlang3::Value::list(std::move(values)), error);
}

bool register_command_source(
    xlang3::Runtime& runtime,
    const std::shared_ptr<const xlang3::ir::Module>& module,
    const std::string& source,
    const std::string& filename,
    std::string& error) {
  xlang3::Value linecache;
  if (!runtime.import_module("linecache", linecache, error)) {
    return false;
  }
  xlang3::Value register_code;
  if (!xlang3::module_get_attr(linecache, "_register_code", register_code, error)) {
    return false;
  }
  xlang3::Value arguments[] = {
      xlang3::Value::code(module, module->entry),
      xlang3::Value::string(source),
      xlang3::Value::string(filename),
  };
  xlang3::Value ignored;
  return xlang3::runtime_call_callable(runtime, register_code, arguments, 3, ignored, error);
}

bool run_source(
    const std::string& source,
    const xlang3::RunConfig& config,
    xlang3::Runtime& runtime,
    xlang3::Interpreter& interpreter,
    bool dump_ir) {
  const auto run_start = std::chrono::steady_clock::now();
  trace_frontend_timing("parse-begin", run_start);
  auto parsed = xlang3::parse_source(source);
  trace_frontend_timing("parse-end", run_start);
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) {
      std::cerr << "parse: " << error << "\n";
    }
    return false;
  }

  trace_frontend_timing("lower-begin", run_start);
  auto lowered = xlang3::lower_to_ir(parsed.module);
  trace_frontend_timing("lower-end", run_start);
  if (!lowered.errors.empty()) {
    for (const auto& error : lowered.errors) {
      std::cerr << "lower: " << error << "\n";
    }
    return false;
  }

  if (dump_ir && !dump_ir_file(config, lowered.module)) {
    return false;
  }

  auto module = std::make_shared<xlang3::ir::Module>(std::move(lowered.module));
  if (!config.source_path.empty()) {
    module->source_file = path_to_utf8(source_file_for_run(config));
  } else if (config.launch_mode == xlang3::RunConfig::LaunchMode::Command) {
    module->source_file = "<string>";
    std::string registration_error;
    if (!register_command_source(runtime, module, source, "<string>", registration_error)) {
      std::cerr << "runtime: cannot register command source: " << registration_error << "\n";
      return false;
    }
  }
  trace_frontend_timing("exec-begin", run_start);
  auto result = interpreter.run(std::move(module));
  trace_frontend_timing("exec-end", run_start);
  if (!result.errors.empty()) {
    if (consume_system_exit_result(runtime, result)) {
      return false;
    }
    std::string hook_error;
    if (report_uncaught_exception(runtime, result, hook_error)) {
      return false;
    }
    if (!hook_error.empty()) {
      std::cerr << "runtime: sys.excepthook failed: " << hook_error << "\n";
    }
    for (const auto& error : result.errors) {
      std::cerr << "runtime: " << error << "\n";
    }
    return false;
  }
  return true;
}

bool run_source_in_module(
    const std::string& source,
    const xlang3::RunConfig& config,
    xlang3::Runtime& runtime,
    xlang3::Interpreter& interpreter,
    xlang3::Value globals_module,
    const std::string& source_file,
    bool dump_ir) {
  const auto run_start = std::chrono::steady_clock::now();
  trace_frontend_timing("parse-begin", run_start);
  auto parsed = xlang3::parse_source(source);
  trace_frontend_timing("parse-end", run_start);
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) {
      std::cerr << "parse: " << error << "\n";
    }
    return false;
  }

  trace_frontend_timing("lower-begin", run_start);
  auto lowered = xlang3::lower_to_ir(parsed.module);
  trace_frontend_timing("lower-end", run_start);
  if (!lowered.errors.empty()) {
    for (const auto& error : lowered.errors) {
      std::cerr << "lower: " << error << "\n";
    }
    return false;
  }

  if (dump_ir && !dump_ir_file(config, lowered.module)) {
    return false;
  }

  auto module = std::make_shared<xlang3::ir::Module>(std::move(lowered.module));
  if (!source_file.empty()) {
    module->source_file = source_file;
  } else if (!config.source_path.empty()) {
    module->source_file = path_to_utf8(source_file_for_run(config));
  } else if (config.launch_mode == xlang3::RunConfig::LaunchMode::Command) {
    module->source_file = "<string>";
  }
  if (!source_file.empty() && source_file.front() == '<' && source_file.back() == '>') {
    std::string registration_error;
    if (!register_command_source(runtime, module, source, source_file, registration_error)) {
      std::cerr << "runtime: cannot register interactive source: " << registration_error << "\n";
      return false;
    }
  }
  trace_frontend_timing("exec-begin", run_start);
  auto result = interpreter.run_module(*module, std::move(globals_module), module);
  trace_frontend_timing("exec-end", run_start);
  if (!result.errors.empty()) {
    if (consume_system_exit_result(runtime, result)) {
      return false;
    }
    std::string hook_error;
    if (report_uncaught_exception(runtime, result, hook_error)) {
      return false;
    }
    if (!hook_error.empty()) {
      std::cerr << "runtime: sys.excepthook failed: " << hook_error << "\n";
    }
    for (const auto& error : result.errors) {
      std::cerr << "runtime: " << error << "\n";
    }
    return false;
  }
  return true;
}

bool run_module_name(
    const xlang3::RunConfig& config,
    xlang3::Runtime& runtime,
    bool dump_ir) {
  (void)dump_ir;
  std::string error;
  if (!runtime.has_registered_module("__main__")) {
    xlang3::Value main_module = xlang3::Value::module("__main__");
    xlang3::module_set_attr(main_module, "__spec__", xlang3::Value::none(), error);
    runtime.register_module("__main__", std::move(main_module));
  }
  xlang3::Value runpy;
  xlang3::Value run_module_as_main;
  if (!runtime.import_module("runpy", runpy, error) ||
      !xlang3::module_get_attr(runpy, "_run_module_as_main", run_module_as_main, error)) {
    std::cerr << "runtime: " << error << "\n";
    return false;
  }
  xlang3::Value arguments[] = {
      xlang3::Value::string(config.module_name),
      xlang3::Value::boolean(true),
  };
  xlang3::Value ignored;
  if (xlang3::runtime_call_callable(
          runtime, run_module_as_main, arguments, 2, ignored, error)) {
    return true;
  }
  xlang3::Value exception;
  if (!runtime.take_pending_exception(exception)) {
    std::cerr << "runtime: " << error << "\n";
    return false;
  }
  xlang3::RuntimeResult result;
  result.exception = exception;
  if (consume_system_exit_result(runtime, result)) {
    return false;
  }
  std::string hook_error;
  if (!report_uncaught_exception(runtime, result, hook_error)) {
    std::cerr << "runtime: " << (hook_error.empty() ? error : hook_error) << "\n";
  }
  return false;
}

bool looks_like_statement(const std::string& line) {
  static constexpr const char* prefixes[] = {
      "print", "import", "from", "def", "class", "if", "for", "while",
      "try", "with", "return", "raise", "pass", "break", "continue"};
  const auto first = line.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return true;
  }
  const std::string trimmed = line.substr(first);
  for (const char* prefix : prefixes) {
    const std::string token(prefix);
    if (trimmed == token || trimmed.rfind(token + " ", 0) == 0 || trimmed.rfind(token + "(", 0) == 0) {
      return true;
    }
  }
  const auto assign = trimmed.find('=');
  if (assign != std::string::npos) {
    const bool comparison =
        assign + 1 < trimmed.size() && trimmed[assign + 1] == '=' ||
        assign > 0 && (trimmed[assign - 1] == '!' || trimmed[assign - 1] == '<' || trimmed[assign - 1] == '>');
    return !comparison;
  }
  return false;
}

std::string repl_source_for_line(const std::string& line) {
  if (looks_like_statement(line)) {
    return line;
  }
  return "print(" + line + ")";
}

bool line_starts_with_indent(const std::string& line) {
  return !line.empty() && (line[0] == ' ' || line[0] == '\t');
}

bool line_opens_block(const std::string& line) {
  const auto last = line.find_last_not_of(" \t");
  return last != std::string::npos && line[last] == ':';
}

std::string join_repl_lines(const std::vector<std::string>& lines) {
  std::string source;
  for (const auto& entry : lines) {
    source += entry;
    source += '\n';
  }
  return source;
}

bool run_repl_line(
    const std::string& line,
    const xlang3::RunConfig& config,
    xlang3::Runtime& runtime,
    xlang3::Interpreter& interpreter,
    xlang3::Value& globals_module) {
  return run_source_in_module(repl_source_for_line(line), config, runtime, interpreter, globals_module, "<stdin>", false);
}

bool run_repl_block(
    const std::vector<std::string>& lines,
    const xlang3::RunConfig& config,
    xlang3::Runtime& runtime,
    xlang3::Interpreter& interpreter,
    xlang3::Value& globals_module) {
  return run_source_in_module(join_repl_lines(lines), config, runtime, interpreter, globals_module, "<stdin>", false);
}

int run_repl(int argc, char** argv) {
  std::cout << "XLang3 interactive shell\n";
  std::cout << "Type .exit to quit.\n";

  xlang3::RunConfig config;
  xlang3::Runtime runtime(std::cout);
  runtime.set_no_debug_ranges(config.no_debug_ranges);
  runtime.prepend_import_root(std::filesystem::current_path());
  std::string startup_error;
  if (!publish_process_sys_attrs(runtime, config, argc, argv, startup_error) ||
      !publish_command_sys_path(runtime, config, startup_error) ||
      !runtime.set_sys_argv({""}, startup_error)) {
    std::cerr << "runtime: cannot initialize interactive sys state: " << startup_error << "\n";
    return 1;
  }
  xlang3::Interpreter interpreter(runtime);
  xlang3::Value globals_module = xlang3::Value::module("__main__");
  xlang3::Value site_module;
  std::string site_error;
  if (!runtime.import_module("site", site_module, site_error)) {
    std::cerr << "runtime: cannot initialize site: " << site_error << "\n";
    return 1;
  }
  xlang3::Value builtins_module;
  if (runtime.import_module("builtins", builtins_module, site_error)) {
    for (const char* name : {"exit", "quit"}) {
      xlang3::Value value;
      if (xlang3::module_get_attr(builtins_module, name, value, site_error)) {
        runtime.register_builtin(name, std::move(value));
      }
    }
  }

  std::string line;
  std::vector<std::string> pending_block;
  while (true) {
    std::cout << (pending_block.empty() ? ">>> " : "... ") << std::flush;
    if (!std::getline(std::cin, line)) {
      if (!pending_block.empty()) {
        run_repl_block(pending_block, config, runtime, interpreter, globals_module);
        if (g_had_system_exit) return g_system_exit_code;
        if (g_had_keyboard_interrupt) return keyboard_interrupt_exit_code();
      }
      std::cout << "\n";
      return 0;
    }
    if (line == ".exit" || line == "exit" || line == "quit") {
      return 0;
    }
    if (line.empty()) {
      if (!pending_block.empty()) {
        run_repl_block(pending_block, config, runtime, interpreter, globals_module);
        if (g_had_system_exit) return g_system_exit_code;
        if (g_had_keyboard_interrupt) return keyboard_interrupt_exit_code();
        pending_block.clear();
      }
      continue;
    }
    if (!pending_block.empty()) {
      if (line_starts_with_indent(line) || line_opens_block(pending_block.back())) {
        pending_block.push_back(line);
        continue;
      }
      run_repl_block(pending_block, config, runtime, interpreter, globals_module);
      if (g_had_system_exit) return g_system_exit_code;
      if (g_had_keyboard_interrupt) return keyboard_interrupt_exit_code();
      pending_block.clear();
    }
    if (line_opens_block(line)) {
      pending_block.push_back(line);
      continue;
    }
    run_repl_line(line, config, runtime, interpreter, globals_module);
    if (g_had_system_exit) return g_system_exit_code;
    if (g_had_keyboard_interrupt) return keyboard_interrupt_exit_code();
  }
}

void configure_binary_stdio() {
#if defined(_WIN32)
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
}

void flush_dap_program_output(
    xlang3::dap::DapSession& session,
    std::ostringstream& program_output,
    size_t& emitted_size) {
  const std::string text = program_output.str();
  if (text.size() <= emitted_size) {
    return;
  }
  const std::string delta = text.substr(emitted_size);
  emitted_size = text.size();
  std::cout << xlang3::dap::make_framed_message(session.make_output_event(delta)) << std::flush;
}

int run_dap_stdio() {
  configure_binary_stdio();

  std::ostringstream program_output;
  xlang3::dap::DapSession session(program_output);
  size_t emitted_output_size = 0;
  std::string input_buffer;
  std::string error;

  char ch = 0;
  while (std::cin.get(ch)) {
    input_buffer.push_back(ch);
    auto responses = session.handle_framed_input(input_buffer, error);
    for (const auto& response : responses) {
      std::cout << response << std::flush;
      flush_dap_program_output(session, program_output, emitted_output_size);
    }
    if (!error.empty()) {
      std::cerr << "dap: " << error << "\n";
      return 1;
    }
  }
  return 0;
}

} // namespace

int xlang3_main(int argc, char** argv) {
  configure_no_popup_error_mode();
  // Keep the CRT streams byte-preserving.  Python's text stream performs
  // newline translation, while sys.stdout.buffer must leave every byte intact.
  configure_binary_stdio();

  if (argc >= 2 && std::string(argv[1]) == "--dap-stdio") {
    return run_dap_stdio();
  }

  xlang3::RunConfig config;
  if (!parse_args(argc, argv, config)) {
    print_usage();
    return 2;
  }
  std::string pth_error;
  if (!load_pth_configuration(argc, argv, config, pth_error)) {
    std::cerr << "runtime: " << pth_error << "\n";
    return 1;
  }
  if (!config.ignore_environment) {
    if (const char* no_user_site = std::getenv("PYTHONNOUSERSITE");
        no_user_site != nullptr && *no_user_site != '\0') {
      config.no_user_site = true;
    }
  }

  if (config.launch_mode == xlang3::RunConfig::LaunchMode::Repl) {
    return run_repl(argc, argv);
  }

  xlang3::Runtime runtime(std::cout);
  runtime.set_no_debug_ranges(config.no_debug_ranges);
  if (config.pth_mode) {
    runtime.replace_import_roots(config.pth_paths);
  } else if (!config.source_path.empty()) {
    if (std::filesystem::is_directory(config.source_path)) {
      runtime.prepend_import_root(config.source_path);
    } else {
      runtime.prepend_import_root(config.source_path.parent_path());
    }
  } else {
    runtime.prepend_import_root(std::filesystem::current_path());
  }
  if (!config.pth_mode && !config.ignore_environment) {
    if (const char* python_path = std::getenv("PYTHONPATH")) {
      std::string paths(python_path);
#if defined(_WIN32)
      constexpr char path_separator = ';';
#else
      constexpr char path_separator = ':';
#endif
      size_t start = 0;
      while (start <= paths.size()) {
        const size_t end = paths.find(path_separator, start);
        const std::string item = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!item.empty()) runtime.prepend_import_root(std::filesystem::u8path(item));
        if (end == std::string::npos) break;
        start = end + 1;
      }
    }
  }
  std::string argv_error;
  if (!publish_process_sys_attrs(runtime, config, argc, argv, argv_error)) {
    std::cerr << "runtime: " << argv_error << "\n";
    return 1;
  }
  if (!publish_command_sys_path(runtime, config, argv_error)) {
    std::cerr << "runtime: " << argv_error << "\n";
    return 1;
  }
  if (!runtime.set_sys_argv(config.argv, argv_error)) {
    std::cerr << "runtime: " << argv_error << "\n";
    return 1;
  }
  if (!config.no_site) {
    xlang3::Value site;
    if (!runtime.import_module("site", site, argv_error)) {
      std::cerr << "runtime: cannot initialize site: " << argv_error << "\n";
      return 1;
    }
  }
  if (config.launch_mode == xlang3::RunConfig::LaunchMode::Command &&
      !config.isolated && !config.pth_mode) {
    xlang3::Value sys;
    xlang3::Value path;
    if (runtime.import_module("sys", sys, argv_error) &&
        xlang3::module_get_attr(sys, "path", path, argv_error)) {
      if (auto* entries = xlang3::value_as_list(path); entries != nullptr && !entries->items.empty()) {
        entries->items[0] = xlang3::Value::string("");
      }
    }
  }
  // Native modules are registered as import providers during runtime startup.
  // Keep optional providers out of sys.modules until Python actually imports
  // them, matching CPython's observable startup state.
  runtime.hide_cached_module("_sre");
  xlang3::Interpreter interpreter(runtime);
  if (config.perf_counters) {
    xlang3::xlang_perf_reset();
    xlang3::xlang_perf_set_enabled(true);
  }
  bool ok = false;
  if (config.launch_mode == xlang3::RunConfig::LaunchMode::Command) {
    ok = run_source(config.command, config, runtime, interpreter, false);
  } else if (config.launch_mode == xlang3::RunConfig::LaunchMode::Module) {
    ok = run_module_name(config, runtime, config.debug.dump_ir);
  } else {
    const auto source_file_path = source_file_for_run(config);
    std::ifstream file(source_file_path, std::ios::binary);
    if (!file) {
      std::cerr << "cannot open " << source_file_path.string() << "\n";
      return 2;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string decoded_source;
    std::string decode_error;
    if (!runtime.decode_python_source(buffer.str(), decoded_source, decode_error)) {
      std::cerr << "SyntaxError: " << decode_error << "\n";
      return 1;
    }
    ok = run_source(decoded_source, config, runtime, interpreter, config.debug.dump_ir);
  }
  if (config.perf_counters) {
    xlang3::xlang_perf_set_enabled(false);
    std::cerr << xlang3::xlang_perf_report();
  }
  if (g_had_system_exit) {
    return g_system_exit_code;
  }
  if (g_had_keyboard_interrupt) {
    return keyboard_interrupt_exit_code();
  }
  return ok ? 0 : 1;
}

#if defined(_WIN32)
std::string wide_argument_to_utf8(const wchar_t* argument) {
  if (argument == nullptr || *argument == L'\0') {
    return {};
  }
  const int required = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1, nullptr, 0, nullptr, nullptr);
  if (required <= 1) {
    return {};
  }
  std::string utf8(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1, utf8.data(), required, nullptr, nullptr);
  utf8.resize(static_cast<size_t>(required - 1));
  return utf8;
}

int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> utf8_arguments;
  utf8_arguments.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    utf8_arguments.push_back(wide_argument_to_utf8(argv[i]));
  }
  std::vector<char*> argument_pointers;
  argument_pointers.reserve(utf8_arguments.size());
  for (auto& argument : utf8_arguments) {
    argument_pointers.push_back(argument.data());
  }
  return xlang3_main(argc, argument_pointers.data());
}
#else
int main(int argc, char** argv) {
  return xlang3_main(argc, argv);
}
#endif
