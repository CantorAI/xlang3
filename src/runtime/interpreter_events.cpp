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
#include "xlang3/interpreter_events.h"

#include <atomic>

namespace xlang3 {
namespace {

std::atomic<uint32_t> g_interpreter_pending_events{0};
thread_local uint32_t g_local_weakref_hint = 0;
thread_local uint32_t g_global_poll_countdown = 64;

} // namespace

uint32_t interpreter_pending_events() noexcept {
  return g_interpreter_pending_events.load(std::memory_order_acquire);
}

uint32_t interpreter_poll_pending_events() noexcept {
  // Weakref callbacks are enqueued synchronously by the thread releasing the
  // last target reference. Preserve next-opcode delivery without a global
  // acquire load on every dispatch; cross-thread events are still bounded to
  // at most 64 VM instructions (and are polled at frame/control boundaries).
  if (g_local_weakref_hint != 0) {
    g_local_weakref_hint = 0;
    return g_interpreter_pending_events.load(std::memory_order_acquire);
  }
  if (--g_global_poll_countdown != 0) return 0;
  g_global_poll_countdown = 64;
  return g_interpreter_pending_events.load(std::memory_order_acquire);
}

void interpreter_set_pending_event(uint32_t event) noexcept {
  if ((event & kInterpreterEventWeakrefCallbacks) != 0) {
    g_local_weakref_hint |= event & kInterpreterEventWeakrefCallbacks;
  }
  g_interpreter_pending_events.fetch_or(event, std::memory_order_release);
}

void interpreter_clear_pending_event(uint32_t event) noexcept {
  if ((event & kInterpreterEventWeakrefCallbacks) != 0) {
    g_local_weakref_hint &= ~(event & kInterpreterEventWeakrefCallbacks);
  }
  g_interpreter_pending_events.fetch_and(~event, std::memory_order_acq_rel);
}

} // namespace xlang3
