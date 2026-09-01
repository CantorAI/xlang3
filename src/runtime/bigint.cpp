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
#include "xlang3/value.h"

#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"

#include "runtime/memory/x3_runtime_memory.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <limits>
#include <string>

namespace xlang3 {

namespace {

struct BigIntPayload {
  int8_t sign = 0;
  uint32_t limb_count = 0;
  uint32_t limb_capacity = 0;
  uint32_t* limbs = nullptr;
  size_t limb_bytes = 0;
};

BigIntPayload* payload(BigIntObject* object) {
  return object == nullptr ? nullptr : static_cast<BigIntPayload*>(object->impl);
}

const BigIntPayload* payload(const BigIntObject* object) {
  return object == nullptr ? nullptr : static_cast<const BigIntPayload*>(object->impl);
}

void release_limbs(BigIntPayload& value) {
  if (value.limbs != nullptr && value.limb_bytes != 0) {
    memory::x3_thread_buckets().release(value.limbs, value.limb_bytes);
  }
  value.limbs = nullptr;
  value.limb_count = 0;
  value.limb_capacity = 0;
  value.limb_bytes = 0;
}

bool ensure_capacity(BigIntPayload& value, uint32_t capacity) {
  if (capacity <= value.limb_capacity) {
    return true;
  }
  uint32_t next = value.limb_capacity == 0 ? 1u : value.limb_capacity;
  while (next < capacity) {
    next *= 2u;
  }
  const size_t bytes = sizeof(uint32_t) * static_cast<size_t>(next);
  const uint32_t old_count = value.limb_count;
  const int8_t old_sign = value.sign;
  auto* new_limbs = static_cast<uint32_t*>(memory::x3_thread_buckets().allocate(bytes));
  if (old_count != 0) {
    std::memcpy(new_limbs, value.limbs, sizeof(uint32_t) * old_count);
  }
  if (next > old_count) {
    std::memset(new_limbs + old_count, 0, sizeof(uint32_t) * (next - old_count));
  }
  release_limbs(value);
  value.sign = old_sign;
  value.limb_count = old_count;
  value.limbs = new_limbs;
  value.limb_capacity = next;
  value.limb_bytes = bytes;
  return true;
}

void normalize(BigIntPayload& value) {
  while (value.limb_count != 0 && value.limbs[value.limb_count - 1] == 0) {
    --value.limb_count;
  }
  if (value.limb_count == 0) {
    value.sign = 0;
  }
}

BigIntPayload make_zero_payload() {
  return {};
}

BigIntPayload make_payload_from_u64(uint64_t magnitude, int8_t sign) {
  BigIntPayload out;
  if (magnitude == 0) {
    return out;
  }
  out.sign = sign;
  ensure_capacity(out, 2);
  out.limbs[0] = static_cast<uint32_t>(magnitude & 0xffffffffu);
  out.limbs[1] = static_cast<uint32_t>(magnitude >> 32u);
  out.limb_count = out.limbs[1] == 0 ? 1u : 2u;
  return out;
}

BigIntPayload clone_payload(const BigIntPayload& value) {
  BigIntPayload out;
  out.sign = value.sign;
  if (value.limb_count != 0) {
    ensure_capacity(out, value.limb_count);
    std::memcpy(out.limbs, value.limbs, sizeof(uint32_t) * value.limb_count);
    out.limb_count = value.limb_count;
  }
  return out;
}

void move_assign_payload(BigIntPayload& target, BigIntPayload& source) {
  release_limbs(target);
  target = source;
  source = {};
}

bool payload_to_i64(const BigIntPayload& value, int64_t& out) {
  if (value.sign == 0 || value.limb_count == 0) {
    out = 0;
    return true;
  }
  if (value.limb_count > 2) {
    return false;
  }
  uint64_t magnitude = value.limbs[0];
  if (value.limb_count > 1) {
    magnitude |= static_cast<uint64_t>(value.limbs[1]) << 32u;
  }
  if (value.sign > 0) {
    if (magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    out = static_cast<int64_t>(magnitude);
    return true;
  }
  constexpr uint64_t kMinMagnitude = uint64_t{1} << 63u;
  if (magnitude > kMinMagnitude) {
    return false;
  }
  out = magnitude == kMinMagnitude ? std::numeric_limits<int64_t>::min() : -static_cast<int64_t>(magnitude);
  return true;
}

BigIntObject* allocate_bigint_object(BigIntPayload& source) {
  auto* object = new BigIntObject();
  object->header.kind = ObjectKind::BigInt;
  object->header.refcnt = 1;
  object->impl = new BigIntPayload();
  move_assign_payload(*payload(object), source);
  xlang_perf_count_object_alloc(ObjectKind::BigInt);
  return object;
}

Value compact_payload(BigIntPayload& value) {
  normalize(value);
  int64_t small = 0;
  if (payload_to_i64(value, small)) {
    release_limbs(value);
    return Value::int64(small);
  }
  Value out;
  out.tag = ValueTag::Object;
  out.as.obj = &allocate_bigint_object(value)->header;
  return out;
}

bool value_to_payload(const Value& value, BigIntPayload& out) {
  if (value.tag == ValueTag::Int64) {
    const bool negative = value.as.i64 < 0;
    uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(value.as.i64 + 1)) + 1u
        : static_cast<uint64_t>(value.as.i64);
    out = make_payload_from_u64(magnitude, negative ? -1 : 1);
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = make_payload_from_u64(value.as.b ? 1u : 0u, 1);
    return true;
  }
  if (auto* bigint = value_as_bigint(value)) {
    out = clone_payload(*payload(bigint));
    return true;
  }
  Value attr;
  std::string ignored;
  if (object_get_attr(value, "__xlang3_int_value__", attr, ignored) ||
      object_get_attr(value, "_value_", attr, ignored)) {
    return value_to_payload(attr, out);
  }
  return false;
}

int compare_abs(const BigIntPayload& lhs, const BigIntPayload& rhs) {
  if (lhs.limb_count != rhs.limb_count) {
    return lhs.limb_count < rhs.limb_count ? -1 : 1;
  }
  for (uint32_t i = lhs.limb_count; i-- > 0;) {
    if (lhs.limbs[i] != rhs.limbs[i]) {
      return lhs.limbs[i] < rhs.limbs[i] ? -1 : 1;
    }
  }
  return 0;
}

int compare_payload(const BigIntPayload& lhs, const BigIntPayload& rhs) {
  if (lhs.sign != rhs.sign) {
    return lhs.sign < rhs.sign ? -1 : 1;
  }
  if (lhs.sign == 0) {
    return 0;
  }
  const int abs_cmp = compare_abs(lhs, rhs);
  return lhs.sign > 0 ? abs_cmp : -abs_cmp;
}

BigIntPayload add_abs(const BigIntPayload& lhs, const BigIntPayload& rhs, int8_t sign) {
  BigIntPayload out;
  out.sign = sign;
  const uint32_t count = std::max(lhs.limb_count, rhs.limb_count);
  ensure_capacity(out, count + 1u);
  uint64_t carry = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t a = i < lhs.limb_count ? lhs.limbs[i] : 0;
    const uint64_t b = i < rhs.limb_count ? rhs.limbs[i] : 0;
    const uint64_t sum = a + b + carry;
    out.limbs[i] = static_cast<uint32_t>(sum & 0xffffffffu);
    carry = sum >> 32u;
  }
  out.limb_count = count;
  if (carry != 0) {
    out.limbs[out.limb_count++] = static_cast<uint32_t>(carry);
  }
  normalize(out);
  return out;
}

BigIntPayload sub_abs(const BigIntPayload& larger, const BigIntPayload& smaller, int8_t sign) {
  BigIntPayload out;
  out.sign = sign;
  ensure_capacity(out, larger.limb_count);
  uint64_t borrow = 0;
  for (uint32_t i = 0; i < larger.limb_count; ++i) {
    const uint64_t a = larger.limbs[i];
    const uint64_t b = (i < smaller.limb_count ? smaller.limbs[i] : 0) + borrow;
    if (a >= b) {
      out.limbs[i] = static_cast<uint32_t>(a - b);
      borrow = 0;
    } else {
      out.limbs[i] = static_cast<uint32_t>((uint64_t{1} << 32u) + a - b);
      borrow = 1;
    }
  }
  out.limb_count = larger.limb_count;
  normalize(out);
  return out;
}

BigIntPayload add_payload(const BigIntPayload& lhs, const BigIntPayload& rhs) {
  if (lhs.sign == 0) return clone_payload(rhs);
  if (rhs.sign == 0) return clone_payload(lhs);
  if (lhs.sign == rhs.sign) {
    return add_abs(lhs, rhs, lhs.sign);
  }
  const int cmp = compare_abs(lhs, rhs);
  if (cmp == 0) {
    return make_zero_payload();
  }
  return cmp > 0 ? sub_abs(lhs, rhs, lhs.sign) : sub_abs(rhs, lhs, rhs.sign);
}

BigIntPayload negate_payload(const BigIntPayload& value) {
  BigIntPayload out = clone_payload(value);
  out.sign = static_cast<int8_t>(-out.sign);
  return out;
}

BigIntPayload mul_payload(const BigIntPayload& lhs, const BigIntPayload& rhs) {
  if (lhs.sign == 0 || rhs.sign == 0) {
    return make_zero_payload();
  }
  BigIntPayload out;
  out.sign = lhs.sign == rhs.sign ? 1 : -1;
  ensure_capacity(out, lhs.limb_count + rhs.limb_count);
  out.limb_count = lhs.limb_count + rhs.limb_count;
  std::memset(out.limbs, 0, sizeof(uint32_t) * out.limb_count);
  for (uint32_t i = 0; i < lhs.limb_count; ++i) {
    uint64_t carry = 0;
    for (uint32_t j = 0; j < rhs.limb_count; ++j) {
      const uint64_t current = out.limbs[i + j] + carry +
          static_cast<uint64_t>(lhs.limbs[i]) * rhs.limbs[j];
      out.limbs[i + j] = static_cast<uint32_t>(current & 0xffffffffu);
      carry = current >> 32u;
    }
    uint32_t k = i + rhs.limb_count;
    while (carry != 0) {
      const uint64_t current = static_cast<uint64_t>(out.limbs[k]) + carry;
      out.limbs[k] = static_cast<uint32_t>(current & 0xffffffffu);
      carry = current >> 32u;
      ++k;
    }
  }
  normalize(out);
  return out;
}

void mul_small_inplace(BigIntPayload& value, uint32_t factor) {
  if (value.sign == 0 || factor == 0) {
    release_limbs(value);
    value = {};
    return;
  }
  ensure_capacity(value, value.limb_count + 1u);
  uint64_t carry = 0;
  for (uint32_t i = 0; i < value.limb_count; ++i) {
    const uint64_t product = static_cast<uint64_t>(value.limbs[i]) * factor + carry;
    value.limbs[i] = static_cast<uint32_t>(product & 0xffffffffu);
    carry = product >> 32u;
  }
  if (carry != 0) {
    value.limbs[value.limb_count++] = static_cast<uint32_t>(carry);
  }
}

void add_small_inplace(BigIntPayload& value, uint32_t addend) {
  if (addend == 0) {
    return;
  }
  if (value.sign == 0) {
    value = make_payload_from_u64(addend, 1);
    return;
  }
  ensure_capacity(value, value.limb_count + 1u);
  uint64_t carry = addend;
  for (uint32_t i = 0; i < value.limb_count && carry != 0; ++i) {
    const uint64_t sum = static_cast<uint64_t>(value.limbs[i]) + carry;
    value.limbs[i] = static_cast<uint32_t>(sum & 0xffffffffu);
    carry = sum >> 32u;
  }
  if (carry != 0) {
    value.limbs[value.limb_count++] = static_cast<uint32_t>(carry);
  }
}

uint32_t div_small_inplace(BigIntPayload& value, uint32_t divisor) {
  uint64_t rem = 0;
  for (uint32_t i = value.limb_count; i-- > 0;) {
    const uint64_t current = (rem << 32u) | value.limbs[i];
    value.limbs[i] = static_cast<uint32_t>(current / divisor);
    rem = current % divisor;
  }
  normalize(value);
  return static_cast<uint32_t>(rem);
}

uint32_t bit_length_abs(const BigIntPayload& value) {
  if (value.limb_count == 0) {
    return 0;
  }
  const uint32_t top = value.limbs[value.limb_count - 1u];
  uint32_t bits = 32u * (value.limb_count - 1u);
  uint32_t v = top;
  while (v != 0) {
    ++bits;
    v >>= 1u;
  }
  return bits;
}

BigIntPayload shift_left_payload(const BigIntPayload& value, uint64_t count) {
  if (value.sign == 0) {
    return make_zero_payload();
  }
  const uint32_t limb_shift = static_cast<uint32_t>(count / 32u);
  const uint32_t bit_shift = static_cast<uint32_t>(count % 32u);
  BigIntPayload out;
  out.sign = value.sign;
  ensure_capacity(out, value.limb_count + limb_shift + 1u);
  std::memset(out.limbs, 0, sizeof(uint32_t) * (value.limb_count + limb_shift + 1u));
  uint64_t carry = 0;
  for (uint32_t i = 0; i < value.limb_count; ++i) {
    const uint64_t shifted = (static_cast<uint64_t>(value.limbs[i]) << bit_shift) | carry;
    out.limbs[i + limb_shift] = static_cast<uint32_t>(shifted & 0xffffffffu);
    carry = shifted >> 32u;
  }
  out.limb_count = value.limb_count + limb_shift;
  if (carry != 0) {
    out.limbs[out.limb_count++] = static_cast<uint32_t>(carry);
  }
  normalize(out);
  return out;
}

BigIntPayload shift_right_positive(const BigIntPayload& value, uint64_t count) {
  if (value.sign == 0) {
    return make_zero_payload();
  }
  const uint32_t limb_shift = static_cast<uint32_t>(count / 32u);
  const uint32_t bit_shift = static_cast<uint32_t>(count % 32u);
  if (limb_shift >= value.limb_count) {
    return make_zero_payload();
  }
  BigIntPayload out;
  out.sign = 1;
  const uint32_t new_count = value.limb_count - limb_shift;
  ensure_capacity(out, new_count);
  uint32_t carry = 0;
  for (uint32_t i = new_count; i-- > 0;) {
    const uint32_t limb = value.limbs[i + limb_shift];
    out.limbs[i] = bit_shift == 0 ? limb : ((limb >> bit_shift) | (carry << (32u - bit_shift)));
    carry = bit_shift == 0 ? 0 : limb;
  }
  out.limb_count = new_count;
  normalize(out);
  return out;
}

BigIntPayload shift_right_payload(const BigIntPayload& value, uint64_t count) {
  if (value.sign >= 0) {
    return shift_right_positive(value, count);
  }
  BigIntPayload one = make_payload_from_u64(1, 1);
  BigIntPayload abs_value = clone_payload(value);
  abs_value.sign = 1;
  BigIntPayload addend = shift_left_payload(one, count);
  BigIntPayload adjusted = add_payload(abs_value, addend);
  BigIntPayload neg_one = make_payload_from_u64(1, -1);
  BigIntPayload numerator = add_payload(adjusted, neg_one);
  BigIntPayload shifted = shift_right_positive(numerator, count);
  shifted.sign = shifted.sign == 0 ? 0 : -1;
  release_limbs(one);
  release_limbs(abs_value);
  release_limbs(addend);
  release_limbs(adjusted);
  release_limbs(neg_one);
  release_limbs(numerator);
  return shifted;
}

uint32_t twos_limb(const BigIntPayload& value, uint32_t index) {
  if (value.sign >= 0) {
    return index < value.limb_count ? value.limbs[index] : 0;
  }
  uint64_t carry = 1;
  uint32_t result = 0xffffffffu;
  for (uint32_t i = 0; i <= index; ++i) {
    const uint32_t source = i < value.limb_count ? value.limbs[i] : 0;
    const uint64_t sum = static_cast<uint64_t>(source ^ 0xffffffffu) + carry;
    result = static_cast<uint32_t>(sum & 0xffffffffu);
    carry = sum >> 32u;
  }
  return result;
}

BigIntPayload from_twos(uint32_t* limbs, uint32_t count) {
  const bool negative = count != 0 && (limbs[count - 1u] & 0x80000000u) != 0;
  BigIntPayload out;
  if (!negative) {
    out.sign = 1;
    ensure_capacity(out, count);
    std::memcpy(out.limbs, limbs, sizeof(uint32_t) * count);
    out.limb_count = count;
    normalize(out);
    return out;
  }
  out.sign = -1;
  ensure_capacity(out, count);
  uint64_t carry = 1;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t sum = static_cast<uint64_t>(limbs[i] ^ 0xffffffffu) + carry;
    out.limbs[i] = static_cast<uint32_t>(sum & 0xffffffffu);
    carry = sum >> 32u;
  }
  out.limb_count = count;
  normalize(out);
  return out;
}

