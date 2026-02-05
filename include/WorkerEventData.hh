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
    std::string inputFileSuffix;        // e.g. "Q0_atm01_0014_10PeV_15_001_c001"
    std::string headerLine;             // first line of input file (written to output)
    G4double zz{0.0};                   // snow level z (from file header)
    G4double xsh{0.0};                  // EAS axis x-shift (from file header)
    G4double ysh{0.0};                  // EAS axis y-shift (from file header)
    std::string height;                 // detector height string (from file header)
    std::vector<PhotonMeta> photonMeta; // metadata per primary, indexed by trackID-1

    // --- Managed by EventAction ---
    std::ofstream moshits;

    // --- Per-event counters (reset in GeneratePrimaries) ---
    G4int TotPhot{0};
    G4int NEntry{0};
    G4double tmin{std::numeric_limits<G4double>::max()};
    G4double tmax{0.0};

    // --- Diagnostic counters: where photons die (reset in GeneratePrimaries) ---
    G4int diag_nKilledMirror{0};
    G4int diag_nKilledMosaic{0};
    G4int diag_nKilledBase{0};
    G4int diag_nKilledLens{0};
    G4int diag_nKilledHood{0};
    G4int diag_nKilledPMT{0};
    G4int diag_nKilledWorld{0};
    G4int diag_nKilledOther{0};
    G4int diag_nLeftWorld{0};
};

#endif
