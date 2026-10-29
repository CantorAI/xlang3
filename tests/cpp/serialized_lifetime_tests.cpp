#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/mapping.h"
#include <iostream>
#include <stdexcept>

using namespace xlang3;
static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
int main() {
  try {
    Runtime runtime(std::cout);
    std::vector<Value> live_frames;
    for (int i = 0; i < 2000; ++i) {
      auto bytes = Value::bytes(std::string(1024, '*'));
      auto child = Value::list({bytes, bytes});
      auto root = Value::list({child, child});
      runtime.retain_serialized_objects({root, child, bytes});
      require(root.as.obj->refcnt.load() == 1, "acyclic frame retained by runtime");
      require(child.as.obj->refcnt.load() == 3, "aliased acyclic child retained by runtime");
      live_frames.push_back(std::move(root));
    }
    require(runtime.collect_serialized_objects() == 0, "acyclic frame history entered cycle collector");
    auto cycle = Value::list({});
    auto* list = value_as_list(cycle);
    list->items.push_back(cycle);
    auto parent = Value::list({cycle, cycle});
    runtime.retain_serialized_objects({parent, cycle});
    require(runtime.collect_serialized_objects() == 0, "live cycle was collected");
    require(value_as_list(parent)->items.size() == 2, "live aliases damaged");
    parent = Value::none();
    cycle = Value::none();
    require(runtime.collect_serialized_objects() >= 2, "unreachable cycle was not collected");
    require(runtime.collect_serialized_objects() == 0, "cycle collection failed to settle");
    require(value_as_list(live_frames.back())->items.size() == 2, "live frame was damaged");
    std::cout << "Serialized lifetime and bounded frame retention passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
