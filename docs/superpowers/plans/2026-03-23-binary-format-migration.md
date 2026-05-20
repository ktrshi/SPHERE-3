# Binary Format Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate SPHERE-3_G4 from ASCII phels/moshits I/O to binary zstd-compressed formats (.phel.zst input, .moshit.zst output) and replace bash orchestration with Python.

**Architecture:** New `PhelReader` reads zstd-compressed binary input, new `MoshitWriter` buffers detection hits and writes zstd-compressed binary output. Both use single-shot ZSTD API (no streaming). Per-worker `MoshitWriter` replaces fd+snprintf hot path. Python `run_batch.py` replaces bash scripts.

**Tech Stack:** C++20, Geant4, libzstd, Python 3 (stdlib only)

**Spec:** `docs/superpowers/specs/2026-03-23-binary-format-migration-design.md`

---

## File Structure

### New Files
| File | Responsibility |
|------|---------------|
| `include/PhelReader.hh` | PhelHeader, PhelPhoton, PhelEvent structs + `PhelReader::Read()` declaration |
| `src/PhelReader.cc` | zstd decompression, binary parsing, validation |
| `include/MoshitWriter.hh` | MoshitHit struct + `MoshitWriter` class declaration |
| `src/MoshitWriter.cc` | hit buffering, zstd compression, file writing |
| `scripts/run_batch.py` | Python batch orchestrator (replaces run_all.sh + run_one.sh) |
| `scripts/deploy.sh` | rsync + remote build |

### Modified Files
| File | Changes |
|------|---------|
| `CMakeLists.txt` | Add zstd dependency, new source files (auto via GLOB) |
| `include/WorkerEventData.hh` | Replace `PhotonMeta` with `PhelPhoton*`, replace ofstream+iobuf with `MoshitWriter` |
| `src/sphmirrPrimaryGeneratorAction.cc` | Replace ASCII parsing with `PhelReader::Read()`, adapt `BuildSuffix()` |
| `src/sphmirrSteppingAction.cc` | Replace `FormatDetectionLine()+write()` with `MoshitWriter::AddHit()` |
| `src/sphmirrEventAction.cc` | Init/flush `MoshitWriter` per event |
| `include/sphmirrPrimaryGeneratorAction.hh` | Remove `ParseHeader()` declaration |

### Deleted Code (within modified files)
- `PhotonMeta` struct (WorkerEventData.hh:12-16)
- `ParseHeader()` method (PrimaryGeneratorAction.cc:47-72)
- `FormatDetectionLine()` function (SteppingAction.cc:55-65)
- `OpenMoshitsFile()` method (WorkerEventData.hh:36-43)
- `iobuf[65536]` + `moshits` ofstream (WorkerEventData.hh:32-33)

---

## Task 1: Add zstd dependency to CMake

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add zstd find logic after Geant4**

In `CMakeLists.txt`, after line 28 (`include(${Geant4_USE_FILE})`), add:

```cmake
# --- zstd ---
find_library(ZSTD_LIB zstd REQUIRED)
find_path(ZSTD_INCLUDE zstd.h REQUIRED)
message(STATUS "zstd library: ${ZSTD_LIB}")
message(STATUS "zstd include: ${ZSTD_INCLUDE}")
```

- [ ] **Step 2: Add zstd include directory**

After `target_include_directories(SPHERE-3 ... include/)`, add:

```cmake
target_include_directories(SPHERE-3 PRIVATE ${ZSTD_INCLUDE})
```

- [ ] **Step 3: Link zstd**

Change line 51 from:
```cmake
target_link_libraries(SPHERE-3 ${Geant4_LIBRARIES})
```
to:
```cmake
target_link_libraries(SPHERE-3 ${Geant4_LIBRARIES} ${ZSTD_LIB})
```

- [ ] **Step 4: Verify build**

```bash
cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j$(nproc)
```

Expected: build succeeds, `zstd library:` and `zstd include:` printed.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add zstd dependency for binary format support"
```

---

## Task 2: Implement PhelReader

**Files:**
- Create: `include/PhelReader.hh`
- Create: `src/PhelReader.cc`

- [ ] **Step 1: Create PhelReader.hh**

```cpp
#ifndef PHEL_READER_HH
#define PHEL_READER_HH

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

#pragma pack(push, 1)
struct PhelFileHeader {
    char     magic[4];     // "PHEL"
    uint8_t  version;      // 1
    uint8_t  flags;        // bit 0: has_background
    uint16_t clone_num;
    float    xsh;          // shower core shift X, meters
    float    ysh;          // shower core shift Y, meters
    float    zz;           // detector altitude, meters (negative)
    uint16_t catm;         // atmosphere model
    uint16_t _pad;
    float    tmin;         // min arrival time, ns
    float    tmax;         // max arrival time, ns
    float    tbig;         // reference time, ns
    uint32_t n_photons;
};
static_assert(sizeof(PhelFileHeader) == 40, "PhelFileHeader must be 40 bytes");

struct PhelFilePhoton {
    uint16_t i;            // X-bin 0..1279
    uint16_t j;            // Y-bin 0..1279
    uint8_t  k;            // bit7: is_background, bits 0-6: time-bin
    uint8_t  _reserved[3];
    float    x;            // X coordinate, meters
    float    y;            // Y coordinate, meters
    float    t;            // arrival time, ns
};
static_assert(sizeof(PhelFilePhoton) == 20, "PhelFilePhoton must be 20 bytes");
#pragma pack(pop)

