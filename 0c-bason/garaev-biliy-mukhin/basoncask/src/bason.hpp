#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace bason {

enum class BasonType { Boolean, Array, String, Object, Number };

struct BasonRecord {
  BasonType type = BasonType::String;
  std::string key;
  std::string value;  // leaf types
  std::vector<BasonRecord> children;  // container types (Array, Object)
};

// Encode a record to bytes. Uses short form when key and value fit in 15 bytes.
std::vector<uint8_t> encode(const BasonRecord& record);

// Decode a record from bytes. Returns (record, bytes_consumed). Throws on error.
std::pair<BasonRecord, size_t> decode(const uint8_t* data, size_t len);

// Decode all records from a byte buffer.
std::vector<BasonRecord> decode_all(const uint8_t* data, size_t len);

// Create a String record (for put).
inline BasonRecord str(const std::string& key, const std::string& value) {
  BasonRecord r;
  r.type = BasonType::String;
  r.key = key;
  r.value = value;
  return r;
}

// Create a Boolean null record (tombstone for del).
inline BasonRecord tombstone(const std::string& key) {
  BasonRecord r;
  r.type = BasonType::Boolean;
  r.key = key;
  r.value = "";  // null
  return r;
}

}  // namespace bason
