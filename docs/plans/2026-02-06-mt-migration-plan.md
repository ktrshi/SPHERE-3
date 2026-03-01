# MT Migration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Migrate SPHERE-3 Geant4 simulation from single-threaded G4RunManager to multi-threaded mode with proper per-worker isolation.

**Architecture:** Eliminate ~40 global mutable variables by introducing per-worker `WorkerEventData`, read-only `SimConfig`, and thread-safe `FileQueue`. Create `ActionInitialization` to clone actions per worker. Change event model from one-photon-per-event to one-file(shower)-per-event.

**Tech Stack:** Geant4 11.03 (G4RunManagerFactory, G4Accumulable, G4Mutex), C++20

**Design doc:** `docs/plans/2026-02-06-mt-migration-design.md`

---

## Team Parallelization Strategy

```
Phase 1: Data structures (leader, sequential — sets interfaces for all agents)
  Task 1: SimConfig.hh, WorkerEventData.hh, FileQueue.hh/.cc

Phase 2: Parallel refactors (4 agents simultaneously)
  Task 2 [Agent A]: PrimaryGeneratorAction.hh/.cc — one-file-per-event model
  Task 3 [Agent B]: SteppingAction.hh/.cc + DetectorConstruction pixel migration
  Task 4 [Agent C]: EventAction.hh/.cc rewrite (per-event file I/O)
  Task 5 [Agent D]: RunAction.hh/.cc + ActionInitialization.hh/.cc + StackingAction

Phase 3: Integration (leader, sequential)
  Task 6: main() rewrite, remove globals
  Task 7: Build, fix, verify
```

---

## Task 1: Create Data Structures

**Owner:** Leader (before spawning agents)

**Files:**
- Create: `include/SimConfig.hh`
- Create: `include/WorkerEventData.hh`
- Create: `include/FileQueue.hh`
- Create: `src/FileQueue.cc`

### Step 1: Create `include/SimConfig.hh`

```cpp
#ifndef SimConfig_hh
#define SimConfig_hh

#include "globals.hh"
#include <string>

/// Read-only simulation configuration. Created once in main(),
/// passed as const pointer to all Action classes via ActionInitialization.
struct SimConfig {
    // Detector rotation angles (set from input, never modified)
    G4double phi{0.0};
    G4double the{0.0};

    // PMT sensitivity exponent: sensitivity(xi) = cos(xi)^p1
    G4double p1{1.093};

    // Paths
    std::string currentPath;
    std::string phelsDir;
    std::string outputDir;
};

#endif
```

### Step 2: Create `include/WorkerEventData.hh`

Per-event metadata is now stored in `photonMeta` vector (one entry per primary photon).
SteppingAction looks up metadata by `trackID - 1`.

```cpp
#ifndef WorkerEventData_hh
#define WorkerEventData_hh

#include "globals.hh"
#include "G4SystemOfUnits.hh"
#include <fstream>
#include <string>
#include <vector>
#include <limits>

/// Per-photon metadata from the input file, stored for SteppingAction lookup.
struct PhotonMeta {
    int ii{0}, jj{0}, kk{0}, mmm{0};
    double xx{0.0}, yy{0.0}, t0{0.0};
    int origin{0};  // 1=CL, 2=BG (derived from mmm)
};

/// Per-worker mutable state. Created in ActionInitialization::Build(),
/// shared by PrimaryGeneratorAction, EventAction, and SteppingAction
/// on the SAME worker thread. No cross-thread access.
struct WorkerEventData {
    // --- Set by PrimaryGeneratorAction::GeneratePrimaries ---
    std::string inputFileSuffix;       // e.g. "Q0_atm01_0014_10PeV_15_001_c001"
    std::string headerLine;            // first line of input file (written to output)
    G4double zz{0.0};                  // snow level z (from file header)
    G4double xsh{0.0};                 // EAS axis x-shift (from file header)
    G4double ysh{0.0};                 // EAS axis y-shift (from file header)
    std::string height;                // detector height string (from file header)
    std::vector<PhotonMeta> photonMeta; // metadata per primary, indexed by trackID-1

    // --- Managed by EventAction ---
    std::ofstream moshits;

    // --- Per-event counters (reset in BeginOfEventAction) ---
    G4int TotPhot{0};
    G4int NEntry{0};
    G4double tmin{std::numeric_limits<G4double>::max()};
    G4double tmax{0.0};
};

#endif
```

### Step 3: Create `include/FileQueue.hh`

```cpp
#ifndef FileQueue_hh
#define FileQueue_hh

#include "G4AutoLock.hh"
#include <queue>
#include <string>

/// Thread-safe FIFO queue of input file names.
/// Created and populated in main() before BeamOn.
/// Workers pop files atomically in GeneratePrimaries().
class FileQueue {
public:
    void Push(const std::string& filename) {
        G4AutoLock lock(&fMutex);
        fFiles.push(filename);
    }

    /// Pop next filename. Returns false if queue is empty.
    bool Pop(std::string& filename) {
        G4AutoLock lock(&fMutex);
        if (fFiles.empty()) return false;
        filename = fFiles.front();
        fFiles.pop();
        return true;
    }

    size_t Size() {
        G4AutoLock lock(&fMutex);
        return fFiles.size();
    }

private:
    std::queue<std::string> fFiles;
    G4Mutex fMutex = G4MUTEX_INITIALIZER;
};

#endif
```

### Step 4: Create `src/FileQueue.cc`

```cpp
#include "FileQueue.hh"
// All methods are inline in the header. This file exists for CMake GLOB.
```

### Step 5: Commit

```bash
git add include/SimConfig.hh include/WorkerEventData.hh include/FileQueue.hh src/FileQueue.cc
git commit -m "feat(mt): add SimConfig, WorkerEventData, FileQueue data structures"
```

---

## Task 2: Refactor PrimaryGeneratorAction (one-file-per-event)

**Owner:** Agent A

**Files:**
- Modify: `include/sphmirrPrimaryGeneratorAction.hh`
- Modify: `src/sphmirrPrimaryGeneratorAction.cc`

