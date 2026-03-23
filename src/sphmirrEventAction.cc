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
    if (fEventData->inputFileSuffix.empty()) return;

    // Flush compressed binary output
    std::string outFile = fConfig->outputDir + "/moshits_" +
                          fEventData->inputFileSuffix + ".moshit.zst";
    try {
        fEventData->moshitWriter.Flush(outFile);
    } catch (const std::runtime_error& e) {
        G4cerr << "MoshitWriter error: " << e.what() << G4endl;
    }

    // Log summary (same format as before)
    char logbuf[512];
    if (fEventData->NEntry > 0) {
        std::snprintf(logbuf, sizeof(logbuf),
            "Event %d [%s]: TotPhot=%d NEntry=%d tmin=%.4gns tmax=%.4gns"
            " | Killed: Mir=%d Mos=%d Base=%d PMT=%d Hood=%d World=%d Other=%d Left=%d",
            event->GetEventID(), fEventData->inputFileSuffix.c_str(),
            fEventData->TotPhot, fEventData->NEntry,
            fEventData->tmin / ns, fEventData->tmax / ns,
            fEventData->diag_nKilledMirror, fEventData->diag_nKilledMosaic,
            fEventData->diag_nKilledBase,
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
        if (fEventData->tmin < accTmin->GetValue())
            *accTmin = fEventData->tmin;
    }
    if (accTmax && fEventData->NEntry > 0) {
        if (fEventData->tmax > accTmax->GetValue())
            *accTmax = fEventData->tmax;
    }

    fEventData->moshitWriter.Reset();
}
