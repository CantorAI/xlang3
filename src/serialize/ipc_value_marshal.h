/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang_stream.h"
#include "xlang3/value.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::serialize {

enum class IpcWireValueKind : uint8_t {
  Invalid = 0,
  None = 1,
  Bool = 2,
  Int64 = 3,
  Double = 4,
  String = 5,
  Bytes = 6,
  Tuple = 7,
  List = 8,
  Dict = 9,
  ObjectRef = 10,
  Callable = 11,
  Error = 12,
  Expression = 13,
  ExpressionDecoratorRef = 14,
  ValueCallRef = 15,
};

struct RemoteObjectId {
  uint64_t node_id = 0;
  uint64_t session_id = 0;
  uint64_t object_id = 0;
  uint32_t generation = 0;
};

struct IpcWireValue {
  IpcWireValueKind kind = IpcWireValueKind::Invalid;
  bool bool_value = false;
  int64_t int_value = 0;
  double double_value = 0.0;
  std::string bytes;
  std::vector<IpcWireValue> items;
  std::vector<std::pair<IpcWireValue, IpcWireValue>> entries;
  RemoteObjectId object_id;
};

class IpcMarshalContext {
public:
  virtual ~IpcMarshalContext() = default;
  virtual bool make_object_ref(const Value& value, RemoteObjectId& out, std::string& error) = 0;
};

bool ipc_arguments_by_value(const Value& callable);

} // namespace xlang3::serialize