**Context:**
- Currently reads ONE line per event (one photon per G4Event)
- Must change to read ALL lines from one input file per event (one shower per G4Event)
- Must populate `WorkerEventData::photonMeta` vector so SteppingAction can look up per-photon tags by trackID
- File header parsing (zz, xsh, ysh, Height) stays here but writes to `fEventData` not globals
- `moshits` file management moves to EventAction — do NOT open/close moshits here
- Remove all `extern` global references
- `G4ParticleGun::GeneratePrimaryVertex(anEvent)` is called once per photon in a loop — Geant4 adds multiple primary vertices to the same event

**Dependencies:** Task 1 files must exist (SimConfig.hh, WorkerEventData.hh, FileQueue.hh)

### Step 1: Rewrite `include/sphmirrPrimaryGeneratorAction.hh`

```cpp
#ifndef sphmirrPrimaryGeneratorAction_h
#define sphmirrPrimaryGeneratorAction_h 1

#include "G4VUserPrimaryGeneratorAction.hh"
#include "globals.hh"

class G4ParticleGun;
class G4Event;
class FileQueue;
struct SimConfig;
struct WorkerEventData;
class sphmirrDetectorConstruction;

class sphmirrPrimaryGeneratorAction final : public G4VUserPrimaryGeneratorAction {
public:
    sphmirrPrimaryGeneratorAction(FileQueue* fileQueue,
                                   WorkerEventData* eventData,
                                   const SimConfig* config,
                                   const sphmirrDetectorConstruction* detector);
    ~sphmirrPrimaryGeneratorAction() override;
    void GeneratePrimaries(G4Event*) override;

private:
    G4ParticleGun* fParticleGun;
    FileQueue* fFileQueue;
    WorkerEventData* fEventData;
    const SimConfig* fConfig;
    const sphmirrDetectorConstruction* fDetector;

    // Cached trigonometry for detector orientation (per-worker, set once)
    G4double fCosTheta{-1}, fSinTheta{0}, fCosPhi{1}, fSinPhi{0};

    void SetOptPhotonPolar() const;
    void SetOptPhotonPolar(G4double angle) const;

    /// Parse input file header line, fill fEventData fields (zz, xsh, ysh, height).
    /// Returns false if header is invalid.
    bool ParseHeader(const std::string& headerLine);

    /// Build output file suffix from input filename.
    /// "phels_to_trace_Q0_atm01_..." -> "Q0_atm01_..."
    std::string BuildSuffix(const std::string& inputFilename);
};

#endif
```

### Step 2: Rewrite `src/sphmirrPrimaryGeneratorAction.cc`

Key changes from current code:
- Constructor takes FileQueue*, WorkerEventData*, SimConfig*, DetectorConstruction*
- No messenger (remove — not used in file-based mode, not MT-safe)
- `GeneratePrimaries`: pops file from queue, opens LOCAL ifstream, reads header,
  loops through ALL lines creating multiple primary vertices, fills photonMeta vector
- All per-photon state → `fEventData->photonMeta`
- `zz`, `xsh`, `ysh` → `fEventData->zz`, `fEventData->xsh`, `fEventData->ysh`
- `zstart` → `fDetector->GetZstart()`
- `phi`, `the` → `fConfig->phi`, `fConfig->the` (cached in fCos/fSin*)
- Remove: `extern` declarations, `static lineCounter`, `LoadNextBuffer`, `currentFile` iterator,
  all Set*() methods (Theta/Phi/Grp/Rad/X0/Y0), all messenger code
- If `fFileQueue->Pop()` returns false → return without adding vertices (empty event)

