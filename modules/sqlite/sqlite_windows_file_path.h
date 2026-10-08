/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "sqlite3.h"

#if defined(_WIN32)
#include "../../src/internal/xlang3/windows_file_path.h"
#include <array>
#include <cstring>
#include <mutex>
#include <new>

namespace xlang3_sqlite {
namespace windows_path_detail {

static_assert(SQLITE_VERSION_NUMBER == 3048000,
              "Review the Windows syscall adapter when updating SQLite");

using CreateFileFn = decltype(&::CreateFileW);
using AttributesFn = decltype(&::GetFileAttributesW);
using AttributesExFn = decltype(&::GetFileAttributesExW);
using DeleteFileFn = decltype(&::DeleteFileW);
using FullPathFn = int (*)(sqlite3_vfs*, const char*, int, char*);

struct Hooks {
  sqlite3_vfs* vfs = nullptr;
  CreateFileFn create_file = nullptr;
  AttributesFn attributes = nullptr;
  AttributesExFn attributes_ex = nullptr;
  DeleteFileFn delete_file = nullptr;
  FullPathFn full_path = nullptr;
  std::array<sqlite3_syscall_ptr, 4> original{};
  bool installed = false;
};

inline Hooks& hooks() { static Hooks value; return value; }

// Keep the original call first. Only a failed ordinary long path gets an
// extended namespace retry. No exception may cross SQLite's C callbacks.
template <class Result, class Call>
Result retry_path(LPCWSTR name, Result failure, Call call) noexcept {
  Result result = call(name);
  DWORD error = GetLastError();
  int error_number = errno;
  if (result == failure && name != nullptr) {
    try {
      const std::wstring prepared = xlang3::windows_native_file_path(name);
      if (prepared != name) {
        SetLastError(error);
        errno = error_number;
        result = call(prepared.c_str());
        error = GetLastError();
        error_number = errno;
      }
    } catch (const std::bad_alloc&) {
      error = ERROR_NOT_ENOUGH_MEMORY;
      error_number = ENOMEM;
    } catch (...) {
      // Preserve the original syscall failure for other preparation errors.
    }
  }
  SetLastError(error);
  errno = error_number;
  return result;
}

inline HANDLE WINAPI create_file(LPCWSTR name, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags,
    HANDLE template_file) noexcept {
  return retry_path(name, INVALID_HANDLE_VALUE, [&](LPCWSTR path) {
    return hooks().create_file(path, access, share, security, disposition,
                               flags, template_file);
  });
}

inline DWORD WINAPI attributes(LPCWSTR name) noexcept {
  return retry_path(name, INVALID_FILE_ATTRIBUTES, [&](LPCWSTR path) {
    return hooks().attributes(path);
  });
}

inline BOOL WINAPI attributes_ex(LPCWSTR name, GET_FILEEX_INFO_LEVELS level,
                                LPVOID result) noexcept {
  return retry_path(name, FALSE, [&](LPCWSTR path) {
    return hooks().attributes_ex(path, level, result);
  });
}

inline BOOL WINAPI delete_file(LPCWSTR name) noexcept {
  return retry_path(name, FALSE, [&](LPCWSTR path) {
    return hooks().delete_file(path);
  });
}

inline int full_path(sqlite3_vfs* vfs, const char* input, int size,
                     char* output) noexcept {
  // SQLite 3.48 winFullPathname uses snprintf and may otherwise return OK
  // after truncation. Preserve its canonicalization but fail closed at an
  // ambiguous buffer boundary; never open a different truncated filename.
  if (input == nullptr || output == nullptr || size <= 1 ||
      vfs->mxPathname <= 1 || std::strlen(input) >= size_t(vfs->mxPathname)) {
    if (output != nullptr && size > 0) output[0] = '\0';
    return SQLITE_CANTOPEN;
  }
  const int result = hooks().full_path(vfs, input, size, output);
  if (result != SQLITE_OK) return result;
  const size_t bound = (std::min)(size_t(size), size_t(vfs->mxPathname));
  const auto* end = static_cast<const char*>(std::memchr(output, '\0', bound));
  if (end == nullptr || size_t(end - output) >= bound - 1) {
    output[0] = '\0';
    return SQLITE_CANTOPEN;
  }
  return SQLITE_OK;
}

inline const std::array<const char*, 4>& names() {
  static const std::array<const char*, 4> value{{"CreateFileW",
      "GetFileAttributesW", "GetFileAttributesExW", "DeleteFileW"}};
  return value;
}

inline std::array<sqlite3_syscall_ptr, 4> replacements() {
  return {{reinterpret_cast<sqlite3_syscall_ptr>(&create_file),
      reinterpret_cast<sqlite3_syscall_ptr>(&attributes),
      reinterpret_cast<sqlite3_syscall_ptr>(&attributes_ex),
      reinterpret_cast<sqlite3_syscall_ptr>(&delete_file)}};
}

// SQLite 3.48 shares its Win32 syscall table across VFSes in this package DLL.
// Other native C callers of this DLL get the same physical long-path retries;
// their locking callbacks, default VFS and URI policy are not replaced.
// Module connections explicitly choose the locking win32-longpath VFS.
inline int install(sqlite3_vfs* vfs, Hooks& state) noexcept {
  if (vfs == nullptr || vfs->iVersion != 3 || vfs->zName == nullptr ||
      std::strcmp(vfs->zName, "win32-longpath") != 0 ||
      vfs->xSetSystemCall == nullptr || vfs->xGetSystemCall == nullptr ||
      vfs->xFullPathname == nullptr || vfs->mxPathname <= 1040) {
    return SQLITE_CANTOPEN;
  }
  state.vfs = vfs;
  state.full_path = vfs->xFullPathname;
  for (size_t i = 0; i < names().size(); ++i) {
    state.original[i] = vfs->xGetSystemCall(vfs, names()[i]);
    if (state.original[i] == nullptr || state.original[i] == replacements()[i])
      return SQLITE_CANTOPEN;
  }
  state.create_file = reinterpret_cast<CreateFileFn>(state.original[0]);
  state.attributes = reinterpret_cast<AttributesFn>(state.original[1]);
  state.attributes_ex = reinterpret_cast<AttributesExFn>(state.original[2]);
  state.delete_file = reinterpret_cast<DeleteFileFn>(state.original[3]);
  for (size_t i = 0; i < names().size(); ++i) {
    if (vfs->xSetSystemCall(vfs, names()[i], replacements()[i]) != SQLITE_OK ||
        vfs->xGetSystemCall(vfs, names()[i]) != replacements()[i]) {
      for (size_t j = 0; j < names().size(); ++j)
        vfs->xSetSystemCall(vfs, names()[j], state.original[j]);
      // No connection is admitted after an installation/rollback failure.
      for (size_t j = 0; j < names().size(); ++j)
        if (vfs->xGetSystemCall(vfs, names()[j]) != state.original[j])
          return SQLITE_CANTOPEN;
      return SQLITE_CANTOPEN;
    }
  }
  vfs->xFullPathname = &full_path;
  state.installed = true;
  return SQLITE_OK;
}

inline bool unchanged(const Hooks& state) noexcept {
  if (!state.installed || state.vfs->xFullPathname != &full_path) return false;
  for (size_t i = 0; i < names().size(); ++i)
    if (state.vfs->xGetSystemCall(state.vfs, names()[i]) != replacements()[i])
      return false;
  return true;
}

} // namespace windows_path_detail
} // namespace xlang3_sqlite
#endif

namespace xlang3_sqlite {

inline int initialize_native_database_paths() noexcept {
#if defined(_WIN32)
  static std::once_flag initialized;
  static int status = SQLITE_CANTOPEN;
  try {
    std::call_once(initialized, [] {
      status = windows_path_detail::install(sqlite3_vfs_find("win32-longpath"),
                                            windows_path_detail::hooks());
    });
  } catch (...) { return SQLITE_NOMEM; }
  if (status != SQLITE_OK) return status;
  return windows_path_detail::unchanged(windows_path_detail::hooks())
      ? SQLITE_OK : SQLITE_CANTOPEN;
#else
  return SQLITE_OK;
#endif
}

inline int open_native_database(const char* filename, sqlite3** database,
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) noexcept {
  if (database == nullptr) return SQLITE_MISUSE;
  *database = nullptr;
  const int ready = initialize_native_database_paths();
  if (ready != SQLITE_OK) return ready;
#if defined(_WIN32)
  return sqlite3_open_v2(filename, database, flags, "win32-longpath");
#else
  return sqlite3_open_v2(filename, database, flags, nullptr);
#endif
}

} // namespace xlang3_sqlite
