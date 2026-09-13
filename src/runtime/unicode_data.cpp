#include "xlang3/unicode_data.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace xlang3 {
namespace {

struct PropertyRange {
  uint32_t first;
  uint32_t last;
  uint8_t category;
  uint8_t bidirectional;
  uint8_t east_asian_width;
  uint8_t combining;
  uint8_t mirrored;
  int8_t decimal;
  int8_t digit;
  uint16_t flags;
};
struct NumericRecord { uint32_t codepoint; double value; };
struct BlobRecord { uint32_t codepoint; uint32_t offset; uint16_t length; };
struct CompositionRecord { uint32_t first; uint32_t second; uint32_t composed; };
struct CaseRecord {
  uint32_t codepoint;
  uint32_t lower_offset; uint8_t lower_length;
  uint32_t upper_offset; uint8_t upper_length;
  uint32_t title_offset; uint8_t title_length;
  uint32_t fold_offset; uint8_t fold_length;
};
struct NameRecord { uint32_t codepoint; uint32_t offset; uint8_t length; };
struct LookupRecord {
  uint32_t name_offset; uint8_t name_length;
  int32_t direct_name_index;
  uint32_t value_offset; uint8_t value_length;
};
struct AlgorithmicRange { uint32_t first; uint32_t last; uint32_t prefix_offset; uint8_t prefix_length; };

#include "unicode_data_generated.inc"
#include "unicode_data_legacy_generated.inc"

std::string_view blob_view(uint32_t offset, size_t length) {
  return {reinterpret_cast<const char*>(unicode_generated::kBlob) + offset, length};
}

std::string_view legacy_blob_view(uint32_t offset, size_t length) {
  return {reinterpret_cast<const char*>(unicode_legacy_generated::kBlob) + offset, length};
}

void append_utf8(uint32_t codepoint, std::string& out) {
  if (codepoint <= 0x7f) out.push_back(static_cast<char>(codepoint));
  else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

std::string upper_ascii(std::string_view value) {
  std::string result(value);
  for (char& character : result) {
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
  }
  return result;
}

template <typename Record, size_t Size>
const Record* find_codepoint(const Record (&records)[Size], uint32_t codepoint) {
  auto iterator = std::lower_bound(std::begin(records), std::end(records), codepoint,
      [](const auto& record, uint32_t wanted) { return record.codepoint < wanted; });
  return iterator != std::end(records) && iterator->codepoint == codepoint ? &*iterator : nullptr;
}

constexpr std::array<const char*, 19> kHangulL = {
    "G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S", "SS", "", "J", "JJ", "C", "K", "T", "P", "H"};
constexpr std::array<const char*, 21> kHangulV = {
    "A", "AE", "YA", "YAE", "EO", "E", "YEO", "YE", "O", "WA", "WAE", "OE", "YO", "U", "WEO", "WE", "WI", "YU", "EU", "YI", "I"};
constexpr std::array<const char*, 28> kHangulT = {
    "", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM", "LB", "LS", "LT", "LP", "LH", "M", "B", "BS", "S", "SS", "NG", "J", "C", "K", "T", "P", "H"};

std::string hangul_name(uint32_t codepoint) {
  constexpr uint32_t base = 0xac00;
  const uint32_t index = codepoint - base;
  const uint32_t l = index / (21 * 28);
  const uint32_t v = (index % (21 * 28)) / 28;
  const uint32_t t = index % 28;
  return std::string("HANGUL SYLLABLE ") + kHangulL[l] + kHangulV[v] + kHangulT[t];
}

bool algorithmic_lookup(std::string_view name, uint32_t& codepoint) {
  if (name.rfind("HANGUL SYLLABLE ", 0) == 0) {
    for (uint32_t cp = 0xac00; cp <= 0xd7a3; ++cp) {
      if (hangul_name(cp) == name) { codepoint = cp; return true; }
    }
    return false;
  }
  const size_t dash = name.rfind('-');
  if (dash == std::string_view::npos || dash + 1 == name.size()) return false;
  uint32_t parsed = 0;
  const auto converted = std::from_chars(name.data() + dash + 1, name.data() + name.size(), parsed, 16);
  if (converted.ec != std::errc{} || converted.ptr != name.data() + name.size()) return false;
  for (const auto& range : unicode_generated::kAlgorithmicRanges) {
    if (parsed >= range.first && parsed <= range.last &&
        name.substr(0, dash) == blob_view(range.prefix_offset, range.prefix_length)) {
      codepoint = parsed;
      return true;
    }
  }
  return false;
}

bool decode_utf8(std::string_view text, std::vector<uint32_t>& output) {
  for (size_t offset = 0; offset < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    size_t width = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 :
        (lead & 0xf0) == 0xe0 ? 3 : (lead & 0xf8) == 0xf0 ? 4 : 0;
    if (width == 0 || offset + width > text.size()) return false;
    uint32_t codepoint = width == 1 ? lead : lead & ((1u << (7 - width)) - 1u);
    for (size_t index = 1; index < width; ++index) {
      const unsigned char byte = static_cast<unsigned char>(text[offset + index]);
      if ((byte & 0xc0u) != 0x80u) return false;
      codepoint = (codepoint << 6) | (byte & 0x3fu);
    }
    output.push_back(codepoint);
    offset += width;
  }
  return true;
}

void decompose_codepoint(uint32_t codepoint, bool compatibility, bool legacy, std::vector<uint32_t>& output) {
  constexpr uint32_t s_base = 0xac00, l_base = 0x1100, v_base = 0x1161, t_base = 0x11a7;
  constexpr uint32_t l_count = 19, v_count = 21, t_count = 28, n_count = v_count * t_count;
  if (codepoint >= s_base && codepoint < s_base + l_count * n_count) {
    const uint32_t index = codepoint - s_base;
    output.push_back(l_base + index / n_count);
    output.push_back(v_base + (index % n_count) / t_count);
    if (index % t_count) output.push_back(t_base + index % t_count);
    return;
  }
  // Unicode normalization corrections are retroactive even for the frozen
  // 3.2 database.  CPython's ucd_3_2_0 reports the original decomposition
  // property but uses the corrected mapping while normalizing.
  if (legacy) {
    switch (codepoint) {
      case 0x2f868: output.push_back(0x2136a); return;
      case 0x2f874: output.push_back(0x5f33); return;
      case 0x2f91f: output.push_back(0x43ab); return;
      case 0x2f95f: output.push_back(0x7aae); return;
      case 0x2f9bf: output.push_back(0x4d57); return;
      default: break;
    }
  }
  std::string_view mapping = legacy ? unicode_legacy_data_decomposition(codepoint) : unicode_data_decomposition(codepoint);
  if (mapping.empty() || (!compatibility && mapping.front() == '<')) {
    output.push_back(codepoint);
    return;
  }
  if (mapping.front() == '<') {
    const size_t close = mapping.find('>');
    mapping.remove_prefix(close == std::string_view::npos ? mapping.size() : close + 1);
  }
  size_t offset = 0;
  while (offset < mapping.size()) {
    while (offset < mapping.size() && mapping[offset] == ' ') ++offset;
    size_t end = offset;
    while (end < mapping.size() && mapping[end] != ' ') ++end;
    uint32_t child = 0;
    const auto parsed = std::from_chars(mapping.data() + offset, mapping.data() + end, child, 16);
    if (parsed.ec == std::errc{}) decompose_codepoint(child, compatibility, legacy, output);
    offset = end;
  }
}

uint32_t composition_pair(uint32_t first, uint32_t second, bool legacy) {
  constexpr uint32_t s_base = 0xac00, l_base = 0x1100, v_base = 0x1161, t_base = 0x11a7;
  constexpr uint32_t l_count = 19, v_count = 21, t_count = 28, n_count = v_count * t_count;
  if (first >= l_base && first < l_base + l_count && second >= v_base && second < v_base + v_count) {
    return s_base + ((first - l_base) * v_count + (second - v_base)) * t_count;
  }
  if (first >= s_base && first < s_base + l_count * n_count &&
      (first - s_base) % t_count == 0 && second > t_base && second < t_base + t_count) {
    return first + second - t_base;
  }
  const uint64_t wanted = (static_cast<uint64_t>(first) << 32) | second;
  const auto search = [=](const auto& records) {
    auto begin = std::begin(records);
    auto end = std::end(records);
    auto found = std::lower_bound(begin, end, wanted, [](const CompositionRecord& record, uint64_t key) {
      return ((static_cast<uint64_t>(record.first) << 32) | record.second) < key;
    });
    return found != end && found->first == first && found->second == second ? found->composed : 0u;
  };
  return legacy ? search(unicode_legacy_generated::kCompositionRecords)
                : search(unicode_generated::kCompositionRecords);
}

}  // namespace

const char* unicode_data_version() { return unicode_generated::kVersion; }

UnicodeDataRecord unicode_data_record(uint32_t codepoint) {
  auto iterator = std::upper_bound(
      std::begin(unicode_generated::kPropertyRanges), std::end(unicode_generated::kPropertyRanges), codepoint,
      [](uint32_t wanted, const PropertyRange& range) { return wanted < range.first; });
  if (iterator != std::begin(unicode_generated::kPropertyRanges)) --iterator;
  if (iterator->first > codepoint || iterator->last < codepoint) return {};
  return {
      unicode_generated::kCategories[iterator->category],
      unicode_generated::kBidirectional[iterator->bidirectional],
      unicode_generated::kEastAsianWidth[iterator->east_asian_width],
      iterator->flags, iterator->combining, iterator->mirrored != 0,
      iterator->decimal, iterator->digit,
  };
}

bool unicode_data_numeric(uint32_t codepoint, double& value) {
  const auto* record = find_codepoint(unicode_generated::kNumericRecords, codepoint);
  if (!record) return false;
  value = record->value;
  return true;
}

std::string_view unicode_data_decomposition(uint32_t codepoint) {
  const auto* record = find_codepoint(unicode_generated::kDecompositionRecords, codepoint);
  return record ? blob_view(record->offset, record->length) : std::string_view{};
}

std::string unicode_data_name(uint32_t codepoint) {
  if (codepoint >= 0xac00 && codepoint <= 0xd7a3) return hangul_name(codepoint);
  for (const auto& range : unicode_generated::kAlgorithmicRanges) {
    if (codepoint >= range.first && codepoint <= range.last) {
      char suffix[16]{};
      const int length = std::snprintf(suffix, sizeof(suffix), "-%04X", codepoint);
      return std::string(blob_view(range.prefix_offset, range.prefix_length)) + std::string(suffix, length);
    }
  }
  const auto* record = find_codepoint(unicode_generated::kNameRecords, codepoint);
  return record ? std::string(blob_view(record->offset, record->length)) : std::string{};
}

bool unicode_data_lookup(std::string_view raw_name, std::string& value) {
  const std::string name = upper_ascii(raw_name);
  uint32_t codepoint = 0;
  if (algorithmic_lookup(name, codepoint)) {
    value.clear(); append_utf8(codepoint, value); return true;
  }
  auto first = std::begin(unicode_generated::kLookupRecords);
  auto last = std::end(unicode_generated::kLookupRecords);
  auto iterator = std::lower_bound(first, last, std::string_view(name),
      [](const LookupRecord& record, std::string_view wanted) {
        return blob_view(record.name_offset, record.name_length) < wanted;
      });
  if (iterator == last || blob_view(iterator->name_offset, iterator->name_length) != name) return false;
  if (iterator->direct_name_index >= 0) {
    value.clear();
    append_utf8(unicode_generated::kNameRecords[iterator->direct_name_index].codepoint, value);
  } else {
    value = blob_view(iterator->value_offset, iterator->value_length);
  }
  return true;
}

bool unicode_data_lookup_canonical(std::string_view raw_name, std::string& value) {
  const std::string name = upper_ascii(raw_name);
  uint32_t codepoint = 0;
  if (algorithmic_lookup(name, codepoint)) {
    value.clear(); append_utf8(codepoint, value); return true;
  }
  auto first = std::begin(unicode_generated::kLookupRecords);
  auto last = std::end(unicode_generated::kLookupRecords);
  auto iterator = std::lower_bound(first, last, std::string_view(name),
      [](const LookupRecord& record, std::string_view wanted) {
        return blob_view(record.name_offset, record.name_length) < wanted;
      });
  if (iterator == last || iterator->direct_name_index < 0 ||
      blob_view(iterator->name_offset, iterator->name_length) != name) return false;
  value.clear();
  append_utf8(unicode_generated::kNameRecords[iterator->direct_name_index].codepoint, value);
  return true;
}

std::string unicode_data_case(uint32_t codepoint, UnicodeCaseMapping mapping) {
  const auto* record = find_codepoint(unicode_generated::kCaseRecords, codepoint);
  uint32_t offset = 0;
  uint8_t length = 0;
  if (record) {
    switch (mapping) {
      case UnicodeCaseMapping::Lower: offset = record->lower_offset; length = record->lower_length; break;
      case UnicodeCaseMapping::Upper: offset = record->upper_offset; length = record->upper_length; break;
      case UnicodeCaseMapping::Title: offset = record->title_offset; length = record->title_length; break;
      case UnicodeCaseMapping::Fold: offset = record->fold_offset; length = record->fold_length; break;
    }
  }
  if (length) return std::string(blob_view(offset, length));
  std::string result;
  append_utf8(codepoint, result);
  return result;
}

bool normalize_data(std::string_view form, std::string_view text, std::string& result, bool legacy) {
  const bool compatibility = form == "NFKC" || form == "NFKD";
  const bool compose = form == "NFC" || form == "NFKC";
  if (!compose && form != "NFD" && form != "NFKD") return false;
  std::vector<uint32_t> source;
  if (!decode_utf8(text, source)) return false;
  std::vector<uint32_t> decomposed;
  decomposed.reserve(source.size());
  for (uint32_t codepoint : source) decompose_codepoint(codepoint, compatibility, legacy, decomposed);
  for (size_t index = 1; index < decomposed.size(); ++index) {
    const uint8_t combining = (legacy ? unicode_legacy_data_record(decomposed[index]) : unicode_data_record(decomposed[index])).combining;
    if (combining == 0) continue;
    size_t insertion = index;
    while (insertion > 0) {
      const uint8_t previous = (legacy ? unicode_legacy_data_record(decomposed[insertion - 1]) : unicode_data_record(decomposed[insertion - 1])).combining;
      if (previous == 0 || previous <= combining) break;
      std::swap(decomposed[insertion], decomposed[insertion - 1]);
      --insertion;
    }
  }
  if (compose && !decomposed.empty()) {
    std::vector<uint32_t> composed;
    composed.reserve(decomposed.size());
    composed.push_back(decomposed[0]);
    size_t starter_index = 0;
    uint32_t starter = decomposed[0];
    uint8_t last_combining = 0;
    for (size_t index = 1; index < decomposed.size(); ++index) {
      const uint32_t codepoint = decomposed[index];
      const uint8_t combining = (legacy ? unicode_legacy_data_record(codepoint) : unicode_data_record(codepoint)).combining;
      const uint32_t replacement =
          (last_combining == 0 || last_combining < combining) ? composition_pair(starter, codepoint, legacy) : 0;
      if (replacement != 0) {
        composed[starter_index] = starter = replacement;
      } else {
        if (combining == 0) { starter_index = composed.size(); starter = codepoint; }
        composed.push_back(codepoint);
        last_combining = combining;
      }
    }
    decomposed.swap(composed);
  }
  result.clear();
  for (uint32_t codepoint : decomposed) append_utf8(codepoint, result);
  return true;
}

bool unicode_data_normalize(std::string_view form, std::string_view text, std::string& result) {
  return normalize_data(form, text, result, false);
}

UnicodeDataRecord unicode_legacy_data_record(uint32_t codepoint) {
  auto iterator = std::upper_bound(
      std::begin(unicode_legacy_generated::kPropertyRanges), std::end(unicode_legacy_generated::kPropertyRanges), codepoint,
      [](uint32_t wanted, const PropertyRange& range) { return wanted < range.first; });
  if (iterator != std::begin(unicode_legacy_generated::kPropertyRanges)) --iterator;
  if (iterator->first > codepoint || iterator->last < codepoint) return {};
  return {
      unicode_legacy_generated::kCategories[iterator->category],
      unicode_legacy_generated::kBidirectional[iterator->bidirectional],
      unicode_legacy_generated::kEastAsianWidth[iterator->east_asian_width],
      iterator->flags, iterator->combining, iterator->mirrored != 0,
      iterator->decimal, iterator->digit,
  };
}

bool unicode_legacy_data_numeric(uint32_t codepoint, double& value) {
  const auto* record = find_codepoint(unicode_legacy_generated::kNumericRecords, codepoint);
  if (!record) return false;
  value = record->value;
  return true;
}

std::string_view unicode_legacy_data_decomposition(uint32_t codepoint) {
  const auto* record = find_codepoint(unicode_legacy_generated::kDecompositionRecords, codepoint);
  return record ? legacy_blob_view(record->offset, record->length) : std::string_view{};
}

std::string unicode_legacy_data_name(uint32_t codepoint) {
  if (codepoint >= 0xac00 && codepoint <= 0xd7a3) return hangul_name(codepoint);
  for (const auto& range : unicode_legacy_generated::kAlgorithmicRanges) {
    if (codepoint >= range.first && codepoint <= range.last) {
      char suffix[16]{};
      const int length = std::snprintf(suffix, sizeof(suffix), "-%04X", codepoint);
      return std::string(legacy_blob_view(range.prefix_offset, range.prefix_length)) + std::string(suffix, length);
    }
  }
  const auto* record = find_codepoint(unicode_legacy_generated::kNameRecords, codepoint);
  return record ? std::string(legacy_blob_view(record->offset, record->length)) : std::string{};
}

bool unicode_legacy_data_lookup(std::string_view raw_name, std::string& value) {
  return unicode_data_lookup_canonical(raw_name, value);
}

bool unicode_legacy_data_normalize(std::string_view form, std::string_view text, std::string& result) {
  return normalize_data(form, text, result, true);
}

}  // namespace xlang3
