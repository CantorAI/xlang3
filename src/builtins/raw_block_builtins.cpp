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

namespace xlang3 {

namespace {

bool print_text_raw_block(
    Runtime& runtime,
    RawBlockContext& context,
    const std::string& language,
    const std::string& provider,
    const std::string& body,
    std::string& error) {
  (void)language;
  (void)provider;
  (void)context;
  (void)error;
  runtime.out() << body;
  if (!body.empty() && body.back() != '\n') {
    runtime.out() << '\n';
  }
  return true;
}

} // namespace

void register_raw_block_builtins(Runtime& runtime) {
  runtime.register_raw_block_handler("text", "print", print_text_raw_block);
}

} // namespace xlang3
