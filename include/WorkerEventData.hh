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