BigIntPayload bitwise_payload(const BigIntPayload& lhs, const BigIntPayload& rhs, char op) {
  const uint32_t count = std::max(lhs.limb_count, rhs.limb_count) + 1u;
  const size_t bytes = sizeof(uint32_t) * count;
  auto* limbs = static_cast<uint32_t*>(memory::x3_thread_buckets().allocate(bytes));
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t a = twos_limb(lhs, i);
    const uint32_t b = twos_limb(rhs, i);
    if (op == '&') limbs[i] = a & b;
    else if (op == '|') limbs[i] = a | b;
    else limbs[i] = a ^ b;
  }
  BigIntPayload out = from_twos(limbs, count);
  memory::x3_thread_buckets().release(limbs, bytes);
  return out;
}

bool digit_value(char ch, int& out) {
  const unsigned char c = static_cast<unsigned char>(ch);
  if (c >= '0' && c <= '9') {
    out = c - '0';
    return true;
  }
  if (c >= 'a' && c <= 'z') {
    out = c - 'a' + 10;
    return true;
  }
  if (c >= 'A' && c <= 'Z') {
    out = c - 'A' + 10;
    return true;
  }
  return false;
}

bool shift_count(const Value& value, uint64_t& out, std::string& error) {
  BigIntPayload count;
  if (!value_to_payload(value, count)) {
    error = "shift count must be int";
    return false;
  }
  if (count.sign < 0) {
    release_limbs(count);
    error = "negative shift count";
    return false;
  }
  int64_t small = 0;
  if (!payload_to_i64(count, small)) {
    release_limbs(count);
    error = "shift count too large";
    return false;
  }
  release_limbs(count);
  out = static_cast<uint64_t>(small);
  return true;
}

} // namespace

