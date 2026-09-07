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
#include "xlang3/module_object.h"

#include <algorithm>

namespace {

bool has_op(const xlang3::ir::Function& fn, xlang3::ir::Op op) {
  for (const auto& instr : fn.code) {
    if (instr.op == op) {
      return true;
    }
  }
  return false;
}

bool has_slot(const xlang3::ir::Module& module, const std::string& name) {
  return std::find(module.global_slots.begin(), module.global_slots.end(), name) != module.global_slots.end();
}

} // namespace

int main() {
  xlang3::test::CaseResult result;

  const std::string source =
      "x = 10\n"
      "\n"
      "def add(y):\n"
      "    return x + y\n"
      "\n"
      "print(add(5))\n";

  auto parsed = xlang3::parse_source(source);
  if (!parsed.errors.empty()) {
    result.ok = false;
    result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());
    return xlang3::test::finish(result);
  }

  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    result.ok = false;
    result.errors.insert(result.errors.end(), lowered.errors.begin(), lowered.errors.end());
    return xlang3::test::finish(result);
  }

  xlang3::test::expect_true(result, lowered.module.global_slots.size() >= 6, "module globals should have fixed slots");
  xlang3::test::expect_true(result, has_slot(lowered.module, "x"), "x should have a module slot");
  xlang3::test::expect_true(result, has_slot(lowered.module, "add"), "add should have a module slot");
  xlang3::test::expect_true(result, lowered.module.functions.size() >= 2, "lowered module should contain add and module entry");
  xlang3::test::expect_true(result, has_op(lowered.module.functions[0], xlang3::ir::Op::LoadModuleSlot),
                            "function should read module global through LoadModuleSlot");
  xlang3::test::expect_true(result, has_op(lowered.module.functions[1], xlang3::ir::Op::StoreModuleSlot),
                            "module entry should write module globals through StoreModuleSlot");

  std::string output;
  auto run = xlang3::test::run_source(source, output);
  result.errors.insert(result.errors.end(), run.errors.begin(), run.errors.end());
  result.ok = result.ok && run.ok;
  xlang3::test::expect_true(result, output == "15\n", "module slot program should run correctly");

  auto membership = xlang3::test::run_source(
      "class Container:\n"
      "    def __contains__(self, key):\n"
      "        return [key] if key == 7 else []\n"
      "c = Container()\n"
      "print(7 in c, 7 not in c, 8 in c, 8 not in c)\n", output);
  result.errors.insert(result.errors.end(), membership.errors.begin(), membership.errors.end());
  result.ok = result.ok && membership.ok;
  xlang3::test::expect_true(result, output == "True False False True\n",
      "Python __contains__ results must be coerced and negated after returning");

  auto module = xlang3::Value::module("conditional");
  std::string error;
  xlang3::Value value;
  uint32_t slot = 0;
  xlang3::module_set_attr(module, "optional", xlang3::Value(), error);
  xlang3::test::expect_true(result, !xlang3::module_get_attr(module, "optional", value, error),
      "uninitialized module slots must not be visible as attributes");
  xlang3::test::expect_true(result, !xlang3::module_find_attr_slot(module, "optional", slot, error),
      "uninitialized module slots must not enter attribute caches");
  xlang3::module_set_attr(module, "present", xlang3::Value::int64(42), error);
  std::ostringstream captured;
  xlang3::Runtime runtime(captured);
  runtime.register_module("conditional", module);
  auto target = xlang3::Value::module("target");
  xlang3::test::expect_true(result, runtime.import_star("conditional", target, error),
      "star imports must skip uninitialized module slots");
  xlang3::test::expect_true(result, !xlang3::module_get_attr(target, "optional", value, error) &&
      xlang3::module_get_attr(target, "present", value, error) && value.as.i64 == 42,
      "star imports must preserve only initialized attributes");
  xlang3::module_set_attr(module, "optional", xlang3::Value::none(), error);
  xlang3::test::expect_true(result, xlang3::module_get_attr(module, "optional", value, error) &&
      value.tag == xlang3::ValueTag::None, "an initialized None attribute must remain visible");

  return xlang3::test::finish(result);
}
