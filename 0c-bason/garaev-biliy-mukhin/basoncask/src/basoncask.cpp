#include "basoncask.hpp"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>

std::unique_ptr<BasonCask> BasonCask::open(Options opts) {
  auto c = std::unique_ptr<BasonCask>(new BasonCask());
  c->opts_ = opts;

  wal::WalReader reader = wal::WalReader::open(opts.dir);
  reader.recover();

  c->wal_ = std::make_unique<wal::WalWriter>(wal::WalWriter::open(opts.dir));
  try {
    c->recover_index();
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("recover_index failed: ") + e.what());
  }
  return c;
}

void BasonCask::recover_index() {
  index_.clear();
  wal::WalReader reader = wal::WalReader::open(opts_.dir);
  auto it = reader.scan(0);

  while (it.valid()) {
    const auto& rec = it.record();
    if (rec.type == bason::BasonType::Boolean && rec.value.empty()) {
      index_.erase(rec.key);
    } else {
      index_[rec.key] = it.offset();
    }
    it.next();
  }
}

bason::BasonRecord BasonCask::read_record_at(uint64_t offset) {
  auto segs = wal::get_segments_ordered(opts_.dir);
  if (segs.empty()) throw std::runtime_error("no WAL segments");

  for (const auto& si : segs) {
    if (si.start_offset > offset) continue;
    std::ifstream f(si.path, std::ios::binary);
    if (!f) continue;
    f.seekg(0, std::ios::end);
    size_t file_sz = f.tellg();
    if (si.start_offset + file_sz <= offset) continue;

    size_t file_pos = 20 + (offset - si.start_offset);
    f.seekg(file_pos);

    char tag;
    if (!f.get(tag)) throw std::runtime_error("read failed");
    bool long_form = (tag >= 'A' && tag <= 'Z');
    size_t kl, vl, header_len;
    uint8_t len_byte = 0;
    uint8_t hdr[5] = {0};
    if (long_form) {
      f.read(reinterpret_cast<char*>(hdr), 5);
      vl = hdr[0] | (hdr[1]<<8) | (hdr[2]<<16) | (hdr[3]<<24);
      kl = hdr[4];
      header_len = 6;
    } else {
      f.read(reinterpret_cast<char*>(&len_byte), 1);
      kl = (len_byte >> 4) & 15;
      vl = len_byte & 15;
      header_len = 2;
    }
    size_t total = header_len + kl + vl;
    std::vector<uint8_t> buf(total);
    buf[0] = static_cast<uint8_t>(tag);
    if (long_form) {
      memcpy(buf.data() + 1, hdr, 5);
      f.read(reinterpret_cast<char*>(buf.data() + 6), kl + vl);
    } else {
      buf[1] = len_byte;
      f.read(reinterpret_cast<char*>(buf.data() + 2), kl + vl);
    }
    auto [rec, n] = bason::decode(buf.data(), total);
    return rec;
  }
  throw std::runtime_error("offset not found");
}

void BasonCask::put(const std::string& key, const bason::BasonRecord& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) throw std::runtime_error("BasonCask closed");

  bason::BasonRecord rec;
  rec.type = value.type;
  rec.key = key;
  rec.value = value.value;
  rec.children = value.children;

  uint64_t off = wal_->append(rec);
  index_[key] = off;

  if (opts_.sync_on_write) {
    wal_->checkpoint();
    wal_->sync();
  }
}

std::optional<bason::BasonRecord> BasonCask::get(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) throw std::runtime_error("BasonCask closed");

  auto it = index_.find(key);
  if (it == index_.end()) return std::nullopt;

  return read_record_at(it->second);
}

void BasonCask::del(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) throw std::runtime_error("BasonCask closed");

  auto tomb = bason::tombstone(key);
  wal_->append(tomb);
  index_.erase(key);

  if (opts_.sync_on_write) {
    wal_->checkpoint();
    wal_->sync();
  }
}

void BasonCask::compact() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) throw std::runtime_error("BasonCask closed");

  if (index_.empty()) return;

  std::string new_dir = opts_.dir + ".compact";
  std::filesystem::create_directories(new_dir);
  auto new_wal = std::make_unique<wal::WalWriter>(wal::WalWriter::open(new_dir));

  std::unordered_map<std::string, bason::BasonRecord> latest;
  wal::WalReader reader = wal::WalReader::open(opts_.dir);
  auto it = reader.scan(0);
  while (it.valid()) {
    const auto& rec = it.record();
    if (rec.type == bason::BasonType::Boolean && rec.value.empty()) {
      latest.erase(rec.key);
    } else {
      latest[rec.key] = rec;
    }
    it.next();
  }
  for (const auto& [k, rec] : latest) {
    new_wal->append(rec);
  }
  new_wal->checkpoint();
  new_wal->sync();

  wal_.reset();
  for (const auto& e : std::filesystem::directory_iterator(opts_.dir)) {
    std::filesystem::remove(e.path());
  }
  for (const auto& e : std::filesystem::directory_iterator(new_dir)) {
    std::filesystem::rename(e.path(), opts_.dir + "/" + e.path().filename().string());
  }
  std::filesystem::remove(new_dir);

  wal_ = std::make_unique<wal::WalWriter>(wal::WalWriter::open(opts_.dir));
  recover_index();
}

void BasonCask::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) return;
  wal_->checkpoint();
  wal_->sync();
  closed_ = true;
}