struct PhelPhoton {
    uint16_t i, j;
    uint8_t  time_bin;
    bool     is_background;
    float    x, y, t;
};

struct PhelEvent {
    float    xsh, ysh, zz;
    float    tmin, tmax, tbig;
    uint16_t catm, clone_num;
    bool     has_background;
    uint32_t n_photons;
    std::vector<PhelPhoton> photons;
};

class PhelReader {
public:
    static PhelEvent Read(const std::string& filename);
};

#endif
```

- [ ] **Step 2: Create PhelReader.cc**

```cpp
#include "PhelReader.hh"
#include <zstd.h>
#include <fstream>
#include <cstring>

PhelEvent PhelReader::Read(const std::string& filename) {
    // Read compressed file
    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    if (!ifs) throw std::runtime_error("PhelReader: cannot open " + filename);

    auto compressedSize = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0);
    std::vector<char> compressed(compressedSize);
    ifs.read(compressed.data(), compressedSize);
    ifs.close();

    // Get decompressed size
    auto decompressedSize = ZSTD_getFrameContentSize(compressed.data(), compressedSize);
    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN || decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("PhelReader: cannot determine decompressed size for " + filename);
    }

    // Decompress
    std::vector<char> decompressed(decompressedSize);
    size_t result = ZSTD_decompress(decompressed.data(), decompressedSize,
                                     compressed.data(), compressedSize);
    if (ZSTD_isError(result)) {
        throw std::runtime_error("PhelReader: zstd error: " +
                                  std::string(ZSTD_getErrorName(result)));
    }

    // Validate size
    if (decompressedSize < sizeof(PhelFileHeader)) {
        throw std::runtime_error("PhelReader: file too small for header");
    }

    // Parse header
    PhelFileHeader hdr;
    std::memcpy(&hdr, decompressed.data(), sizeof(hdr));

    if (std::memcmp(hdr.magic, "PHEL", 4) != 0) {
        throw std::runtime_error("PhelReader: bad magic in " + filename);
    }

    size_t expectedSize = sizeof(PhelFileHeader) + hdr.n_photons * sizeof(PhelFilePhoton);
    if (decompressedSize != expectedSize) {
        throw std::runtime_error("PhelReader: size mismatch: expected " +
                                  std::to_string(expectedSize) + ", got " +
                                  std::to_string(decompressedSize));
    }

    // Build PhelEvent
    PhelEvent event;
    event.xsh = hdr.xsh;
    event.ysh = hdr.ysh;
    event.zz  = hdr.zz;
    event.tmin = hdr.tmin;
    event.tmax = hdr.tmax;
    event.tbig = hdr.tbig;
    event.catm = hdr.catm;
    event.clone_num = hdr.clone_num;
    event.has_background = (hdr.flags & 0x01) != 0;
    event.n_photons = hdr.n_photons;

    // Parse photons
    event.photons.resize(hdr.n_photons);
    const auto* raw = reinterpret_cast<const PhelFilePhoton*>(
        decompressed.data() + sizeof(PhelFileHeader));

    for (uint32_t n = 0; n < hdr.n_photons; ++n) {
        auto& p = event.photons[n];
        p.i = raw[n].i;
        p.j = raw[n].j;
        p.time_bin = raw[n].k & 0x7F;
        p.is_background = (raw[n].k & 0x80) != 0;
        p.x = raw[n].x;
        p.y = raw[n].y;
        p.t = raw[n].t;
    }

    return event;
}
```

- [ ] **Step 3: Verify build**

```bash
cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j$(nproc)
```

Expected: compiles without errors (PhelReader is compiled but not yet called).

- [ ] **Step 4: Quick smoke test with a real .phel.zst file (if available locally)**

If there's a test file in `../sphall/phels/`, we can add a temporary `main()` test later. For now, confirm compilation.

- [ ] **Step 5: Commit**

```bash
git add include/PhelReader.hh src/PhelReader.cc
git commit -m "feat: add PhelReader for .phel.zst binary input format"
```

---

## Task 3: Implement MoshitWriter

**Files:**
- Create: `include/MoshitWriter.hh`
- Create: `src/MoshitWriter.cc`

- [ ] **Step 1: Create MoshitWriter.hh**

```cpp
#ifndef MOSHIT_WRITER_HH
#define MOSHIT_WRITER_HH

#include <cstdint>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct MoshitFileHeader {
    char     magic[4];    // "MOSH"
    uint8_t  version;     // 1
    uint8_t  _pad0;       // alignment
    uint16_t _pad1;       // alignment
    float    zz;          // detector altitude, meters
    float    xsh;         // shower core shift X, meters
    float    ysh;         // shower core shift Y, meters
    uint32_t n_hits;      // number of detection records
};
static_assert(sizeof(MoshitFileHeader) == 24, "MoshitFileHeader must be 24 bytes");

struct MoshitFileHit {
    uint16_t pixel;       // absolute pixel (0-2652)
    uint8_t  origin;      // 1=Cherenkov, 2=background
    uint8_t  kk;          // CORSIKA k-index
    uint16_t ii;          // CORSIKA i-index
    uint16_t jj;          // CORSIKA j-index
    float    t;           // detection time, ns
    float    t0;          // emission time, ns
};
static_assert(sizeof(MoshitFileHit) == 16, "MoshitFileHit must be 16 bytes");
#pragma pack(pop)