Value value_bigint_from_i64(int64_t value) {
  BigIntPayload payload = make_payload_from_u64(
      value < 0 ? static_cast<uint64_t>(-(value + 1)) + 1u : static_cast<uint64_t>(value),
      value < 0 ? -1 : 1);
  return compact_payload(payload);
}

Value value_bigint_from_decimal(std::string_view text, int base, std::string& error) {
  if (base < 2 || base > 36) {
    error = "int() base must be >= 2 and <= 36";
    return Value::invalid();
  }
  size_t pos = 0;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  bool negative = false;
  if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    negative = text[pos] == '-';
    ++pos;
  }
  BigIntPayload value = make_zero_payload();
  bool saw_digit = false;
  for (; pos < text.size(); ++pos) {
    const char ch = text[pos];
    if (std::isspace(static_cast<unsigned char>(ch))) {
      while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
      }
      break;
    }
    if (ch == '_') {
      continue;
    }
    int digit = 0;
    if (!digit_value(ch, digit) || digit >= base) {
      release_limbs(value);
      error = "invalid literal for int()";
      return Value::invalid();
    }
    if (value.sign == 0) {
      value.sign = 1;
    }
    mul_small_inplace(value, static_cast<uint32_t>(base));
    add_small_inplace(value, static_cast<uint32_t>(digit));
    saw_digit = true;
  }
  if (!saw_digit || pos != text.size()) {
    release_limbs(value);
    error = "invalid literal for int()";
    return Value::invalid();
  }
  if (negative && value.sign != 0) {
    value.sign = -1;
  }
  return compact_payload(value);
}

