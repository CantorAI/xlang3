#pragma once

#include "../../src/runtime/modules/thread/runtime_lock.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

// A host thread must not modify or enumerate a container while another thread
// owns the VM, including while the VM is rebuilding a dictionary's lazy index.
static void sdk_collection_thread_cases() {
  X::Runtime runtime;
  auto dict = runtime.Dict();
  auto list = runtime.List();
  require(dict.SetItem("anchor", 42), "cannot seed SDK dictionary");

  auto check_boundary = [&](const char* name, const std::function<bool()>& operation) {
    std::atomic<bool> entered{false}, completed{false}, success{false};
    bool ran_without_vm_lock = false;
    std::thread worker;
    {
      xlang3::XlangRuntimeExecutionGuard owner;
      worker = std::thread([&] {
        entered.store(true, std::memory_order_release);
        success = operation();
        completed.store(true, std::memory_order_release);
      });
      while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
      while (!completed.load(std::memory_order_acquire) &&
             std::chrono::steady_clock::now() < until) std::this_thread::yield();
      ran_without_vm_lock = completed.load(std::memory_order_acquire);
    }
    worker.join();
    if (ran_without_vm_lock) throw std::runtime_error(std::string(name) + " bypassed VM lock");
    require(success, "SDK collection call failed after VM lock release");
  };
  check_boundary("dict_set_item", [&] { return dict.SetItem("inserted", 7); });
  check_boundary("dict_get_entry", [&] {
    X::Value key, value;
    return dict.DictEntry(0, key, value);
  });
  check_boundary("list_append", [&] { return list.Append(X::Value(7)); });
  check_boundary("container_to_string", [&] { return !dict.ToString().empty(); });
  X::Value encoded;
  require(dict.ToBytes(encoded), "cannot seed serialized SDK dictionary");
  check_boundary("container_to_bytes", [&] {
    X::Value bytes;
    return dict.ToBytes(bytes);
  });
  check_boundary("container_from_bytes", [&] {
    X::Value decoded;
    return encoded.FromBytes(decoded) && decoded["anchor"].ToLongLong() == 42;
  });
  check_boundary("container_to_stream", [&] {
    X::Stream stream(runtime);
    return dict.ToBytes(stream);
  });
  X::Stream restore_stream(runtime);
  require(dict.ToBytes(restore_stream) && restore_stream.Rewind(), "cannot seed read stream");
  check_boundary("container_from_stream", [&] {
    X::Value decoded;
    return decoded.FromBytes(restore_stream) && decoded["anchor"].ToLongLong() == 42;
  });

  std::vector<X::Value> keys;
  for (int i = 0; i < 2000; ++i) keys.emplace_back(runtime, "host-key-" + std::to_string(i));
  std::atomic<bool> start{false}, passed{true};
  std::vector<std::thread> threads;
  for (int writer = 0; writer < 4; ++writer) threads.emplace_back([&, writer] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = writer; i < 2000; i += 4) {
      if (!dict.SetItem(keys[i], X::Value(i)) || !list.Append(X::Value(i))) passed = false;
    }
  });
  for (int reader = 0; reader < 4; ++reader) threads.emplace_back([&] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 3000; ++i) {
      if (dict["anchor"].ToLongLong() != 42) passed = false;
      X::Value key, value;
      if (!dict.DictEntry(0, key, value) || value.ToLongLong() != 42) passed = false;
      if (list.Size() < 1) passed = false;
    }
  });
  start.store(true, std::memory_order_release);
  for (auto& thread : threads) thread.join();
  require(passed && dict.Size() == 2002 && list.Size() == 2001,
      "concurrent SDK collection access corrupted entries or lost writes");
  for (int i = 0; i < 2000; ++i)
    require(dict.GetItem(keys[i]).ToLongLong() == i, "concurrent dictionary index lost a key");
}