class MoshitWriter {
public:
    void Begin(float zz, float xsh, float ysh);
    void AddHit(uint16_t pixel, float t, uint8_t origin,
                uint16_t ii, uint16_t jj, uint8_t kk, float t0);
    void Flush(const std::string& filename);
    void Reset();

    uint32_t HitCount() const { return n_hits_; }

private:
    float zz_ = 0, xsh_ = 0, ysh_ = 0;
    uint32_t n_hits_ = 0;
    std::vector<MoshitFileHit> hits_;
};

#endif
```

- [ ] **Step 2: Create MoshitWriter.cc**

```cpp
#include "MoshitWriter.hh"
#include <zstd.h>
#include <fstream>
#include <cstring>
#include <stdexcept>

void MoshitWriter::Begin(float zz, float xsh, float ysh) {
    zz_  = zz;
    xsh_ = xsh;
    ysh_ = ysh;
    n_hits_ = 0;
    hits_.clear();
    hits_.reserve(10000);
}

void MoshitWriter::AddHit(uint16_t pixel, float t, uint8_t origin,
                           uint16_t ii, uint16_t jj, uint8_t kk, float t0) {
    hits_.push_back({pixel, origin, kk, ii, jj, t, t0});
    ++n_hits_;
}

void MoshitWriter::Flush(const std::string& filename) {
    // Build raw buffer: header + hits
    MoshitFileHeader hdr{};
    std::memcpy(hdr.magic, "MOSH", 4);
    hdr.version = 1;
    hdr._pad0 = 0;
    hdr._pad1 = 0;
    hdr.zz  = zz_;
    hdr.xsh = xsh_;
    hdr.ysh = ysh_;
    hdr.n_hits = n_hits_;

    size_t rawSize = sizeof(hdr) + n_hits_ * sizeof(MoshitFileHit);
    std::vector<char> raw(rawSize);
    std::memcpy(raw.data(), &hdr, sizeof(hdr));
    if (n_hits_ > 0) {
        std::memcpy(raw.data() + sizeof(hdr), hits_.data(),
                     n_hits_ * sizeof(MoshitFileHit));
    }

    // Compress
    size_t bound = ZSTD_compressBound(rawSize);
    std::vector<char> compressed(bound);
    size_t compressedSize = ZSTD_compress(compressed.data(), bound,
                                           raw.data(), rawSize, 3);
    if (ZSTD_isError(compressedSize)) {
        throw std::runtime_error("MoshitWriter: zstd error: " +
                                  std::string(ZSTD_getErrorName(compressedSize)));
    }

    // Write
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) throw std::runtime_error("MoshitWriter: cannot open " + filename);
    ofs.write(compressed.data(), compressedSize);
}

void MoshitWriter::Reset() {
    n_hits_ = 0;
    hits_.clear();
}
```

- [ ] **Step 3: Verify build**

```bash
cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j$(nproc)
```

- [ ] **Step 4: Commit**

```bash
git add include/MoshitWriter.hh src/MoshitWriter.cc
git commit -m "feat: add MoshitWriter for .moshit.zst binary output format"
```

---

## Task 4: Migrate all C++ I/O code atomically (WorkerEventData + PrimaryGeneratorAction + SteppingAction + EventAction)

> **Note:** These changes are tightly coupled — modifying WorkerEventData breaks SteppingAction and EventAction until they're also updated. All changes in this task MUST be applied together for the project to compile. One atomic commit.

**Files:**
- Modify: `include/WorkerEventData.hh`
- Modify: `include/sphmirrPrimaryGeneratorAction.hh`
- Modify: `src/sphmirrPrimaryGeneratorAction.cc`
- Modify: `src/sphmirrSteppingAction.cc`
- Modify: `src/sphmirrEventAction.cc`

### 4A: Update WorkerEventData

- [ ] **Step 1: Replace WorkerEventData.hh**

Replace the entire file. Key changes:
- Remove `PhotonMeta` struct (lines 12-16)
- Remove `headerLine` (line 24), `iobuf` (line 32), `moshits` ofstream (line 33)
- Remove `OpenMoshitsFile()` method (lines 36-43)
- Add `#include "PhelReader.hh"` and `#include "MoshitWriter.hh"`
- Add `const std::vector<PhelPhoton>* photonData` pointer
- Add `MoshitWriter moshitWriter`

```cpp
#ifndef WorkerEventData_hh
#define WorkerEventData_hh

#include <string>
#include <vector>
#include "PhelReader.hh"
#include "MoshitWriter.hh"

struct WorkerEventData {
    // Set by PrimaryGeneratorAction
    std::string inputFileSuffix;
    float zz = 0, xsh = 0, ysh = 0;
    std::string height;
    const std::vector<PhelPhoton>* photonData = nullptr;  // borrowed from PhelEvent

    // Output writer (per-worker, no contention)
    MoshitWriter moshitWriter;

    // Per-event counters
    int TotPhot = 0;
    int NEntry  = 0;
    double tmin = 1e20;
    double tmax = -1e20;

    // Diagnostic counters
    int diag_nKilledMirror = 0;
    int diag_nKilledMosaic = 0;
    int diag_nKilledBase   = 0;
    int diag_nKilledHood   = 0;
    int diag_nKilledPMT    = 0;
    int diag_nKilledWorld  = 0;
    int diag_nKilledOther  = 0;
    int diag_nLeftWorld    = 0;
};

#endif
```

### 4B: Update PrimaryGeneratorAction — read .phel.zst

- [ ] **Step 2: Update header file**

