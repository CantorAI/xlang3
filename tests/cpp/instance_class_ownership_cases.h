#pragma once
#include "test_harness.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"

namespace xlang3::test {
inline void check_instance_class_ownership(CaseResult& result) {
  auto klass = Value::class_object("InstanceClassOwnership", {});
  // Warm the live class's container-trait cache before checking owner traffic.
  { auto warm = Value::instance(klass); }
  const auto initial_refs = klass.as.obj->refcnt.load(std::memory_order_relaxed);
  for (unsigned iteration = 0; iteration < 32; ++iteration) {
    {
      auto instance = Value::instance(klass);
      auto* object = value_as_instance(instance);
      expect_true(result, object != nullptr && value_is(object->klass, klass) &&
          (object->klass.flags & kXlangValueBorrowedRefFlag) == 0 &&
          klass.as.obj->refcnt.load(std::memory_order_relaxed) == initial_refs + 1,
          "lvalue class construction must retain exactly one lasting owner");
    }
    expect_true(result, klass.as.obj->refcnt.load(std::memory_order_relaxed) == initial_refs,
        "instance recycling must return its class owner");
  }
  {
    auto owner = klass;
    Value instance;
    struct CounterScope {
      bool previous = xlang_perf_counters().enabled.load(std::memory_order_relaxed);
      CounterScope() { xlang_perf_set_enabled(true); }
      ~CounterScope() { xlang_perf_set_enabled(previous); }
    } scope;
    const auto kind = xlang_perf_kind_index(ObjectKind::Class);
    const auto before = xlang_perf_counters().value_incref[kind].load(std::memory_order_relaxed);
    instance = Value::instance(std::move(owner));
    const auto after = xlang_perf_counters().value_incref[kind].load(std::memory_order_relaxed);
    expect_true(result, owner.tag == ValueTag::Invalid && after == before,
        "an owning class argument must transfer without a redundant retain");
    const auto* object = value_as_instance(instance);
    expect_true(result, object != nullptr && value_is(object->klass, klass) &&
        klass.as.obj->refcnt.load(std::memory_order_relaxed) == initial_refs + 1,
        "moved class construction must preserve its lasting owner");
  }
  expect_true(result, klass.as.obj->refcnt.load(std::memory_order_relaxed) == initial_refs,
      "moved instance destruction must release its class owner");
  {
    Value borrowed;
    value_borrow_assign_fast(borrowed, klass);
    auto instance = Value::instance(std::move(borrowed));
    auto* object = value_as_instance(instance);
    expect_true(result, borrowed.tag == ValueTag::Invalid && object != nullptr &&
        (object->klass.flags & kXlangValueBorrowedRefFlag) == 0 &&
        klass.as.obj->refcnt.load(std::memory_order_relaxed) == initial_refs + 1,
        "a moved borrowed class must become an instance-owned reference");
    // The originating owner may disappear while the instance is still alive.
    // Guard the dereference so a failed ownership assertion does not cause UB.
    if (object != nullptr && (object->klass.flags & kXlangValueBorrowedRefFlag) == 0) {
      value_set_invalid(klass);
      expect_true(result, value_as_class(object->klass) != nullptr &&
          value_as_class(object->klass)->name == "InstanceClassOwnership",
          "an instance must keep its class alive after the source owner dies");
    }
  }
}
} // namespace xlang3::test
