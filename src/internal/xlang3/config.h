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
