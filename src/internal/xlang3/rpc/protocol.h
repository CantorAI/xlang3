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
#pragma once

#include <cstdint>

namespace xlang3::rpc {

constexpr uint32_t kProtocolMagic = 0x33525058u; // XPR3, little-endian on wire.
constexpr uint16_t kProtocolVersion = 1;

enum class Opcode : uint16_t {
  Hello = 1,
  Info = 2,
  PutFile = 10,
  GetFile = 11,
  ListFiles = 12,
  DeleteFile = 13,
  ImportModule = 20,
  GetAttr = 21,
  SetAttr = 22,
  Call = 23,
  CallMethod = 24,
  Release = 25,
  Exec = 30,
  Eval = 31,
  Reset = 40,
  Ping = 41,
};

enum class Status : uint16_t {
  Ok = 0,
  Error = 1,
};

struct FrameHeader {
  uint32_t magic = kProtocolMagic;
  uint16_t version = kProtocolVersion;
  uint16_t opcode = 0;
  uint32_t message_id = 0;
  uint32_t payload_size = 0;
};

} // namespace xlang3::rpc