bool value_bigint_from_bytes(
    const uint8_t* bytes,
    size_t size,
    bool is_big,
    bool signed_value,
    Value& out,
    std::string&) {
  BigIntPayload value = make_zero_payload();
  value.sign = 1;
  for (size_t i = 0; i < size; ++i) {
    const size_t index = is_big ? i : (size - 1u - i);
    mul_small_inplace(value, 256);
    add_small_inplace(value, bytes[index]);
  }
  if (signed_value && size != 0) {
    const uint8_t sign_byte = is_big ? bytes[0] : bytes[size - 1u];
    if ((sign_byte & 0x80u) != 0) {
      BigIntPayload range = make_payload_from_u64(1, 1);
      BigIntPayload shifted = shift_left_payload(range, size * 8u);
      BigIntPayload negative_range = negate_payload(shifted);
      BigIntPayload signed_result = add_payload(value, negative_range);
      release_limbs(value);
      release_limbs(range);
      release_limbs(shifted);
      release_limbs(negative_range);
      value = signed_result;
    }
  }
  out = compact_payload(value);
  return true;
}

bool value_int_like_to_bytes(
    const Value& value,
    size_t length,
    bool is_big,
    bool signed_value,
    std::string& out,
    std::string& error) {
  BigIntPayload payload;
  if (!value_to_payload(value, payload)) {
    error = "int.to_bytes value must be int";
    return false;
  }

  BigIntPayload limit = make_payload_from_u64(1, 1);
  BigIntPayload range = shift_left_payload(limit, length * 8u);
  BigIntPayload encoded = clone_payload(payload);
  if (payload.sign < 0) {
    if (!signed_value) {
      release_limbs(payload);
      release_limbs(limit);
      release_limbs(range);
      release_limbs(encoded);
      error = "can't convert negative int to unsigned";
      return false;
    }
    BigIntPayload plus_range = add_payload(encoded, range);
    move_assign_payload(encoded, plus_range);
  }
  const int upper_cmp = compare_abs(encoded, range);
  if (encoded.sign < 0 || upper_cmp >= 0) {
    release_limbs(payload);
    release_limbs(limit);
    release_limbs(range);
    release_limbs(encoded);
    error = "int too big to convert";
    return false;
  }

  out.assign(length, '\0');
  for (size_t i = 0; i < length; ++i) {
    const uint32_t byte = div_small_inplace(encoded, 256);
    const size_t index = is_big ? (length - 1u - i) : i;
    out[index] = static_cast<char>(byte);
  }
  release_limbs(payload);
  release_limbs(limit);
  release_limbs(range);
  release_limbs(encoded);
  return true;
}

