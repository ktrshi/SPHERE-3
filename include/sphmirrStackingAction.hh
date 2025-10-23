#ifndef sphmirrStackingAction_H
#define sphmirrStackingAction_H 1
#include "globals.hh"
#include "G4UserStackingAction.hh"
class sphmirrStackingAction final: public G4UserStackingAction
{
    public:
        sphmirrStackingAction();
        ~sphmirrStackingAction() override;
        G4ClassificationOfNewTrack ClassifyNewTrack(const G4Track* aTrack) override;
        void NewStage() override;
        void PrepareNewEvent() override;
    private:
        G4int gammaCounter;
};
#endif