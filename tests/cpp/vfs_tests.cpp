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
#include "test_harness.h"

#include "embedded/device_file_system.h"
#include "xlang3/rpc/memory_file_store.h"
#include "xlang3/vfs.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

int main() {
  xlang3::test::CaseResult result;
  xlang3::rpc::MemoryFileStore ram_files;
  xlang3::rpc::MemoryFileStore flash_files;
  auto ram = ram_files.as_file_store();
  auto flash = flash_files.as_file_store();

  const std::string module_source = "answer = 42\n";
  std::string error;
  xlang3::test::expect_true(
      result,
      flash.put(
          flash.context, "/vfs_contract_module.py",
          reinterpret_cast<const uint8_t*>(module_source.data()),
          static_cast<uint32_t>(module_source.size()), error),
      "embedded file store should accept an import source");

  std::ostringstream output;
  xlang3::Runtime runtime(output);
  auto device =
      std::make_shared<xlang3::pico::DeviceFileSystem>(&ram, &flash);
  runtime.vfs().mount("/ram", device);
  runtime.vfs().mount("/flash", device);
  xlang3::test::expect_true(
      result, runtime.vfs().is_mounted_path("/ram/example.py"),
      "mounted paths should be recognized before host normalization");
  runtime.prepend_import_root("/flash");

  std::vector<std::string> entries;
  error.clear();
  xlang3::test::expect_true(
      result,
      runtime.vfs().list_dir("/flash", entries, error) &&
          entries == std::vector<std::string>{"vfs_contract_module.py"},
      "embedded and host VFS list operations should return direct children");

  const std::string source =
      "import vfs_contract_module\n"
      "with open('/ram/result.txt', 'w', newline='\\n') as stream:\n"
      "    stream.write(str(vfs_contract_module.answer))\n"
      "print(vfs_contract_module.answer)\n";
  auto parsed = xlang3::parse_source(source);
  xlang3::test::expect_true(
      result, parsed.errors.empty(), "VFS integration source should parse");
  if (parsed.errors.empty()) {
    auto lowered = xlang3::lower_to_ir(parsed.module);
    xlang3::test::expect_true(
        result, lowered.errors.empty(), "VFS integration source should lower");
    if (lowered.errors.empty()) {
      xlang3::Interpreter interpreter(runtime);
      auto module = std::make_shared<xlang3::ir::Module>(
          std::move(lowered.module));
      auto run = interpreter.run(std::move(module));
      xlang3::test::expect_true(
          result, run.errors.empty(),
          run.errors.empty() ? "VFS integration source should execute"
                             : run.errors.front());
    }
  }
  xlang3::test::expect_true(
      result, output.str() == "42\n",
      "source-backed import should execute through the mounted VFS");

  std::vector<uint8_t> contents;
  error.clear();
  xlang3::test::expect_true(
      result,
      runtime.vfs().read_file("/ram/result.txt", contents, error) &&
          std::string(contents.begin(), contents.end()) == "42",
      "builtin open should use the same mounted VFS as imports");

  error.clear();
  xlang3::test::expect_true(
      result,
      runtime.vfs().rename(
          "/ram/result.txt", "/ram/renamed.txt", false, error),
      "embedded VFS should rename files within a mount");
  contents.clear();
  error.clear();
  xlang3::test::expect_true(
      result,
      runtime.vfs().read_file("/ram/renamed.txt", contents, error) &&
          std::string(contents.begin(), contents.end()) == "42",
      "embedded rename should preserve contents");

  error.clear();
  xlang3::test::expect_true(
      result, runtime.vfs().make_dirs("/ram/package/cache", true, error),
      "implicit embedded directories should satisfy the VFS contract");
  error.clear();
  xlang3::test::expect_true(
      result,
      !runtime.vfs().rename(
          "/ram/renamed.txt", "/flash/renamed.txt", false, error) &&
          error == "cross-filesystem rename is not supported",
      "embedded VFS should reject cross-mount rename");

  return xlang3::test::finish(result);
}