void value_bigint_destroy(BigIntObject* value) {
  BigIntPayload* p = payload(value);
  if (p != nullptr) {
    release_limbs(*p);
    delete p;
  }
  delete value;
}

std::string value_bigint_to_string(const Value& value) {
  auto* object = value_as_bigint(value);
  const BigIntPayload* p = payload(object);
  if (p == nullptr || p->sign == 0) {
    return "0";
  }
  BigIntPayload temp = clone_payload(*p);
  temp.sign = 1;
  std::string digits;
  while (temp.sign != 0) {
    const uint32_t rem = div_small_inplace(temp, 10);
    digits.push_back(static_cast<char>('0' + rem));
  }
  if (p->sign < 0) {
    digits.push_back('-');
  }
  std::reverse(digits.begin(), digits.end());
  release_limbs(temp);
  return digits;
}

bool value_bigint_truthy(const Value& value) {
  auto* object = value_as_bigint(value);
  const BigIntPayload* p = payload(object);
  return p != nullptr && p->sign != 0;
}

bool value_bigint_to_i64(const Value& value, int64_t& out) {
  auto* object = value_as_bigint(value);
  const BigIntPayload* p = payload(object);
  return p != nullptr && payload_to_i64(*p, out);
}

bool value_int_like_to_i64(const Value& value, int64_t& out) {
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  Value attr;
  std::string ignored;
  if (object_get_attr(value, "__xlang3_int_value__", attr, ignored) && attr.tag == ValueTag::Int64) {
    out = attr.as.i64;
    return true;
  }
  if (object_get_attr(value, "_value_", attr, ignored) && attr.tag == ValueTag::Int64) {
    out = attr.as.i64;
    return true;
  }
  return value_bigint_to_i64(value, out);
}

