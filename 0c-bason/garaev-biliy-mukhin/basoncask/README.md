# BASONCask

Append-only log-structured key-value store with in-memory hash index (inspired by Bitcask).

## Build

```bash
make
```

## Run tests

```bash
make test
```

## API

```cpp
auto db = BasonCask::open({.dir = "/path/to/store", .sync_on_write = true});

db->put("key", bason::str("", "value"));
auto v = db->get("key");
db->del("key");
db->compact();  // Rewrite WAL with only latest versions
db->close();
```

## Components

- **BASON Codec** (`bason.hpp/cpp`): Minimal encoder/decoder for BASON TLV format
- **WAL** (`wal.hpp/cpp`): Append-only log with SHA256 checkpoints, segment files
- **BasonCask** (`basoncask.hpp/cpp`): KV store using WAL + hash index

## Format

- WAL segment: 20-byte header (BASONWAL magic, version, start_offset) + records (8-byte aligned) + checkpoints (0x48 + 32-byte hash)
- Records: BASON flat-mode (key = path, value = payload). Tombstones: Boolean with empty value
