#pragma once

#include "test_harness.h"
#include "xlang3/object_model.h"
#include "xlang3/value.h"

#include <limits>

namespace xlang3::test {

struct BigIntMultiplyReentryState {
  Value* left = nullptr;
  unsigned calls = 0;
};

inline bool bigint_multiply_reentrant_wrapper(const Value& self, const std::string& name,
                                             Value& out, std::string&) {
  if (name != "__xlang3_int_value__") return false;
  auto* state = static_cast<BigIntMultiplyReentryState*>(
      instance_get_native_data(self, "BigIntMultiplyReentry"));
  ++state->calls;
  *state->left = Value::int64(7);
  // Reuse bigint storage after destroying the former sole external owner.
  std::string error;
  Value churn = value_bigint_from_decimal("18446744073709551999", 10, error);
  out = Value::int64(129);
  return true;
}

inline void check_bigint_multiply_operand_views(CaseResult& result) {
  std::string error;
  const std::string original = "18446744073709551617";
  const std::string scaled = "2379629985508532158593";
  const std::string squared = "340282366920938463500268095579187314689";
  const auto make_big = [&] { return value_bigint_from_decimal(original, 10, error); };
  Value value = make_big();
  expect_true(result, value_int_like_mul(value, Value::int64(129), value) &&
      value_to_string(value) == scaled, "bigint result may replace sole left operand owner");
  value = make_big();
  expect_true(result, value_int_like_mul(Value::int64(129), value, value) &&
      value_to_string(value) == scaled, "bigint result may replace sole right operand owner");
  value = make_big();
  expect_true(result, value_int_like_mul(value, value, value) &&
      value_to_string(value) == squared, "bigint square may replace both operand aliases");
  value = make_big();
  Value retained = value;
  expect_true(result, value_int_like_mul(value, Value::int64(-129), value) &&
      value_to_string(value) == "-" + scaled && value_to_string(retained) == original,
      "multiplication must preserve separately retained input contents");
  value = make_big();
  expect_true(result, value_int_like_mul(value, Value::boolean(false), value) &&
      value.tag == ValueTag::Int64 && value.as.i64 == 0, "bigint times false compacts to zero");
  value = make_big();
  expect_true(result, value_int_like_mul(value, Value::boolean(true), value) &&
      value_to_string(value) == original, "bigint times true preserves its integer value");
  value = Value::int64(std::numeric_limits<int64_t>::min());
  expect_true(result, value_int_like_mul(value, Value::int64(-1), value) &&
      value_to_string(value) == "9223372036854775808", "INT64_MIN magnitude conversion must not overflow");
  expect_true(result, value_int_like_mul(Value::int64(-1), value, value) &&
      value.tag == ValueTag::Int64 && value.as.i64 == std::numeric_limits<int64_t>::min(),
      "negative boundary product compacts back to INT64_MIN");

  value = make_big();
  BigIntMultiplyReentryState state{&value};
  Value wrapper = Value::instance(Value::class_object("BigIntMultiplyWrapper", {}));
  expect_true(result, instance_set_native_data(wrapper, "BigIntMultiplyReentry", &state, nullptr, error),
      "reentrant integer wrapper initializes");
  value_as_instance(wrapper)->native_get_attr = bigint_multiply_reentrant_wrapper;
  Value product;
  expect_true(result, value_int_like_mul(value, wrapper, product) &&
      value_to_string(product) == scaled && value.as.i64 == 7 && state.calls == 1,
      "wrapper fallback must retain the original left payload across right-operand reentry");
}

} // namespace xlang3::test
