#include "G4RunManager.hh"
#include <iostream>
#include <string>
#include <unistd.h>
#include <filesystem>
#include <limits>
#include "sphmirrPhysicsList.hh"
#include "sphmirrPrimaryGeneratorAction.hh"
#include "sphmirrDetectorConstruction.hh"
#include "sphmirrRunAction.hh"
#include "sphmirrEventAction.hh"
#include "sphmirrStackingAction.hh"
#include "sphmirrSteppingAction.hh"
#include "G4SystemOfUnits.hh"
#include "ini.h"
#include "G4VisExecutive.hh"
#include "G4UIExecutive.hh"
#include "G4UImanager.hh"

// ==========================================================================
// WARNING: Global mutable state below. NOT thread-safe.
// Current code uses G4RunManager (single-threaded). If migrating to
// G4MTRunManager, all counters (TotPhot, NEntry, tmin, tmax, etc.)
// and the moshits ofstream must be protected with G4Accumulable or mutex.
// ==========================================================================
std::ofstream moshits;
std::ifstream inpho, names;
[[maybe_unused]] G4int DirGrp;
G4double zstart;
G4double xgr, ygr;  // coordinates of the starting point of a photon in meters
[[maybe_unused]] G4double xax, yax;  // coordinates of the shower axis position
[[maybe_unused]] [[maybe_unused]] G4double u, xpmt[379], ypmt[379], zpmt[379];   // PMT centers' coordinates, mm
G4double rs[379], angles[379];     // segments
G4double rc[7], anglec[7]; // pixels in a segment
G4double pix_x[2653], pix_y[2653], pix_z[2653]; // pixels' coordinates
G4double pix_phi[2653], pix_theta[2653]; // pixels' angles
G4double p1 = 1.093;                 // sensitivity(xi) = cos(xi)^p1
[[maybe_unused]] G4double costh;
G4int input_file_open = 0;     //  key shows the number of open moshits,  0 means clone#1 is to be opened
G4double zz;      // snow level z (<0.0); in case of inclined detector -- snow level z under the detector, m
G4double xsh;     // EAS axis x-shift at snow level, m
G4double ysh;     // EAS axis y-shift at snow level, m
G4int CorAtmMod;  // CORSIKA atmosphere model number
G4int CloneNum;   // event clone number
G4String NameIn;  // inpho prefix
G4String NameOut; // moshits prefix
G4String CurrentPath;
G4String AbsolutePath;
std::string Height;
std::vector<std::string> fileList;
std::string phelsDir;
int phl_CloneNum{0}, phl_ii{0}, phl_jj{0}, phl_kk{0}, phl_mmm{0};
double phl_xx{0.0}, phl_yy{0.0}, phl_t0{0.0};
G4double phi = 0.0 * deg;   // detector rotation
G4double the = 0.0 * deg;   // angles
G4int TotPhot = 0;         // total number of tracked photons that entered PMTs
G4int NEntry = 0;         // number of tracked photons that entered PMTs within the time window
G4double xx, yy, t0;         // starting coordinates and time of a photon at snow level
G4int NbOfEvents;
G4double tmin = 1.8e6 * ns;      // minimum delay at mosaic, ns
G4double tmax = 1.8e5 * ns;      // maximum delay at mosaic, ns
[[maybe_unused]] G4double tdmi;          // minimum delay at snow level, ns
[[maybe_unused]] G4double tdma;          // maximum delay at snow level, ns
[[maybe_unused]] G4double tbig;          // delay term due to EAS fly to the ground, ns
G4int origin;         // phel origin tag: 1 - CL, 2 - BG

int main(const int argc, char **argv) {
    // Seed the random number generator manually
    constexpr G4long myseed = 3453544;
    CLHEP::HepRandom::setTheSeed(myseed);
    if (argc > 1) {
        CurrentPath = argv[1];
    } else {
        CurrentPath = std::filesystem::path(argv[0]).parent_path().string();
    }
    phelsDir = CurrentPath + "/phels";
    const mINI::INIFile file("input.ini");
    mINI::INIStructure ini;
    file.read(ini);
    Height = ini.get("DEFAULT").get("Height");
    if (Height.empty()) {
        Height = "500";
        G4cout << "WARNING: Height not found in input.ini, using default 500" << G4endl;
    }
    for (const auto &entry : std::filesystem::directory_iterator(phelsDir)) {
        if (entry.is_regular_file()) {
            fileList.push_back(entry.path().filename().string());
        }
    }
    std::cout << "List of files in phels directory:" << std::endl;
    for (const auto &f : fileList) {
        std::cout << f << std::endl;
    }
    AbsolutePath = CurrentPath;
    G4cout << argc << G4endl << CurrentPath << G4endl;
    NbOfEvents = std::numeric_limits<G4int>::max();
    // Run manager
    auto *runManager = new G4RunManager;
    // UserInitialization classes - mandatory
    auto *physics = new sphmirrPhysicsList;
    runManager->SetUserInitialization(physics);
    auto *gen_action = new sphmirrPrimaryGeneratorAction;
    runManager->SetUserAction(gen_action);
    auto *detector = new sphmirrDetectorConstruction;
    runManager->SetUserInitialization(detector);
    // UserAction classes
    auto *run_action = new RunAction;
    runManager->SetUserAction(run_action);
    auto *event_action = new EventAction(run_action);
    runManager->SetUserAction(event_action);
    G4UserStackingAction *stacking_action = new sphmirrStackingAction;
    runManager->SetUserAction(stacking_action);
    auto *stepping_action = new sphmirrSteppingAction(detector, event_action);
    runManager->SetUserAction(stepping_action);
    runManager->Initialize();
    // visualization manager
    // G4VisManager *visManager = new G4VisExecutive;
    // visManager->Initialize();
    // G4UImanager *UImanager = G4UImanager::GetUIpointer();
    // if (argc == 1)   // Define UI session for interactive mode
    // {
    //     auto *ui = new G4UIExecutive(argc, argv);
    //     UImanager->ApplyCommand("/control/execute vis.mac");
    //     ui->SessionStart();
    //     delete ui;
    // } else         // Batch mode
    // {
    //     G4String command = "/control/execute ";
    //     G4String fileName = argv[1];
    //     UImanager->ApplyCommand(command + fileName);
    // }
    //
    // delete visManager;
    if (NbOfEvents > 0) runManager->BeamOn(NbOfEvents);
    delete runManager;
    return 0;
}