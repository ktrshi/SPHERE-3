# Binary Format Migration: phel.zst Input + moshit.zst Output

**Date:** 2026-03-23
**Status:** Approved

## Context

The `sphall` project has migrated from ASCII `phels_to_trace_*` files to binary `.phel.zst` format (zstd-compressed). SPHERE-3_G4 must be updated to:
1. Read the new binary input format
2. Write a new binary output format (`.moshit.zst`)
3. Replace bash orchestration scripts with Python
4. Deploy and run on two servers (213.131.1.111 and 213.131.1.50) with different atmosphere models

No backward compatibility with old ASCII format is needed.

## Binary Formats

### Input: `.phel.zst`

Single zstd-compressed frame. Defined by sphall project.

**Header (40 bytes, little-endian):**

| Offset | Size | Field      | Description                              |
|--------|------|------------|------------------------------------------|
| 0      | 4B   | magic      | 'P','H','E','L'                          |
| 4      | 1B   | version    | 1                                        |
| 5      | 1B   | flags      | bit 0: has_background                    |
| 6      | 2B   | clone_num  | uint16                                   |
| 8      | 4B   | xsh        | shower core shift X, meters (float32)    |
| 12     | 4B   | ysh        | shower core shift Y, meters (float32)    |
| 16     | 4B   | zz         | detector altitude, meters (float32, neg) |
| 20     | 2B   | catm       | atmosphere model (uint16)                |
| 22     | 2B   | _pad       | reserved zeros                           |
| 24     | 4B   | tmin       | min arrival time, ns (float32)           |
| 28     | 4B   | tmax       | max arrival time, ns (float32)           |
| 32     | 4B   | tbig       | reference time, ns (float32)             |
| 36     | 4B   | n_photons  | uint32                                   |

**Photon record (20 bytes each):**

| Offset | Size | Field | Description                                   |
|--------|------|-------|-----------------------------------------------|
| 0      | 2B   | i     | X-bin (0..1279) uint16                        |
| 2      | 2B   | j     | Y-bin (0..1279) uint16                        |
| 4      | 1B   | k     | bit7: is_background; bits 0-6: time-bin       |
| 5      | 3B   | _pad  | reserved zeros                                |
| 8      | 4B   | x     | X coordinate, meters (float32)               |
| 12     | 4B   | y     | Y coordinate, meters (float32)               |
| 16     | 4B   | t     | arrival time, ns (float32)                    |

### Output: `.moshit.zst`

Single zstd-compressed frame containing header + all detection records. The entire content (header + records) is compressed as one zstd frame; the `.moshit.zst` file IS that frame.

**Header (24 bytes, little-endian):**

| Offset | Size | Field     | Description                              |
|--------|------|-----------|------------------------------------------|
| 0      | 4B   | magic     | 'M','O','S','H'                          |
| 4      | 1B   | version   | 1                                        |
| 5      | 1B   | _pad0     | reserved (alignment)                     |
| 6      | 2B   | _pad1     | reserved (alignment)                     |
| 8      | 4B   | zz        | detector altitude, meters (float32)      |
| 12     | 4B   | xsh       | shower core shift X, meters (float32)    |
| 16     | 4B   | ysh       | shower core shift Y, meters (float32)    |
| 20     | 4B   | n_hits    | number of detection records (uint32)     |

All float32 fields are 4-byte aligned. `n_hits` (not `n_photons`) to distinguish from input photon count.

Fields NOT included (by design):
- `catm` — determined by server/directory structure
- `tmin/tmax/tbig` — not consumed by SPHERE-3_G4; time range derivable from `t` fields in records

**Detection record (16 bytes each, naturally aligned):**

| Offset | Size | Field  | Description                        |
|--------|------|--------|------------------------------------|
| 0      | 2B   | pixel  | absolute pixel (0-2652), uint16    |
| 2      | 1B   | origin | 1=Cherenkov, 2=background (uint8)  |
| 3      | 1B   | kk     | CORSIKA k-index (uint8)            |
| 4      | 2B   | ii     | CORSIKA i-index (uint16)           |
| 6      | 2B   | jj     | CORSIKA j-index (uint16)           |
| 8      | 4B   | t      | detection time, ns (float32)       |
| 12     | 4B   | t0     | emission time, ns (float32)        |

All fields are naturally aligned: uint16 at even offsets, float32 at 4-byte offsets. No `memcpy` workarounds needed for strict-alignment architectures.

Crosstalk hits are written as regular records with the neighbor's pixel index.

## Architecture Changes

### New Files

- `include/PhelReader.hh` + `src/PhelReader.cc` — reads `.phel.zst`, returns `PhelEvent` struct
- `include/MoshitWriter.hh` + `src/MoshitWriter.cc` — buffers hits, compresses and writes `.moshit.zst`

### Modified Files

- **`CMakeLists.txt`** — add zstd dependency (`find_library` + `find_path` with fallback)
- **`src/sphmirrPrimaryGeneratorAction.cc`** — replace ASCII parsing with `PhelReader::Read()`. Physics (entry point randomization, direction vector) unchanged. `BuildSuffix()` adapted: strip `.phel.zst` extension, rest of suffix logic (find `_cNNN`, insert `_{height}m`) unchanged. `height` derived from `abs(header.zz)`.
- **`src/sphmirrSteppingAction.cc`** — replace `snprintf+write()` with `MoshitWriter::AddHit(pixel, t, origin, ii, jj, kk, t0)`. Pixel = `cluster*7+pix`. Crosstalk: `AddHit(neighbor_pixel, ...)`.
- **`src/sphmirrEventAction.cc`** — `BeginOfEvent`: init MoshitWriter with header data (zz, xsh, ysh). `EndOfEvent`: `Flush()` to `.moshit.zst`, log summary.
- **`include/WorkerEventData.hh`** — replace fd+char buffer with `MoshitWriter*`.

