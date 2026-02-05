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
#include <string>

// AbsolutePath is read by DetectorConstruction for config/STL file paths.
// Set once in main() before DetectorConstruction is created; never modified after.
G4String AbsolutePath;

int main(const int argc, char** argv) {
    // Seed the random number generator
    constexpr G4long myseed = 3453544;
    CLHEP::HepRandom::setTheSeed(myseed);

    // Determine working directory
    std::string currentPath;
    if (argc > 1) {
        currentPath = argv[1];
    } else {
        currentPath = std::filesystem::path(argv[0]).parent_path().string();
    }
    AbsolutePath = currentPath;

    // Read configuration from input.ini
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
    const auto nEvents = static_cast<G4int>(fileQueue->Size());
    G4cout << "Found " << nEvents << " input files in " << config->phelsDir << G4endl;

    // Create run manager (auto-selects MT if Geant4 built with MT support)
    auto* runManager = G4RunManagerFactory::CreateRunManager();

    // Detector construction (reads pixel data and STL, sets zstart)
    auto* detector = new sphmirrDetectorConstruction();
    runManager->SetUserInitialization(detector);
    runManager->SetUserInitialization(new sphmirrPhysicsList());

    // Action initialization (creates per-worker action instances in MT mode)
    runManager->SetUserInitialization(
        new sphmirrActionInitialization(fileQueue, config, detector));

    // Initialize geometry + physics
    runManager->Initialize();

    // Run simulation: one event per input file
    if (nEvents > 0) {
        G4UImanager* UImanager = G4UImanager::GetUIpointer();
        UImanager->ApplyCommand("/run/beamOn " + std::to_string(nEvents));
    }

    delete runManager;
    delete config;
    delete fileQueue;
    return 0;
}