```cpp
#include "sphmirrPrimaryGeneratorAction.hh"
#include "FileQueue.hh"
#include "SimConfig.hh"
#include "WorkerEventData.hh"
#include "sphmirrDetectorConstruction.hh"
#include "G4Event.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4SystemOfUnits.hh"
#include "G4RunManager.hh"
#include "Randomize.hh"
#include <fstream>
#include <sstream>
#include <cmath>
#include <optional>
#include <cstdlib>
#include <regex>

sphmirrPrimaryGeneratorAction::sphmirrPrimaryGeneratorAction(
        FileQueue* fileQueue,
        WorkerEventData* eventData,
        const SimConfig* config,
        const sphmirrDetectorConstruction* detector)
    : fParticleGun(new G4ParticleGun(1))
    , fFileQueue(fileQueue)
    , fEventData(eventData)
    , fConfig(config)
    , fDetector(detector)
{
    G4ParticleTable* particleTable = G4ParticleTable::GetParticleTable();
    G4ParticleDefinition* particle = particleTable->FindParticle("opticalphoton");
    fParticleGun->SetParticleDefinition(particle);
    fParticleGun->SetParticleEnergy(2.0 * eV);

    // Cache detector orientation trigonometry (read-only after init)
    fCosTheta = std::cos(-fConfig->the);
    fSinTheta = std::sin(-fConfig->the);
    fCosPhi = std::cos(fConfig->phi);
    fSinPhi = std::sin(fConfig->phi);
}

sphmirrPrimaryGeneratorAction::~sphmirrPrimaryGeneratorAction() {
    delete fParticleGun;
}

bool sphmirrPrimaryGeneratorAction::ParseHeader(const std::string& headerLine) {
    auto safeParse = [](const std::string& s) -> std::optional<double> {
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0') return std::nullopt;
        return v;
    };

    std::istringstream iss(headerLine);
    std::vector<std::string> values;
    std::string value;
    while (iss >> value) values.push_back(value);

    if (values.size() >= 4) {
        if (auto vx = safeParse(values[3])) fEventData->xsh = *vx * m;
        if (auto vy = safeParse(values[2])) fEventData->ysh = *vy * m;
        if (auto vz = safeParse(values[1])) fEventData->zz = *vz * m;
    }
    if (values.size() >= 2) {
        if (auto vh = safeParse(values[1])) {
            const int h_int = static_cast<int>(std::lround(std::fabs(*vh)));
            fEventData->height = std::to_string(h_int);
        }
    }
    return true;
}

std::string sphmirrPrimaryGeneratorAction::BuildSuffix(const std::string& inputFilename) {
    size_t pos = inputFilename.find("Q");
    if (pos == std::string::npos) return {};
    std::string params = inputFilename.substr(pos);
    std::erase(params, ' ');
    std::regex pattern(R"((_c\d{3})$)");
    std::string insert = "_" + fEventData->height + "m$1";
    return std::regex_replace(params, pattern, insert);
}

void sphmirrPrimaryGeneratorAction::GeneratePrimaries(G4Event* anEvent) {
    // Pop next input file from the thread-safe queue
    std::string filename;
    if (!fFileQueue->Pop(filename)) {
        // No more files — generate empty event (Geant4 handles gracefully)
        return;
    }

    // Open input file (local variable — no shared state)
    std::ifstream inpho(fConfig->phelsDir + "/" + filename);
    if (!inpho.is_open()) {
        G4cout << "WARNING: Cannot open " << filename << ", skipping." << G4endl;
        return;
    }

    // Read and parse header line
    std::string headerLine;
    if (!std::getline(inpho, headerLine)) {
        G4cout << "WARNING: Empty file " << filename << ", skipping." << G4endl;
        return;
    }
    fEventData->headerLine = headerLine;
    ParseHeader(headerLine);

    // Build output suffix
    fEventData->inputFileSuffix = BuildSuffix(filename);
    if (fEventData->inputFileSuffix.empty()) {
        G4cout << "WARNING: Invalid filename format: " << filename << G4endl;
        return;
    }

    // Read all photon lines and create primary vertices
    fEventData->photonMeta.clear();
    const G4double zstart = fDetector->GetZstart();
    const G4double zz = fEventData->zz;

    std::string line;
    int lineCount = 0;
    while (std::getline(inpho, line)) {
        int ii, jj, kk, mmm;
        double xx, yy, t0;
        std::istringstream iss(line);
        if (!(iss >> ii >> jj >> kk >> mmm >> xx >> yy >> t0)) {
            G4cout << "WARNING: Skipping malformed line: " << line << G4endl;
            continue;
        }

        // Store per-photon metadata for SteppingAction lookup
        PhotonMeta meta;
        meta.ii = ii; meta.jj = jj; meta.kk = kk; meta.mmm = mmm;
        meta.xx = xx; meta.yy = yy; meta.t0 = t0;
        meta.origin = (mmm < 100) ? 1 : 2;
        fEventData->photonMeta.push_back(meta);

        // Compute entry point in hood opening
        G4double r = 85.0 * cm * std::sqrt(G4UniformRand());
        G4double dzeta = 360.0 * deg * G4UniformRand();
        G4double xi0 = r * std::cos(dzeta);
        G4double yi0 = r * std::sin(dzeta);
        G4double zi0 = 0.0;

        // Rotate to detector frame
        G4double xi = fCosTheta * fCosPhi * xi0 - fSinPhi * yi0 - fSinTheta * fCosPhi * zi0;
        G4double yi = fCosTheta * fSinPhi * xi0 + fCosPhi * yi0 - fSinTheta * fSinPhi * zi0;
        G4double zi = fSinTheta * xi0 + fCosTheta * zi0 + zstart;

        // Direction from snow-level source to entry point
        G4double xgr = xx * m;
        G4double ygr = yy * m;
        G4double diag = std::sqrt((xi - xgr) * (xi - xgr) + (yi - ygr) * (yi - ygr) + (zz - zi) * (zz - zi));
        G4double pxb = (xi - xgr) / diag;
        G4double pyb = (yi - ygr) / diag;
        G4double pzb = (zi - zz) / diag;

        fParticleGun->SetParticlePosition(G4ThreeVector(xi, yi, zi));
        fParticleGun->SetParticleMomentumDirection(G4ThreeVector(pxb, pyb, pzb));
        fParticleGun->SetParticleTime(t0 * ns);
        SetOptPhotonPolar();
        fParticleGun->GeneratePrimaryVertex(anEvent);

        lineCount++;
        if (lineCount % 1000000 == 0) {
            G4cout << "[" << filename << "] Loaded " << lineCount << " photons." << G4endl;
        }
    }

    G4cout << "Event " << anEvent->GetEventID()
           << ": loaded " << lineCount << " photons from " << filename << G4endl;
}

void sphmirrPrimaryGeneratorAction::SetOptPhotonPolar() const {
    const G4double angle = G4UniformRand() * 360.0 * deg;
    SetOptPhotonPolar(angle);
}

void sphmirrPrimaryGeneratorAction::SetOptPhotonPolar(const G4double angle) const {
    const G4ThreeVector normal(1., 0., 0.);
    const G4ThreeVector kphoton = fParticleGun->GetParticleMomentumDirection();
    const G4ThreeVector product = normal.cross(kphoton);
    const G4double modul2 = product * product;
    G4ThreeVector e_perpend(0., 0., 1.);
    if (modul2 > 0.) e_perpend = (1. / std::sqrt(modul2)) * product;
    const G4ThreeVector e_paralle = e_perpend.cross(kphoton);
    const G4ThreeVector polar = std::cos(angle) * e_paralle + std::sin(angle) * e_perpend;
    fParticleGun->SetParticlePolarization(polar);
}
```

### Step 3: Delete messenger files (no longer needed)

Delete: `include/sphmirrPrimaryGeneratorMessenger.hh`, `src/sphmirrPrimaryGeneratorMessenger.cc`

### Step 4: Commit

```bash
git add include/sphmirrPrimaryGeneratorAction.hh src/sphmirrPrimaryGeneratorAction.cc
git rm include/sphmirrPrimaryGeneratorMessenger.hh src/sphmirrPrimaryGeneratorMessenger.cc
git commit -m "feat(mt): refactor PrimaryGeneratorAction for one-file-per-event model

- Accept FileQueue*, WorkerEventData*, SimConfig*, DetectorConstruction*
- Read all photons from file in single GeneratePrimaries call
- Store per-photon metadata in WorkerEventData::photonMeta
- Remove messenger (not MT-safe, unused in file mode)
- Remove all extern global references"
```

