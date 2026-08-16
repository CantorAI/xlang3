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
#include <string>

namespace xlang3::pico {

constexpr uint32_t kGpioModeIn = 0;
constexpr uint32_t kGpioModeOut = 1;
constexpr uint32_t kGpioPullNone = 0;
constexpr uint32_t kGpioPullUp = 1;
constexpr uint32_t kGpioPullDown = 2;
constexpr uint32_t kGpioEdgeRising = 1;
constexpr uint32_t kGpioEdgeFalling = 2;
constexpr uint32_t kGpioEdgeChange = 3;
constexpr uint32_t kMaxGpioPins = 30;

struct GpioIrqEvent {
  uint32_t pin = 0;
  uint32_t events = 0;
  uint32_t sequence = 0;
};

struct GpioWaitResult {
  bool triggered = false;
  uint32_t pin = 0;
  uint32_t events = 0;
  uint32_t sequence = 0;
  uint32_t overflow = 0;
  uint32_t value = 0;
  uint64_t elapsed_ms = 0;
};

bool gpio_configure(uint32_t pin, uint32_t mode, uint32_t pull, std::string& error);
bool gpio_read_pin(uint32_t pin, uint32_t& out, std::string& error);
bool gpio_write_pin(uint32_t pin, uint32_t value, std::string& error);
bool gpio_wait_for_edge(uint32_t pin, uint32_t edge, uint32_t timeout_ms, GpioWaitResult& out, std::string& error);

} // namespace xlang3::pico
