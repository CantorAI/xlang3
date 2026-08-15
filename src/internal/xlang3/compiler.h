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

#if defined(_MSC_VER)
#define XLANG3_FORCE_INLINE __forceinline
#define XLANG3_HOT_INLINE __forceinline
#define XLANG3_NOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
#define XLANG3_FORCE_INLINE inline __attribute__((always_inline))
#define XLANG3_HOT_INLINE inline __attribute__((always_inline, hot))
#define XLANG3_NOINLINE __attribute__((noinline))
#else
#define XLANG3_FORCE_INLINE inline
#define XLANG3_HOT_INLINE inline
#define XLANG3_NOINLINE
#endif
