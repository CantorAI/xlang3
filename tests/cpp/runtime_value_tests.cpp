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
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/value.h"
#include "xlang3/value_hash.h"

int main() {
  xlang3::test::CaseResult result;
  std::string error;
  xlang3::Value out;

  xlang3::test::expect_true(result, xlang3::value_add(xlang3::Value::int64(20), xlang3::Value::int64(22), out, error),
                            "int64 add should succeed");
  xlang3::test::expect_true(result, out.tag == xlang3::ValueTag::Int64 && out.as.i64 == 42,
                            "int64 add should produce 42");

  xlang3::test::expect_true(result, xlang3::value_truthy(xlang3::Value::string("x")),
                            "non-empty string should be truthy");
  xlang3::test::expect_true(result, !xlang3::value_truthy(xlang3::Value::none()),
                            "None should be falsey");
  xlang3::test::expect_true(result, xlang3::value_to_string(xlang3::Value::tuple({xlang3::Value::int64(1)})) == "(1,)",
                            "single item tuple should print with trailing comma");
  xlang3::test::expect_true(result, !xlang3::value_truthy(xlang3::Value::tuple({})),
                            "empty tuple should be falsey");

  {
    xlang3::Value dict = xlang3::Value::dict({
        {xlang3::Value::string("a"), xlang3::Value::int64(1)},
        {xlang3::Value::string("b"), xlang3::Value::int64(2)},
        {xlang3::Value::string("a"), xlang3::Value::int64(3)},
    });
    xlang3::Value item;
    error.clear();
    xlang3::test::expect_true(result, xlang3::sequence_get_item(dict, xlang3::Value::string("a"), item, error),
                              "dict lookup should accept string keys");
    xlang3::test::expect_true(result, item.tag == xlang3::ValueTag::Int64 && item.as.i64 == 3,
                              "duplicate dict key should keep last value");
    error.clear();
    xlang3::test::expect_true(result, xlang3::sequence_set_item(dict, xlang3::Value::string("c"), xlang3::Value::int64(4), error),
                              "dict set item should add a new key");
    error.clear();
    xlang3::test::expect_true(result, xlang3::sequence_get_item(dict, xlang3::Value::string("c"), item, error),
                              "dict lookup should find assigned key");
    xlang3::test::expect_true(result, item.tag == xlang3::ValueTag::Int64 && item.as.i64 == 4,
                              "dict assigned value should round-trip");
    error.clear();
    xlang3::test::expect_true(result, xlang3::sequence_len(dict, item, error),
                              "dict len should succeed");
    xlang3::test::expect_true(result, item.tag == xlang3::ValueTag::Int64 && item.as.i64 == 3,
                              "dict len should count unique keys");
  }

  {
    xlang3::Value set = xlang3::Value::set({
        xlang3::Value::int64(1),
        xlang3::Value::int64(2),
        xlang3::Value::int64(2),
        xlang3::Value::int64(3),
    });
    xlang3::Value len;
    error.clear();
    xlang3::test::expect_true(result, xlang3::sequence_len(set, len, error),
                              "set len should succeed");
    xlang3::test::expect_true(result, len.tag == xlang3::ValueTag::Int64 && len.as.i64 == 3,
                              "set len should count unique values");

    xlang3::Value iter;
    error.clear();
    xlang3::test::expect_true(result, xlang3::sequence_get_iter(set, iter, error),
                              "set should be iterable");
    bool done = false;
    int64_t sum = 0;
    while (true) {
      xlang3::Value item;
      error.clear();
      xlang3::test::expect_true(result, xlang3::sequence_iter_next(iter, done, item, error),
                                "set iterator next should succeed");
      if (done) {
        break;
      }
      sum += item.as.i64;
    }
    xlang3::test::expect_true(result, sum == 6, "set iterator should yield unique values");
  }

  {
    size_t hash = 0;
    error.clear();
    xlang3::test::expect_true(result, xlang3::value_hash_key(xlang3::Value::string("key"), hash, error),
                              "string keys should be hashable");
    xlang3::test::expect_true(result, xlang3::value_key_equal(xlang3::Value::int64(3), xlang3::Value::number(3.0)),
                              "numeric key equality should match int and double values");
  }

  {
    std::ostringstream output;
    xlang3::Runtime runtime(output);
    xlang3::NativeModuleBuilder builder(runtime, "unit_native");
    builder.value("answer", xlang3::Value::int64(42));
    auto module = builder.finish();
    runtime.register_module("unit_native", module);

    xlang3::Value imported_a;
    xlang3::Value imported_b;
    error.clear();
    xlang3::test::expect_true(result, runtime.import_module("unit_native", imported_a, error),
                              "runtime should import registered native module");
    error.clear();
    xlang3::test::expect_true(result, runtime.import_module("unit_native", imported_b, error),
                              "runtime should import cached native module again");
    xlang3::test::expect_true(result, imported_a.as.obj == imported_b.as.obj,
                              "native module imports should return the cached module object");

    xlang3::Value answer;
    error.clear();
    xlang3::test::expect_true(result, xlang3::module_get_attr(imported_a, "answer", answer, error),
                              "native module builder should expose values as attrs");
    xlang3::test::expect_true(result, answer.tag == xlang3::ValueTag::Int64 && answer.as.i64 == 42,
                              "native module attr should round-trip");
  }

  return xlang3::test::finish(result);
}