bool value_int_like_bit_length(const Value& value, int64_t& out) {
  BigIntPayload p;
  if (!value_to_payload(value, p)) {
    return false;
  }
  out = bit_length_abs(p);
  release_limbs(p);
  return true;
}

bool value_int_like_hash(const Value& value, size_t& out) {
  BigIntPayload p;
  if (!value_to_payload(value, p)) {
    return false;
  }
  int64_t small = 0;
  if (payload_to_i64(p, small)) {
    out = std::hash<int64_t>{}(small);
    release_limbs(p);
    return true;
  }
  size_t hash = p.sign < 0 ? 0x9e3779b97f4a7c15ull : 0xcbf29ce484222325ull;
  for (uint32_t i = 0; i < p.limb_count; ++i) {
    hash ^= p.limbs[i] + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
  }
  out = hash == static_cast<size_t>(-1) ? static_cast<size_t>(-2) : hash;
  release_limbs(p);
  return true;
}

bool value_int_like_compare(const std::string& op, const Value& lhs, const Value& rhs, Value& out) {
  BigIntPayload left;
  BigIntPayload right;
  if (!value_to_payload(lhs, left) || !value_to_payload(rhs, right)) {
    release_limbs(left);
    release_limbs(right);
    return false;
  }
  const int cmp = compare_payload(left, right);
  release_limbs(left);
  release_limbs(right);
  bool result = false;
  if (op == "==") result = cmp == 0;
  else if (op == "!=") result = cmp != 0;
  else if (op == "<") result = cmp < 0;
  else if (op == "<=") result = cmp <= 0;
  else if (op == ">") result = cmp > 0;
  else if (op == ">=") result = cmp >= 0;
  else return false;
  value_set_bool(out, result);
  return true;
}

