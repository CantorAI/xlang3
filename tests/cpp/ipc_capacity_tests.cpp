#include "ipc/shared_memory_backpressure.h"
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
    std::cout << "IPC admission fairness passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
