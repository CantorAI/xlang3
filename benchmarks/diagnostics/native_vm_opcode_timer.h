/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0.
*/
#pragma once

// Diagnostic instrumentation only: the ordinary runtime must not include this
// header. Set XLANG3_VM_OPCODE_TIMING=1 in the diagnostic process to activate
// its scopes and stderr report. Clock reads and counter updates perturb
// execution. Use a preserved Release binary for pyperf comparisons and this
// separately built probe to
// locate native costs. A parent scope excludes nested execution and its probe
// recording overhead, so recursive interpreter calls are not counted twice.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace xlang3::diagnostics {

// These diagnostic IDs must stay above the IR enum used by the probe build.
constexpr uint16_t kVMInvocationTimer = 253;
constexpr uint16_t kVMFrameSwitchTimer = 254;
constexpr uint16_t kVMLoopControlTimer = 255;

struct VMOpcodeTimingRow {
  std::atomic<uint64_t> calls{0};
  std::atomic<uint64_t> self_ns{0};
  std::atomic<uint64_t> total_ns{0};
};

struct VMOpcodeTimingStore {
  using Clock = std::chrono::steady_clock;
  std::array<VMOpcodeTimingRow, 256> rows{};
  bool enabled = false;
  uint64_t clock_pair_mean_ns = 0;

  VMOpcodeTimingStore() {
    const char* flag = std::getenv("XLANG3_VM_OPCODE_TIMING");
    enabled = flag != nullptr && flag[0] == '1' && flag[1] == '\0';
    if (!enabled) return;
    uint64_t elapsed_ns = 0;
    constexpr uint64_t samples = 4096;
    for (uint64_t i = 0; i < samples; ++i) {
      const auto first = Clock::now();
      const auto second = Clock::now();
      elapsed_ns += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(second - first).count());
    }
    clock_pair_mean_ns = elapsed_ns / samples;
  }

  ~VMOpcodeTimingStore() {
    if (!enabled) return;
    struct Snapshot {
      uint16_t opcode;
      uint64_t calls;
      uint64_t self_ns;
      uint64_t total_ns;
    };
    std::vector<Snapshot> snapshots;
    for (uint16_t i = 0; i < rows.size(); ++i) {
      const auto count = rows[i].calls.load(std::memory_order_relaxed);
      if (count != 0) {
        snapshots.push_back({i, count,
            rows[i].self_ns.load(std::memory_order_relaxed),
            rows[i].total_ns.load(std::memory_order_relaxed)});
      }
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& a, const auto& b) {
      return a.self_ns > b.self_ns;
    });
    std::fprintf(stderr, "vm-time: clock_pair_mean_ns=%llu\n",
        static_cast<unsigned long long>(clock_pair_mean_ns));
    for (const auto& row : snapshots) {
      std::fprintf(stderr, "vm-time: opcode=%u calls=%llu self_ns=%llu total_ns=%llu\n",
          static_cast<unsigned>(row.opcode),
          static_cast<unsigned long long>(row.calls),
          static_cast<unsigned long long>(row.self_ns),
          static_cast<unsigned long long>(row.total_ns));
    }
    std::fflush(stderr);
  }
};

inline VMOpcodeTimingStore& vm_opcode_timing_store() {
  static VMOpcodeTimingStore store;
  return store;
}

class VMOpcodeTimer;
inline thread_local VMOpcodeTimer* active_vm_opcode_timer = nullptr;

class VMOpcodeTimer {
  using Clock = std::chrono::steady_clock;
  VMOpcodeTimingStore* store_;
  VMOpcodeTimer* parent_;
  uint16_t opcode_;
  uint64_t child_probe_ns_ = 0;
  Clock::time_point start_;

public:
  explicit VMOpcodeTimer(uint16_t opcode)
      : store_(&vm_opcode_timing_store()), parent_(active_vm_opcode_timer), opcode_(opcode) {
    if (!store_->enabled || opcode_ >= store_->rows.size()) return;
    active_vm_opcode_timer = this;
    start_ = Clock::now();
  }
  VMOpcodeTimer(const VMOpcodeTimer&) = delete;
  VMOpcodeTimer& operator=(const VMOpcodeTimer&) = delete;

  ~VMOpcodeTimer() {
    if (!store_->enabled || opcode_ >= store_->rows.size()) return;
    const auto end = Clock::now();
    const uint64_t elapsed_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count());
    active_vm_opcode_timer = parent_;
    const uint64_t self_ns = elapsed_ns > child_probe_ns_ ? elapsed_ns - child_probe_ns_ : 0;
    auto& row = store_->rows[opcode_];
    row.calls.fetch_add(1, std::memory_order_relaxed);
    row.self_ns.fetch_add(self_ns, std::memory_order_relaxed);
    row.total_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
    if (parent_ != nullptr) {
      // Include this child's clock/counter bookkeeping in the subtraction,
      // keeping it out of the enclosing opcode's reported self time.
      parent_->child_probe_ns_ += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_).count());
    }
  }
};

} // namespace xlang3::diagnostics
