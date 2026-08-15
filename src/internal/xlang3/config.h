#pragma once

#include <filesystem>

namespace xlang3 {

struct DebugConfig {
  bool dump_ir = false;
  std::filesystem::path output_dir;
};

struct RunConfig {
  std::filesystem::path source_path;
  DebugConfig debug;
};

} // namespace xlang3
