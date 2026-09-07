#pragma once
#include "xlang3/xlang3.h"
#include <cstring>
#include <stdexcept>

inline void TensorUInt8(X::Runtime& runtime) {
    auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    auto* host = runtime.host();
    const uint8_t bytes[] = {0, 1, 127, 128, 254, 255};
    auto tensor = X::Tensor::Create(host, X3_TENSOR_UINT8, {2, 3}, bytes, sizeof(bytes));
    auto info = tensor.Info();
    require(info.dtype == X3_TENSOR_UINT8 && info.byte_size == sizeof(bytes) &&
        info.strides[0] == 3 && info.strides[1] == 1, "uint8 byte layout");
    {
        auto use = tensor.Acquire();
        require(std::memcmp(info.data, bytes, sizeof(bytes)) == 0, "uint8 create preserves high bits");
    }
    auto view = tensor.View({3, 2}, {1, 3});
    require(view.Info().data == info.data && view.Info().dtype == X3_TENSOR_UINT8, "uint8 view aliases storage");
    X::Value list;
    require(view["tolist"].Call({}, list), "uint8 strided tolist");
    const int expected[] = {0, 128, 1, 254, 127, 255};
    for (int i = 0; i < 6; ++i) require(list[i].ToLongLong() == expected[i], "uint8 strided values");
    auto offset = tensor.View({2}, {2}, 1);
    require(offset.Info().data == static_cast<uint8_t*>(info.data) + 1, "uint8 odd-byte view offset");
    int64_t shape[] = {2, 3};
    X3Value raw = x3_value_invalid();
    require(x3_tensor_create(runtime.get(), X3_TENSOR_UINT8, shape, 2, bytes, 5, &raw) == X3_STATUS_ERROR,
        "uint8 rejects undersized initialization");
    auto empty = X::Tensor::Create(host, X3_TENSOR_UINT8, {0, 3});
    require(empty.Info().byte_size == 0, "uint8 empty storage");

    int cleanups = 0;
    X3TensorInfo external{};
    external.size = sizeof(external); external.dtype = X3_TENSOR_UINT8;
    external.shape = shape; external.rank = 2; external.data = const_cast<uint8_t*>(bytes);
    external.byte_size = sizeof(bytes); external.readonly = 1;
    {
        auto owner = X::Tensor::Wrap(host, external, &cleanups, [](void* p) { ++*static_cast<int*>(p); });
        auto alias = owner.View({3}, {2});
        owner = X::Tensor();
        require(cleanups == 0 && alias.Info().readonly, "uint8 view retains readonly owner");
        X3TensorUse* use = nullptr;
        require(x3_tensor_begin_use(runtime.get(), alias.raw(), X3_TENSOR_WRITE, nullptr, &use) == X3_STATUS_ERROR,
            "uint8 readonly write rejected");
    }
    require(cleanups == 1, "uint8 external cleanup once");

    X::Module module(runtime, "tensor");
    require(module["uint8"].ToLongLong() == X3_TENSOR_UINT8, "tensor.uint8 exposed");
    auto values = runtime.List();
    require(values.Append(0) && values.Append(255), "uint8 list fixture");
    X::Value constructed;
    require(module["tensor"].Call({values}, {{"dtype", module["uint8"]}}, constructed), "uint8 Python factory");
    for (int invalid : {-1, 256}) {
        auto bad = runtime.List(); require(bad.Append(invalid), "uint8 invalid fixture");
        require(!module["tensor"].Call({bad}, {{"dtype", module["uint8"]}}, constructed), "uint8 bounds rejection");
    }
    X::TensorGraph graph(tensor + tensor);
    auto bindings = runtime.Dict();
    require(x3_tensor_graph_run(runtime.get(), graph.raw(), bindings.raw(), &raw) == X3_STATUS_ERROR,
        "uint8 CPU arithmetic explicitly unsupported");

    // Public value serialization of an explicit storage envelope. Tensor objects
    // themselves need a native codec, which is not provided by this storage test.
    auto envelope = runtime.Dict();
    require(envelope.SetItem("dtype", static_cast<int>(X3_TENSOR_UINT8)) &&
        envelope.SetItem("data", X::Value::Bytes(host, bytes, sizeof(bytes))), "uint8 storage envelope");
    X::Stream stream(host);
    X::Value decoded;
    require(envelope.ToBytes(stream) && stream.Rewind() && decoded.FromBytes(stream), "uint8 envelope roundtrip");
    uint64_t size = 0;
    auto payload = decoded["data"];
    const void* data = payload.BytesData(&size);
    require(decoded["dtype"].ToLongLong() == X3_TENSOR_UINT8 && size == sizeof(bytes) &&
        data && std::memcmp(data, bytes, size) == 0, "uint8 serialized bytes unchanged");
}
