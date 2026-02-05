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
