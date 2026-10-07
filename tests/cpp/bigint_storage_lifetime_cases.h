#pragma once
#include "test_harness.h"
#include "xlang3/value.h"

#include <limits>
#include <thread>
#include <vector>

namespace xlang3::test {
inline void check_bigint_storage_lifetime(CaseResult& result) {
  // SDK-facing object layout stays unchanged; opaque storage is runtime owned.
  value_bigint_destroy(nullptr);
  value_bigint_destroy(new BigIntObject());
  std::vector<Value> escaped;
  std::thread producer([&] {
    for (uint64_t i = 0; i < 128; ++i) {
      escaped.push_back(value_bigint_from_u64((uint64_t{1} << 63u) + i));
    }
  });
  producer.join();
  // The producer's TLS has gone away. Read and release its values here, then
  // churn the consumer's allocator to expose stale storage or double frees.
  for (uint64_t i = 0; i < escaped.size(); ++i) {
    Value expected = value_bigint_from_u64((uint64_t{1} << 63u) + i);
    Value equal;
    expect_true(result, value_int_like_compare("==", escaped[i], expected, equal) && equal.as.b,
        "bigint object and limb storage survive creating thread exit");
    Value retained = escaped[i];
    expect_true(result, value_int_like_sub(escaped[i], Value::int64(1), escaped[i]),
        "cross-thread bigint can replace its original owner");
    expect_true(result, value_int_like_compare("==", retained, expected, equal) && equal.as.b,
        "cross-thread retained bigint remains immutable");
    if (i == 0) {
      expect_true(result, escaped[i].tag == ValueTag::Int64 &&
          escaped[i].as.i64 == std::numeric_limits<int64_t>::max(),
          "bigint result compacts at signed 64-bit boundary");
    }
  }
  escaped.clear();
  std::string error;
  for (unsigned i = 0; i < 1000; ++i) {
    Value value = value_bigint_from_decimal("340282366920938463463374607431768211493", 10, error);
    Value retained = value;
    expect_true(result, value_int_like_add(value, Value::int64(1), value) &&
        value_to_string(value) == "340282366920938463463374607431768211494" &&
        value_to_string(retained) == "340282366920938463463374607431768211493",
        "reused storage preserves independently retained result contents");
  }
}
} // namespace xlang3::test
