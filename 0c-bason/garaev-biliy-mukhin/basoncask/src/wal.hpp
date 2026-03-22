#pragma once

#include "bason.hpp"
#include <cstdint>
#include <string>
#include <memory>
#include <fstream>
#include <vector>

namespace wal {

constexpr uint16_t WAL_VERSION = 1;
constexpr uint8_t CHECKPOINT_TAG = 0x48;  // 'H'
constexpr size_t HASH_SIZE = 32;
constexpr size_t RECORD_ALIGN = 8;

struct WalWriter {
  static WalWriter open(const std::string& dir);

  uint64_t append(const bason::BasonRecord& record);
  void checkpoint();
  void sync();
  void rotate(uint64_t max_segment_size);

  std::string dir_;
  uint64_t current_offset_ = 0;
  uint64_t segment_start_ = 0;
  uint64_t max_segment_size_ = 64 * 1024 * 1024;
  std::unique_ptr<std::ofstream> file_;
  std::vector<uint8_t> hash_state_;
  size_t bytes_since_checkpoint_ = 0;

 private:
  WalWriter() = default;
  void write_record(const uint8_t* data, size_t len);
  void ensure_segment();
};

struct WalIterator {
  bool valid() const;
  void next();
  uint64_t offset() const;
  const bason::BasonRecord& record() const;

  std::vector<std::pair<std::string, uint64_t>> segments_;  // path, start_offset
  size_t seg_idx_ = 0;
  uint64_t file_offset_ = 0;
  uint64_t global_offset_ = 0;
  uint64_t from_offset_ = 0;
  std::vector<uint8_t> buffer_;
  bason::BasonRecord current_;
  bool has_current_ = false;
  std::ifstream file_;
};

struct WalReader {
  static WalReader open(const std::string& dir);
  uint64_t recover();
  WalIterator scan(uint64_t from_offset);

  std::string dir_;
};

void truncate_before(const std::string& dir, uint64_t offset);

struct SegmentInfo {
  std::string path;
  uint64_t start_offset;
};
std::vector<SegmentInfo> get_segments_ordered(const std::string& dir);

}  // namespace wal
