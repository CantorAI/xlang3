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

#include "xlang_frame.h"
#include "xlang3/runtime.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Logical XLang/Python execution context.

Ownership rule:
A VMThread owns the frame stack and saved execution state. It is not an OS
thread. Many VMThreads can be cooperatively scheduled on one OS thread, or later
distributed across a worker pool when object-safety permits.
*/

namespace xlang3 {

enum class XlangVMThreadState : uint8_t {
  New,
  Ready,
  Running,
  Waiting,
  Done,
  Error,
};

enum class XlangVMWaitKind : uint8_t {
  None,
  Timer,
  IO,
  Command,
  RemoteCall,
  Future,
};

struct XlangVMWaitState {
  XlangVMWaitKind kind = XlangVMWaitKind::None;
  uint64_t token = 0;
};

class XlangVMThread {
public:
  explicit XlangVMThread(uint64_t id) : id_(id) {
    frames_.reserve(64);
  }

  uint64_t id() const {
    return id_;
  }

  XlangVMThreadState state() const {
    return state_;
  }

  void set_state(XlangVMThreadState state) {
    state_ = state;
  }

  XlangVMWaitState& wait_state() {
    return wait_state_;
  }

  const XlangVMWaitState& wait_state() const {
    return wait_state_;
  }

  std::vector<XlangVMFrame>& frames() {
    return frames_;
  }

  const std::vector<XlangVMFrame>& frames() const {
    return frames_;
  }

  size_t active_frame_count() const {
    return active_frame_count_;
  }

  void set_active_frame_count(size_t count) {
    active_frame_count_ = count;
  }

  bool has_frames() const {
    return active_frame_count_ != 0;
  }

  XlangVMFrame& current_frame() {
    return frames_[active_frame_count_ - 1];
  }

private:
  uint64_t id_ = 0;
  XlangVMThreadState state_ = XlangVMThreadState::New;
  XlangVMWaitState wait_state_;
  std::vector<XlangVMFrame> frames_;
  size_t active_frame_count_ = 0;
};

} // namespace xlang3
