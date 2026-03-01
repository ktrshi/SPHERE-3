#include "sphmirrStackingAction.hh"
#include "G4Track.hh"
#include "G4OpticalPhoton.hh"

sphmirrStackingAction::sphmirrStackingAction() = default;
sphmirrStackingAction::~sphmirrStackingAction() = default;

G4ClassificationOfNewTrack
sphmirrStackingAction::ClassifyNewTrack(const G4Track* aTrack) {
    // Kill all non-optical secondaries immediately — only opticalphoton
    // is relevant for this simulation.  Prevents wasted CPU on any
    // accidental EM secondaries.
    if (aTrack->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
        return fKill;
    return fUrgent;
}

void sphmirrStackingAction::NewStage() {}
void sphmirrStackingAction::PrepareNewEvent() {}