### Removed Code

- `FormatDetectionLine()` — replaced by `MoshitWriter::AddHit()`
- `ParseHeader()` — replaced by `PhelReader::Read()`
- ASCII snprintf+write() hot path
- File descriptor open/close management
- Sentinel line bug (old ASCII parser created spurious photon from `-1 -1 -1 -1 tmin tmax tbig` terminator)

### PhelReader Design

```cpp
struct PhelHeader {
    float xsh, ysh, zz;
    uint16_t catm, clone_num;
    float tmin, tmax, tbig;
    uint32_t n_photons;
    bool has_background;
};

struct PhelPhoton {
    uint16_t i, j;
    uint8_t k;
    bool is_background;
    float x, y, t;
};

struct PhelEvent {
    PhelHeader header;
    std::vector<PhelPhoton> photons;
};

class PhelReader {
public:
    // Reads entire .phel.zst file. Throws std::runtime_error on:
    // - file open failure
    // - zstd decompression failure
    // - magic mismatch
    // - n_photons vs actual record count mismatch
    static PhelEvent Read(const std::string& filename);
};
```

Implementation:
1. `fread` entire compressed file into buffer
2. `ZSTD_getFrameContentSize()` to determine decompressed size
3. `ZSTD_decompress()` into allocated buffer
4. Validate magic bytes ('P','H','E','L')
5. Parse 40-byte header into `PhelHeader`
6. Interpret photon array (N × 20 bytes) into `vector<PhelPhoton>`
7. Validate `n_photons` matches actual record count

### MoshitWriter Design

```cpp
class MoshitWriter {
public:
    void Begin(float zz, float xsh, float ysh);
    void AddHit(uint16_t pixel, float t, uint8_t origin,
                uint16_t ii, uint16_t jj, uint8_t kk, float t0);
    void Flush(const std::string& filename);
    void Reset();
private:
    float zz_, xsh_, ysh_;
    uint32_t n_hits_ = 0;
    std::vector<uint8_t> buffer_; // pre-allocated, grows as needed
};
```

- `AddHit()`: pack 16-byte record into buffer (direct struct write, naturally aligned). No formatting, no syscalls.
- `Flush()`: serialize 24-byte header (with final `n_hits_`) + buffer into contiguous block → `ZSTD_compress()` → write single zstd frame to file.
- `Reset()`: clear buffer, zero `n_hits_` for next event.

## Scripts

### `scripts/run_batch.py` — replaces run_all.sh + run_one.sh

```
run_batch.py [--phels-root PATH] [--moshits-root PATH] [--sphere-bin PATH]
             [--max-jobs N] [--particle X] [--energy X] [--angle X]
             [--dry-run]
```

- Scans `phels-root` for leaf directories containing `.phel.zst` files
- Reads `progress.tsv` — skips completed directories
- `concurrent.futures.ProcessPoolExecutor(max_workers=max_jobs)`
- Each worker: `subprocess.run([sphere-bin, "--phels", dir, "--moshits", out_dir, "--threads", "1"])`
- Updates `progress.tsv` with `fcntl.flock` for concurrency safety
- Resume on restart, `--dry-run` for preview

### `scripts/deploy.sh` — server deployment

```bash
#!/bin/bash
SERVER=$1
rsync -avz --exclude build/ --exclude '.git/' ./ $SERVER:/home/ivanov/SPHERE-3_G4/
ssh $SERVER 'cd /home/ivanov/SPHERE-3_G4 && mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j$(nproc)'
```

## Deployment

### Servers

| Server               | Atmosphere model | Data location   |
|----------------------|------------------|-----------------|
| ivanov@213.131.1.111 | TBD              | ~/sphall/phels/ |
| ivanov@213.131.1.50  | TBD              | ~/sphall/phels/ |

### Prerequisites

- Geant4 installed (verify, install if needed)
- `libzstd-dev` (or `libzstd-devel`) installed
- C++17 compiler (gcc/clang)

### Deployment Steps

1. `scripts/deploy.sh ivanov@213.131.1.111`
2. `scripts/deploy.sh ivanov@213.131.1.50`
3. Verify build on each server
4. Launch in tmux on each:
   ```bash
   tmux new -s sphere
   python3 /home/ivanov/SPHERE-3_G4/scripts/run_batch.py \
     --phels-root /home/ivanov/sphall/phels \
     --moshits-root /home/ivanov/sphall/moshits \
     --sphere-bin /home/ivanov/SPHERE-3_G4/build/SPHERE-3 \
     --max-jobs 30
   ```
5. Monitor: `grep -c completed /home/ivanov/sphall/progress.tsv`

## Data Flow Summary

```
[Server 111/50]
~/sphall/phels/<particle>/<energy>/<angle>/<height>/*.phel.zst
    │
    ▼  run_batch.py → SPHERE-3 (30 processes × 1 thread)
    │
    ▼  PhelReader::Read() → GeneratePrimaries() → SteppingAction → MoshitWriter
    │
~/sphall/moshits/<particle>/<energy>/<angle>/<height>/*.moshit.zst
```
