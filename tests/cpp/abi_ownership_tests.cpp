#include "xlang3/xlang3.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/value.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

static void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

int main() {
  try {
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
