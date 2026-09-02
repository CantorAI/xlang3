/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "block_stream.h"

namespace xlang3::serialize {

BlockStream::BlockStream() = default;

BlockStream::BlockStream(char* buf, STREAM_SIZE size, bool own_buf) {
  BlockInfo info;
  info.buf = buf;
  info.block_size = size;
  info.data_size = size;
  info.own_buf = own_buf;
  blocks_.push_back(info);
  SetPos({0, 0});
}

BlockStream::~BlockStream() {
  for (auto& block : blocks_) {
    if (block.own_buf) {
      delete[] block.buf;
    }
  }
}

int BlockStream::BlockNum() {
  return static_cast<int>(blocks_.size());
}

blockInfo& BlockStream::GetBlockInfo(int index) {
  return blocks_[static_cast<size_t>(index)];
}

bool BlockStream::NewBlock() {
  char* data = new char[static_cast<size_t>(kBlockSize)];
  BlockInfo info;
  info.buf = data;
  info.block_size = kBlockSize;
  info.data_size = 0;
  info.own_buf = true;
  blocks_.push_back(info);
  return true;
}

bool BlockStream::MoveToNextBlock() {
  return true;
}

void BlockStream::Refresh() {}

} // namespace xlang3::serialize
