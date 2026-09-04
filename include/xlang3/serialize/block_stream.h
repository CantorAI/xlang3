/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "xlang_stream.h"

#include <vector>

namespace xlang3::serialize {

class BlockStream : public XLangStream {
public:
  BlockStream();
  BlockStream(char* buf, STREAM_SIZE size, bool own_buf);
  ~BlockStream() override;

  int BlockNum() override;
  blockInfo& GetBlockInfo(int index) override;
  bool NewBlock() override;
  bool MoveToNextBlock() override;
  void Refresh() override;

private:
  struct BlockInfo : blockInfo {
    bool own_buf = false;
  };

  static constexpr STREAM_SIZE kBlockSize = 32 * 1024;
  std::vector<BlockInfo> blocks_;
};

} // namespace xlang3::serialize
