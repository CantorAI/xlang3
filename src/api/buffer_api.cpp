#include "xlang3/abi/xbuffer.h"
#include "xlang3/runtime.h"
#include "xlang3/value.h"
#include "xlang3/c_api_bridge.h"
#include "runtime/modules/thread/runtime_lock.h"
#include <memory>

struct X3Buffer {
  xlang3::Value owner;
  std::shared_ptr<xlang3::NativeBufferStorage> external;
  xlang3::ByteArrayObject* bytearray = nullptr;
  std::string format = "B";
  ~X3Buffer() { if (bytearray) --bytearray->buffer_exports; }
};

extern "C" X3Status x3_buffer_acquire(X3Runtime* runtime, X3Value value,
    int32_t writable, X3Buffer** result, X3BufferInfo* info) {
  xlang3::XlangRuntimeExecutionGuard guard;
  auto* rt = reinterpret_cast<xlang3::Runtime*>(runtime);
  if (result) *result = nullptr;
  if (!rt || !result || !info) return X3_STATUS_ERROR;
  *info = {};
  try {
    std::string error;
    auto source = xlang3::from_c_value(value, error);
    if (!error.empty()) throw std::runtime_error(error);
    auto handle = std::make_unique<X3Buffer>();
    auto root = source;
    bool readonly = true;
    std::string_view storage;
    if (auto* view = xlang3::value_as_memoryview(source)) {
      if (view->released) throw std::runtime_error("memoryview is released");
      storage = xlang3::memoryview_object_view(*view);
      readonly = view->readonly;
      handle->format = view->format;
      while (auto* parent = xlang3::value_as_memoryview(root)) {
        if (parent->released) throw std::runtime_error("memoryview is released");
        if (parent->external) { handle->external = parent->external; break; }
        root = parent->owner;
      }
    } else if (auto* bytes = xlang3::value_as_bytes(source)) {
      storage = xlang3::bytes_object_view(*bytes);
    } else if (auto* bytes = xlang3::value_as_bytearray(source)) {
      storage = bytes->value;
      readonly = false;
    } else throw std::runtime_error("value does not export contiguous buffer storage");
    if (writable && readonly) throw std::runtime_error("buffer is read-only");
    handle->owner = root;
    if (auto* bytes = xlang3::value_as_bytearray(root)) {
      ++bytes->buffer_exports;
      handle->bytearray = bytes;
    }
    info->data = const_cast<char*>(storage.data());
    info->size = storage.size();
    info->readonly = readonly ? 1 : 0;
    info->item_size = xlang3::memoryview_format_itemsize(handle->format);
    info->format = handle->format.c_str();
    *result = handle.release();
    return X3_STATUS_OK;
  } catch (const std::exception& error) {
    rt->set_last_error(error.what());
    return X3_STATUS_ERROR;
  }
}

extern "C" void x3_buffer_release(X3Buffer* buffer) {
  xlang3::XlangRuntimeExecutionGuard guard;
  delete buffer;
}