In `include/sphmirrPrimaryGeneratorAction.hh`:
- Add `#include "PhelReader.hh"` at top
- Remove `void ParseHeader(const std::string& line);` from private section
- Change `std::string BuildSuffix(const std::string& filename);` to `std::string BuildSuffix(const std::string& filename, const std::string& height);`
- Add `PhelEvent fCurrentEvent;` to private section (owns the event data for current event lifetime)

- [ ] **Step 3: Remove ParseHeader() definition**

Delete the `ParseHeader` method body (lines 47-72 of `src/sphmirrPrimaryGeneratorAction.cc`).

- [ ] **Step 4: Adapt BuildSuffix for .phel.zst extension**

Replace `BuildSuffix()` (lines 74-91) with:

```cpp
std::string sphmirrPrimaryGeneratorAction::BuildSuffix(
    const std::string& filename, const std::string& height)
{
    // Strip .phel.zst extension
    std::string name = filename;
    const std::string ext = ".phel.zst";
    if (name.size() > ext.size() &&
        name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
        name = name.substr(0, name.size() - ext.size());
    }

    // Find "Q" marker
    auto qpos = name.find('Q');
    if (qpos == std::string::npos) return name;
    std::string suffix = name.substr(qpos);

    // Remove spaces
    suffix.erase(std::remove(suffix.begin(), suffix.end(), ' '), suffix.end());

    // Insert _{height}m before _cNNN at end
    if (suffix.size() >= 5 && suffix[suffix.size()-5] == '_' && suffix[suffix.size()-4] == 'c') {
        bool allDigits = true;
        for (int i = 3; i >= 1; --i) {
            if (!std::isdigit(suffix[suffix.size()-i])) allDigits = false;
        }
        if (allDigits) {
            suffix.insert(suffix.size()-5, "_" + height + "m");
        }
    }
    return suffix;
}
```

- [ ] **Step 5: Rewrite GeneratePrimaries to use PhelReader**

Replace the body of `GeneratePrimaries()` (lines 93-200). **CRITICAL: The physics (rotation matrix, direction calculation, SetOptPhotonPolar) MUST be preserved exactly.** The only change is the data source: binary PhelReader instead of ASCII sscanf.

Reference the exact current physics code at lines 166-191:
- Line 167-170: random entry point in hood (r=85cm circle)
- Line 174-176: rotation matrix using `fCosTheta/fSinTheta/fCosPhi/fSinPhi` and `zstart = fDetector->GetZstart()`
- Line 179-184: direction from snow source `(xx*m, yy*m)` to entry point, with `zz` for snow-level z
- Line 186-191: SetParticlePosition, SetParticleMomentumDirection, SetParticleTime, **SetOptPhotonPolar()**, GeneratePrimaryVertex

```cpp
void sphmirrPrimaryGeneratorAction::GeneratePrimaries(G4Event* anEvent)
{
    // Reset per-event state (must be here, not in BeginOfEventAction —
    // Geant4 calls GeneratePrimaries BEFORE BeginOfEventAction)
    fEventData->inputFileSuffix.clear();
    fEventData->photonData = nullptr;
    fEventData->TotPhot = 0;
    fEventData->NEntry  = 0;
    fEventData->tmin = std::numeric_limits<G4double>::max();
    fEventData->tmax = 0.0;
    fEventData->diag_nKilledMirror = 0;
    fEventData->diag_nKilledMosaic = 0;
    fEventData->diag_nKilledBase   = 0;
    fEventData->diag_nKilledHood   = 0;
    fEventData->diag_nKilledPMT    = 0;
    fEventData->diag_nKilledWorld  = 0;
    fEventData->diag_nKilledOther  = 0;
    fEventData->diag_nLeftWorld    = 0;

    // Get next file
    std::string filename;
    if (!fFileQueue->Pop(filename)) return;

    // Read binary phel file
    std::string filepath = fConfig->phelsDir + "/" + filename;
    try {
        fCurrentEvent = PhelReader::Read(filepath);
    } catch (const std::runtime_error& e) {
        G4cerr << "PhelReader error: " << e.what() << G4endl;
        return;
    }

    // Extract header data (stored in meters, same as old ASCII format)
    fEventData->zz  = fCurrentEvent.zz;
    fEventData->xsh = fCurrentEvent.xsh;
    fEventData->ysh = fCurrentEvent.ysh;

    // Height string from abs(zz)
    int heightVal = static_cast<int>(std::abs(fCurrentEvent.zz));
    fEventData->height = std::to_string(heightVal);

    // Build output suffix
    fEventData->inputFileSuffix = BuildSuffix(filename, fEventData->height);

    // Store pointer for SteppingAction lookup
    fEventData->photonData = &fCurrentEvent.photons;

    // Init MoshitWriter for this event
    fEventData->moshitWriter.Begin(fCurrentEvent.zz, fCurrentEvent.xsh, fCurrentEvent.ysh);

    // Generate primaries — PHYSICS MUST MATCH EXACTLY lines 144-191 of original code
    const G4double zstart = fDetector->GetZstart();
    const G4double zz = fEventData->zz;

    for (size_t idx = 0; idx < fCurrentEvent.photons.size(); ++idx) {
        const auto& ph = fCurrentEvent.photons[idx];

        // Random entry point in hood opening (circle r=85cm) — lines 167-171
        G4double r = 85.0 * cm * std::sqrt(G4UniformRand());
        G4double dzeta = 360.0 * deg * G4UniformRand();
        G4double xi0 = r * std::cos(dzeta);
        G4double yi0 = r * std::sin(dzeta);
        G4double zi0 = 0.0;

        // Rotate to detector frame — lines 174-176 (EXACT rotation matrix)
        G4double xi = fCosTheta * fCosPhi * xi0 - fSinPhi * yi0 - fSinTheta * fCosPhi * zi0;
        G4double yi = fCosTheta * fSinPhi * xi0 + fCosPhi * yi0 - fSinTheta * fSinPhi * zi0;
        G4double zi = fSinTheta * xi0 + fCosTheta * zi0 + zstart;

        // Direction from snow-level source to entry point — lines 179-184
        // NOTE: ph.x and ph.y are in meters (same as old xx, yy from ASCII)
        G4double xgr = ph.x * m;
        G4double ygr = ph.y * m;
        G4double diag = std::sqrt((xi - xgr) * (xi - xgr) + (yi - ygr) * (yi - ygr) + (zz - zi) * (zz - zi));
        G4double pxb = (xi - xgr) / diag;
        G4double pyb = (yi - ygr) / diag;
        G4double pzb = (zi - zz) / diag;

        // Fire photon — lines 186-191
        fParticleGun->SetParticlePosition(G4ThreeVector(xi, yi, zi));
        fParticleGun->SetParticleMomentumDirection(G4ThreeVector(pxb, pyb, pzb));
        fParticleGun->SetParticleTime(ph.t * ns);
        SetOptPhotonPolar();  // MUST NOT OMIT — randomizes polarization for boundary processes
        fParticleGun->GeneratePrimaryVertex(anEvent);
    }

    G4cout << "Event " << anEvent->GetEventID()
           << ": loaded " << fCurrentEvent.photons.size() << " photons from " << filename << G4endl;
}
```