---

## Task 3: Refactor SteppingAction + DetectorConstruction Pixel Migration

**Owner:** Agent B

**Files:**
- Modify: `include/sphmirrDetectorConstruction.hh` (add const getters for pixel arrays and zstart)
- Modify: `src/sphmirrDetectorConstruction.cc` (move pixel arrays to members, add getters)
- Modify: `include/sphmirrSteppingAction.hh`
- Modify: `src/sphmirrSteppingAction.cc`

**Context:**
- Pixel arrays `pix_x[2653]`, `pix_y[2653]`, `pix_z[2653]` are currently globals filled in
  DetectorConstruction constructor (line 56-63 of `src/sphmirrDetectorConstruction.cc`).
  Move to DetectorConstruction members with const getters.
- `zstart` is set in DetectorConstruction::Construct() (line 184). Add const getter.
- SteppingAction already receives DetectorConstruction* — use its getters.
- Replace all extern globals with WorkerEventData*/SimConfig*/DetectorConstruction* access.
- Per-photon tags (origin, phl_*) now come from `fEventData->photonMeta[trackID - 1]`.
  Track ID for primary optical photons is guaranteed sequential starting from 1.

**Dependencies:** Task 1 files must exist

### Step 1: Add getters to `include/sphmirrDetectorConstruction.hh`

Add after the private member declarations (line 42):

```cpp
// --- Public const accessors for pixel geometry (MT-safe: read-only after Construct) ---
public:
    const G4double* GetPixX() const { return fPixX; }
    const G4double* GetPixY() const { return fPixY; }
    const G4double* GetPixZ() const { return fPixZ; }
    G4double GetZstart() const { return fZstart; }
    static constexpr G4int kNPixels = 2653;

private:
    G4double fPixX[2653]{};
    G4double fPixY[2653]{};
    G4double fPixZ[2653]{};
    G4double fZstart{0.0};
```

### Step 2: Update `src/sphmirrDetectorConstruction.cc`

Replace global `extern` references for pix_x/pix_y/pix_z with member access:
- Lines 33-34: remove `extern G4double pix_x[2653]...` and `extern G4double pix_phi[2653]...`
- Lines 56-63: change `pix_x[i]` → `fPixX[i]`, same for y/z
  (pix_phi and pix_theta remain local or removed if unused elsewhere)
- Line 184: change `zstart = -0.1 * cm` → `fZstart = -0.1 * cm`
- Lines 328, 332: change `pix_x[i]` → `fPixX[i]`, `pix_y[i]` → `fPixY[i]`, `pix_z[i]` → `fPixZ[i]`

Keep the global `pix_phi[2653]` and `pix_theta[2653]` arrays for now (they are only used in
DetectorConstruction for placement rotations, not in SteppingAction).

### Step 3: Rewrite `include/sphmirrSteppingAction.hh`

```cpp
#ifndef SteppingAction_h
#define SteppingAction_h 1

#include "G4UserSteppingAction.hh"
#include "globals.hh"
#include "G4ThreeVector.hh"
#include <vector>

class sphmirrDetectorConstruction;
struct WorkerEventData;
struct SimConfig;

struct PixelNormalCache {
    G4double inv_r;
    G4double u, v, w;
};

class sphmirrSteppingAction final : public G4UserSteppingAction {
public:
    sphmirrSteppingAction(WorkerEventData* eventData,
                           const SimConfig* config,
                           const sphmirrDetectorConstruction* detector);
    ~sphmirrSteppingAction() override;
    void UserSteppingAction(const G4Step*) override;

private:
    WorkerEventData* fEventData;
    const SimConfig* fConfig;
    const sphmirrDetectorConstruction* fDetector;

    std::vector<PixelNormalCache> pixelCache;
    void InitializePixelCache();
};

#endif
```

### Step 4: Rewrite `src/sphmirrSteppingAction.cc`

Key changes:
- Remove ALL `extern` declarations (lines 19-35 of current file)
- Constructor: accept WorkerEventData*, SimConfig*, DetectorConstruction*
- `InitializePixelCache()`: read from `fDetector->GetPixX()` etc. instead of global arrays
- `UserSteppingAction()`:
  - `p1` → `fConfig->p1`
  - `moshits` → `fEventData->moshits`
  - `TotPhot++` → `fEventData->TotPhot++`
  - `NEntry++` → `fEventData->NEntry++`
  - `tmin`/`tmax` → `fEventData->tmin`/`tmax`
  - Per-photon tags: get trackID, look up `fEventData->photonMeta[trackID - 1]`
  - Remove references to `NEntryTotal`, `TotPhotTotal`, `tminAll`, `tmaxAll`
    (accumulation happens in EventAction::EndOfEventAction via G4Accumulable)

