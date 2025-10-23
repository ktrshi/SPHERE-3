#ifndef sphmirrSD_h
#define sphmirrSD_h 1
#include "G4VSensitiveDetector.hh"
#include "sphmirrHit.hh"
class G4Step;
class G4HCofThisEvent;
class [[maybe_unused]] sphmirrSD : public G4VSensitiveDetector
{
    public:
        [[maybe_unused]] explicit sphmirrSD(G4String);
        ~sphmirrSD() override;
        void Initialize(G4HCofThisEvent*) override;
        G4bool ProcessHits(G4Step*, G4TouchableHistory*) override;
        void EndOfEvent(G4HCofThisEvent*) override;
    private:
        sphmirrHitsCollection* sphmirrCollection;
};
#endif