**Key differences from old code:**
- Data source: `PhelReader::Read()` instead of `getline + sscanf` loop
- `ph.x / ph.y / ph.t` instead of `xx / yy / t0` — same values, same units (meters / ns)
- `origin` derived from `ph.is_background` (bit 7 of k-byte) instead of `mmm < 100`
- No sentinel line bug (old ASCII parser created spurious photon from terminator)
- `photonData` pointer set for SteppingAction, replaces `photonMeta` vector

**What is preserved identically:**
- Rotation matrix (lines 174-176)
- `zstart` from `fDetector->GetZstart()` — NOT from config, NOT hardcoded
- Source coordinates NOT shifted by xsh/ysh (xsh/ysh only stored for output)
- `SetOptPhotonPolar()` call
- Unit conversions: `xx * m`, `t0 * ns`

### 4C: Update SteppingAction — use MoshitWriter

- [ ] **Step 6: Remove FormatDetectionLine()**

Delete the `FormatDetectionLine` static function (lines 55-65 of `src/sphmirrSteppingAction.cc`).

- [ ] **Step 7: Replace detection recording + crosstalk with MoshitWriter::AddHit()**

Replace lines 113-161 (inside the `if (pp > 0.0 && ...)` block, after angular acceptance). The surrounding code (PMT detection, dot product, angular acceptance, tmin/tmax update) is unchanged.

```cpp
            // Look up per-photon metadata by trackID — same logic as before
            const G4int trackID = fTrack->GetTrackID();
            const PhelPhoton* meta = nullptr;
            const auto* photonData = fEventData->photonData;
            if (photonData && trackID >= 1 &&
                trackID <= static_cast<G4int>(photonData->size())) {
                meta = &(*photonData)[trackID - 1];
            }
            // Fallback for secondary optical photons: use parent
            if (!meta) {
                const G4int parentID = fTrack->GetParentID();
                if (photonData && parentID >= 1 &&
                    parentID <= static_cast<G4int>(photonData->size())) {
                    meta = &(*photonData)[parentID - 1];
                }
            }
            if (!meta) return;  // shouldn't happen, but guard

            uint8_t origin = meta->is_background ? 2 : 1;
            uint16_t pixel = static_cast<uint16_t>(cluster_num * 7 + pix_num);
            float det_t = static_cast<float>(glt / ns);
            float det_t0 = meta->t;

            fEventData->NEntry++;
            fEventData->moshitWriter.AddHit(pixel, det_t, origin,
                                             meta->i, meta->j, meta->time_bin, det_t0);

            // Crosstalk simulation: each of 6 neighbors fires independently with 7% probability
            constexpr G4double crosstalk_prob = 0.07;
            for (int nb = 1; nb <= 6; nb++) {
                if (G4UniformRand() < crosstalk_prob) {
                    const G4int neighbor_pix = (pix_num + nb) % 7;
                    uint16_t neighbor_pixel = static_cast<uint16_t>(cluster_num * 7 + neighbor_pix);
                    fEventData->NEntry++;
                    fEventData->moshitWriter.AddHit(neighbor_pixel, det_t, origin,
                                                     meta->i, meta->j, meta->time_bin, det_t0);
                }
            }
```

Note: `glt` is already computed on line 103 as `fTrack->GetGlobalTime()`. Removed `OpenMoshitsFile()` call, removed position/direction variables (`x_m, y_m, z_m, dirx, diry, dirz`) that are no longer needed.

`TotPhot++` on line 165 stays unchanged (outside this block).

### 4D: Update EventAction — flush MoshitWriter

- [ ] **Step 8: Rewrite EndOfEventAction**

Replace `EndOfEventAction` (lines 22-74 of `src/sphmirrEventAction.cc`). The class is `EventAction` (NOT `sphmirrEventAction`), parameter is `const G4Event*` (NOT `const G4Run*`):