bool value_int_like_add(const Value& lhs, const Value& rhs, Value& out) {
  BigIntPayload left;
  BigIntPayload right;
  if (!value_to_payload(lhs, left) || !value_to_payload(rhs, right)) {
    release_limbs(left);
    release_limbs(right);
    return false;
  }
  BigIntPayload result = add_payload(left, right);
  release_limbs(left);
  release_limbs(right);
  out = compact_payload(result);
  return true;
}

bool value_int_like_sub(const Value& lhs, const Value& rhs, Value& out) {
  BigIntPayload left;
  BigIntPayload right;
  if (!value_to_payload(lhs, left) || !value_to_payload(rhs, right)) {
    release_limbs(left);
    release_limbs(right);
    return false;
  }
  BigIntPayload negative = negate_payload(right);
  BigIntPayload result = add_payload(left, negative);
  release_limbs(left);
  release_limbs(right);
  release_limbs(negative);
  out = compact_payload(result);
  return true;
}

bool value_int_like_mul(const Value& lhs, const Value& rhs, Value& out) {
  BigIntPayload left;
  BigIntPayload right;
  if (!value_to_payload(lhs, left) || !value_to_payload(rhs, right)) {
    release_limbs(left);
    release_limbs(right);
    return false;
  }
  BigIntPayload result = mul_payload(left, right);
  release_limbs(left);
  release_limbs(right);
  out = compact_payload(result);
  return true;
}