```cpp
#include "sphmirrSteppingAction.hh"
#include "sphmirrDetectorConstruction.hh"
#include "WorkerEventData.hh"
#include "SimConfig.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4Track.hh"
#include "G4VPhysicalVolume.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"
#include <algorithm>
#include <iomanip>
#include <cmath>

sphmirrSteppingAction::sphmirrSteppingAction(
        WorkerEventData* eventData,
        const SimConfig* config,
        const sphmirrDetectorConstruction* detector)
    : fEventData(eventData), fConfig(config), fDetector(detector)
{
    InitializePixelCache();
}

sphmirrSteppingAction::~sphmirrSteppingAction() = default;

void sphmirrSteppingAction::InitializePixelCache() {
    const G4double* px = fDetector->GetPixX();
    const G4double* py = fDetector->GetPixY();
    const G4double* pz = fDetector->GetPixZ();
    pixelCache.assign(sphmirrDetectorConstruction::kNPixels, {});
    for (G4int i = 0; i < sphmirrDetectorConstruction::kNPixels; i++) {
        const G4double r = std::sqrt(px[i]*px[i] + py[i]*py[i] + pz[i]*pz[i]);
        const G4double inv_r = 1.0 / r;
        pixelCache[i] = {inv_r, px[i]*inv_r, py[i]*inv_r, pz[i]*inv_r};
    }
}

void sphmirrSteppingAction::UserSteppingAction(const G4Step* aStep) {
    const G4Track* fTrack = aStep->GetTrack();
    const G4StepPoint* thePostPoint = aStep->GetPostStepPoint();
    const G4VPhysicalVolume* thePostPV = thePostPoint->GetPhysicalVolume();
    if (!thePostPV) return;

    const G4String& thePostPVname = thePostPV->GetName();
    if (fTrack->GetDefinition()->GetParticleName() != "opticalphoton") return;
    if (thePostPVname != "PMT") return;

    const G4TouchableHandle theTouchable = thePostPoint->GetTouchableHandle();
    const G4int copyNo = theTouchable->GetCopyNumber(1);
    const auto& cache = pixelCache[copyNo];

    const G4double dirx = thePostPoint->GetMomentumDirection().x();
    const G4double diry = thePostPoint->GetMomentumDirection().y();
    const G4double dirz = thePostPoint->GetMomentumDirection().z();
    const G4double dot = cache.u * dirx + cache.v * diry + cache.w * dirz;

    if (dot < 0.0) {
        const G4double glt = fTrack->GetGlobalTime();
        if (fEventData->tmin > glt) fEventData->tmin = glt;
        if (fEventData->tmax < glt) fEventData->tmax = glt;

        const G4int cluster_num = copyNo / 7;
        const G4int pix_num = copyNo - 7 * cluster_num;
        if (pix_num >= 7 || cluster_num >= 379) return;

        const G4double pp = std::clamp(-dot, 0.0, 1.0);
        if (pp > 0.0 && G4UniformRand() < std::pow(pp, fConfig->p1)) {
            // Look up per-photon metadata by trackID
            const G4int trackID = fTrack->GetTrackID();
            const PhotonMeta* meta = nullptr;
            if (trackID >= 1 && trackID <= static_cast<G4int>(fEventData->photonMeta.size())) {
                meta = &fEventData->photonMeta[trackID - 1];
            }
            // Fallback for secondary optical photons: use parent
            if (!meta) {
                const G4int parentID = fTrack->GetParentID();
                if (parentID >= 1 && parentID <= static_cast<G4int>(fEventData->photonMeta.size())) {
                    meta = &fEventData->photonMeta[parentID - 1];
                }
            }
            if (!meta) return;  // shouldn't happen, but guard

            const G4double x = thePostPoint->GetPosition().x();
            const G4double y = thePostPoint->GetPosition().y();
            const G4double z = thePostPoint->GetPosition().z();

            fEventData->NEntry++;
            fEventData->moshits << std::setw(5) << cluster_num << std::setw(5) << pix_num
                    << std::setw(14) << x / m << std::setw(14) << y / m
                    << std::setw(14) << z / m << std::setw(14) << glt / ns
                    << std::setw(14) << dirx << std::setw(14) << diry
                    << std::setw(14) << dirz << std::setw(5) << meta->origin
                    << std::setw(5) << meta->ii << std::setw(5) << meta->jj
                    << std::setw(5) << meta->kk
                    << std::setw(14) << meta->xx << std::setw(14) << meta->yy
                    << std::setw(14) << meta->t0
                    << '\n';

            // Crosstalk
            constexpr G4double crosstalk_prob = 0.07;
            for (int n = 1; n <= 6; n++) {
                if (G4UniformRand() < crosstalk_prob) {
                    const G4int neighbor_pix = (pix_num + n) % 7;
                    fEventData->NEntry++;
                    fEventData->moshits << std::setw(5) << cluster_num << std::setw(5) << neighbor_pix
                            << std::setw(14) << x / m << std::setw(14) << y / m
                            << std::setw(14) << z / m << std::setw(14) << glt / ns
                            << std::setw(14) << dirx << std::setw(14) << diry
                            << std::setw(14) << dirz << std::setw(5) << meta->origin
                            << std::setw(5) << meta->ii << std::setw(5) << meta->jj
                            << std::setw(5) << meta->kk
                            << std::setw(14) << meta->xx << std::setw(14) << meta->yy
                            << std::setw(14) << meta->t0
                            << '\n';
                }
            }
        }
    }

    fEventData->TotPhot++;
}
```

### Step 5: Commit

```bash
git add include/sphmirrDetectorConstruction.hh src/sphmirrDetectorConstruction.cc \
        include/sphmirrSteppingAction.hh src/sphmirrSteppingAction.cc
git commit -m "feat(mt): refactor SteppingAction + migrate pixel arrays to DetectorConstruction

- Move pix_x/y/z arrays and zstart to DetectorConstruction members with const getters
- SteppingAction accepts WorkerEventData*, SimConfig*, DetectorConstruction*
- Per-photon metadata lookup via trackID from WorkerEventData::photonMeta
- Remove all extern global references"
```

---

## Task 4: Rewrite EventAction (per-event file I/O)

**Owner:** Agent C

**Files:**
- Modify: `include/sphmirrEventAction.hh`
- Modify: `src/sphmirrEventAction.cc`

**Context:**
- Currently EventAction is empty (just stores RunAction pointer)
- Must manage per-event output file lifecycle:
  - `BeginOfEventAction()`: reset counters in WorkerEventData, open `moshits_<suffix>` file
  - `EndOfEventAction()`: write per-event summary to file, close file, accumulate into G4Accumulable
- Output path: `<SimConfig::outputDir>/moshits_<WorkerEventData::inputFileSuffix>`
- If `inputFileSuffix` is empty (no file was popped — empty event), skip file operations
- Accumulate NEntry and TotPhot into worker-local G4Accumulable (registered in RunAction)

**Dependencies:** Task 1 files must exist

### Step 1: Rewrite `include/sphmirrEventAction.hh`

