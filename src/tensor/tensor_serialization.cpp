#include "tensor_internal.h"
#include "serialize/native_graph_context.h"
#include "xlang3/object_model.h"
#include <cstring>
#include <limits>

namespace xlang3::tensor {
namespace {
int byte_order() {
  const uint16_t value = 1;
  return *reinterpret_cast<const uint8_t*>(&value) ? 0 : 1;
}
Value integers(const std::vector<int64_t>& input) {
  std::vector<Value> result;
  result.reserve(input.size());
  for (auto value : input) result.push_back(Value::int64(value));
  return Value::list(std::move(result));
}
const std::vector<Value>& fields(const Value& value, size_t expected) {
  auto* list = value_as_list(value);
  if (!list || list->items.size() != expected) throw std::runtime_error("invalid serialized tensor state");
  return list->items;
}
int64_t integer(const Value& value) {
  if (value.tag != ValueTag::Int64) throw std::runtime_error("invalid serialized tensor integer");
  return value.as.i64;
}
Value encode(const Value& value) {
  const auto* tensor = get(value);
  if (!tensor || !tensor->storage || tensor->registration)
    throw std::runtime_error("tensor serialization requires concrete storage; symbolic graphs are unsupported");
  validate_layout(*tensor);
  const auto& storage = tensor->storage;
  if (storage->device != 0)
    throw std::runtime_error("device tensor serialization requires an explicit host transfer");
  if (tensor->offset > INT64_MAX || storage->bytes > static_cast<uint64_t>(SIZE_MAX))
    throw std::runtime_error("tensor storage exceeds serialization limits");
  auto& snapshots = serialize::NativeContext().encoded_storage;
  auto found = snapshots.find(storage.get());
  if (found == snapshots.end()) {
    X3TensorExecution host{}; host.size = sizeof(host);
    StorageUse access(storage, X3_TENSOR_READ, host);
    if (storage->bytes && !storage->data) throw std::runtime_error("tensor storage has no data");
    const auto bytes = storage->bytes ? std::string_view(static_cast<const char*>(storage->data),
        static_cast<size_t>(storage->bytes)) : std::string_view();
    auto state = Value::list({Value::bytes(bytes), Value::boolean(storage->readonly),
        Value::int64(byte_order()), Value::int64(item_size(tensor->dtype)), Value::int64(storage->device_id)});
    found = snapshots.emplace(storage.get(), std::move(state)).first;
  }
  return Value::list({Value::int64(tensor->dtype), integers(tensor->shape), integers(tensor->strides),
      Value::int64(static_cast<int64_t>(tensor->offset)), found->second});
}
std::unique_ptr<Tensor> decode(Runtime& runtime, const Value& state) {
  const auto& values = fields(state, 5);
  const auto dtype = integer(values[0]);
  if (dtype < 0 || dtype > UINT32_MAX) throw std::runtime_error("invalid serialized tensor dtype");
  auto tensor = std::make_unique<Tensor>();
  tensor->dtype = static_cast<X3TensorDType>(dtype);
  const auto size = item_size(tensor->dtype);
  tensor->shape = dimensions(values[1]);
  tensor->strides = dimensions(values[2], false);
  const auto offset = integer(values[3]);
  if (offset < 0) throw std::runtime_error("negative serialized tensor offset");
  tensor->offset = static_cast<uint64_t>(offset);
  const auto& storageState = fields(values[4], 5);
  auto* bytes = value_as_bytes(storageState[0]);
  if (!bytes || storageState[1].tag != ValueTag::Bool || integer(storageState[3]) != static_cast<int64_t>(size))
    throw std::runtime_error("invalid serialized tensor storage");
  const auto order = integer(storageState[2]);
  if (order != 0 && order != 1) throw std::runtime_error("invalid serialized tensor byte order");
  if (order != byte_order() && size != 1)
    throw std::runtime_error("serialized tensor byte order differs from this host");
  const auto deviceId = integer(storageState[4]);
  if (deviceId < INT32_MIN || deviceId > INT32_MAX) throw std::runtime_error("invalid serialized CPU device id");
  auto& storages = serialize::NativeContext().decoded_storage;
  auto found = storages.find(values[4].as.obj);
  if (found == storages.end()) {
    auto storage = std::make_shared<Storage>();
    const auto payload = bytes_object_view(*bytes);
    auto data = std::make_unique<unsigned char[]>(payload.size());
    if (!payload.empty()) std::memcpy(data.get(), payload.data(), payload.size());
    storage->data = data.get(); storage->bytes = payload.size();
    storage->readonly = storageState[1].as.b;
    storage->device_id = static_cast<int32_t>(deviceId);
    storage->owner = data.release();
    storage->cleanup = [](void* p) { delete[] static_cast<unsigned char*>(p); };
    found = storages.emplace(values[4].as.obj, storage).first;
  }
  tensor->storage = std::static_pointer_cast<Storage>(found->second);
  validate_layout(*tensor);
  tensor->runtime = &runtime;
  tensor->id = next_tensor_id();
  return tensor;
}
}
void register_serializer(Runtime& runtime) {
  auto codec = std::make_shared<NativeSerializationCodec>();
  codec->type_id = "xlang3.tensor.cpu";
  codec->native_type = tensor_type;
  codec->version = 1;
  codec->encode = [](const Value& value, Value& state, std::string& error) {
    try { state = encode(value); return true; }
    catch (const std::exception& e) { error = e.what(); return false; }
  };
  codec->decode = [&runtime](Value& value, const Value& state, std::string& error) {
    try {
      auto tensor = decode(runtime, state);
      if (!instance_set_native_data(value, tensor_type, tensor.get(),
          [](void* p) { delete static_cast<Tensor*>(p); }, error)) return false;
      tensor.release();
      return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
  };
  if (!runtime.register_native_codec(std::move(codec))) throw std::runtime_error("cannot register tensor serializer");
}
}
