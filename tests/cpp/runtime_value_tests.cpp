#include "test_harness.h"

#include "xlang3/value.h"

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

  return xlang3::test::finish(result);
}
