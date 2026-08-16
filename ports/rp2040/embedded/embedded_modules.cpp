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
#include "embedded/embedded_modules.h"

#include "board/time.h"
#include "embedded/gpio_irq.h"
#include "xlang3/module_object.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3::pico {
namespace {

constexpr uint kI2CTimeoutUs = 1000;
bool g_i2c0_initialized = false;
uint32_t g_i2c0_sda = 0;
uint32_t g_i2c0_scl = 0;
uint32_t g_i2c0_baud = 0;

bool require_uint_arg(const Value* args, uint32_t argc, uint32_t index, const char* name, uint32_t& out, std::string& error) {
  if (index >= argc || args[index].tag != ValueTag::Int64 || args[index].as.i64 < 0) {
    error = std::string(name) + "() argument " + std::to_string(index + 1) + " must be a non-negative int";
    return false;
  }
  out = static_cast<uint32_t>(args[index].as.i64);
  return true;
}

bool require_argc(uint32_t argc, uint32_t expected, const char* name, std::string& error) {
  if (argc == expected) {
    return true;
  }
  error = std::string(name) + "() expected " + std::to_string(expected) + " arguments";
  return false;
}

void init_i2c_bus(uint32_t sda, uint32_t scl, uint32_t baud) {
  const uint32_t effective_baud = baud == 0 ? 100000 : baud;
  if (g_i2c0_initialized &&
      g_i2c0_sda == sda &&
      g_i2c0_scl == scl &&
      g_i2c0_baud == effective_baud) {
    return;
  }
  i2c_init(i2c0, effective_baud);
  gpio_set_function(sda, GPIO_FUNC_I2C);
  gpio_set_function(scl, GPIO_FUNC_I2C);
  gpio_pull_up(sda);
  gpio_pull_up(scl);
  g_i2c0_initialized = true;
  g_i2c0_sda = sda;
  g_i2c0_scl = scl;
  g_i2c0_baud = effective_baud;
}

bool list_to_bytes(const Value& value, const char* name, std::vector<uint8_t>& out, std::string& error) {
  auto* list = value_as_list(value);
  if (list == nullptr) {
    error = std::string(name) + "() data must be a list of byte ints";
    return false;
  }
  out.clear();
  out.reserve(list->items.size());
  for (const auto& item : list->items) {
    if (item.tag != ValueTag::Int64 || item.as.i64 < 0 || item.as.i64 > 255) {
      error = std::string(name) + "() data must contain byte ints";
      return false;
    }
    out.push_back(static_cast<uint8_t>(item.as.i64));
  }
  return true;
}

Value gpio_wait_result_to_dict(const GpioWaitResult& result) {
  return Value::dict({
      {Value::string("triggered"), Value::boolean(result.triggered)},
      {Value::string("pin"), Value::int64(result.pin)},
      {Value::string("events"), Value::int64(result.events)},
      {Value::string("sequence"), Value::int64(result.sequence)},
      {Value::string("overflow"), Value::int64(result.overflow)},
      {Value::string("value"), Value::int64(result.value)},
      {Value::string("elapsed_ms"), Value::int64(static_cast<int64_t>(result.elapsed_ms))},
  });
}

bool gpio_config_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 2 && argc != 3) {
    error = "gpio.config() expected 2 or 3 arguments";
    return false;
  }
  uint32_t pin = 0;
  uint32_t mode = 0;
  uint32_t pull = kGpioPullNone;
  if (!require_uint_arg(args, argc, 0, "gpio.config", pin, error) ||
      !require_uint_arg(args, argc, 1, "gpio.config", mode, error)) {
    return false;
  }
  if (argc == 3 && !require_uint_arg(args, argc, 2, "gpio.config", pull, error)) {
    return false;
  }
  if (!gpio_configure(pin, mode, pull, error)) {
    error = "gpio.config: " + error;
    return false;
  }
  value_set_none(out);
  return true;
}

bool gpio_read_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!require_argc(argc, 1, "gpio.read", error)) {
    return false;
  }
  uint32_t pin = 0;
  uint32_t value = 0;
  if (!require_uint_arg(args, argc, 0, "gpio.read", pin, error) || !gpio_read_pin(pin, value, error)) {
    return false;
  }
  value_set_int64(out, value);
  return true;
}

