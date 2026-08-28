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
#include "runtime/memory/x3_runtime_memory.h"

namespace xlang3::memory {

X3ObjectPoolManager& x3_thread_object_pools() {
  thread_local X3ObjectPoolManager pools;
  return pools;
}

X3BucketAllocator& x3_thread_buckets() {
  // Objects allocated in one XLang3 native thread can legally outlive that
  // thread and be released by another. Keep each per-thread bucket alive for
  // process lifetime; variable-sized objects store their owning bucket.
  thread_local X3BucketAllocator* buckets = new X3BucketAllocator();
  return *buckets;
}

} // namespace xlang3::memory
