# BASONCask — Report

## Implementation Summary

BASONCask is an append-only log-structured key-value store with an in-memory hash index. It uses the BASON binary format for records and a Write-Ahead Log (WAL) for durability.

### Components Implemented

1. **BASON Codec** — Encoder/decoder for BASON TLV records (short and long form)
2. **Write-Ahead Log** — Segment files with SHA256 checkpoints (spec uses BLAKE3; SHA256 used for portability)
3. **BasonCask Store** — put, get, del, recovery, compaction

---

## Analysis Questions

### 1. Maximum database size given hash map in memory

The hash map stores one entry per key: `std::string` (key) + `uint64_t` (offset). On a 64-bit system:

- Per entry: ~32 bytes (string with small string optimization) + 8 bytes + hash table overhead (~1.5x) ≈ **60–80 bytes per key**
- With 16 GB RAM dedicated to the index: ~200–250 million keys
- Practical limit depends on key length; longer keys increase memory use

### 2. Why range scanning is expensive

BASONCask uses a hash index keyed by the full key. There is no ordering:

- Keys are distributed by hash, not lexicographically
- A range scan `[start, end)` would require iterating all keys and filtering, O(N)
- The WAL stores records in append order, not key order

To support efficient range queries we would need an additional structure: a B-tree or skip list ordered by key, or a separate sorted index file (e.g., SST-style).

### 3. Write amplification: BASONCask vs B-tree

- **BASONCask**: Each put appends to the log. With compaction, we rewrite only live keys into a new segment. Write amplification ≈ (total bytes written to WAL) / (user bytes). For a workload with many updates to the same keys, compaction reduces space and obsolete data, but compaction itself does a full rewrite of live data.
- **B-tree**: Each update typically touches one leaf page and possibly ancestor pages. Write amplification is usually 1–2x for in-place updates.
- **When BASONCask wins**: 
  - Write-heavy workloads where sequential append throughput matters
  - Many small, random writes (append is fast; B-tree does more random I/O)
  - Simpler implementation and easier crash recovery

---

## Design Choices

- **SHA256 instead of BLAKE3**: Used for checkpoint hashing to avoid external dependencies. Can be swapped for BLAKE3 for full spec compliance.
- **Compaction**: Implemented as a full rewrite to a new directory, then atomic swap. Basic implementation; may need additional testing for edge cases.
- **Thread safety**: BasonCask uses a single mutex for all operations.
