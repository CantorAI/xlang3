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

#if defined(_WIN32)
#include <algorithm>
#include <cerrno>
#include <string>
#include <string_view>
#include <windows.h>

namespace xlang3 {
namespace windows_file_path_detail {

inline bool separator(wchar_t value) { return value == L'\\' || value == L'/'; }

inline bool equal_ascii(std::wstring_view value, std::wstring_view expected) {
  if (value.size() != expected.size()) return false;
  for (size_t i = 0; i < value.size(); ++i) {
    const wchar_t ch = value[i] >= L'a' && value[i] <= L'z'
        ? value[i] - L'a' + L'A' : value[i];
    if (ch != expected[i]) return false;
  }
  return true;
}

inline bool reserved_device(std::wstring_view component) {
  const size_t suffix = component.find_first_of(L".:");
  if (suffix != std::wstring_view::npos) component = component.substr(0, suffix);
  while (!component.empty() && component.back() == L' ') component.remove_suffix(1);
  if (equal_ascii(component, L"CON") || equal_ascii(component, L"PRN") ||
      equal_ascii(component, L"AUX") || equal_ascii(component, L"NUL") ||
      equal_ascii(component, L"CONIN$") || equal_ascii(component, L"CONOUT$")) {
    return true;
  }
  if (component.size() != 4 ||
      (!equal_ascii(component.substr(0, 3), L"COM") &&
       !equal_ascii(component.substr(0, 3), L"LPT"))) return false;
  const wchar_t number = component[3];
  return (number >= L'1' && number <= L'9') || number == L'\u00b9' ||
      number == L'\u00b2' || number == L'\u00b3';
}

inline bool special_component(std::wstring_view path) {
  for (size_t start = 0; start < path.size();) {
    size_t end = start;
    while (end < path.size() && !separator(path[end])) ++end;
    const auto component = path.substr(start, end - start);
    if (reserved_device(component)) return true;
    // Extended paths suppress Win32 trailing-dot/space processing. Do not
    // silently change that legacy meaning while fixing ordinary long paths.
    if (!component.empty() && component != L"." && component != L".." &&
        (component.back() == L'.' || component.back() == L' ')) return true;
    start = end + 1;
  }
  return false;
}

struct LastError {
  DWORD saved = GetLastError();
  int saved_errno = errno;
  ~LastError() { SetLastError(saved); errno = saved_errno; }
};

} // namespace windows_file_path_detail

// Call only after an original native open failed. Successful short paths keep
// their existing cost and semantics. This converts a long ordinary path, not a
// device or already-extended namespace, and never changes the public filename.
inline std::wstring windows_native_file_path(std::wstring_view path) {
  using namespace windows_file_path_detail;
  LastError preserve;
  if (path.empty() || path.find(L'\0') != std::wstring_view::npos) return std::wstring(path);
  if ((path.size() >= 4 && separator(path[0]) && separator(path[1]) &&
       (path[2] == L'?' || path[2] == L'.') && separator(path[3])) ||
      (path.size() >= 4 && separator(path[0]) && path[1] == L'?' &&
       path[2] == L'?' && separator(path[3])) || special_component(path)) {
    return std::wstring(path);
  }
  const bool absolute_drive = path.size() >= 3 &&
      ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
      path[1] == L':' && separator(path[2]);
  const bool unc = path.size() >= 2 && separator(path[0]) && separator(path[1]);
  if (path.size() < MAX_PATH && (absolute_drive || unc)) return std::wstring(path);

  // An extended prefix disables slash and dot normalization. Resolve first,
  // in one call so a relative path is not resolved against two different CWDs.
  // https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
  const std::wstring input(path);
  std::wstring full(32768, L'\0');
  const DWORD length = GetFullPathNameW(input.c_str(), static_cast<DWORD>(full.size()),
                                      full.data(), nullptr);
  if (length == 0 || length >= full.size()) return input;
  full.resize(length);
  if (full.size() < MAX_PATH) return input;
  std::replace(full.begin(), full.end(), L'/', L'\\');
  if (full.size() >= 3 && full[1] == L':' && full[2] == L'\\' &&
      ((full[0] >= L'A' && full[0] <= L'Z') || (full[0] >= L'a' && full[0] <= L'z'))) {
    return L"\\\\?\\" + full;
  }
  if (full.size() >= 5 && full[0] == L'\\' && full[1] == L'\\') {
    const size_t server_end = full.find(L'\\', 2);
    if (server_end != std::wstring::npos && server_end > 2 && server_end + 1 < full.size()) {
      return L"\\\\?\\UNC\\" + full.substr(2);
    }
  }
  return input;
}

} // namespace xlang3
#endif
