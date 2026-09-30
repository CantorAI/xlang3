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

namespace xlang3 {

// Low-frequency asynchronous work shares one VM safepoint poll. Producers set
// bits when work is queued; the interpreter tests all sources with one acquire
// load per opcode rather than polling each subsystem independently.
constexpr uint32_t kInterpreterEventWeakrefCallbacks = 1u << 0;
constexpr uint32_t kInterpreterEventSignals = 1u << 1;

uint32_t interpreter_pending_events() noexcept;
// Cheap per-op poll: local weakref callbacks remain immediate, while signals
// and events queued by other threads are sampled at a bounded interval.
uint32_t interpreter_poll_pending_events() noexcept;
// Restore a poll hint after a batched opcode observes work it must hand back
// to the VM dispatcher for delivery.
void interpreter_hint_pending_event_poll(uint32_t event) noexcept;
void interpreter_set_pending_event(uint32_t event) noexcept;
void interpreter_clear_pending_event(uint32_t event) noexcept;

} // namespace xlang3