```cpp
#ifndef EventAction_h
#define EventAction_h 1

#include "G4UserEventAction.hh"
#include "globals.hh"

struct WorkerEventData;
struct SimConfig;

class EventAction final : public G4UserEventAction {
public:
    EventAction(WorkerEventData* eventData, const SimConfig* config);
    ~EventAction() override;
    void BeginOfEventAction(const G4Event*) override;
    void EndOfEventAction(const G4Event*) override;

private:
    WorkerEventData* fEventData;
    const SimConfig* fConfig;
};

#endif
```

### Step 2: Rewrite `src/sphmirrEventAction.cc`

```cpp
#include "sphmirrEventAction.hh"
#include "WorkerEventData.hh"
#include "SimConfig.hh"
#include "G4Event.hh"
#include "G4AccumulableManager.hh"
#include "G4SystemOfUnits.hh"
#include <filesystem>
#include <limits>

EventAction::EventAction(WorkerEventData* eventData, const SimConfig* config)
    : fEventData(eventData), fConfig(config) {}

EventAction::~EventAction() = default;

void EventAction::BeginOfEventAction(const G4Event*) {
    // Reset per-event counters
    fEventData->TotPhot = 0;
    fEventData->NEntry = 0;
    fEventData->tmin = std::numeric_limits<G4double>::max();
    fEventData->tmax = 0.0;
    fEventData->inputFileSuffix.clear();
    fEventData->photonMeta.clear();

    // Output file is opened AFTER GeneratePrimaries fills inputFileSuffix.
    // Geant4 calls GeneratePrimaries between BeginOfEventAction and stepping.
    // So we open the file lazily — not here.
}

void EventAction::EndOfEventAction(const G4Event* event) {
    // Skip empty events (no file was assigned)
    if (fEventData->inputFileSuffix.empty()) return;

    // Open output file if not already open
    // (GeneratePrimaries set the suffix; first step opens the file)
    if (!fEventData->moshits.is_open()) {
        const std::string outputDir = fConfig->outputDir;
        if (!std::filesystem::exists(outputDir)) {
            std::filesystem::create_directories(outputDir);
        }
        const std::string path = outputDir + "/moshits_" + fEventData->inputFileSuffix;
        fEventData->moshits.open(path, std::ios::out);
        if (fEventData->moshits.is_open()) {
            fEventData->moshits << fEventData->headerLine << '\n';
        }
    }

    // Write per-event summary as footer
    if (fEventData->moshits.is_open()) {
        fEventData->moshits.close();
    }

    G4cout << "Event " << event->GetEventID()
           << " [" << fEventData->inputFileSuffix << "]:"
           << " TotPhot=" << fEventData->TotPhot
           << " NEntry=" << fEventData->NEntry;
    if (fEventData->NEntry > 0) {
        G4cout << " tmin=" << fEventData->tmin / ns << "ns"
               << " tmax=" << fEventData->tmax / ns << "ns";
    }
    G4cout << G4endl;

    // Accumulate into G4Accumulable (will be merged in EndOfRunAction)
    auto* accMgr = G4AccumulableManager::Instance();
    auto* accTotPhot = accMgr->GetAccumulable<G4int>("TotPhotTotal");
    auto* accNEntry  = accMgr->GetAccumulable<G4int>("NEntryTotal");
    auto* accTmin    = accMgr->GetAccumulable<G4double>("TminAll");
    auto* accTmax    = accMgr->GetAccumulable<G4double>("TmaxAll");
    if (accTotPhot) *accTotPhot += fEventData->TotPhot;
    if (accNEntry)  *accNEntry  += fEventData->NEntry;
    if (accTmin && fEventData->NEntry > 0) {
        // For min: only update if this event had hits
        if (fEventData->tmin < accTmin->GetValue())
            accTmin->SetValue(fEventData->tmin);
    }
    if (accTmax && fEventData->NEntry > 0) {
        if (fEventData->tmax > accTmax->GetValue())
            accTmax->SetValue(fEventData->tmax);
    }
}
```

**NOTE on file opening:** There's a subtlety — Geant4 calls `BeginOfEventAction` BEFORE
`GeneratePrimaries`, so the suffix isn't available in `BeginOfEventAction`. The output file
is opened lazily: either in the first SteppingAction call or in EndOfEventAction.
A simpler approach is to open it at the START of EndOfEventAction if it wasn't opened yet
(handles events with 0 detections). This means SteppingAction must check `moshits.is_open()`
or we open right after GeneratePrimaries. Actually, the cleanest fix: open in the
SteppingAction on first write, or open at start of EndOfEventAction. Both approaches are
shown above — EventAction opens if needed in End.

**CORRECTION:** Better approach — open the file at the start of `EndOfEventAction` if not
already open, and have SteppingAction open it on first write:

Add to SteppingAction (at the top of the detection block, before first moshits write):
```cpp
if (!fEventData->moshits.is_open() && !fEventData->inputFileSuffix.empty()) {
    const std::string outputDir = fConfig->outputDir;
    if (!std::filesystem::exists(outputDir))
        std::filesystem::create_directories(outputDir);
    fEventData->moshits.open(outputDir + "/moshits_" + fEventData->inputFileSuffix, std::ios::out);
    fEventData->moshits << fEventData->headerLine << '\n';
}
```

### Step 3: Commit

```bash
git add include/sphmirrEventAction.hh src/sphmirrEventAction.cc
git commit -m "feat(mt): rewrite EventAction for per-event output file management

- Open/close per-event moshits file
- Reset and accumulate per-event counters via G4Accumulable
- Print per-event summary"
```

---

## Task 5: Refactor RunAction + Create ActionInitialization

**Owner:** Agent D

**Files:**
- Modify: `include/sphmirrRunAction.hh`
- Modify: `src/sphmirrRunAction.cc`
- Create: `include/sphmirrActionInitialization.hh`
- Create: `src/sphmirrActionInitialization.cc`

**Context:**
- RunAction must register G4Accumulable variables and merge them in EndOfRunAction
- ActionInitialization creates per-worker action instances in Build(), master RunAction in BuildForMaster()
- StackingAction has no changes but must be registered in Build()
- RunAction class is currently named `RunAction` — keep name for consistency or rename, your choice

