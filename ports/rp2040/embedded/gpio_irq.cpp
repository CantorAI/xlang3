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
#include "embedded/gpio_irq.h"

#include "board/time.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

#include <vector>

namespace xlang3::pico {
namespace {

constexpr uint32_t kGpioIrqQueueSize = 32;

volatile uint32_t g_gpio_irq_head = 0;
volatile uint32_t g_gpio_irq_tail = 0;
volatile uint32_t g_gpio_irq_sequence = 0;
volatile uint32_t g_gpio_irq_overflow = 0;
GpioIrqEvent g_gpio_irq_queue[kGpioIrqQueueSize] = {};

void gpio_irq_callback(uint gpio, uint32_t events) {
  if (gpio >= kMaxGpioPins) {
    return;
  }
  const uint32_t head = g_gpio_irq_head;
  const uint32_t next = (head + 1u) % kGpioIrqQueueSize;
  if (next == g_gpio_irq_tail) {
    ++g_gpio_irq_overflow;
    return;
  }
  g_gpio_irq_queue[head] = GpioIrqEvent{gpio, events, ++g_gpio_irq_sequence};
  g_gpio_irq_head = next;
}

bool pop_gpio_irq_event(GpioIrqEvent& out) {
  const uint32_t interrupts = save_and_disable_interrupts();
  if (g_gpio_irq_tail == g_gpio_irq_head) {
    restore_interrupts(interrupts);
    return false;
  }
  out = g_gpio_irq_queue[g_gpio_irq_tail];
  g_gpio_irq_tail = (g_gpio_irq_tail + 1u) % kGpioIrqQueueSize;
  restore_interrupts(interrupts);
  return true;
}

void push_gpio_irq_event(const GpioIrqEvent& event) {
  const uint32_t interrupts = save_and_disable_interrupts();
  const uint32_t head = g_gpio_irq_head;
  const uint32_t next = (head + 1u) % kGpioIrqQueueSize;
  if (next == g_gpio_irq_tail) {
    ++g_gpio_irq_overflow;
  } else {
    g_gpio_irq_queue[head] = event;
    g_gpio_irq_head = next;
  }
  restore_interrupts(interrupts);
}

bool gpio_irq_event_matches(const GpioIrqEvent& event, uint32_t pin, uint32_t event_mask) {
  return event.pin == pin && (event.events & event_mask) != 0;
}

bool gpio_event_mask(uint32_t edge, uint32_t& out) {
  uint32_t mask = 0;
  if ((edge & kGpioEdgeRising) != 0) {
    mask |= GPIO_IRQ_EDGE_RISE;
  }
  if ((edge & kGpioEdgeFalling) != 0) {
    mask |= GPIO_IRQ_EDGE_FALL;
  }
  out = mask;
  return mask != 0;
}

} // namespace

bool gpio_configure(uint32_t pin, uint32_t mode, uint32_t pull, std::string& error) {
  if (pin >= kMaxGpioPins) {
    error = "invalid GPIO pin";
    return false;
  }
  gpio_init(pin);
  gpio_set_dir(pin, mode == kGpioModeOut ? GPIO_OUT : GPIO_IN);
  if (pull == kGpioPullUp) {
    gpio_pull_up(pin);
  } else if (pull == kGpioPullDown) {
    gpio_pull_down(pin);
  } else {
    gpio_disable_pulls(pin);
  }
  return true;
}

bool gpio_read_pin(uint32_t pin, uint32_t& out, std::string& error) {
  if (pin >= kMaxGpioPins) {
    error = "invalid GPIO pin";
    return false;
  }
  out = gpio_get(pin) ? 1u : 0u;
  return true;
}

bool gpio_write_pin(uint32_t pin, uint32_t value, std::string& error) {
  if (pin >= kMaxGpioPins) {
    error = "invalid GPIO pin";
    return false;
  }
  gpio_put(pin, value != 0);
  return true;
}

bool gpio_wait_for_edge(uint32_t pin, uint32_t edge, uint32_t timeout_ms, GpioWaitResult& out, std::string& error) {
  if (pin >= kMaxGpioPins) {
    error = "invalid GPIO pin";
    return false;
  }
  uint32_t event_mask = 0;
  if (!gpio_event_mask(edge, event_mask)) {
    error = "invalid GPIO edge";
    return false;
  }

  GpioIrqEvent event;
  std::vector<GpioIrqEvent> deferred;
  while (pop_gpio_irq_event(event)) {
    if (event.pin != pin) {
      deferred.push_back(event);
    }
  }
  for (const auto& item : deferred) {
    push_gpio_irq_event(item);
  }
  deferred.clear();

  gpio_set_irq_enabled_with_callback(pin, event_mask, true, gpio_irq_callback);

  const absolute_time_t start = get_absolute_time();
  const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  GpioIrqEvent matched;
  bool triggered = false;
  while (timeout_ms == 0 || absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
    while (pop_gpio_irq_event(event)) {
      if (gpio_irq_event_matches(event, pin, event_mask)) {
        matched = event;
        triggered = true;
        break;
      }
      deferred.push_back(event);
    }
    if (triggered) {
      break;
    }
    board::sleep_milliseconds(1);
  }

  gpio_set_irq_enabled(pin, event_mask, false);
  for (const auto& item : deferred) {
    push_gpio_irq_event(item);
  }

  out.triggered = triggered;
  out.pin = pin;
  out.events = triggered ? matched.events : 0;
  out.sequence = triggered ? matched.sequence : 0;
  out.overflow = g_gpio_irq_overflow;
  out.value = gpio_get(pin) ? 1u : 0u;
  out.elapsed_ms = static_cast<uint64_t>(absolute_time_diff_us(start, get_absolute_time()) / 1000);
  return true;
}

} // namespace xlang3::pico
