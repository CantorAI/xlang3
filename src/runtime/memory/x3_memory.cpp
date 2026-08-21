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
#include "runtime/memory/x3_memory.h"

namespace xlang3::memory {

X3MemoryStats X3MemoryManager::stats() const {
  X3MemoryStats out;
  out.object_pools = object_pools_.stats();
  out.buckets = buckets_.bucket_stats();
  out.large = buckets_.large_stats();
  return out;
}

} // namespace xlang3::memory