```cpp
void EventAction::EndOfEventAction(const G4Event* event) {
    // Skip empty events (no file was assigned)
    if (fEventData->inputFileSuffix.empty()) return;

    // Build output path and flush compressed binary output
    std::string outFile = fConfig->outputDir + "/moshits_" +
                          fEventData->inputFileSuffix + ".moshit.zst";
    try {
        fEventData->moshitWriter.Flush(outFile);
    } catch (const std::runtime_error& e) {
        G4cerr << "MoshitWriter error: " << e.what() << G4endl;
    }

    // Single consolidated log line (one G4endl flush reduces MT mutex contention)
    char logbuf[512];
    if (fEventData->NEntry > 0) {
        std::snprintf(logbuf, sizeof(logbuf),
            "Event %d [%s]: TotPhot=%d NEntry=%d tmin=%.4gns tmax=%.4gns"
            " | Killed: Mir=%d Mos=%d Base=%d PMT=%d Hood=%d World=%d Other=%d Left=%d",
            event->GetEventID(), fEventData->inputFileSuffix.c_str(),
            fEventData->TotPhot, fEventData->NEntry,
            fEventData->tmin / ns, fEventData->tmax / ns,
            fEventData->diag_nKilledMirror, fEventData->diag_nKilledMosaic,
            fEventData->diag_nKilledBase,
            fEventData->diag_nKilledPMT, fEventData->diag_nKilledHood,
            fEventData->diag_nKilledWorld, fEventData->diag_nKilledOther,
            fEventData->diag_nLeftWorld);
    } else {
        std::snprintf(logbuf, sizeof(logbuf),
            "Event %d [%s]: TotPhot=%d NEntry=0",
            event->GetEventID(), fEventData->inputFileSuffix.c_str(),
            fEventData->TotPhot);
    }
    G4cout << logbuf << G4endl;

    // Accumulate into G4Accumulable (will be merged in EndOfRunAction)
    auto* accMgr = G4AccumulableManager::Instance();
    auto* accTotPhot = accMgr->GetAccValue<G4int>("TotPhotTotal");
    auto* accNEntry  = accMgr->GetAccValue<G4int>("NEntryTotal");
    auto* accTmin    = accMgr->GetAccValue<G4double>("TminAll");
    auto* accTmax    = accMgr->GetAccValue<G4double>("TmaxAll");
    if (accTotPhot) *accTotPhot += fEventData->TotPhot;
    if (accNEntry)  *accNEntry  += fEventData->NEntry;
    if (accTmin && fEventData->NEntry > 0) {
        if (fEventData->tmin < accTmin->GetValue())
            *accTmin = fEventData->tmin;
    }
    if (accTmax && fEventData->NEntry > 0) {
        if (fEventData->tmax > accTmax->GetValue())
            *accTmax = fEventData->tmax;
    }

    fEventData->moshitWriter.Reset();
}
```

### 4E: Build and commit

- [ ] **Step 9: Verify full build**

```bash
cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j$(nproc)
```

Expected: clean compilation with zero errors.

- [ ] **Step 10: Commit all C++ I/O changes atomically**

```bash
git add include/WorkerEventData.hh include/sphmirrPrimaryGeneratorAction.hh \
        src/sphmirrPrimaryGeneratorAction.cc src/sphmirrSteppingAction.cc \
        src/sphmirrEventAction.cc
git commit -m "feat: migrate I/O from ASCII to binary zstd (.phel.zst input, .moshit.zst output)"
```

---

## Task 5: End-to-end test with real data

- [ ] **Step 1: Prepare test data**

Copy a few `.phel.zst` files from `../sphall/phels/` (or generate with sphall) into a test directory:

```bash
mkdir -p /tmp/sphere-test/phels /tmp/sphere-test/moshits
cp ../sphall/phels/0014/10PeV/15/1000/phels_to_trace_*_c001.phel.zst /tmp/sphere-test/phels/ 2>/dev/null || echo "No local test data — test on server"
```

- [ ] **Step 2: Run SPHERE-3 on test data**

```bash
./build/SPHERE-3 . --phels /tmp/sphere-test/phels --moshits /tmp/sphere-test/moshits --threads 1
```

Expected: processes each .phel.zst file, prints event summary lines, creates `.moshit.zst` files in output dir.

- [ ] **Step 3: Verify output files exist and are valid zstd**

```bash
ls -la /tmp/sphere-test/moshits/
file /tmp/sphere-test/moshits/*.moshit.zst
zstd -l /tmp/sphere-test/moshits/*.moshit.zst
```

Expected: files are recognized as zstd compressed, have reasonable sizes.

- [ ] **Step 4: Verify output content (quick sanity check)**

```bash
# Decompress and check header magic
zstd -d /tmp/sphere-test/moshits/*.moshit.zst --stdout | head -c 4 | xxd
```

Expected: `4d4f 5348` ("MOSH").

- [ ] **Step 5: Commit (if any fixes were needed)**

```bash
git add -A
git commit -m "fix: adjustments from end-to-end testing"
```

---

## Task 6: Write Python run_batch.py

**Files:**
- Create: `scripts/run_batch.py`

- [ ] **Step 1: Create run_batch.py**

