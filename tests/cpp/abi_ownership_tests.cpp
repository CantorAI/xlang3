#include "xlang3/xlang3.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/value.h"
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

static void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

int main() {
  try {
    for (uint64_t number : {uint64_t{0}, uint64_t{42},
        static_cast<uint64_t>(INT64_MAX), static_cast<uint64_t>(INT64_MAX) + 1,
        std::numeric_limits<uint64_t>::max()}) {
      std::string error;
      auto imported = xlang3::from_c_value(x3_value_uint64(number), error);
      require(error.empty(), "uint64 ABI import failed");
      if (number <= static_cast<uint64_t>(INT64_MAX)) {
        require(imported.tag == xlang3::ValueTag::Int64 && imported.as.i64 == static_cast<int64_t>(number),
            "small uint64 import left fast int64 path");
        auto exported = xlang3::to_c_value(imported);
        require(exported.tag == X3_TAG_INT64 && exported.as.i64 == static_cast<int64_t>(number),
            "small integer export changed representation");
      } else {
        require(xlang3::value_as_bigint(imported) != nullptr, "large uint64 did not become bigint");
        const auto refs = imported.as.obj->refcnt.load();
        for (int iteration = 0; iteration < 2000; ++iteration) {
          auto exported = xlang3::to_c_value(imported);
          require(exported.tag == X3_TAG_UINT64 && exported.as.u64 == number, "uint64 ABI roundtrip changed value");
          x3_value_release(exported);
          require(imported.as.obj->refcnt.load() == refs, "scalar bigint export changed object ownership");
        }
      }
    }
    std::string bigint_error;
    xlang3::Value huge;
    require(xlang3::value_int_like_shift_left(xlang3::Value::int64(1),
        xlang3::Value::int64(64), huge, bigint_error), "cannot construct huge regression integer");
    xlang3::Value negative;
    require(xlang3::value_int_like_sub(xlang3::Value::int64(0), huge, negative), "cannot construct negative bigint");
    for (const auto* integer : {&huge, &negative}) {
      const auto refs = integer->as.obj->refcnt.load();
      auto exported = xlang3::to_c_value(*integer);
      require(exported.tag == X3_TAG_OBJECT && exported.as.obj == reinterpret_cast<X3Object*>(integer->as.obj),
          "out-of-range bigint was narrowed or copied");
      require(integer->as.obj->refcnt.load() == refs + 1, "bigint object export failed to retain");
      x3_value_release(exported);
      require(integer->as.obj->refcnt.load() == refs, "bigint object export leaked reference");
    }
    auto source = xlang3::Value::string("borrowed native argument with spaces");
    const auto baseline = source.as.obj->refcnt.load();
    xlang3::Value borrowed;
    borrowed.tag = xlang3::ValueTag::Object;
    borrowed.flags = xlang3::kXlangValueBorrowedRefFlag;
    borrowed.as.obj = source.as.obj;
    for (int i = 0; i < 2000; ++i) {
      auto exported = xlang3::to_c_value(borrowed);
      require(!(exported.flags & xlang3::kXlangValueBorrowedRefFlag), "internal borrow flag escaped the ABI");
      require(source.as.obj->refcnt.load() == baseline + 1, "ABI export did not acquire ownership");
      x3_value_release(exported);
      require(source.as.obj->refcnt.load() == baseline, "ABI release consumed the VM's reference");
    }
    std::atomic<bool> passed{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t) workers.emplace_back([&, t] {
      for (int i = 0; i < 1500; ++i) {
        auto name = "abi_thread_" + std::to_string(t) + "_" + std::to_string(i % 64);
        auto a = xlang3::Value::string(name);
        auto b = xlang3::Value::string(name);
        if (a.as.obj != b.as.obj || !xlang3::string_value_is_interned(a)) passed = false;
        (void)xlang3::interned_string_count();
      }
    });
    for (auto& worker : workers) worker.join();
    require(passed, "concurrent string interning lost identity");
    std::cout << "ABI borrowed-reference and concurrent interning tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
