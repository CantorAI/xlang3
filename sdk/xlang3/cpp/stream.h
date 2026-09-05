/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once
#include "xlang3/cpp/runtime.h"

namespace X {

// The host and runtime must outlive the stream. Only the stream handle is owned.
class Stream {
public:
  explicit Stream(X3PackageHost* host) : host_(host) {
    Validate();
    Check(host_->stream_create(host_->runtime, &stream_));
  }
  explicit Stream(Runtime& runtime) : Stream(runtime.host()) {}
  Stream(X3PackageHost* host, const void* data, uint64_t size) : host_(host) {
    Validate();
    X3StreamBlock block{data, size};
    Check(host_->stream_from_blocks(host_->runtime, &block, 1, &stream_));
  }
  Stream(Runtime& runtime, const void* data, uint64_t size) : Stream(runtime.host(), data, size) {}
  Stream(X3PackageHost* host, const X3StreamBlock* blocks, uint32_t count) : host_(host) {
    Validate();
    Check(host_->stream_from_blocks(host_->runtime, blocks, count, &stream_));
  }
  Stream(X3PackageHost* host, X3StreamAllocate allocate, void* context) : host_(host) {
    Validate();
    Check(host_->stream_create_provider(host_->runtime, allocate, context, &stream_));
  }
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
  Stream(Stream&& other) noexcept : host_(other.host_), stream_(std::exchange(other.stream_, nullptr)) {}
  Stream& operator=(Stream&& other) noexcept {
    if (this != &other) {
      Reset();
      host_ = other.host_;
      stream_ = std::exchange(other.stream_, nullptr);
    }
    return *this;
  }
  ~Stream() { Reset(); }
  X3PackageHost* host() const { return host_; }
  X3Stream* get() const { return stream_; }
  uint64_t Size() const { return host_->stream_size(stream_); }
  bool Rewind() { return host_->stream_rewind(stream_) == X3_STATUS_OK; }
  bool Write(const void* data, uint64_t size) { return host_->stream_write(stream_, data, size) == X3_STATUS_OK; }
  bool Read(void* data, uint64_t size) { return host_->stream_read(stream_, data, size) == X3_STATUS_OK; }
  bool FullCopyTo(void* data, uint64_t size) const { return host_->stream_copy(stream_, data, size) == X3_STATUS_OK; }
private:
  void Validate() const {
    if (!host_ || host_->abi_version != X3_ABI_VERSION || host_->size < sizeof(X3PackageHost) ||
        !host_->stream_create || !host_->stream_destroy)
      throw std::runtime_error("host does not support streams");
  }
  void Check(X3Status status) const {
    if (status != X3_STATUS_OK) throw std::runtime_error(host_->runtime_last_error(host_->runtime));
  }
  void Reset() { if (stream_) host_->stream_destroy(std::exchange(stream_, nullptr)); }
  X3PackageHost* host_ = nullptr;
  X3Stream* stream_ = nullptr;
};

inline bool Value::ToBytes(Stream& stream) const {
  auto* host = stream.host();
  if (host_ && host_->runtime != host->runtime) return false;
  return host->value_to_stream(host->runtime, value_, stream.get()) == X3_STATUS_OK;
}
inline bool Value::FromBytes(Stream& stream) {
  auto* host = stream.host();
  X3Value result = x3_value_invalid();
  if (host->value_from_stream(host->runtime, stream.get(), &result) != X3_STATUS_OK) return false;
  *this = Value(host, result, false);
  return true;
}
} // namespace X