```python
#!/usr/bin/env python3
"""Batch orchestrator for SPHERE-3_G4 simulation.

Replaces run_all.sh + run_one.sh. Finds leaf directories with .phel.zst files,
runs SPHERE-3 for each, tracks progress in progress.tsv.
"""

import argparse
import fcntl
import logging
import os
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger(__name__)

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_SPHERE_BIN = SCRIPT_DIR.parent / "build" / "SPHERE-3"


def find_phel_dirs(root: Path, particle=None, energy=None, angle=None) -> list[Path]:
    """Find leaf directories containing .phel.zst files (depth 4: particle/energy/angle/height)."""
    dirs = []
    for p_dir in sorted(root.iterdir()):
        if not p_dir.is_dir():
            continue
        if particle and p_dir.name != particle:
            continue
        for e_dir in sorted(p_dir.iterdir()):
            if not e_dir.is_dir():
                continue
            if energy and e_dir.name != energy:
                continue
            for a_dir in sorted(e_dir.iterdir()):
                if not a_dir.is_dir():
                    continue
                if angle and a_dir.name != angle:
                    continue
                for h_dir in sorted(a_dir.iterdir()):
                    if not h_dir.is_dir():
                        continue
                    if any(h_dir.glob("*.phel.zst")):
                        dirs.append(h_dir)
    return dirs


def read_progress(tsv_path: Path) -> dict[str, str]:
    """Read progress.tsv → {rel_path: status}."""
    progress = {}
    if tsv_path.exists():
        for line in tsv_path.read_text().splitlines():
            parts = line.split("\t")
            if len(parts) >= 2:
                progress[parts[0]] = parts[1]
    return progress


def update_progress(tsv_path: Path, rel_path: str, status: str):
    """Thread-safe update of progress.tsv using flock."""
    lock_path = str(tsv_path) + ".lock"
    with open(lock_path, "w") as lock_fd:
        fcntl.flock(lock_fd, fcntl.LOCK_EX)
        progress = read_progress(tsv_path)
        progress[rel_path] = status
        lines = [f"{k}\t{v}" for k, v in sorted(progress.items())]
        tsv_path.write_text("\n".join(lines) + "\n")


def run_one(phels_dir: Path, moshits_root: Path, sphere_bin: Path,
            logs_dir: Path, progress_tsv: Path, phels_root: Path) -> tuple[str, bool]:
    """Process one phels directory. Returns (rel_path, success)."""
    rel_path = str(phels_dir.relative_to(phels_root))
    moshits_dir = moshits_root / rel_path
    moshits_dir.mkdir(parents=True, exist_ok=True)

    log_file = logs_dir / (rel_path.replace("/", "__") + ".log")
    log_file.parent.mkdir(parents=True, exist_ok=True)

    update_progress(progress_tsv, rel_path, "running")

    # SPHERE-3 expects currentPath as first positional arg for configs/
    current_path = str(sphere_bin.parent.parent)
    cmd = [
        str(sphere_bin), current_path,
        "--phels", str(phels_dir),
        "--moshits", str(moshits_dir),
        "--threads", "1",
    ]

    try:
        with open(log_file, "w") as lf:
            result = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT, timeout=7200)
        success = result.returncode == 0
        status = "completed" if success else f"failed:{result.returncode}"
    except subprocess.TimeoutExpired:
        success = False
        status = "failed:timeout"
    except Exception as e:
        success = False
        status = f"failed:{e}"

    update_progress(progress_tsv, rel_path, status)
    return rel_path, success


def main():
    parser = argparse.ArgumentParser(description="SPHERE-3_G4 batch processor")
    parser.add_argument("--phels-root", type=Path, default=Path.home() / "sphall" / "phels")
    parser.add_argument("--moshits-root", type=Path, default=Path.home() / "sphall" / "moshits")
    parser.add_argument("--sphere-bin", type=Path, default=DEFAULT_SPHERE_BIN)
    parser.add_argument("--max-jobs", type=int, default=30)
    parser.add_argument("--particle", help="Filter by particle (e.g. 0014)")
    parser.add_argument("--energy", help="Filter by energy (e.g. 10PeV)")
    parser.add_argument("--angle", help="Filter by zenith angle (e.g. 15)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    work_dir = args.moshits_root.parent
    logs_dir = work_dir / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    progress_tsv = work_dir / "progress.tsv"

    # Find directories
    all_dirs = find_phel_dirs(args.phels_root, args.particle, args.energy, args.angle)
    log.info("Found %d phel directories", len(all_dirs))

    # Filter completed
    progress = read_progress(progress_tsv)
    pending = []
    for d in all_dirs:
        rel = str(d.relative_to(args.phels_root))
        if progress.get(rel) == "completed":
            continue
        pending.append(d)

    log.info("Pending: %d (skipped %d completed)", len(pending), len(all_dirs) - len(pending))

    if args.dry_run:
        for d in pending:
            print(f"  {d.relative_to(args.phels_root)}")
        return

    if not args.sphere_bin.exists():
        log.error("SPHERE-3 binary not found: %s", args.sphere_bin)
        sys.exit(1)

    # Process
    completed = 0
    failed = 0
    start = time.time()

    with ProcessPoolExecutor(max_workers=args.max_jobs) as pool:
        futures = {
            pool.submit(run_one, d, args.moshits_root, args.sphere_bin,
                        logs_dir, progress_tsv, args.phels_root): d
            for d in pending
        }
        for future in as_completed(futures):
            rel_path, success = future.result()
            if success:
                completed += 1
                log.info("OK  %s (%d/%d)", rel_path, completed + failed, len(pending))
            else:
                failed += 1
                log.warning("FAIL %s (%d/%d)", rel_path, completed + failed, len(pending))

    elapsed = time.time() - start
    log.info("Done: %d completed, %d failed, %.0f seconds", completed, failed, elapsed)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Make executable**

```bash
chmod +x scripts/run_batch.py
```

- [ ] **Step 3: Test with --dry-run (if local phels data available)**

```bash
python3 scripts/run_batch.py --phels-root ../sphall/phels --dry-run
```

- [ ] **Step 4: Commit**

```bash
git add scripts/run_batch.py
git commit -m "feat: add Python batch orchestrator (replaces bash scripts)"
```

---

## Task 7: Write deploy.sh

**Files:**
- Create: `scripts/deploy.sh`

- [ ] **Step 1: Create deploy.sh**

```bash
#!/bin/bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <user@host> [--build-only]"
    exit 1