**Dependencies:** Task 1 files must exist

### Step 1: Rewrite `include/sphmirrRunAction.hh`

```cpp
#ifndef sphmirrRunAction_h
#define sphmirrRunAction_h 1

#include "G4UserRunAction.hh"
#include "G4Accumulable.hh"
#include "globals.hh"
#include <limits>

class G4Timer;
class G4Run;

class RunAction final : public G4UserRunAction {
public:
    RunAction();
    ~RunAction() override;
    void BeginOfRunAction(const G4Run* aRun) override;
    void EndOfRunAction(const G4Run* aRun) override;

private:
    G4Timer* fTimer;
    G4Accumulable<G4int> fTotPhotTotal{0};
    G4Accumulable<G4int> fNEntryTotal{0};
    G4Accumulable<G4double> fTminAll{std::numeric_limits<G4double>::max()};
    G4Accumulable<G4double> fTmaxAll{0.0};
};

#endif
```

### Step 2: Rewrite `src/sphmirrRunAction.cc`

```cpp
#include "sphmirrRunAction.hh"
#include "G4Run.hh"
#include "G4Timer.hh"
#include "G4AccumulableManager.hh"
#include "G4SystemOfUnits.hh"

RunAction::RunAction() : fTimer(new G4Timer) {
    auto* accMgr = G4AccumulableManager::Instance();
    accMgr->RegisterAccumulable("TotPhotTotal", fTotPhotTotal);
    accMgr->RegisterAccumulable("NEntryTotal", fNEntryTotal);
    accMgr->RegisterAccumulable("TminAll", fTminAll);
    accMgr->RegisterAccumulable("TmaxAll", fTmaxAll);
}

RunAction::~RunAction() { delete fTimer; }

void RunAction::BeginOfRunAction(const G4Run* aRun) {
    G4cout << "### Run " << aRun->GetRunID() << " start." << G4endl;
    G4AccumulableManager::Instance()->Reset();
    fTimer->Start();
}

void RunAction::EndOfRunAction(const G4Run* aRun) {
    fTimer->Stop();

    // Merge accumulables from all worker threads
    G4AccumulableManager::Instance()->Merge();

    G4cout << "### Run " << aRun->GetRunID() << " end. "
           << aRun->GetNumberOfEvent() << " events. " << *fTimer << G4endl;

    const G4int totPhot = fTotPhotTotal.GetValue();
    const G4int nEntry  = fNEntryTotal.GetValue();
    G4cout << "[All events] TotPhot = " << totPhot
           << ", NEntry = " << nEntry << G4endl;
    if (nEntry > 0) {
        G4cout << "[All events] tmin = " << fTminAll.GetValue() / ns << " ns"
               << ", tmax = " << fTmaxAll.GetValue() / ns << " ns" << G4endl;
    } else {
        G4cout << "[All events] tmin/tmax: n/a" << G4endl;
    }
}
```

### Step 3: Create `include/sphmirrActionInitialization.hh`

```cpp
#ifndef sphmirrActionInitialization_hh
#define sphmirrActionInitialization_hh 1

#include "G4VUserActionInitialization.hh"

class FileQueue;
struct SimConfig;
class sphmirrDetectorConstruction;

class sphmirrActionInitialization final : public G4VUserActionInitialization {
public:
    sphmirrActionInitialization(FileQueue* fileQueue,
                                 const SimConfig* config,
                                 const sphmirrDetectorConstruction* detector);
    ~sphmirrActionInitialization() override = default;

    void Build() const override;
    void BuildForMaster() const override;

private:
    FileQueue* fFileQueue;
    const SimConfig* fConfig;
    const sphmirrDetectorConstruction* fDetector;
};

#endif
```

### Step 4: Create `src/sphmirrActionInitialization.cc`

```cpp
#include "sphmirrActionInitialization.hh"
#include "sphmirrPrimaryGeneratorAction.hh"
#include "sphmirrRunAction.hh"
#include "sphmirrEventAction.hh"
#include "sphmirrSteppingAction.hh"
#include "sphmirrStackingAction.hh"
#include "FileQueue.hh"
#include "SimConfig.hh"
#include "WorkerEventData.hh"
#include "sphmirrDetectorConstruction.hh"

sphmirrActionInitialization::sphmirrActionInitialization(
        FileQueue* fileQueue,
        const SimConfig* config,
        const sphmirrDetectorConstruction* detector)
    : fFileQueue(fileQueue), fConfig(config), fDetector(detector) {}

void sphmirrActionInitialization::Build() const {
    // Each worker thread gets its own WorkerEventData and action instances
    auto* eventData = new WorkerEventData();

    SetUserAction(new sphmirrPrimaryGeneratorAction(fFileQueue, eventData, fConfig, fDetector));
    SetUserAction(new RunAction());
    SetUserAction(new EventAction(eventData, fConfig));
    SetUserAction(new sphmirrSteppingAction(eventData, fConfig, fDetector));
    SetUserAction(new sphmirrStackingAction());
}

void sphmirrActionInitialization::BuildForMaster() const {
    SetUserAction(new RunAction());
}
```

### Step 5: Commit

```bash
git add include/sphmirrRunAction.hh src/sphmirrRunAction.cc \
        include/sphmirrActionInitialization.hh src/sphmirrActionInitialization.cc
git commit -m "feat(mt): add ActionInitialization + G4Accumulable in RunAction

- ActionInitialization creates per-worker instances of all actions
- RunAction uses G4Accumulable for thread-safe counter aggregation
- Merge accumulables in EndOfRunAction on master thread"
```

---

## Task 6: Rewrite main() and Remove Globals

**Owner:** Leader (after all agents complete)

**Files:**
- Modify: `SPHERE-3.cpp`
- Delete: `include/sphmirrCounters.hh`, `src/sphmirrCounters.cc`

**Dependencies:** Tasks 2-5 complete

### Step 1: Rewrite `SPHERE-3.cpp`

