#include "sphmirrStackingAction.hh"
#include "G4ParticleTypes.hh"
#include "G4Track.hh"
sphmirrStackingAction::sphmirrStackingAction()
        : gammaCounter(0) {}
sphmirrStackingAction::~sphmirrStackingAction() = default;
G4ClassificationOfNewTrack
sphmirrStackingAction::ClassifyNewTrack(const G4Track *aTrack) {
    if (aTrack->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition()) { // particle is optical photon
        if (aTrack->GetParentID() > 0) { // particle is secondary
            gammaCounter++;
        }
    }
    return fUrgent;
}
void sphmirrStackingAction::NewStage() {
}
void sphmirrStackingAction::PrepareNewEvent() { gammaCounter = 0; }