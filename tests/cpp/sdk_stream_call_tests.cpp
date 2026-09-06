#include "xlang3/xlang3.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
struct Provider {
  std::vector<std::unique_ptr<char[]>> blocks;
  static X3Status Allocate(void* context, void** data, uint64_t* capacity) {
    auto& self = *static_cast<Provider*>(context);
    try {
      *capacity = 257;
      self.blocks.push_back(std::make_unique<char[]>(257));
      *data = self.blocks.back().get();
      return X3_STATUS_OK;
    } catch (...) { return X3_STATUS_ERROR; }
  }
};

int main(int argc, char** argv) {
  try {
    require(argc == 2, "missing fixtures directory");
    X::Runtime runtime;
    X::Value nulString(runtime.host(), x3_value_string_utf8(runtime.get(), "a\0b", 3), false);
    auto strings = X::Value::Dict(runtime.host());
    char mutableString[] = "script.py";
    require(strings.SetItem("path", mutableString) && strings["path"].IsString() &&
        strings["path"].ToString() == "script.py", "mutable C string was converted to bool");
    require(nulString.ToString() == std::string("a\0b", 3), "SDK string conversion truncated an embedded NUL");
    require(X::Value(runtime, std::string("a\0b", 3)).ToString() == std::string("a\0b", 3),
        "SDK string construction truncated an embedded NUL");
    const char byte = 'x';
    require(!X::Value::Bytes(runtime.host(), &byte, UINT64_C(0x100000000)).IsValid(),
        "oversized binary length accepted");
    require(!X::Value::Bytes(runtime.host(), nullptr, 1).IsValid(), "null binary data accepted");
    runtime.AddImportRoot(argv[1]);
    X::Module module(runtime, "sdk_calls");
    for (size_t size : {size_t(12), size_t(65536), size_t(200000), size_t(1048576)}) {
      auto storage = std::make_shared<std::vector<char>>(size);
      std::weak_ptr<std::vector<char>> lifetime = storage;
      auto view = X::Value::MemoryView(runtime.host(), storage->data(), size, storage);
      uint64_t length = 0;
      require(view.BytesData(&length) == storage->data() && length == size,
          "native memoryview copied storage");
      X::Value derived;
      require(module["check_native_view"].Call({view}, {}, derived),
          "native memoryview Python operations failed");
      require((*storage)[0] == 17 && (*storage)[size - 1] == 29,
          "native memoryview writes lost");
      view = X::Value();
      storage.reset();
      require(!lifetime.expired(), "derived native memoryview lost storage owner");
      const auto* data = static_cast<const unsigned char*>(derived.BytesData(&length));
      require(data && length == 4 && data[0] == 0x78 && data[3] == 0x12,
          "derived native memoryview contents changed");
      X::Stream encoded(runtime);
      X::Value decoded;
      require(derived.ToBytes(encoded) && encoded.Rewind() && decoded.FromBytes(encoded),
          "native memoryview serialization failed");
      require(decoded.BytesData(&length) && length == 4,
          "native memoryview serialization size changed");
      derived = X::Value();
      require(lifetime.expired(), "native memoryview owner leaked");
    }
    int cleanups = 0;
    auto invalid_view = x3_value_memoryview(runtime.get(), nullptr, 1, 0, &cleanups,
        [](void* context) { ++*static_cast<int*>(context); });
    require(invalid_view.tag == X3_TAG_INVALID && cleanups == 1,
        "failed native memoryview did not release owner exactly once");
    auto binder = runtime.List();
    X::Value result;
    require(module["task"].Call({X::Value(runtime, 40)},
        {{"Binder", binder}, {"InstanceId", X::Value(runtime, "task-1")},
         {"extra", X::Value(runtime, 2)}}, result), "keyword task call failed");
    require(result.ToInt64() == 42 && binder[0].ToString() == "task-1", "kwargs identity lost");
    require(module["worker"]["run"].Call({X::Value(runtime, 40)},
        {{"offset", X::Value(runtime, 2)}}, result) && result.ToInt64() == 42, "bound method kwargs failed");
    require(module["worker"].Call({X::Value(runtime, 40)},
        {{"offset", X::Value(runtime, 2)}}, result) && result.ToInt64() == 42, "callable instance kwargs failed");
    auto function = module["task"];
    X::Value configured;
    require(module["ConfiguredWorker"].Call({}, {{"offset", X::Value(runtime, 2)}}, configured),
        "class construction kwargs failed");
    require(configured(40).ToInt64() == 42, "constructed class state lost");
    X3KeywordArg duplicate[] = {{"a", x3_value_int64(1)}, {"a", x3_value_int64(2)}};
    X3Value raw_result = x3_value_invalid();
    require(x3_call_kw(runtime.get(), function.raw(), nullptr, 0, duplicate, 2, &raw_result) == X3_STATUS_ERROR,
        "duplicate kwargs accepted");
    require(!module["broken"].Call({}, {{"reason", X::Value(runtime, "expected failure")}}, result), "exception swallowed");

    for (size_t size : {size_t(0), size_t(1), size_t(32767), size_t(32768), size_t(65537), size_t(1048576)}) {
      std::vector<char> payload(size);
      for (size_t i = 0; i < size; ++i) payload[i] = static_cast<char>(i);
      auto value = X::Value::Bytes(runtime.host(), payload.data(), payload.size());
      Provider provider;
      X::Stream stream(runtime.host(), Provider::Allocate, &provider);
      require(value.ToBytes(stream), "provider serialization failed");
      std::vector<char> wire(stream.Size());
      require(stream.FullCopyTo(wire.data(), wire.size()), "stream copy failed");
      std::vector<X3StreamBlock> spans;
      for (size_t i = 0; i < wire.size(); i += 113)
        spans.push_back({wire.data() + i, std::min(size_t(113), wire.size() - i)});
      X::Stream input(runtime.host(), spans.data(), static_cast<uint32_t>(spans.size()));
      X::Value decoded;
      require(decoded.FromBytes(input), "fragmented decode failed");
      uint64_t length = 0;
      const void* data = decoded.BytesData(&length);
      require(length == size && (!size || std::memcmp(data, payload.data(), size) == 0), "binary changed");
      require(stream.Rewind() && decoded.FromBytes(stream), "provider rewind failed");
      X::Stream truncated(runtime, wire.data(), wire.size() - 1);
      require(!decoded.FromBytes(truncated), "truncated payload accepted");
    }
    auto list = runtime.List();
    for (int i = 0; i < 4096; ++i) list += X::Value(runtime, i);
    auto dict = runtime.Dict();
    require(dict.Set("items", list), "dict set failed");
    X::Stream stream(runtime);
    require(dict.ToBytes(stream) && X::Value(runtime, 2.5).ToBytes(stream), "large nested serialization failed");
    require(stream.Rewind() && result.FromBytes(stream), "nested decode failed");
    require(result["items"].Size() == 4096 && result["items"][4095].ToInt64() == 4095, "large list lost");
    require(result.FromBytes(stream) && result.ToDouble() == 2.5, "sequential decode failed");
    X::Stream failed(runtime.host(), [](void*, void**, uint64_t*) { return X3_STATUS_ERROR; }, nullptr);
    require(!dict.ToBytes(failed) && !failed.Rewind(), "allocation failure ignored");
    X::Stream function_stream(runtime);
    if (!function.ToBytes(function_stream) || !function_stream.Rewind() || !result.FromBytes(function_stream))
      throw std::runtime_error("function stream round trip failed: " + runtime.LastError());
    X::Stream scalar_stream(runtime);
    require(X::Value(runtime, "small").ToBytes(scalar_stream), "scalar encode failed");
    std::vector<char> scalar_wire(scalar_stream.Size());
    require(scalar_stream.FullCopyTo(scalar_wire.data(), scalar_wire.size()), "scalar copy failed");
    for (size_t prefix = 0; prefix < scalar_wire.size(); ++prefix) {
      X::Stream truncated(runtime, scalar_wire.data(), prefix);
      require(!result.FromBytes(truncated), "truncated prefix accepted");
    }
    // Header (16 bytes), kind (1), number/name counts (8), then payload length.
    std::fill(scalar_wire.begin() + 25, scalar_wire.begin() + 33, static_cast<char>(0xff));
    X::Stream malicious(runtime, scalar_wire.data(), scalar_wire.size());
    require(!result.FromBytes(malicious), "oversized string length accepted");
    X3Stream* invalid = nullptr;
    X3StreamBlock invalid_block{nullptr, 1};
    require(x3_stream_from_blocks(runtime.get(), &invalid_block, 1, &invalid) == X3_STATUS_ERROR && !invalid,
        "null input block accepted");
    X::Stream raw(runtime);
    const uint64_t marker = 123;
    require(raw.Write(&marker, sizeof(marker)) && raw.Rewind(), "raw stream write failed");
    uint64_t read_marker = 0;
    require(raw.Read(&read_marker, sizeof(read_marker)) && read_marker == marker, "raw stream read failed");
    require(!raw.Read(&read_marker, 1), "raw stream overread accepted");
    std::cout << "SDK streams and keyword calls passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
