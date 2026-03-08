#include "G4RunManagerFactory.hh"
#include "G4UImanager.hh"
#include "G4UIExecutive.hh"
#include "G4VisExecutive.hh"
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

int main(int argc, char** argv) {
    // Parse command-line arguments in a single pass
    bool visMode = false;
    std::string currentPath;
    std::string cliPhelsDir;
    std::string cliMoshitsDir;
    std::string cliThreads;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vis") {
            visMode = true;
        } else if (arg == "--phels" && i + 1 < argc) {
            cliPhelsDir = argv[++i];
        } else if (arg == "--moshits" && i + 1 < argc) {
            cliMoshitsDir = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            cliThreads = argv[++i];
        } else if (currentPath.empty()) {
            currentPath = argv[i];
        }
    }

    // Seed the random number generator
    constexpr G4long myseed = 3453544;
    CLHEP::HepRandom::setTheSeed(myseed);

    if (currentPath.empty()) {
        currentPath = std::filesystem::path(argv[0]).parent_path().string();
    }
    AbsolutePath = currentPath;

    // Read configuration from input.ini
    const mINI::INIFile iniFile("input.ini");
    mINI::INIStructure ini;
    iniFile.read(ini);

    // Read optional thread count from ini (0 or absent = Geant4 default)
    const std::string threadsStr = ini.get("DEFAULT").get("Threads");

    // Build SimConfig (read-only after this point)
    auto* config = new SimConfig();
    config->phi = 0.0 * deg;
    config->the = 0.0 * deg;
    config->p1 = 1.093;
    config->currentPath = currentPath;
    config->phelsDir = cliPhelsDir.empty() ? currentPath + "/phels" : cliPhelsDir;
    config->outputDir = cliMoshitsDir.empty() ? currentPath + "/moshits" : cliMoshitsDir;

    // Build FileQueue from phels directory (skip in vis mode if dir missing)
    auto* fileQueue = new FileQueue();
    G4int nEvents = 0;
    if (!visMode || std::filesystem::exists(config->phelsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(config->phelsDir)) {
            if (entry.is_regular_file()) {
                fileQueue->Push(entry.path().filename().string());
            }
        }
        nEvents = static_cast<G4int>(fileQueue->Size());
        G4cout << "Found " << nEvents << " input files in " << config->phelsDir << G4endl;
    } else {
        G4cout << "Visualization mode: skipping phels directory" << G4endl;
    }

    // Create run manager (auto-selects MT if Geant4 built with MT support)
    auto* runManager = G4RunManagerFactory::CreateRunManager();
    const std::string finalThreads = cliThreads.empty() ? threadsStr : cliThreads;
    if (!finalThreads.empty()) {
        const G4int nThreads = std::stoi(finalThreads);
        if (nThreads > 0) {
            runManager->SetNumberOfThreads(nThreads);
            G4cout << "Using " << nThreads << " worker threads" << G4endl;
        }
    }

    // Detector construction (reads pixel data and STL, sets zstart)
    auto* detector = new sphmirrDetectorConstruction();
    runManager->SetUserInitialization(detector);
    runManager->SetUserInitialization(new sphmirrPhysicsList());

    // Action initialization (creates per-worker action instances in MT mode)
    runManager->SetUserInitialization(
        new sphmirrActionInitialization(fileQueue, config, detector));

    // Initialize geometry + physics
    runManager->Initialize();

    if (visMode) {
        // Interactive visualization mode
        auto* visManager = new G4VisExecutive();
        visManager->Initialize();

        auto* ui = new G4UIExecutive(argc, argv);
        G4UImanager* UImanager = G4UImanager::GetUIpointer();
        UImanager->ApplyCommand("/control/execute init_vis.mac");
        ui->SessionStart();
        delete ui;
        delete visManager;
    } else {
        // Batch simulation mode
        if (nEvents > 0) {
            G4UImanager* UImanager = G4UImanager::GetUIpointer();
            UImanager->ApplyCommand("/run/beamOn " + std::to_string(nEvents));
        }
    }

    delete runManager;
    delete config;
    delete fileQueue;
    return 0;
}
