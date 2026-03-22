#include "bason.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace bason {

namespace {

constexpr uint8_t TAG_B = 0x62, TAG_A = 0x61, TAG_S = 0x73, TAG_O = 0x6F, TAG_N = 0x6E;
constexpr uint8_t TAG_BL = 0x42, TAG_AL = 0x41, TAG_SL = 0x53, TAG_OL = 0x4F, TAG_NL = 0x4E;

uint8_t tag_for_type(BasonType t, bool long_form) {
  switch (t) {
    case BasonType::Boolean: return long_form ? TAG_BL : TAG_B;
    case BasonType::Array:   return long_form ? TAG_AL : TAG_A;
    case BasonType::String:  return long_form ? TAG_SL : TAG_S;
    case BasonType::Object:  return long_form ? TAG_OL : TAG_O;
    case BasonType::Number:  return long_form ? TAG_NL : TAG_N;
  }
  return TAG_S;
}

BasonType type_from_tag(uint8_t tag) {
  uint8_t upper = tag & ~0x20;  // normalize to uppercase
  switch (upper) {
    case TAG_BL: return BasonType::Boolean;
    case TAG_AL: return BasonType::Array;
    case TAG_SL: return BasonType::String;
    case TAG_OL: return BasonType::Object;
    case TAG_NL: return BasonType::Number;
  }
  throw std::runtime_error("invalid BASON tag");
}

bool is_long_form(uint8_t tag) {
  return (tag >= 'A' && tag <= 'Z');
}

void append_le32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 24));
}

}  // namespace

std::vector<uint8_t> encode(const BasonRecord& record) {
  std::vector<uint8_t> out;

  // For container types, value is concatenated children
  std::vector<uint8_t> value_bytes;
  if (record.type == BasonType::Array || record.type == BasonType::Object) {
    for (const auto& c : record.children) {
      auto enc = encode(c);
      value_bytes.insert(value_bytes.end(), enc.begin(), enc.end());
    }
  } else {
    value_bytes.assign(record.value.begin(), record.value.end());
  }

  size_t kl = record.key.size();
  size_t vl = value_bytes.size();
  bool use_short = (kl <= 15 && vl <= 15);

  out.push_back(tag_for_type(record.type, !use_short));

  if (use_short) {
    out.push_back(static_cast<uint8_t>((kl << 4) | (vl & 15)));
  } else {
    if (kl > 255) throw std::runtime_error("key too long");
    append_le32(out, static_cast<uint32_t>(vl));
    out.push_back(static_cast<uint8_t>(kl));
  }

  out.insert(out.end(), record.key.begin(), record.key.end());
  out.insert(out.end(), value_bytes.begin(), value_bytes.end());

  return out;
}

std::pair<BasonRecord, size_t> decode(const uint8_t* data, size_t len) {
  if (len < 2) throw std::runtime_error("truncated BASON record");

  BasonRecord rec;
  rec.type = type_from_tag(data[0]);
  bool long_form = is_long_form(data[0]);

  size_t kl, vl;
  size_t header_size;

  if (long_form) {
    if (len < 6) throw std::runtime_error("truncated long BASON record");
    vl = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    kl = data[5];
    header_size = 6;
  } else {
    kl = (data[1] >> 4) & 15;
    vl = data[1] & 15;
    header_size = 2;
  }

  if (len < header_size + kl + vl) throw std::runtime_error("truncated BASON payload");

  rec.key.assign(reinterpret_cast<const char*>(data + header_size), kl);
  size_t consumed = header_size + kl + vl;

  if (rec.type == BasonType::Array || rec.type == BasonType::Object) {
    const uint8_t* p = data + header_size + kl;
    size_t rem = vl;
    while (rem > 0) {
      auto [child, n] = decode(p, rem);
      rec.children.push_back(std::move(child));
      p += n;
      rem -= n;
    }
  } else {
    rec.value.assign(reinterpret_cast<const char*>(data + header_size + kl), vl);
  }

  return {rec, consumed};
}

std::vector<BasonRecord> decode_all(const uint8_t* data, size_t len) {
  std::vector<BasonRecord> result;
  size_t pos = 0;
  while (pos < len) {
    auto [rec, n] = decode(data + pos, len - pos);
    result.push_back(std::move(rec));
    pos += n;
  }
  return result;
}

}  // namespace bason