bool gpio_write_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!require_argc(argc, 2, "gpio.write", error)) {
    return false;
  }
  uint32_t pin = 0;
  uint32_t value = 0;
  if (!require_uint_arg(args, argc, 0, "gpio.write", pin, error) ||
      !require_uint_arg(args, argc, 1, "gpio.write", value, error) ||
      !gpio_write_pin(pin, value, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool gpio_wait_edge_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 2 && argc != 3) {
    error = "gpio.wait_edge() expected 2 or 3 arguments";
    return false;
  }
  uint32_t pin = 0;
  uint32_t edge = 0;
  uint32_t timeout_ms = 10000;
  if (!require_uint_arg(args, argc, 0, "gpio.wait_edge", pin, error) ||
      !require_uint_arg(args, argc, 1, "gpio.wait_edge", edge, error)) {
    return false;
  }
  if (argc == 3 && !require_uint_arg(args, argc, 2, "gpio.wait_edge", timeout_ms, error)) {
    return false;
  }
  GpioWaitResult result;
  if (!gpio_wait_for_edge(pin, edge, timeout_ms, result, error)) {
    error = "gpio.wait_edge: " + error;
    return false;
  }
  out = gpio_wait_result_to_dict(result);
  return true;
}

bool i2c_write_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!require_argc(argc, 5, "i2c.write", error)) {
    return false;
  }
  uint32_t sda = 0;
  uint32_t scl = 0;
  uint32_t baud = 0;
  uint32_t address = 0;
  std::vector<uint8_t> data;
  if (!require_uint_arg(args, argc, 0, "i2c.write", sda, error) ||
      !require_uint_arg(args, argc, 1, "i2c.write", scl, error) ||
      !require_uint_arg(args, argc, 2, "i2c.write", baud, error) ||
      !require_uint_arg(args, argc, 3, "i2c.write", address, error) ||
      !list_to_bytes(args[4], "i2c.write", data, error)) {
    return false;
  }
  if (address == 0 || address >= 0x80) {
    error = "i2c.write: invalid address";
    return false;
  }
  init_i2c_bus(sda, scl, baud);
  const int rc = i2c_write_timeout_us(
      i2c0,
      static_cast<uint8_t>(address),
      data.data(),
      data.size(),
      false,
      kI2CTimeoutUs);
  if (rc < 0) {
    error = "i2c.write: device did not acknowledge";
    return false;
  }
  value_set_int64(out, rc);
  return true;
}

bool i2c_scan_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 2 && argc != 3) {
    error = "i2c.scan() expected 2 or 3 arguments";
    return false;
  }
  uint32_t sda = 0;
  uint32_t scl = 0;
  uint32_t baud = 100000;
  if (!require_uint_arg(args, argc, 0, "i2c.scan", sda, error) ||
      !require_uint_arg(args, argc, 1, "i2c.scan", scl, error)) {
    return false;
  }
  if (argc == 3 && !require_uint_arg(args, argc, 2, "i2c.scan", baud, error)) {
    return false;
  }
  init_i2c_bus(sda, scl, baud);
  std::vector<Value> addresses;
  for (uint8_t address = 0x20; address <= 0x27; ++address) {
    uint8_t dummy = 0;
    const int rc = i2c_write_timeout_us(i2c0, address, &dummy, 1, false, kI2CTimeoutUs);
    if (rc >= 0) {
      addresses.push_back(Value::int64(address));
    }
    board::sleep_milliseconds(1);
  }
  out = Value::list(std::move(addresses));
  return true;
}

bool sleep_ms_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!require_argc(argc, 1, "time.sleep_ms", error)) {
    return false;
  }
  uint32_t ms = 0;
  if (!require_uint_arg(args, argc, 0, "time.sleep_ms", ms, error)) {
    return false;
  }
  board::sleep_milliseconds(ms);
  value_set_none(out);
  return true;
}

} // namespace

void register_embedded_modules(Runtime& runtime) {
  NativeModuleBuilder gpio(runtime, "gpio");
  gpio.value("IN", Value::int64(kGpioModeIn))
      .value("OUT", Value::int64(kGpioModeOut))
      .value("PULL_NONE", Value::int64(kGpioPullNone))
      .value("PULL_UP", Value::int64(kGpioPullUp))
      .value("PULL_DOWN", Value::int64(kGpioPullDown))
      .value("RISING", Value::int64(kGpioEdgeRising))
      .value("FALLING", Value::int64(kGpioEdgeFalling))
      .value("CHANGE", Value::int64(kGpioEdgeChange))
      .function("config", gpio_config_fn)
      .function("read", gpio_read_fn)
      .function("write", gpio_write_fn)
      .function("wait_edge", gpio_wait_edge_fn);
  runtime.register_module("gpio", gpio.finish());

  NativeModuleBuilder i2c(runtime, "i2c");
  i2c.function("write", i2c_write_fn)
      .function("scan", i2c_scan_fn);
  runtime.register_module("i2c", i2c.finish());

  NativeModuleBuilder time(runtime, "time");
  time.function("sleep_ms", sleep_ms_fn);
  runtime.register_module("time", time.finish());
}

} // namespace xlang3::pico