```cpp
#include "G4RunManagerFactory.hh"
#include "G4UImanager.hh"
#include "G4SystemOfUnits.hh"
#include "sphmirrPhysicsList.hh"
#include "sphmirrDetectorConstruction.hh"
#include "sphmirrActionInitialization.hh"
#include "FileQueue.hh"
#include "SimConfig.hh"
#include "ini.h"
#include <iostream>
#include <filesystem>

int main(const int argc, char** argv) {
    // Seed random number generator
    constexpr G4long myseed = 3453544;
    CLHEP::HepRandom::setTheSeed(myseed);

    // Determine working directory
    std::string currentPath;
    if (argc > 1) {
        currentPath = argv[1];
    } else {
        currentPath = std::filesystem::path(argv[0]).parent_path().string();
    }

    // Read configuration
    const mINI::INIFile iniFile("input.ini");
    mINI::INIStructure ini;
    iniFile.read(ini);
    std::string height = ini.get("DEFAULT").get("Height");
    if (height.empty()) {
        height = "500";
        G4cout << "WARNING: Height not found in input.ini, using default 500" << G4endl;
    }

    // Build SimConfig (read-only after this point)
    auto* config = new SimConfig();
    config->phi = 0.0 * deg;
    config->the = 0.0 * deg;
    config->p1 = 1.093;
    config->currentPath = currentPath;
    config->phelsDir = currentPath + "/phels";
    config->outputDir = currentPath + "/moshits";

    // Build FileQueue from phels directory
    auto* fileQueue = new FileQueue();
    for (const auto& entry : std::filesystem::directory_iterator(config->phelsDir)) {
        if (entry.is_regular_file()) {
            fileQueue->Push(entry.path().filename().string());
        }
    }
    const G4int nEvents = static_cast<G4int>(fileQueue->Size());
    G4cout << "Found " << nEvents << " input files in " << config->phelsDir << G4endl;

    // Create run manager (auto-selects MT if Geant4 built with MT support)
    auto* runManager = G4RunManagerFactory::CreateRunManager();

    // Detector construction
    auto* detector = new sphmirrDetectorConstruction();
    runManager->SetUserInitialization(detector);
    runManager->SetUserInitialization(new sphmirrPhysicsList());

    // Action initialization (creates per-worker action instances)
    runManager->SetUserInitialization(
        new sphmirrActionInitialization(fileQueue, config, detector));

    // Initialize and run
    runManager->Initialize();

    if (nEvents > 0) {
        G4UImanager* UImanager = G4UImanager::GetUIpointer();
        UImanager->ApplyCommand("/run/beamOn " + std::to_string(nEvents));
    }

    delete runManager;
    delete config;
    delete fileQueue;
    return 0;
}
```

### Step 2: Delete counter files

```bash
git rm include/sphmirrCounters.hh src/sphmirrCounters.cc
```

### Step 3: Commit

```bash
git add SPHERE-3.cpp
git commit -m "feat(mt): rewrite main() with RunManagerFactory, remove all globals

- Use G4RunManagerFactory for automatic MT/sequential selection
- Create SimConfig, FileQueue, pass to ActionInitialization
- Remove ~40 global mutable variables
- Delete sphmirrCounters (replaced by G4Accumulable)"
```

---

## Task 7: Build, Fix, Verify

**Owner:** Leader

### Step 1: Build

```bash
cd build && cmake .. && make -j$(nproc)
```

Expected: compilation errors from leftover references to removed globals or mismatched signatures.

### Step 2: Fix any compilation issues

Common expected issues:
- `sphmirrDetectorConstruction.cc` may still reference globals that moved (pix_phi, pix_theta, xpmt, etc.)
- Any other files referencing removed globals
- Missing includes

### Step 3: Run overlap check

```bash
./SPHERE-3
```

Verify: no geometry overlap warnings, events start processing.

### Step 4: Run with test data

Place 2-3 small input files in `phels/`, run simulation, verify:
- Separate `moshits_*` files created per input file
- Output format matches expected columns
- Per-event and global summary statistics printed

### Step 5: Compare output with single-threaded run

For validation:
1. Run with `/run/numberOfThreads 1` — should produce identical results to old code
2. Run with `/run/numberOfThreads 4` — results may differ (different random sequences per event
   due to different worker assignment), but statistics should be comparable

### Step 6: Final commit

```bash
git add -A
git commit -m "fix(mt): resolve compilation issues from MT migration"
```

---

## Notes for Agents

### File ownership (no conflicts)

| Agent | Exclusive files |
|-------|----------------|
| A (PrimaryGen) | `include/sphmirrPrimaryGeneratorAction.hh`, `src/sphmirrPrimaryGeneratorAction.cc`, `include/sphmirrPrimaryGeneratorMessenger.hh` (delete), `src/sphmirrPrimaryGeneratorMessenger.cc` (delete) |
| B (Stepping) | `include/sphmirrSteppingAction.hh`, `src/sphmirrSteppingAction.cc`, `include/sphmirrDetectorConstruction.hh`, `src/sphmirrDetectorConstruction.cc` |
| C (Event) | `include/sphmirrEventAction.hh`, `src/sphmirrEventAction.cc` |
| D (Run+Init) | `include/sphmirrRunAction.hh`, `src/sphmirrRunAction.cc`, `include/sphmirrActionInitialization.hh` (create), `src/sphmirrActionInitialization.cc` (create) |

### Key invariants agents must follow

1. **No extern globals.** All data flows through constructor parameters: `WorkerEventData*`, `const SimConfig*`, `const sphmirrDetectorConstruction*`, `FileQueue*`.
2. **WorkerEventData is per-worker.** Never share between threads. Never use static/global storage.
3. **SimConfig is const.** Never modify after main() initialization.
4. **DetectorConstruction getters are const.** Pixel arrays are immutable after Construct().
5. **G4Accumulable names must match.** RunAction registers "TotPhotTotal", "NEntryTotal", "TminAll", "TmaxAll". EventAction looks them up by the same names.
6. **TrackID indexing for photonMeta.** Primary photons get trackIDs 1..N. Index into `photonMeta` as `[trackID - 1]`.
