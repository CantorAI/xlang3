#pragma once
#include "xlang3/xlang3.h"
#include <cstring>
#include <future>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace tensor_serialization_test {
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline std::vector<unsigned char> encode(const X::Value& value) {
  X::Stream stream(value.host());
  require(value.ToBytes(stream), "native tensor encode failed");
  std::vector<unsigned char> bytes(static_cast<size_t>(stream.Size()));
  require(stream.FullCopyTo(bytes.data(), bytes.size()), "tensor stream copy failed");
  return bytes;
}
inline X::Value decode(X::Runtime& runtime, const std::vector<unsigned char>& bytes) {
  X::Stream stream(runtime.host(), bytes.data(), bytes.size());
  X::Value value;
  require(value.FromBytes(stream), "native tensor decode failed");
  return value;
}
}
inline void TensorSerialization(X::Runtime& runtime) {
  using namespace tensor_serialization_test;
  auto* host = runtime.host();
  for (int64_t width : {0, 1, 17, 768, 2048, 65539}) {
    std::vector<float> data(static_cast<size_t>(width));
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<float>(i % 257) / 16.0f;
    auto tensor = X::Tensor::Create(host, X3_TENSOR_FLOAT32, {width}, data.data(), data.size() * sizeof(float));
    auto item = runtime.Dict();
    require(item.SetItem("embedding", tensor), "embedding field");
    auto items = runtime.List(); require(items.Append(item), "embedding list");
    auto frame = runtime.Dict();
    require(frame.SetItem("object_embeddings", items) && frame.SetItem("image_embedding", item) &&
        frame.SetItem("same_tensor", tensor), "frame-shaped tensor fixture");
    auto bytes = encode(frame);
    X::Runtime other;
    auto decoded = decode(other, bytes);
    auto value = decoded["object_embeddings"][0]["embedding"];
    require(value.raw().as.obj == decoded["same_tensor"].raw().as.obj &&
        value.raw().as.obj == decoded["image_embedding"]["embedding"].raw().as.obj, "tensor graph reference identity");
    X::Tensor restored(value);
    auto info = restored.Info();
    auto access = restored.Acquire();
    require(info.dtype == X3_TENSOR_FLOAT32 && info.rank == 1 && info.shape[0] == width &&
        info.byte_size == data.size() * sizeof(float), "arbitrary-width tensor metadata");
    require(data.empty() || std::memcmp(info.data, data.data(), info.byte_size) == 0, "embedding bytes roundtrip");
    if (bytes.size() > 1) {
      X::Stream truncated(other.host(), bytes.data(), bytes.size() / 2);
      X::Value invalid;
      require(!invalid.FromBytes(truncated), "truncated tensor graph rejected");
    }
  }
  {
    const float data[] = {1, 2, 3, 4, 5, 6};
    auto owner = X::Tensor::Create(host, X3_TENSOR_FLOAT32, {2, 3}, data, sizeof(data));
    auto transpose = owner.View({3, 2}, {4, 12});
    auto offset = owner.View({2}, {8}, 4);
    auto root = runtime.List();
    require(root.Append(owner) && root.Append(transpose) && root.Append(offset), "view graph fixture");
    auto bytes = encode(root);
    auto decoded = decode(runtime, bytes);
    X::Tensor a(decoded[0]), b(decoded[1]), c(decoded[2]);
    require(a.Info().data == b.Info().data && c.Info().data == static_cast<char*>(a.Info().data) + 4,
        "distinct decoded views share one storage");
    require(b.Info().shape[0] == 3 && b.Info().strides[0] == 4 && b.Info().strides[1] == 12 &&
        c.Info().strides[0] == 8, "view shape strides and offsets preserved");
    {
      auto use = c.Acquire(X3_TENSOR_WRITE);
      *static_cast<float*>(c.Info().data) = 29;
    }
    {
      auto use = a.Acquire();
      require(static_cast<float*>(a.Info().data)[1] == 29, "decoded alias mutation visible");
    }
    require(static_cast<float*>(owner.Info().data)[1] == 2, "snapshot does not alias original storage");
    // Each encode takes a fresh snapshot; no runtime-wide cached bytes.
    {
      auto use = owner.Acquire(X3_TENSOR_WRITE);
      static_cast<float*>(owner.Info().data)[0] = 31;
    }
    auto again = decode(runtime, encode(owner));
    require(*static_cast<float*>(X::Tensor(again).Info().data) == 31, "tensor serialization refreshes storage");
  }
  {
    const std::pair<X3TensorDType, size_t> formats[] = {
      {X3_TENSOR_UINT8,1}, {X3_TENSOR_UINT16,2}, {X3_TENSOR_INT32,4}, {X3_TENSOR_INT64,8},
      {X3_TENSOR_FLOAT32,4}, {X3_TENSOR_FLOAT64,8}, {X3_TENSOR_FLOAT16,2}, {X3_TENSOR_BFLOAT16,2},
      {X3_TENSOR_FLOAT8_E4M3FN,1}, {X3_TENSOR_FLOAT8_E4M3FNUZ,1},
      {X3_TENSOR_FLOAT8_E5M2,1}, {X3_TENSOR_FLOAT8_E5M2FNUZ,1}};
    for (auto format : formats) {
      unsigned char storage[24];
      for (size_t i = 0; i < sizeof(storage); ++i) storage[i] = static_cast<unsigned char>(i * 53);
      int64_t shape = 3;
      X3TensorInfo descriptor{}; descriptor.size = sizeof(descriptor); descriptor.dtype = format.first;
      descriptor.rank = 1; descriptor.shape = &shape; descriptor.data = storage;
      descriptor.byte_size = 3 * format.second; descriptor.readonly = 1;
      auto tensor = X::Tensor::Wrap(host, descriptor);
      auto value = decode(runtime, encode(tensor));
      X::Tensor restored(value);
      auto info = restored.Info();
      require(info.dtype == format.first && info.readonly && info.byte_size == descriptor.byte_size &&
          std::memcmp(info.data, storage, info.byte_size) == 0, "storage dtype and readonly preserved");
      X3TensorUse* use = nullptr;
      require(x3_tensor_begin_use(runtime.get(), restored.raw(), X3_TENSOR_WRITE, nullptr, &use) == X3_STATUS_ERROR,
          "decoded readonly storage rejects writes");
    }
  }
  {
    auto tensor = X::Tensor::Create(host, X3_TENSOR_FLOAT32, {7});
    auto* data = static_cast<float*>(tensor.Info().data);
    auto use = tensor.Acquire(X3_TENSOR_WRITE);
    auto producer = std::async(std::launch::async, [use = std::move(use), data, &runtime]() mutable {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      auto entersRuntime = runtime.Dict();
      for (int i = 0; i < 7; ++i) data[i] = static_cast<float>(i + 40);
      use.Finish();
    });
    auto bytes = encode(tensor);
    producer.get();
    X::Tensor result(decode(runtime, bytes));
    require(static_cast<float*>(result.Info().data)[6] == 46, "serialization waits without holding runtime lock");
  }
  {
    int64_t shape = 1;
    X3TensorInfo descriptor{}; descriptor.size = sizeof(descriptor); descriptor.dtype = X3_TENSOR_UINT8;
    descriptor.rank = 1; descriptor.shape = &shape; descriptor.data = reinterpret_cast<void*>(uintptr_t{1});
    descriptor.byte_size = 1; descriptor.device_type = 2;
    auto device = X::Tensor::Wrap(host, descriptor);
    X::Stream stream(host);
    require(!device.ToBytes(stream) && runtime.LastError().find("explicit host transfer") != std::string::npos,
        "device storage rejected before any CPU dereference");
    auto symbolic = X::Tensor::Input(host, "symbolic", X3_TENSOR_FLOAT32, {3});
    X::Stream symbolicStream(host);
    require(!symbolic.ToBytes(symbolicStream), "symbolic tensor serialization rejected");
  }
}