bool value_int_like_pow(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  BigIntPayload base;
  BigIntPayload exponent_payload;
  if (!value_to_payload(lhs, base) || !value_to_payload(rhs, exponent_payload)) {
    release_limbs(base);
    release_limbs(exponent_payload);
    return false;
  }
  int64_t exponent = 0;
  if (!payload_to_i64(exponent_payload, exponent)) {
    release_limbs(base);
    release_limbs(exponent_payload);
    error = "exponent too large";
    return false;
  }
  release_limbs(exponent_payload);
  if (exponent < 0) {
    release_limbs(base);
    return false;
  }
  BigIntPayload result = make_payload_from_u64(1, 1);
  uint64_t e = static_cast<uint64_t>(exponent);
  while (e != 0) {
    if ((e & 1u) != 0) {
      BigIntPayload next = mul_payload(result, base);
      move_assign_payload(result, next);
    }
    e >>= 1u;
    if (e != 0) {
      BigIntPayload next_base = mul_payload(base, base);
      move_assign_payload(base, next_base);
    }
  }
  release_limbs(base);
  out = compact_payload(result);
  return true;
}

bool value_int_like_bit_and(const Value& lhs, const Value& rhs, Value& out) {
  BigIntPayload left;
  BigIntPayload right;
  if (!value_to_payload(lhs, left) || !value_to_payload(rhs, right)) {
    release_limbs(left);
    release_limbs(right);
    return false;
  }
  BigIntPayload result = bitwise_payload(left, right, '&');
  release_limbs(left);
  release_limbs(right);
  out = compact_payload(result);
  return true;
}

bool value_int_like_bit_or(const Value& lhs, const Value& rhs, Value& out) {
  BigIntPayload left;
  BigIntPayload right;
  if (!value_to_payload(lhs, left) || !value_to_payload(rhs, right)) {
    release_limbs(left);
    release_limbs(right);
    return false;
  }
  BigIntPayload result = bitwise_payload(left, right, '|');
  release_limbs(left);
  release_limbs(right);
  out = compact_payload(result);
  return true;
}

bool value_int_like_bit_xor(const Value& lhs, const Value& rhs, Value& out) {
  BigIntPayload left;
  BigIntPayload right;
  if (!value_to_payload(lhs, left) || !value_to_payload(rhs, right)) {
    release_limbs(left);
    release_limbs(right);
    return false;
  }
  BigIntPayload result = bitwise_payload(left, right, '^');
  release_limbs(left);
  release_limbs(right);
  out = compact_payload(result);
  return true;
}

bool value_int_like_shift_left(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  BigIntPayload left;
  if (!value_to_payload(lhs, left)) {
    return false;
  }
  uint64_t count = 0;
  if (!shift_count(rhs, count, error)) {
    release_limbs(left);
    return false;
  }
  BigIntPayload result = shift_left_payload(left, count);
  release_limbs(left);
  out = compact_payload(result);
  return true;
}

bool value_int_like_shift_right(const Value& lhs, const Value& rhs, Value& out, std::string& error) {
  BigIntPayload left;
  if (!value_to_payload(lhs, left)) {
    return false;
  }
  uint64_t count = 0;
  if (!shift_count(rhs, count, error)) {
    release_limbs(left);
    return false;
  }
  BigIntPayload result = shift_right_payload(left, count);
  release_limbs(left);
  out = compact_payload(result);
  return true;
}

bool value_int_like_invert(const Value& value, Value& out) {
  BigIntPayload one = make_payload_from_u64(1, 1);
  BigIntPayload current;
  if (!value_to_payload(value, current)) {
    release_limbs(one);
    return false;
  }
  BigIntPayload plus_one = add_payload(current, one);
  BigIntPayload result = negate_payload(plus_one);
  release_limbs(one);
  release_limbs(current);
  release_limbs(plus_one);
  out = compact_payload(result);
  return true;
}

} // namespace xlang3
