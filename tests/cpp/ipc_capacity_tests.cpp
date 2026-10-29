#include "ipc/shared_memory_backpressure.h"
#include "ipc/shared_memory_call_lifetime.h"
#include "ipc/shared_memory_server_threads.h"
#include <iostream>
#include <stdexcept>

using namespace xlang3::ipc;
struct Region {
  struct Slot { uint32_t state = SlotFree; } slots[4];
  uint32_t slot_count = 4;
  uint64_t next_capacity_ticket = 0;
  SharedCapacityWaiter capacity_waiters[4]{};
};
static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
int main() {
  try {
    std::string error;
    auto alive = [](uint32_t pid) { return pid != 99; };
    Region region;
    region.capacity_waiters[0] = {1, 20};
    int waits = 0;
    require(wait_for_message_capacity(region, 1, 10, [&] {
      ++waits;
      region.capacity_waiters[0].process = 0;
    }, alive, error), "new request never admitted");
    require(waits == 1, "new request bypassed a waiting message");

    region.slots[0].state = region.slots[1].state = SlotProcessing;
    require(wait_for_message_capacity(region, 3, 10, [&] {
      require(region.capacity_waiters[0].process == 30, "message did not reserve admission priority");
      region.slots[0].state = SlotFree;
    }, alive, error, 30), "completed message did not acquire space");
    require(region.capacity_waiters[0].process == 0, "successful wait leaked priority");

    region.capacity_waiters[0] = {1, 99};
    require(wait_for_message_capacity(region, 1, 10, [&] {
      throw std::runtime_error("dead waiter blocked admission");
    }, alive, error), "dead waiter cleanup failed");
    require(region.capacity_waiters[0].process == 0, "dead waiter was not removed");

    bool peer_alive = true;
    require(!wait_for_message_capacity(region, 4, 10, [&] { peer_alive = false; },
        [&](uint32_t pid) { return pid != 10 || peer_alive; }, error, 30), "dead peer accepted");
    require(error.find("peer exited") != std::string::npos, "peer failure was not reported");
    require(region.capacity_waiters[0].process == 0, "failed wait leaked priority");
    require(!wait_for_message_capacity(region, 1, 10, [] {}, alive, error, 30,
        [] { return true; }), "cancelled message acquired available slots");
    require(region.capacity_waiters[0].process == 0, "cancellation leaked priority");

    struct MessageRegion { uint32_t slot_count = 2; SharedSlot slots[2]{}; } message;
    message.slots[0].state = SlotQueued;
    message.slots[0].owner_pid = 10; message.slots[0].call_id = 123;
    message.slots[0].next_slot = 1; message.slots[0].total_size = 2;
    message.slots[0].payload_size = 1; message.slots[0].payload[0] = 'a';
    message.slots[1] = message.slots[0]; message.slots[1].next_slot = kNoSharedSlot;
    message.slots[1].payload[0] = 'b';
    auto free_chain = [](auto& region, uint32_t index) {
      while (index != kNoSharedSlot) {
        auto next = region.slots[index].next_slot;
        region.slots[index] = {}; index = next;
      }
    };
    SharedCallKey old{0, 10, 123};
    std::vector<char> request;
    require(detach_shared_request(message, old, request, free_chain, error), "request detach failed");
    require(request == std::vector<char>({'a','b'}), "private request lost chained bytes");
    require(message.slots[1].state == SlotFree, "detached continuation was not released");
    free_chain(message, old.index); // Caller times out while handler owns private bytes.
    message.slots[0].state = SlotQueued; message.slots[0].owner_pid = 10;
    message.slots[0].call_id = 124;
    require(!owns_shared_call(message, old), "late handler owns a reused slot");
    require(!detach_shared_request(message, old, request, free_chain, error), "cancelled queued call was dispatched");
    require(message.slots[0].call_id == 124, "late handler damaged a new request");
    require(request == std::vector<char>({'a','b'}), "slot reuse damaged detached request bytes");

    // Saturate every worker with intentionally non-cooperative handlers.
    // Repeatedly cancelled requests must not accumulate queued closures.
    SharedMemoryServerThreads pool;
    std::mutex gate;
    std::condition_variable changed;
    int entered = 0;
    bool release = false;
    for (int i = 0; i < 8; ++i) require(pool.launch([&] {
      std::unique_lock<std::mutex> lock(gate);
      ++entered; changed.notify_all();
      changed.wait(lock, [&] { return release; });
    }), "cannot start worker");
    {
      std::unique_lock<std::mutex> lock(gate);
      require(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered == 8; }),
          "worker saturation fixture did not start");
    }
    std::atomic<int> cancelled_runs{0}, valid_runs{0};
    bool queued = true;
    for (int i = 0; i < 200; ++i) {
      auto valid = std::make_shared<std::atomic_bool>(true);
      if (!pool.launch([&] { ++cancelled_runs; }, [valid] { return valid->load(); })) queued = false;
      valid->store(false);
    }
    for (uint32_t i = 0; i < kSharedSlotCount; ++i)
      if (!pool.launch([&] { ++valid_runs; })) queued = false;
    bool bounded = !pool.launch([] {});
    {
      std::lock_guard<std::mutex> lock(gate); release = true;
    }
    changed.notify_all(); pool.drain();
    require(queued, "cancelled queued requests consumed queue capacity");
    require(bounded, "dispatch queue grew past the fixed admission bound");
    require(cancelled_runs.load() == 0, "cancelled queued request was executed");
    require(valid_runs.load() == kSharedSlotCount, "valid queued requests were lost");
    std::cout << "IPC admission fairness passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
