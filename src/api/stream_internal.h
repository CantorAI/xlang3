/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once
#include "xlang3/abi/xstream.h"
#include "serialize/block_stream.h"

struct X3Stream {
  class Storage final : public xlang3::serialize::BlockStream {
  public:
    X3StreamAllocate allocate = nullptr;
    void* context = nullptr;
    bool NewBlock() override {
      if (allocate == nullptr) return BlockStream::NewBlock();
      void* data = nullptr;
      uint64_t capacity = 0;
      if (allocate(context, &data, &capacity) != X3_STATUS_OK || !data ||
          capacity == 0 || capacity > INT64_MAX) return false;
      AddBlock(static_cast<char*>(data), static_cast<int64_t>(capacity), 0);
      return true;
    }
  } storage;
  X3Runtime* runtime = nullptr;
  bool reading = false;
  bool failed = false;
  uint64_t size = 0;
  uint64_t position = 0;
};
