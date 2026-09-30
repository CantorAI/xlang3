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

#include <string_view>

namespace xlang3 {

// Bump the third byte whenever serialized IR op encodings or marshal layouts
// change. Source caches otherwise remain structurally readable but can execute
// with the wrong instruction meanings after an enum change.
inline constexpr char kPycMagic[4] = {'O', 'X', '\x0e', '\n'};
inline constexpr std::string_view kPycMagicView{kPycMagic, sizeof(kPycMagic)};

} // namespace xlang3
