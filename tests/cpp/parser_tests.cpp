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

int main() {
  xlang3::test::CaseResult result;
  const char* source =
      "def main():\n"
      "    x = 1 + 2 * 3\n"
      "    if x > 5:\n"
      "        print(x)\n"
      "    else:\n"
      "        print(0)\n"
      "    for item in range(3):\n"
      "        print(item)\n"
      "    print([item + 1 for item in range(2)])\n"
      "    print([item for item in [1, 2, 3] if item > 1][0])\n"
      "    d = {\"a\": 1}\n"
      "    d[\"b\"] = 2\n"
      "    print({1, 2, 2})\n"
      "\n"
      "main()\n";

  auto parsed = xlang3::parse_source(source);
  xlang3::test::expect_true(result, parsed.errors.empty(), "parser should accept core function/if syntax");
  xlang3::test::expect_true(result, parsed.module.body.size() == 2, "module should contain def and call");
  return xlang3::test::finish(result);
}
