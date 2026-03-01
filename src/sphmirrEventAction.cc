#include "sphmirrEventAction.hh"
#include "WorkerEventData.hh"
#include "SimConfig.hh"
#include "G4Event.hh"
#include "G4AccumulableManager.hh"
#include "G4SystemOfUnits.hh"
#include <limits>
#include <cstdio>

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

    // Open output file if not already open (e.g. zero detections — still write header)
    // Output directory is created by RunAction::BeginOfRunAction on master thread
    if (!fEventData->moshits.is_open()) {
        const std::string path = fConfig->outputDir + "/moshits_" + fEventData->inputFileSuffix;
        fEventData->moshits.rdbuf()->pubsetbuf(fEventData->iobuf, sizeof(fEventData->iobuf));
        fEventData->moshits.open(path, std::ios::out);
        if (fEventData->moshits.is_open()) {
            fEventData->moshits << fEventData->headerLine << '\n';
        }
    }

    // Close the output file
    if (fEventData->moshits.is_open()) {
        fEventData->moshits.close();
    }

    // Single consolidated log line (one G4endl flush reduces MT mutex contention)
    char logbuf[512];
    if (fEventData->NEntry > 0) {
        std::snprintf(logbuf, sizeof(logbuf),
            "Event %d [%s]: TotPhot=%d NEntry=%d tmin=%.4gns tmax=%.4gns"
            " | Killed: Mir=%d Mos=%d Base=%d Lens=%d PMT=%d Hood=%d World=%d Other=%d Left=%d",
            event->GetEventID(), fEventData->inputFileSuffix.c_str(),
            fEventData->TotPhot, fEventData->NEntry,
            fEventData->tmin / ns, fEventData->tmax / ns,
            fEventData->diag_nKilledMirror, fEventData->diag_nKilledMosaic,
            fEventData->diag_nKilledBase, fEventData->diag_nKilledLens,
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
        // For min: only update if this event's tmin is smaller
        if (fEventData->tmin < accTmin->GetValue())
            *accTmin = fEventData->tmin;
    }
    if (accTmax && fEventData->NEntry > 0) {
        if (fEventData->tmax > accTmax->GetValue())
            *accTmax = fEventData->tmax;
    }
}
