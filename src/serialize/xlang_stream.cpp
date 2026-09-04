/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "serialize/xlang_stream.h"

#include <cstring>

namespace xlang3::serialize {

XLangStream::~XLangStream() {
  if (provider_ != nullptr) {
    provider_->SetPos(GetPos());
  }
}

void XLangStream::SetProvider(XLStream* provider) {
  provider_ = provider;
  if (provider_ != nullptr) {
    SetPos(provider_->GetPos());
  }
}

void XLangStream::ResetPos() {
  curPos_ = {0, 0};
  size_ = CalcSize(curPos_);
}

bool XLangStream::FullCopyTo(char* buf, STREAM_SIZE bufSize) {
  if (bufSize < Size()) {
    return false;
  }
  for (int i = 0; i < BlockNum(); ++i) {
    blockInfo& block = GetBlockInfo(i);
    if (block.data_size > 0) {
      std::memcpy(buf, block.buf, static_cast<size_t>(block.data_size));
      buf += block.data_size;
    }
  }
  return true;
}

bool XLangStream::CopyTo(char* buf, STREAM_SIZE size) {
  blockIndex pos = curPos_;
  STREAM_SIZE left = size;
  char* output = buf;
  int block_index = pos.block_index;
  STREAM_SIZE block_offset = pos.offset;
  while (left > 0) {
    if (block_index >= BlockNum()) {
      return false;
    }
    blockInfo& block = GetBlockInfo(block_index);
    STREAM_SIZE rest = block.data_size - block_offset;
    STREAM_SIZE copy_size = left < rest ? left : rest;
    if (copy_size < 0) {
      return false;
    }
    if (buf != nullptr && copy_size > 0) {
      std::memcpy(output, block.buf + block_offset, static_cast<size_t>(copy_size));
    }
    if (output != nullptr) {
      output += copy_size;
    }
    block_offset += copy_size;
    left -= copy_size;
    if (left > 0) {
      if (!MoveToNextBlock()) {
        return false;
      }
      block_offset = 0;
      ++block_index;
    }
  }
  curPos_.block_index = block_index;
  curPos_.offset = block_offset;
  return true;
}

bool XLangStream::appendchar(char c) {
  return append(&c, 1);
}

bool XLangStream::fetchchar(char& c) {
  return CopyTo(&c, 1);
}

bool XLangStream::append(const void* data, STREAM_SIZE size) {
  if (size < 0) {
    return false;
  }
  const char* input = static_cast<const char*>(data);
  STREAM_SIZE left = size;
  int block_index = curPos_.block_index;
  STREAM_SIZE block_offset = curPos_.offset;
  while (left > 0) {
    if (block_index >= BlockNum() && !NewBlock()) {
      return false;
    }
    blockInfo& block = GetBlockInfo(block_index);
    STREAM_SIZE rest = block.block_size - block_offset;
    STREAM_SIZE copy_size = left < rest ? left : rest;
    if (copy_size <= 0) {
      ++block_index;
      block_offset = 0;
      continue;
    }
    std::memcpy(block.buf + block_offset, input, static_cast<size_t>(copy_size));
    block_offset += copy_size;
    input += copy_size;
    left -= copy_size;
    if (block.data_size < block_offset) {
      block.data_size = block_offset;
    }
    if (left > 0) {
      ++block_index;
      block_offset = 0;
    }
  }
  curPos_.block_index = block_index;
  curPos_.offset = block_offset;
  size_ += size;
  return true;
}

bool XLangStream::append_view(std::string_view data) {
  return data.empty() || append(data.data(), static_cast<STREAM_SIZE>(data.size()));
}

bool XLangStream::fetch_bytes(std::string& bytes, STREAM_SIZE size) {
  if (size < 0) {
    return false;
  }
  bytes.resize(static_cast<size_t>(size));
  return size == 0 || CopyTo(bytes.data(), size);
}

XLangStream& XLangStream::operator<<(std::string_view value) {
  const auto size = static_cast<uint32_t>(value.size());
  (*this) << size;
  append_view(value);
  return *this;
}

XLangStream& XLangStream::operator<<(const std::string& value) {
  return (*this) << std::string_view(value);
}

XLangStream& XLangStream::operator>>(std::string& value) {
  uint32_t size = 0;
  (*this) >> size;
  fetch_bytes(value, size);
  return *this;
}

void XLangStream::SetPos(blockIndex pos) {
  curPos_ = pos;
  size_ = CalcSize(curPos_);
}

STREAM_SIZE XLangStream::CalcSize(blockIndex pos) {
  if (BlockNum() == 0 && pos.block_index == 0 && pos.offset == 0) {
    return 0;
  }
  if (BlockNum() <= pos.block_index) {
    return -1;
  }
  STREAM_SIZE size = 0;
  for (int i = 0; i < pos.block_index; ++i) {
    size += GetBlockInfo(i).data_size;
  }
  size += pos.offset;
  return size;
}

STREAM_SIZE XLangStream::CalcSize() {
  STREAM_SIZE size = 0;
  for (int i = 0; i < BlockNum(); ++i) {
    size += GetBlockInfo(i).data_size;
  }
  return size;
}

bool XLangStream::IsEOS() {
  if ((BlockNum() - 1) != curPos_.block_index) {
    return false;
  }
  blockInfo& block = GetBlockInfo(curPos_.block_index);
  return block.data_size == curPos_.offset;
}

void XLangStream::Refresh() {
  if (provider_ != nullptr) {
    provider_->Refresh();
  }
}

int XLangStream::BlockNum() {
  return provider_ != nullptr ? provider_->BlockNum() : 0;
}

blockInfo& XLangStream::GetBlockInfo(int index) {
  if (provider_ != nullptr) {
    return provider_->GetBlockInfo(index);
  }
  static blockInfo empty;
  return empty;
}

bool XLangStream::NewBlock() {
  return provider_ != nullptr && provider_->NewBlock();
}

bool XLangStream::MoveToNextBlock() {
  return provider_ != nullptr && provider_->MoveToNextBlock();
}

} // namespace xlang3::serialize
