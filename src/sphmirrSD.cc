#include <utility>
#include "sphmirrSD.hh"
#include "G4HCofThisEvent.hh"
#include "G4Step.hh"
#include "G4SDManager.hh"
#include "G4ios.hh"
[[maybe_unused]] sphmirrSD::sphmirrSD(G4String name)
        : G4VSensitiveDetector(std::move(name)), sphmirrCollection(nullptr) {
    G4String HCname;
    collectionName.insert(HCname = "mosaicCollection");
}
sphmirrSD::~sphmirrSD() = default;
void sphmirrSD::Initialize(G4HCofThisEvent *HCE) {
    sphmirrCollection = new sphmirrHitsCollection
            (SensitiveDetectorName, collectionName[0]);
    static G4int HCID = -1;
    if (HCID < 0) { HCID = G4SDManager::GetSDMpointer()->GetCollectionID(collectionName[0]); }
    HCE->AddHitsCollection(HCID, sphmirrCollection);
}
G4bool sphmirrSD::ProcessHits(G4Step *aStep, G4TouchableHistory *) {
    auto *newHit = new sphmirrHit();
    newHit->SetPos(aStep->GetPostStepPoint()->GetPosition());
    sphmirrCollection->insert(newHit);
    G4cout << "\n-------->  newHit\n";
    return true;
}
void sphmirrSD::EndOfEvent(G4HCofThisEvent *) {
    if (verboseLevel > 0) {
        const G4int NbHits = sphmirrCollection->entries();
        G4cout << "\n-------->Hits Collection: in this event they are " << NbHits
               << " hits in Mosaic: " << G4endl;
        for (G4int i = 0; i < NbHits; i++) (*sphmirrCollection)[i]->Print();
    }
}