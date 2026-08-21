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
#include "runtime/memory/x3_page_allocator.h"

#include <new>

namespace xlang3::memory {

void* X3PageAllocator::allocate(size_t bytes, size_t alignment) {
  if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    return ::operator new(bytes);
  }
  return ::operator new(bytes, std::align_val_t(alignment));
}

void X3PageAllocator::release(void* ptr, size_t, size_t alignment) noexcept {
  if (ptr == nullptr) {
    return;
  }
  if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    ::operator delete(ptr);
    return;
  }
  ::operator delete(ptr, std::align_val_t(alignment));
}

} // namespace xlang3::memory
