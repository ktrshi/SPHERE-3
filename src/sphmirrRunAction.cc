#include "sphmirrRunAction.hh"
#include "SimConfig.hh"
#include "G4Run.hh"
#include "G4Timer.hh"
#include "G4AccumulableManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4Threading.hh"
#include <filesystem>

RunAction::RunAction(const SimConfig* config)
    : fTimer(new G4Timer), fConfig(config) {
    auto* accMgr = G4AccumulableManager::Instance();
    accMgr->Register(fTotPhotTotal);
    accMgr->Register(fNEntryTotal);
    accMgr->Register(fTminAll);
    accMgr->Register(fTmaxAll);
}

RunAction::~RunAction() { delete fTimer; }

void RunAction::BeginOfRunAction(const G4Run* aRun) {
    G4cout << "### Run " << aRun->GetRunID() << " start." << G4endl;
    G4AccumulableManager::Instance()->Reset();

    // Create output directory on master thread before workers start
    if (G4Threading::IsMasterThread() && fConfig &&
        !fConfig->outputDir.empty()) {
        std::filesystem::create_directories(fConfig->outputDir);
    }

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