fi

SERVER="$1"
BUILD_ONLY="${2:-}"
REMOTE_DIR="/home/ivanov/SPHERE-3_G4"

if [ "$BUILD_ONLY" != "--build-only" ]; then
    echo "==> Syncing code to $SERVER:$REMOTE_DIR ..."
    rsync -avz --exclude build/ --exclude '.git/' --exclude '*.o' \
        "$(dirname "$0")/../" "$SERVER:$REMOTE_DIR/"
fi

echo "==> Building on $SERVER ..."
ssh "$SERVER" "cd $REMOTE_DIR && mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j\$(nproc)"

echo "==> Verifying binary ..."
ssh "$SERVER" "$REMOTE_DIR/build/SPHERE-3 --help 2>&1 || true"

echo "==> Done. To run:"
echo "   ssh $SERVER"
echo "   tmux new -s sphere"
echo "   python3 $REMOTE_DIR/scripts/run_batch.py --phels-root ~/sphall/phels --moshits-root ~/sphall/moshits --sphere-bin $REMOTE_DIR/build/SPHERE-3 --max-jobs 30"
```

- [ ] **Step 2: Make executable**

```bash
chmod +x scripts/deploy.sh
```

- [ ] **Step 3: Commit**

```bash
git add scripts/deploy.sh
git commit -m "feat: add deployment script for remote servers"
```

---

## Task 8: Clean up old bash scripts

**Files:**
- Delete: `scripts/run_all.sh`
- Delete: `scripts/run_one.sh`

- [ ] **Step 1: Remove old scripts**

```bash
git rm scripts/run_all.sh scripts/run_one.sh
```

- [ ] **Step 2: Commit**

```bash
git commit -m "chore: remove old bash orchestration scripts (replaced by run_batch.py)"
```

---

## Task 9: Deploy and run on servers

- [ ] **Step 1: Check zstd availability on servers**

```bash
ssh ivanov@213.131.1.111 'dpkg -l | grep zstd || rpm -qa | grep zstd || echo "zstd not found"'
ssh ivanov@213.131.1.50 'dpkg -l | grep zstd || rpm -qa | grep zstd || echo "zstd not found"'
```

If not found: `ssh ivanov@<host> 'sudo apt install libzstd-dev'` or equivalent.

- [ ] **Step 2: Check Geant4 availability**

```bash
ssh ivanov@213.131.1.111 'ls /usr/local/lib/Geant4* 2>/dev/null || ls /opt/geant4* 2>/dev/null || echo "Geant4 not found"'
ssh ivanov@213.131.1.50 'ls /usr/local/lib/Geant4* 2>/dev/null || ls /opt/geant4* 2>/dev/null || echo "Geant4 not found"'
```

- [ ] **Step 3: Deploy to both servers**

```bash
scripts/deploy.sh ivanov@213.131.1.111
scripts/deploy.sh ivanov@213.131.1.50
```

- [ ] **Step 4: Verify phels data structure on servers**

```bash
ssh ivanov@213.131.1.111 'ls ~/sphall/phels/ | head -5 && find ~/sphall/phels -name "*.phel.zst" | head -3'
ssh ivanov@213.131.1.50 'ls ~/sphall/phels/ | head -5 && find ~/sphall/phels -name "*.phel.zst" | head -3'
```

- [ ] **Step 5: Dry run on each server**

```bash
ssh ivanov@213.131.1.111 'python3 /home/ivanov/SPHERE-3_G4/scripts/run_batch.py --phels-root ~/sphall/phels --moshits-root ~/sphall/moshits --sphere-bin /home/ivanov/SPHERE-3_G4/build/SPHERE-3 --dry-run'
```

- [ ] **Step 6: Launch on both servers**

```bash
ssh ivanov@213.131.1.111 'tmux new -d -s sphere "python3 /home/ivanov/SPHERE-3_G4/scripts/run_batch.py --phels-root ~/sphall/phels --moshits-root ~/sphall/moshits --sphere-bin /home/ivanov/SPHERE-3_G4/build/SPHERE-3 --max-jobs 30 2>&1 | tee ~/sphall/run.log"'

ssh ivanov@213.131.1.50 'tmux new -d -s sphere "python3 /home/ivanov/SPHERE-3_G4/scripts/run_batch.py --phels-root ~/sphall/phels --moshits-root ~/sphall/moshits --sphere-bin /home/ivanov/SPHERE-3_G4/build/SPHERE-3 --max-jobs 30 2>&1 | tee ~/sphall/run.log"'
```

- [ ] **Step 7: Verify processing started**

```bash
ssh ivanov@213.131.1.111 'sleep 30 && grep -c running ~/sphall/progress.tsv && grep -c completed ~/sphall/progress.tsv'
```
