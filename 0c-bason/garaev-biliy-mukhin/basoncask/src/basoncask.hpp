#pragma once

#include "bason.hpp"
#include "wal.hpp"
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <memory>

struct BasonCask {
  struct Options {
    std::string dir;
    uint64_t max_segment_size = 64 * 1024 * 1024;  // 64 MB
    bool sync_on_write = false;
  };

  static std::unique_ptr<BasonCask> open(Options opts);

  void put(const std::string& key, const bason::BasonRecord& value);
  std::optional<bason::BasonRecord> get(const std::string& key);
  void del(const std::string& key);
  void compact();  // Rewrite WAL with only latest versions of live keys
  void close();

 private:
  BasonCask() = default;
  void recover_index();
  bason::BasonRecord read_record_at(uint64_t offset);

  Options opts_;
  std::unique_ptr<wal::WalWriter> wal_;
  std::unordered_map<std::string, uint64_t> index_;  // key -> WAL offset
  std::mutex mutex_;
  bool closed_ = false;
};
