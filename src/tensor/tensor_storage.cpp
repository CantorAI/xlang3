#include "tensor_internal.h"
#include "xlang3/object_model.h"
#include "xlang3/module_object.h"
#include <cstring>
#include <limits>
#include "runtime/modules/thread/runtime_lock.h"

namespace xlang3::tensor {
Storage::~Storage() {
  XlangRuntimeExecutionSuspension suspension;
  // A batch fence may also be retained by another storage. Waiting cannot be
  // delegated to its final shared cleanup: this allocation is retiring now.
  X3TensorExecution host{}; host.size = sizeof(host);
  for (const auto& pendingUse : pending)
    pendingUse.completion->callback.wait(pendingUse.completion->callback.context, &host);
  pending.clear();
  if (cleanup) cleanup(owner);
}
StorageUse::StorageUse(std::shared_ptr<Storage> source, X3TensorAccess mode,
    const X3TensorExecution& execution, bool async) : storage(std::move(source)), access(mode) {
  if (!storage || (mode != X3_TENSOR_READ && mode != X3_TENSOR_WRITE))
    throw std::runtime_error("tensor access requires concrete storage and valid mode");
  if (mode == X3_TENSOR_WRITE && storage->readonly)
    throw std::runtime_error("tensor storage is readonly");
  if (async) {
    completion = std::make_unique<std::list<PendingCompletion>>();
    completion->push_back({std::make_shared<Completion>(), mode});
  }
  XlangRuntimeExecutionSuspension suspension;
  std::vector<std::shared_ptr<Completion>> dependencies;
  std::vector<std::shared_ptr<Completion>> retired;
  std::unique_lock<std::mutex> lock(storage->use_mutex);
  if (mode == X3_TENSOR_WRITE) ++storage->waiting_writers;
  try {
    storage->use_changed.wait(lock, [&] {
      // Nested read operands may alias; a queued writer must not block the
      // second read while the first read still belongs to this operation.
      return !storage->writer && (mode != X3_TENSOR_WRITE || !storage->readers);
    });
    for (auto it = storage->pending.begin(); it != storage->pending.end();) {
      auto& fence = *it->completion;
      const auto ready = fence.callback.query(fence.callback.context);
      if (ready < 0) throw std::runtime_error("tensor completion failed");
      if (ready) { retired.push_back(it->completion); it = storage->pending.erase(it); }
      else {
        if (mode == X3_TENSOR_WRITE || it->access == X3_TENSOR_WRITE) dependencies.push_back(it->completion);
        ++it;
      }
    }
  } catch (...) {
    if (mode == X3_TENSOR_WRITE) --storage->waiting_writers;
    lock.unlock(); storage->use_changed.notify_all(); throw;
  }
  if (mode == X3_TENSOR_WRITE) { --storage->waiting_writers; storage->writer = true; }
  else ++storage->readers;
  lock.unlock();
  try {
    for (const auto& fence : dependencies)
      if (fence->callback.wait(fence->callback.context, &execution) != X3_STATUS_OK)
        throw std::runtime_error("tensor dependency wait failed");
  } catch (...) { finish(); throw; }
}
StorageUse::~StorageUse() { finish(); }
void StorageUse::finish(std::shared_ptr<Completion> callback) {
  if (!storage) return;
  auto retained = std::move(storage);
  {
    std::lock_guard<std::mutex> lock(retained->use_mutex);
    if (callback) {
      completion->front().completion = std::move(callback);
      retained->pending.splice(retained->pending.end(), *completion);
    }
    if (access == X3_TENSOR_WRITE) retained->writer = false;
    else --retained->readers;
  }
  retained->use_changed.notify_all();
}
namespace { std::atomic_uint64_t next_id{1}; }
Tensor* get(const Value& v) { return static_cast<Tensor*>(instance_get_native_data(v, tensor_type)); }
Operator* get_operator(const Value& v) { return static_cast<Operator*>(instance_get_native_data(v, operator_type)); }
Graph* get_graph(const Value& v) { return static_cast<Graph*>(instance_get_native_data(v, graph_type)); }
uint64_t item_size(X3TensorDType dtype) {
  switch (dtype) {
    case X3_TENSOR_FLOAT8_E4M3FN: case X3_TENSOR_FLOAT8_E4M3FNUZ:
    case X3_TENSOR_FLOAT8_E5M2: case X3_TENSOR_FLOAT8_E5M2FNUZ: return 1;
    case X3_TENSOR_FLOAT16: case X3_TENSOR_BFLOAT16: case X3_TENSOR_UINT16: return 2;
    case X3_TENSOR_FLOAT32: case X3_TENSOR_INT32: return 4;
    case X3_TENSOR_FLOAT64: case X3_TENSOR_INT64: return 8;
    default: throw std::runtime_error("unsupported tensor dtype");
  }
}
uint64_t count(const std::vector<int64_t>& shape) {
  if (shape.size() > 32) throw std::runtime_error("tensor rank exceeds 32");
  uint64_t n = 1;
  for (auto d : shape) {
    if (d < 0 || (d && n > INT64_MAX / static_cast<uint64_t>(d)))
      throw std::runtime_error("invalid or overflowing tensor shape");
    n *= d;
  }
  return n;
}
std::vector<int64_t> contiguous_strides(const std::vector<int64_t>& shape, uint64_t size) {
  count(shape);
  std::vector<int64_t> result(shape.size());
  for (size_t i = shape.size(); i-- > 0;) {
    if (size > INT64_MAX) throw std::runtime_error("tensor strides overflow");
    result[i] = static_cast<int64_t>(size);
    if (shape[i] && size > INT64_MAX / static_cast<uint64_t>(shape[i]))
      throw std::runtime_error("tensor strides overflow");
    size *= shape[i];
  }
  return result;
}
bool contiguous(const Tensor& t) { return t.strides == contiguous_strides(t.shape, item_size(t.dtype)); }
void validate_layout(const Tensor& t) {
  const auto n = count(t.shape), size = item_size(t.dtype);
  if (t.strides.size() != t.shape.size()) throw std::runtime_error("tensor stride rank mismatch");
  uint64_t end = t.offset;
  if (t.offset % size) throw std::runtime_error("unaligned tensor offset");
  for (size_t i = 0; i < t.shape.size(); ++i) {
    if (t.strides[i] < 0 || t.strides[i] % size) throw std::runtime_error("invalid tensor byte stride");
    auto extent = t.shape[i] ? static_cast<uint64_t>(t.shape[i] - 1) : 0;
    if (extent && static_cast<uint64_t>(t.strides[i]) > (UINT64_MAX - end) / extent)
      throw std::runtime_error("tensor layout overflow");
    end += extent * t.strides[i];
  }
  if (t.storage && (t.offset > t.storage->bytes ||
      (n && (!t.storage->data || end > t.storage->bytes || size > t.storage->bytes - end))))
    throw std::runtime_error("tensor view exceeds its storage");
}
std::unique_ptr<Tensor> allocate(Runtime& rt, X3TensorDType dtype, std::vector<int64_t> shape) {
  auto t = std::make_unique<Tensor>();
  t->runtime = &rt; t->dtype = dtype; t->shape = std::move(shape);
  const auto n = count(t->shape), size = item_size(dtype);
  if (n > static_cast<uint64_t>(SIZE_MAX) / size) throw std::runtime_error("tensor allocation overflow");
  t->strides = contiguous_strides(t->shape, size);
  t->storage = std::make_shared<Storage>();
  t->storage->bytes = n * size;
  auto* data = new unsigned char[static_cast<size_t>(n * size)]{};
  t->storage->data = data; t->storage->owner = data;
  t->storage->cleanup = [](void* p) { delete[] static_cast<unsigned char*>(p); };
  return t;
}
Value type(Runtime& rt, const char* name) {
  Value module, result; std::string error;
  if (!rt.import_module("tensor", module, error) || !module_get_attr(module, name, result, error))
    throw std::runtime_error(error);
  return result;
}
template<class T> Value wrap(Runtime& rt, const char* klass, const char* native, std::unique_ptr<T> p) {
  Value v = Value::instance(type(rt, klass)); std::string error;
  if (!instance_set_native_data(v, native, p.get(), [](void* q) { delete static_cast<T*>(q); }, error))
    throw std::runtime_error(error);
  p.release(); return v;
}
Value wrap_tensor(Runtime& rt, std::unique_ptr<Tensor> t) {
  t->id = next_id.fetch_add(1); t->runtime = &rt;
  return wrap(rt, "Tensor", tensor_type, std::move(t));
}
Value wrap_operator(Runtime& rt, std::unique_ptr<Operator> p) { return wrap(rt, "Operator", operator_type, std::move(p)); }
Value wrap_graph(Runtime& rt, std::unique_ptr<Graph> p) { return wrap(rt, "Graph", graph_type, std::move(p)); }
Value attr(const Value& v, const std::string& name, Value fallback) {
  if (auto* d = value_as_dict(v)) for (const auto& entry : d->entries)
    if (value_to_string(entry.first) == name) return entry.second;
  return fallback;
}
Value record(std::initializer_list<std::pair<const char*, Value>> fields) {
  std::vector<std::pair<Value, Value>> items;
  for (const auto& f : fields) items.emplace_back(Value::string(f.first), f.second);
  return Value::dict(std::move(items));
}
std::vector<int64_t> dimensions(const Value& v, bool validate_shape) {
  std::vector<Value> values;
  if (auto* l = value_as_list(v)) values = l->items;
  else if (auto* l = value_as_tuple(v)) values = static_cast<std::vector<Value>>(l->items);
  else throw std::runtime_error("tensor dimensions must be a list or tuple");
  std::vector<int64_t> result;
  for (const auto& x : values) {
    if (x.tag != ValueTag::Int64) throw std::runtime_error("tensor dimensions must be integers");
    result.push_back(x.as.i64);
  }
  if (validate_shape) count(result);
  return result;
}
}
