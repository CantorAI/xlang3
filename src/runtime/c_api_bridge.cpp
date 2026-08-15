/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "xlang3/c_api_bridge.h"

namespace xlang3 {

X3Value to_c_value(const Value& value) {
  X3Value out = x3_value_invalid();
  out.flags = value.flags;
  switch (value.tag) {
    case ValueTag::Invalid:
      out.tag = X3_TAG_INVALID;
      break;
    case ValueTag::None:
      out.tag = X3_TAG_NONE;
      break;
    case ValueTag::Bool:
      out.tag = X3_TAG_BOOL;
      out.as.b = value.as.b ? 1 : 0;
      break;
    case ValueTag::Int64:
      out.tag = X3_TAG_INT64;
      out.as.i64 = value.as.i64;
      break;
    case ValueTag::Double:
      out.tag = X3_TAG_DOUBLE;
      out.as.f64 = value.as.f64;
      break;
    case ValueTag::Object:
      out.tag = X3_TAG_OBJECT;
      out.as.obj = reinterpret_cast<X3Object*>(value.as.obj);
      retain(value);
      break;
  }
  return out;
}

Value from_c_value(const X3Value& value, std::string& error) {
  Value out;
  out.flags = value.flags;
  switch (value.tag) {
    case X3_TAG_INVALID:
      return Value::invalid();
    case X3_TAG_NONE:
      return Value::none();
    case X3_TAG_BOOL:
      return Value::boolean(value.as.b != 0);
    case X3_TAG_INT64:
      return Value::int64(value.as.i64);
    case X3_TAG_UINT64:
      if (value.as.u64 > static_cast<uint64_t>(INT64_MAX)) {
        error = "uint64 value does not fit in XLang3 int64";
        return Value::invalid();
      }
      return Value::int64(static_cast<int64_t>(value.as.u64));
    case X3_TAG_DOUBLE:
      return Value::number(value.as.f64);
    case X3_TAG_OBJECT:
      out.tag = ValueTag::Object;
      out.as.obj = reinterpret_cast<Object*>(value.as.obj);
      retain(out);
      return out;
    default:
      error = "unknown X3Value tag";
      return Value::invalid();
  }
}

} // namespace xlang3
