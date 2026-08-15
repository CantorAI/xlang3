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
#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/parser.h"
#include "xlang3/runtime.h"
#include "xlang3/sema.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

void print_usage() {
  std::cerr << "usage: xlang3 [--dump-ir] [--debug-dir <folder>] <file.py>\n";
}

bool parse_args(int argc, char** argv, xlang3::RunConfig& config) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--dump-ir") {
      config.debug.dump_ir = true;
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
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option: " << arg << "\n";
      return false;
    }
    if (!config.source_path.empty()) {
      std::cerr << "only one source file is supported\n";
      return false;
    }
    config.source_path = arg;
  }
  if (config.source_path.empty()) {
    return false;
  }
  return true;
}

std::filesystem::path default_debug_dir(const std::filesystem::path& source_path) {
  return source_path.parent_path() / ".xlang3" / "ir";
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

} // namespace

int main(int argc, char** argv) {
  xlang3::RunConfig config;
  if (!parse_args(argc, argv, config)) {
    print_usage();
    return 2;
  }

  std::ifstream file(config.source_path, std::ios::binary);
  if (!file) {
    std::cerr << "cannot open " << config.source_path.string() << "\n";
    return 2;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();

  auto parsed = xlang3::parse_source(buffer.str());
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) {
      std::cerr << "parse: " << error << "\n";
    }
    return 1;
  }

  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    for (const auto& error : lowered.errors) {
      std::cerr << "lower: " << error << "\n";
    }
    return 1;
  }

  if (config.debug.dump_ir && !dump_ir_file(config, lowered.module)) {
    return 1;
  }

  xlang3::Runtime runtime(std::cout);
  runtime.add_import_root(config.source_path.parent_path());
  xlang3::Interpreter interpreter(runtime);
  auto result = interpreter.run(lowered.module);
  if (!result.errors.empty()) {
    for (const auto& error : result.errors) {
      std::cerr << "runtime: " << error << "\n";
    }
    return 1;
  }
  return 0;
}
