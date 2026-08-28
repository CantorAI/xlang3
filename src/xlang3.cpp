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
#include "xlang3/dap_session.h"
#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/module_object.h"
#include "xlang3/parser.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/sema.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

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
      config.debug.output_dir = argv[++i];
      continue;
    }
    if (arg == "-X") {
      if (i + 1 >= argc) {
        std::cerr << "-X requires an option value\n";
        return false;
      }
      ++i;
      continue;
    }
    if (arg.rfind("-X", 0) == 0) {
      continue;
    }
    if (arg == "-u" || arg == "-B" || arg == "-E" || arg == "-I" || arg == "-s" || arg == "-S") {
      continue;
    }
    if (arg == "-c") {
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
    config.source_path = arg;
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

bool publish_process_sys_attrs(xlang3::Runtime& runtime, int argc, char** argv, std::string& error) {
  xlang3::Value sys;
  if (!runtime.import_module("sys", sys, error)) {
    return false;
  }
  std::filesystem::path executable = argc > 0 && argv != nullptr && argv[0] != nullptr ? std::filesystem::path(argv[0]) : std::filesystem::path();
  std::error_code ec;
  auto absolute = std::filesystem::absolute(executable, ec);
  if (!ec) {
    executable = std::move(absolute);
  }
  if (!xlang3::module_set_attr(sys, "executable", xlang3::Value::string(executable.string()), error)) {
    return false;
  }
  if (!xlang3::module_set_attr(sys, "_base_executable", xlang3::Value::string(executable.string()), error)) {
    return false;
  }
  std::vector<xlang3::Value> original_argv;
  if (argc > 0 && argv != nullptr) {
    original_argv.reserve(static_cast<size_t>(argc));
    original_argv.push_back(xlang3::Value::string(executable.string()));
    for (int i = 1; i < argc; ++i) {
      original_argv.push_back(xlang3::Value::string(argv[i] == nullptr ? "" : argv[i]));
    }
  }
  if (!xlang3::module_set_attr(sys, "orig_argv", xlang3::Value::list(std::move(original_argv)), error)) {
    return false;
  }
  const auto prefix = executable.parent_path().string();
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
  return true;
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
    module->source_file = source_file_for_run(config).string();
  } else if (config.launch_mode == xlang3::RunConfig::LaunchMode::Command) {
    module->source_file = "<string>";
  }
  trace_frontend_timing("exec-begin", run_start);
  auto result = interpreter.run(std::move(module));
  trace_frontend_timing("exec-end", run_start);
  if (!result.errors.empty()) {
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
    module->source_file = source_file_for_run(config).string();
  } else if (config.launch_mode == xlang3::RunConfig::LaunchMode::Command) {
    module->source_file = "<string>";
  }
  trace_frontend_timing("exec-begin", run_start);
  auto result = interpreter.run_module(*module, std::move(globals_module), module);
  trace_frontend_timing("exec-end", run_start);
  if (!result.errors.empty()) {
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
  (void)config;
  std::string error;
  xlang3::Value module;
  if (!runtime.import_module(config.module_name, module, error)) {
    std::cerr << "runtime: " << error << "\n";
    return false;
  }
  return true;
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

int run_repl() {
  std::cout << "XLang3 interactive shell\n";
  std::cout << "Type .exit to quit.\n";

  xlang3::RunConfig config;
  xlang3::Runtime runtime(std::cout);
  runtime.prepend_import_root(std::filesystem::current_path());
  xlang3::Interpreter interpreter(runtime);
  xlang3::Value globals_module = xlang3::Value::module("__main__");

  std::string line;
  std::vector<std::string> pending_block;
  while (true) {
    std::cout << (pending_block.empty() ? ">>> " : "... ") << std::flush;
    if (!std::getline(std::cin, line)) {
      if (!pending_block.empty()) {
        run_repl_block(pending_block, config, runtime, interpreter, globals_module);
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
      pending_block.clear();
    }
    if (line_opens_block(line)) {
      pending_block.push_back(line);
      continue;
    }
    run_repl_line(line, config, runtime, interpreter, globals_module);
  }
}

void configure_binary_stdio() {
#if defined(_WIN32)
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
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

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--dap-stdio") {
    return run_dap_stdio();
  }

  xlang3::RunConfig config;
  if (!parse_args(argc, argv, config)) {
    print_usage();
    return 2;
  }

  if (config.launch_mode == xlang3::RunConfig::LaunchMode::Repl) {
    return run_repl();
  }

  xlang3::Runtime runtime(std::cout);
  if (!config.source_path.empty()) {
    if (std::filesystem::is_directory(config.source_path)) {
      runtime.prepend_import_root(config.source_path);
    } else {
      runtime.prepend_import_root(config.source_path.parent_path());
    }
  } else {
    runtime.prepend_import_root(std::filesystem::current_path());
  }
  std::string argv_error;
  if (!publish_process_sys_attrs(runtime, argc, argv, argv_error)) {
    std::cerr << "runtime: " << argv_error << "\n";
    return 1;
  }
  if (!runtime.publish_sys_path(argv_error)) {
    std::cerr << "runtime: " << argv_error << "\n";
    return 1;
  }
  if (!runtime.set_sys_argv(config.argv, argv_error)) {
    std::cerr << "runtime: " << argv_error << "\n";
    return 1;
  }
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
    ok = run_source(buffer.str(), config, runtime, interpreter, config.debug.dump_ir);
  }
  if (config.perf_counters) {
    xlang3::xlang_perf_set_enabled(false);
    std::cerr << xlang3::xlang_perf_report();
  }
  return ok ? 0 : 1;
}
