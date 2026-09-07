#pragma once
#include "xlang3/value.h"
#include <memory>
#include <unordered_map>
#include <stdexcept>

namespace xlang3::serialize {
// Native storage aliases belong to one graph operation, never to a runtime-wide
// cache. Nested serializers get independent contexts and restore their caller.
struct NativeGraphContext {
  std::unordered_map<const void*, Value> encoded_storage;
  std::unordered_map<const Object*, std::shared_ptr<void>> decoded_storage;
};
inline thread_local NativeGraphContext* native_graph_context = nullptr;
class NativeGraphScope {
  NativeGraphContext context_;
  NativeGraphContext* previous_;
public:
  NativeGraphScope() : previous_(native_graph_context) { native_graph_context = &context_; }
  ~NativeGraphScope() { native_graph_context = previous_; }
  NativeGraphScope(const NativeGraphScope&) = delete;
  NativeGraphScope& operator=(const NativeGraphScope&) = delete;
};
inline NativeGraphContext& NativeContext() {
  if (!native_graph_context) throw std::runtime_error("native codec requires a value graph session");
  return *native_graph_context;
}
}
