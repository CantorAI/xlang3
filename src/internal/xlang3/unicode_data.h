#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace xlang3 {

enum UnicodeCharacterFlag : uint16_t {
  kUnicodeLower = 1u << 0,
  kUnicodeUpper = 1u << 1,
  kUnicodeTitle = 1u << 2,
  kUnicodeAlpha = 1u << 3,
  kUnicodeDigit = 1u << 4,
  kUnicodeDecimal = 1u << 5,
  kUnicodeNumeric = 1u << 6,
  kUnicodeAlnum = 1u << 7,
  kUnicodeSpace = 1u << 8,
  kUnicodePrintable = 1u << 9,
  kUnicodeIdentifierStart = 1u << 10,
  kUnicodeIdentifierContinue = 1u << 11,
  kUnicodeCased = 1u << 12,
  kUnicodeCaseIgnorable = 1u << 13,
};

struct UnicodeDataRecord {
  std::string_view category;
  std::string_view bidirectional;
  std::string_view east_asian_width;
  uint16_t flags = 0;
  uint8_t combining = 0;
  bool mirrored = false;
  int8_t decimal = -1;
  int8_t digit = -1;
};

enum class UnicodeCaseMapping { Lower, Upper, Title, Fold };

const char* unicode_data_version();
UnicodeDataRecord unicode_data_record(uint32_t codepoint);
bool unicode_data_numeric(uint32_t codepoint, double& value);
std::string_view unicode_data_decomposition(uint32_t codepoint);
std::string unicode_data_name(uint32_t codepoint);
bool unicode_data_lookup(std::string_view name, std::string& value);
bool unicode_data_lookup_canonical(std::string_view name, std::string& value);
std::string unicode_data_case(uint32_t codepoint, UnicodeCaseMapping mapping);
bool unicode_data_normalize(std::string_view form, std::string_view text, std::string& result);

UnicodeDataRecord unicode_legacy_data_record(uint32_t codepoint);
bool unicode_legacy_data_numeric(uint32_t codepoint, double& value);
std::string_view unicode_legacy_data_decomposition(uint32_t codepoint);
std::string unicode_legacy_data_name(uint32_t codepoint);
bool unicode_legacy_data_lookup(std::string_view name, std::string& value);
bool unicode_legacy_data_normalize(std::string_view form, std::string_view text, std::string& result);

}  // namespace xlang3
