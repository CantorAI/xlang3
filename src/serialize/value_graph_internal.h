/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once
#include "value_graph.h"
#include "xlang3/ir_codec.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace xlang3::serialize::graph {
constexpr uint32_t magic = 0x47563358; // X3VG
constexpr uint32_t version = 1;
constexpr uint32_t max_nodes = 1000000;
constexpr uint32_t max_fields = 16000000;
constexpr uint64_t max_payload = 1024ull * 1024 * 1024;
enum class Kind : uint8_t {
  String, Bytes, List, Tuple, Dict, Cell, Function, Globals, Module,
  Class, Instance, BoundMethod, StaticMethod, ClassMethod, Property,
  Slot, Symbol, Expression, ByteArray, NativeInstance
};
struct Reference {
  uint8_t tag = 0;
  uint64_t bits = 0;
};
struct Record {
  Kind kind{};
  std::vector<uint64_t> numbers;
  std::vector<std::string> names;
  std::vector<Reference> refs;
  std::string payload;
};
class IO {
public:
  explicit IO(XLangStream& stream) : stream(stream) {}
  XLangStream& stream;
  uint64_t consumed = 0;
  static void require(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
  void put(const void* data, uint64_t size) {
    require(size <= max_payload - consumed, "serialized graph exceeds size limit");
    consumed += size;
    require(size <= INT64_MAX && stream.append(data, static_cast<int64_t>(size)), "graph stream write failed");
  }
  void get(void* data, uint64_t size) {
    require(size <= max_payload - consumed, "serialized graph exceeds size limit");
    consumed += size;
    require(size <= INT64_MAX && stream.CopyTo(static_cast<char*>(data), static_cast<int64_t>(size)), "truncated value graph");
  }
  template<class T> void put_number(T value) {
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    std::reverse(bytes, bytes + sizeof(T));
#endif
    put(bytes, sizeof(T));
  }
  template<class T> T number() {
    unsigned char bytes[sizeof(T)];
    get(bytes, sizeof(T));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    std::reverse(bytes, bytes + sizeof(T));
#endif
    T value;
    std::memcpy(&value, bytes, sizeof(T));
    return value;
  }
  void count(size_t count) { require(count <= max_fields, "graph field count exceeds limit"); put_number<uint32_t>(static_cast<uint32_t>(count)); }
  uint32_t count() { auto n = number<uint32_t>(); require(n <= max_fields, "invalid graph field count"); return n; }
  void text(std::string_view value) { put_number<uint64_t>(value.size()); put(value.data(), value.size()); }
  std::string text() {
    auto size = number<uint64_t>();
    require(size <= max_payload - consumed && stream.CanRead(static_cast<int64_t>(size)), "invalid graph payload size");
    std::string value(static_cast<size_t>(size), '\0');
    get(value.data(), size);
    return value;
  }
};
void clear_edges(Value& value);
}
