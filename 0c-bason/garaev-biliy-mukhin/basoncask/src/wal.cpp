#include "wal.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "sha256.h"

namespace wal {

namespace {

const char MAGIC[8] = {'B','A','S','O','N','W','A','L'};

void hash_update(std::vector<uint8_t>& ctx, const uint8_t* data, size_t len) {
  SHA256_CTX* c = reinterpret_cast<SHA256_CTX*>(ctx.data());
  SHA256_Update(c, data, len);
}

void hash_final(std::vector<uint8_t>& ctx, uint8_t* out) {
  SHA256_CTX* c = reinterpret_cast<SHA256_CTX*>(ctx.data());
  SHA256_Final(out, c);
  SHA256_Init(c);  // reset for next use
}

void hash_init(std::vector<uint8_t>& ctx) {
  ctx.resize(sizeof(SHA256_CTX));
  SHA256_CTX* c = reinterpret_cast<SHA256_CTX*>(ctx.data());
  SHA256_Init(c);
}

std::string segment_name(uint64_t start) {
  std::ostringstream o;
  o << std::setfill('0') << std::setw(20) << start << ".wal";
  return o.str();
}

std::vector<std::string> list_segments(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  struct dirent* e;
  while ((e = readdir(d))) {
    std::string name = e->d_name;
    if (name.size() >= 24 && name.substr(name.size()-4) == ".wal") {
      out.push_back(dir + "/" + name);
    }
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

static std::vector<SegmentInfo> get_segments_impl(const std::string& dir) {
  auto files = list_segments(dir);
  std::vector<SegmentInfo> infos;
  for (const auto& p : files) {
    std::string name = p.substr(p.find_last_of("/") + 1);
    if (name.size() < 24) continue;
    uint64_t start = 0;
    try {
      start = std::stoull(name.substr(0, 20));
    } catch (...) { continue; }
    infos.push_back({p, start});
  }
  std::sort(infos.begin(), infos.end(),
            [](const SegmentInfo& a, const SegmentInfo& b) {
              return a.start_offset < b.start_offset;
            });
  return infos;
}

}  // namespace

std::vector<SegmentInfo> get_segments_ordered(const std::string& dir) {
  return get_segments_impl(dir);
}

WalWriter WalWriter::open(const std::string& dir) {
  WalWriter w;
  w.dir_ = dir;
  mkdir(dir.c_str(), 0755);

  auto segs = get_segments_impl(dir);
  if (!segs.empty()) {
    // Find end of last segment to get current_offset
    auto& last = segs.back();
    std::ifstream f(last.path, std::ios::binary | std::ios::ate);
    if (f) {
      size_t file_size = f.tellg();
      f.close();
      // Header is 20 bytes; rest is records. We need the offset of the next write.
      // The global offset of the first byte after the last segment's content.
      w.current_offset_ = last.start_offset + (file_size - 20);
      w.segment_start_ = last.start_offset;
    }
  }

  w.ensure_segment();
  hash_init(w.hash_state_);
  if (!segs.empty()) {
    auto& last = segs.back();
    std::ifstream f(last.path, std::ios::binary);
    if (f) {
      f.seekg(0, std::ios::end);
      size_t sz = f.tellg();
      f.seekg(0);
      size_t to_hash = sz;
      if (sz > HASH_SIZE + 1) {
        f.seekg(sz - HASH_SIZE - 1);
        char c;
        if (f.get(c) && static_cast<uint8_t>(c) == CHECKPOINT_TAG)
          to_hash = sz - HASH_SIZE;
        f.seekg(0);
      }
      if (to_hash > 0) {
        std::vector<uint8_t> content(to_hash);
        f.read(reinterpret_cast<char*>(content.data()), to_hash);
        hash_update(w.hash_state_, content.data(), to_hash);
      }
    }
  } else {
    uint8_t hdr[20];
    memcpy(hdr, MAGIC, 8);
    uint16_t ver = WAL_VERSION, flags = 0;
    memcpy(hdr + 8, &ver, 2);
    memcpy(hdr + 10, &flags, 2);
    uint64_t start_le = w.segment_start_;
    memcpy(hdr + 12, &start_le, 8);
    hash_update(w.hash_state_, hdr, 20);
  }
  return w;
}

void WalWriter::ensure_segment() {
  if (file_ && file_->is_open()) return;

  std::string path = dir_ + "/" + segment_name(segment_start_);
  file_ = std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::app);
  if (!file_->good()) throw std::runtime_error("cannot open WAL segment: " + path);

  // If new file, write header
  file_->seekp(0, std::ios::end);
  auto pos = file_->tellp();
  if (pos == 0) {
    file_->write(MAGIC, 8);
    uint16_t ver = WAL_VERSION;
    uint16_t flags = 0;
    file_->write(reinterpret_cast<const char*>(&ver), 2);
    file_->write(reinterpret_cast<const char*>(&flags), 2);
    uint64_t start_le = segment_start_;
    file_->write(reinterpret_cast<const char*>(&start_le), 8);
  }
}

void WalWriter::write_record(const uint8_t* data, size_t len) {
  size_t padding = (RECORD_ALIGN - (len % RECORD_ALIGN)) % RECORD_ALIGN;
  file_->write(reinterpret_cast<const char*>(data), len);
  uint8_t zeros[7] = {0};
  if (padding) file_->write(reinterpret_cast<const char*>(zeros), padding);
  hash_update(hash_state_, data, len);
  hash_update(hash_state_, zeros, padding);
  bytes_since_checkpoint_ += len + padding;
}

uint64_t WalWriter::append(const bason::BasonRecord& record) {
  auto enc = bason::encode(record);
  uint64_t offset = current_offset_;
  write_record(enc.data(), enc.size());
  current_offset_ += enc.size();
  size_t padding = (RECORD_ALIGN - (enc.size() % RECORD_ALIGN)) % RECORD_ALIGN;
  current_offset_ += padding;
  return offset;
}

void WalWriter::checkpoint() {
  file_->write(reinterpret_cast<const char*>(&CHECKPOINT_TAG), 1);
  hash_update(hash_state_, &CHECKPOINT_TAG, 1);
  uint8_t digest[HASH_SIZE];
  hash_final(hash_state_, digest);
  hash_init(hash_state_);
  file_->write(reinterpret_cast<const char*>(digest), HASH_SIZE);
  bytes_since_checkpoint_ = 0;
}

void WalWriter::sync() {
  if (file_) file_->flush();
  // fsync the file descriptor - ofstream doesn't expose it easily
  // For robustness we'd use open()+write() instead of ofstream
}

void WalWriter::rotate(uint64_t max_segment_size) {
  max_segment_size_ = max_segment_size;
  file_->close();
  file_.reset();
  segment_start_ = current_offset_;
  ensure_segment();
}

WalReader WalReader::open(const std::string& dir) {
  WalReader r;
  r.dir_ = dir;
  return r;
}

uint64_t WalReader::recover() {
  auto segs = get_segments_impl(dir_);
  uint64_t last_valid_offset = 0;
  std::string last_truncate_path;
  size_t last_truncate_pos = 0;

  for (const auto& si : segs) {
    std::ifstream f(si.path, std::ios::binary);
    if (!f) continue;

    f.seekg(0, std::ios::end);
    size_t file_size = f.tellg();
    f.seekg(0);

    if (file_size < 20) continue;

    char magic[8];
    f.read(magic, 8);
    if (memcmp(magic, MAGIC, 8) != 0) break;

    uint64_t start_le;
    f.seekg(12);
    f.read(reinterpret_cast<char*>(&start_le), 8);
    f.seekg(0);

    std::vector<uint8_t> hash_ctx;
    hash_init(hash_ctx);
    std::vector<uint8_t> header(20);
    f.read(reinterpret_cast<char*>(header.data()), 20);
    hash_update(hash_ctx, header.data(), 20);

    size_t pos = 20;
    while (pos < file_size) {
      char tag;
      if (!f.get(tag)) break;
      hash_update(hash_ctx, reinterpret_cast<const uint8_t*>(&tag), 1);
      pos++;

      if (static_cast<uint8_t>(tag) == CHECKPOINT_TAG) {
        uint8_t stored_hash[HASH_SIZE];
        if (pos + HASH_SIZE > file_size) break;
        f.read(reinterpret_cast<char*>(stored_hash), HASH_SIZE);
        pos += HASH_SIZE;

        uint8_t computed[HASH_SIZE];
        hash_final(hash_ctx, computed);
        if (memcmp(stored_hash, computed, HASH_SIZE) == 0) {
          last_valid_offset = start_le + (pos - 20);
          last_truncate_path = si.path;
          last_truncate_pos = pos;
        }
        hash_init(hash_ctx);
        continue;
      }

      bool long_form = (tag >= 'A' && tag <= 'Z');
      size_t kl, vl, header_len;
      if (long_form) {
        uint8_t hdr[5];
        f.read(reinterpret_cast<char*>(hdr), 5);
        pos += 5;
        vl = hdr[0] | (hdr[1]<<8) | (hdr[2]<<16) | (hdr[3]<<24);
        kl = hdr[4];
        header_len = 6;
      } else {
        uint8_t len_byte;
        f.read(reinterpret_cast<char*>(&len_byte), 1);
        pos += 1;
        kl = (len_byte >> 4) & 15;
        vl = len_byte & 15;
        header_len = 2;
      }
      size_t total = header_len + kl + vl;
      size_t padding = (RECORD_ALIGN - (total % RECORD_ALIGN)) % RECORD_ALIGN;
      total += padding;

      if (pos + total > file_size) break;
      std::vector<uint8_t> rec(total);
      f.read(reinterpret_cast<char*>(rec.data()), total);
      pos += total;
      hash_update(hash_ctx, rec.data(), total);
    }
  }

  if (!last_truncate_path.empty()) {
    int fd = ::open(last_truncate_path.c_str(), O_WRONLY);
    if (fd >= 0) {
      ftruncate(fd, last_truncate_pos);
      close(fd);
    }
    bool found = false;
    for (const auto& si : segs) {
      if (si.path == last_truncate_path) found = true;
      else if (found) unlink(si.path.c_str());
    }
  }

  return last_valid_offset;
}

WalIterator WalReader::scan(uint64_t from_offset) {
  WalIterator it;
  it.from_offset_ = from_offset;

  auto segs = get_segments_impl(dir_);
  for (const auto& si : segs) {
    std::ifstream probe(si.path, std::ios::binary | std::ios::ate);
    size_t sz = probe.tellg();
    probe.close();
    if (si.start_offset + sz > from_offset) {
      it.segments_.push_back({si.path, si.start_offset});
    }
  }

  if (it.segments_.empty()) return it;

  it.seg_idx_ = 0;
  const auto& first = it.segments_[0];
  it.file_offset_ = 20 + (from_offset - first.second);
  if (it.file_offset_ < 20) it.file_offset_ = 20;
  it.file_.open(it.segments_[0].first, std::ios::binary);
  it.file_.seekg(it.file_offset_);
  it.next();
  return it;
}

bool WalIterator::valid() const {
  return has_current_;
}

void WalIterator::next() {
  has_current_ = false;
  while (seg_idx_ < segments_.size()) {
    const auto& [path, seg_start] = segments_[seg_idx_];
    if (!file_.is_open() || file_.fail()) {
      file_.open(path, std::ios::binary);
      file_.seekg(file_offset_);
    }

    size_t rec_start = file_.tellg();
    char tag;
    if (!file_.get(tag)) {
      file_.close();
      seg_idx_++;
      file_offset_ = 20;
      continue;
    }

    if (static_cast<uint8_t>(tag) == CHECKPOINT_TAG) {
      file_.seekg(HASH_SIZE, std::ios::cur);
      file_offset_ = file_.tellg();
      continue;
    }

    bool long_form = (tag >= 'A' && tag <= 'Z');
    uint8_t len_byte = 0;
    uint8_t hdr[5] = {0};
    size_t kl, vl, header_len;
    if (long_form) {
      file_.read(reinterpret_cast<char*>(hdr), 5);
      vl = hdr[0] | (hdr[1]<<8) | (hdr[2]<<16) | (hdr[3]<<24);
      kl = hdr[4];
      header_len = 6;
    } else {
      file_.read(reinterpret_cast<char*>(&len_byte), 1);
      kl = (len_byte >> 4) & 15;
      vl = len_byte & 15;
      header_len = 2;
    }

    size_t rec_len = header_len + kl + vl;
    size_t padding = (RECORD_ALIGN - (rec_len % RECORD_ALIGN)) % RECORD_ALIGN;

    buffer_.resize(rec_len);
    buffer_[0] = static_cast<uint8_t>(tag);
    if (long_form) {
      memcpy(buffer_.data() + 1, hdr, 5);
      file_.read(reinterpret_cast<char*>(buffer_.data() + 6), kl + vl);
    } else {
      buffer_[1] = len_byte;
      file_.read(reinterpret_cast<char*>(buffer_.data() + 2), kl + vl);
    }
    file_.seekg(padding, std::ios::cur);
    file_offset_ = file_.tellg();

    global_offset_ = seg_start + (rec_start - 20);
    if (global_offset_ < from_offset_) continue;

    auto [rec, n] = bason::decode(buffer_.data(), rec_len);
    current_ = std::move(rec);
    has_current_ = true;
    return;
  }
}

uint64_t WalIterator::offset() const {
  return global_offset_;
}

const bason::BasonRecord& WalIterator::record() const {
  return current_;
}

void truncate_before(const std::string& dir, uint64_t offset) {
  auto segs = get_segments_impl(dir);
  for (const auto& si : segs) {
    std::ifstream f(si.path, std::ios::binary | std::ios::ate);
    size_t sz = f.tellg();
    f.close();
    uint64_t seg_end = si.start_offset + sz;
    if (seg_end <= offset) {
      unlink(si.path.c_str());
    }
  }
}

}  // namespace wal
