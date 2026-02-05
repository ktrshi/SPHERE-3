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
    // NOTE: per-event data reset is done at the START of GeneratePrimaries,
    // because Geant4 calls GeneratePrimaries BEFORE BeginOfEventAction.
    // (G4RunManager::GenerateEvent → GeneratePrimaries, then
    //  G4EventManager::DoProcessing → BeginOfEventAction → tracking → EndOfEventAction)
}

void EventAction::EndOfEventAction(const G4Event* event) {
    // Skip empty events (no file was assigned)
    if (fEventData->inputFileSuffix.empty()) return;

    // Open output file if not already open
    // (GeneratePrimaries set the suffix; SteppingAction may or may not have opened it)
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

    // Close the output file
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

    // Diagnostic: photon fate breakdown
    G4cout << "  [DIAG] Killed in:"
           << " Mirror=" << fEventData->diag_nKilledMirror
           << " Mosaic=" << fEventData->diag_nKilledMosaic
           << " Base=" << fEventData->diag_nKilledBase
           << " Lens=" << fEventData->diag_nKilledLens
           << " PMT=" << fEventData->diag_nKilledPMT
           << " Hood=" << fEventData->diag_nKilledHood
           << " World=" << fEventData->diag_nKilledWorld
           << " Other=" << fEventData->diag_nKilledOther
           << " LeftWorld=" << fEventData->diag_nLeftWorld
           << G4endl;

    // Accumulate into G4Accumulable (will be merged in EndOfRunAction)
    auto* accMgr = G4AccumulableManager::Instance();
    auto* accTotPhot = accMgr->GetAccValue<G4int>("TotPhotTotal");
    auto* accNEntry  = accMgr->GetAccValue<G4int>("NEntryTotal");
    auto* accTmin    = accMgr->GetAccValue<G4double>("TminAll");
    auto* accTmax    = accMgr->GetAccValue<G4double>("TmaxAll");
    if (accTotPhot) *accTotPhot += fEventData->TotPhot;
    if (accNEntry)  *accNEntry  += fEventData->NEntry;
    if (accTmin && fEventData->NEntry > 0) {
        // For min: only update if this event's tmin is smaller
        if (fEventData->tmin < accTmin->GetValue())
            *accTmin = fEventData->tmin;
    }
    if (accTmax && fEventData->NEntry > 0) {
        if (fEventData->tmax > accTmax->GetValue())
            *accTmax = fEventData->tmax;
    }
}
