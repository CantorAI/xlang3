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
    require(X::Value(runtime, -42LL).ToLongLong() == -42,
        "runtime-bound long long construction failed");
    require(X::Value(runtime, ~0ULL).ToString() == "18446744073709551615",
        "runtime-bound unsigned integer construction lost precision");
    auto integerList = runtime.List();
    integerList += X::Value(42);
    require(integerList[int64_t{0}].ToLongLong() == 42 &&
        integerList[size_t{0}].ToLongLong() == 42,
        "platform-sized integer list indexing failed");
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
    for (uint64_t number : {uint64_t{0}, static_cast<uint64_t>(INT64_MAX),
        static_cast<uint64_t>(INT64_MAX) + 1, UINT64_MAX}) {
      X::Value input(number);
      X::Value echoed;
      require(module["uint64_roundtrip"].Call({input}, echoed), "uint64 Python call failed");
      require(echoed.ToUInt64() == number, "uint64 Python call narrowed result");
      require(echoed.raw().tag == (number > static_cast<uint64_t>(INT64_MAX) ? X3_TAG_UINT64 : X3_TAG_INT64),
          "integer Python call returned wrong ABI representation");
      auto values = runtime.Dict();
      require(values.SetItem("unsigned", input) && values.SetItem(input, echoed), "uint64 dict insertion failed");
      require(values["unsigned"].ToUInt64() == number && values.GetItem(input).ToUInt64() == number,
          "uint64 dict key/value roundtrip failed");
      X::Stream encoded(runtime);
      X::Value decoded;
      if (!input.ToBytes(encoded)) throw std::runtime_error("uint64 encode " + std::to_string(number) + ": " + runtime.LastError());
      if (!encoded.Rewind()) throw std::runtime_error("uint64 rewind " + std::to_string(number) + ": " + runtime.LastError());
      if (!decoded.FromBytes(encoded)) throw std::runtime_error("uint64 decode " + std::to_string(number) + ": " + runtime.LastError());
      require(decoded.ToUInt64() == number, "uint64 serialization narrowed result");
      X::Stream dictionaryStream(runtime);
      require(values.ToBytes(dictionaryStream) && dictionaryStream.Rewind() && decoded.FromBytes(dictionaryStream),
          "uint64 dictionary serialization failed");
      require(decoded["unsigned"].ToUInt64() == number && decoded.GetItem(input).ToUInt64() == number,
          "serialized uint64 dictionary lost key/value");
    }
    X::Value huge;
    require(module["integer_successor"].Call({X::Value(UINT64_MAX)}, huge), "uint64 overflow promotion call failed");
    require(huge.raw().tag == X3_TAG_OBJECT, "integer larger than uint64 was narrowed");
    X::Value hugeCheck;
    require(module["check_large_integer"].Call({huge}, hugeCheck) && hugeCheck == X::Value(true),
        "huge bigint numeric value changed");
    X::Stream hugeStream(runtime);
    X::Value hugeDecoded;
    require(huge.ToBytes(hugeStream) && hugeStream.Rewind() && hugeDecoded.FromBytes(hugeStream),
        "huge bigint serialization failed");
    require(hugeDecoded.raw().tag == X3_TAG_OBJECT &&
        module["check_large_integer"].Call({hugeDecoded}, hugeCheck) && hugeCheck == X::Value(true),
        "huge bigint serialization changed numeric value");
    X::Value negative;
    require(module["negative_large_integer"].Call({}, negative) && negative.raw().tag == X3_TAG_OBJECT,
        "negative bigint was converted to unsigned scalar");
    X::Value pair;
    require(module["bigint_pair"].Call({}, pair), "cannot create arbitrary-size bigint graph");
    X::Stream pairStream(runtime);
    X::Value pairDecoded;
    require(pair.ToBytes(pairStream) && pairStream.Rewind() && pairDecoded.FromBytes(pairStream),
        "arbitrary-size signed bigint graph serialization failed");
    require(module["check_bigint_pair"].Call({pairDecoded}, hugeCheck) && hugeCheck == X::Value(true),
        "arbitrary-size signed bigint graph changed keys, signs or limbs");
    X::Stream limbStream(runtime);
    require(X::Value(UINT64_MAX).ToBytes(limbStream), "cannot encode malformed-bigint fixture");
    std::vector<char> limbWire(limbStream.Size());
    require(limbStream.FullCopyTo(limbWire.data(), limbWire.size()), "cannot copy bigint fixture");
    // One BigInt record: header16, kind1, number-count4, sign8,
    // name-count4, payload-length8, then two little-endian limbs.
    require(limbWire.size() >= 49, "bigint fixture is missing binary limbs");
    auto badSign = limbWire;
    badSign[21] = 2;
    X::Stream badSignStream(runtime, badSign.data(), badSign.size());
    require(!pairDecoded.FromBytes(badSignStream), "invalid bigint sign accepted");
    auto badLimbs = limbWire;
    std::fill(badLimbs.begin() + 45, badLimbs.begin() + 49, 0);
    X::Stream badLimbsStream(runtime, badLimbs.data(), badLimbs.size());
    require(!pairDecoded.FromBytes(badLimbsStream), "noncanonical bigint limbs accepted");
    auto rejectBigint = [&](const std::vector<char>& wire, const char* message) {
      X::Stream malformed(runtime, wire.data(), wire.size());
      X::Value ignored;
      require(!ignored.FromBytes(malformed), message);
    };
    auto badKind = limbWire;
    badKind[16] = static_cast<char>(0xff);
    rejectBigint(badKind, "unknown graph kind accepted");
    auto badSignCount = limbWire;
    badSignCount[17] = 2;
    badSignCount.insert(badSignCount.begin() + 29, 8, 0);
    rejectBigint(badSignCount, "bigint with extra sign field accepted");
    auto countOverflow = limbWire;
    std::fill(countOverflow.begin() + 17, countOverflow.begin() + 21, static_cast<char>(0xff));
    rejectBigint(countOverflow, "overflowing bigint field count accepted");
    auto lengthOverflow = limbWire;
    std::fill(lengthOverflow.begin() + 33, lengthOverflow.begin() + 41, static_cast<char>(0xff));
    rejectBigint(lengthOverflow, "overflowing bigint payload length accepted");
    auto emptyLimbs = limbWire;
    std::fill(emptyLimbs.begin() + 33, emptyLimbs.begin() + 41, 0);
    emptyLimbs.erase(emptyLimbs.begin() + 41, emptyLimbs.begin() + 49);
    rejectBigint(emptyLimbs, "empty bigint limb payload accepted");
    auto unalignedLimbs = limbWire;
    unalignedLimbs[33] = 7;
    unalignedLimbs.erase(unalignedLimbs.begin() + 48);
    rejectBigint(unalignedLimbs, "unaligned bigint limb payload accepted");
    for (char sign : {char{0}, char{1}}) {
      auto zeroLimbs = limbWire;
      zeroLimbs[21] = sign;
      std::fill(zeroLimbs.begin() + 41, zeroLimbs.begin() + 49, 0);
      rejectBigint(zeroLimbs, "noncanonical signed zero bigint accepted");
    }
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
