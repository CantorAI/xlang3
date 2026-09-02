/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include <string>
#include <string_view>

namespace xlang3 {
struct Value;
}

namespace xlang3::serialize {

using STREAM_SIZE = long long;

struct blockIndex {
  int block_index = 0;
  STREAM_SIZE offset = 0;
};

struct blockInfo {
  char* buf = nullptr;
  STREAM_SIZE block_size = 0;
  STREAM_SIZE data_size = 0;
};

struct IpcWireValue;
class IpcMarshalContext;

class XLStream {
public:
  virtual ~XLStream() = default;
  virtual STREAM_SIZE Size() = 0;
  virtual blockIndex GetPos() = 0;
  virtual void SetPos(blockIndex pos) = 0;
  virtual void Refresh() = 0;
  virtual int BlockNum() = 0;
  virtual blockInfo& GetBlockInfo(int index) = 0;
  virtual bool NewBlock() = 0;
  virtual bool MoveToNextBlock() = 0;
  virtual bool FullCopyTo(char* buf, STREAM_SIZE bufSize) = 0;
};

class XLangStream : public XLStream {
public:
  XLangStream() = default;
  explicit XLangStream(XLStream* provider) { SetProvider(provider); }
  ~XLangStream() override;

  void SetProvider(XLStream* provider);
  void SetMarshalContext(IpcMarshalContext* context) { marshal_context_ = context; }
  IpcMarshalContext* MarshalContext() const { return marshal_context_; }
  void ResetPos();

  STREAM_SIZE Size() override { return size_; }
  bool FullCopyTo(char* buf, STREAM_SIZE bufSize) override;
  bool CopyTo(char* buf, STREAM_SIZE size);
  bool appendchar(char c);
  bool fetchchar(char& c);
  bool append(const void* data, STREAM_SIZE size);
  bool append_view(std::string_view data);
  bool fetch_bytes(std::string& bytes, STREAM_SIZE size);

  template <typename T>
  XLangStream& operator<<(const T& value) {
    append(&value, static_cast<STREAM_SIZE>(sizeof(value)));
    return *this;
  }

  template <typename T>
  XLangStream& operator>>(T& value) {
    CopyTo(reinterpret_cast<char*>(&value), static_cast<STREAM_SIZE>(sizeof(value)));
    return *this;
  }

  XLangStream& operator<<(std::string_view value);
  XLangStream& operator<<(const std::string& value);
  XLangStream& operator>>(std::string& value);

  bool MarshalToBytes(const Value& value, const std::string& callable_name, std::string& error);
  bool MarshalFromBytes(IpcWireValue& value, std::string& error);
  void MarshalError(const std::string& message);

  blockIndex GetPos() override { return curPos_; }
  void SetPos(blockIndex pos) override;
  STREAM_SIZE CalcSize(blockIndex pos);
  STREAM_SIZE CalcSize();
  bool IsEOS();

  void Refresh() override;
  int BlockNum() override;
  blockInfo& GetBlockInfo(int index) override;
  bool NewBlock() override;
  bool MoveToNextBlock() override;

private:
  XLStream* provider_ = nullptr;
  IpcMarshalContext* marshal_context_ = nullptr;
  blockIndex curPos_{0, 0};
  STREAM_SIZE size_ = 0;
};

} // namespace xlang3::serialize
