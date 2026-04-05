#ifndef SteppingAction_h
#define SteppingAction_h 1

#include "G4UserSteppingAction.hh"
#include "globals.hh"
#include "G4ThreeVector.hh"
#include <vector>

class G4ParticleDefinition;
class G4LogicalVolume;
class sphmirrDetectorConstruction;
struct WorkerEventData;
struct SimConfig;

struct PixelNormalCache {
    G4double u, v, w;
};

class sphmirrSteppingAction final : public G4UserSteppingAction {
public:
    sphmirrSteppingAction(WorkerEventData* eventData,
                           const SimConfig* config,
                           const sphmirrDetectorConstruction* detector);
    ~sphmirrSteppingAction() override;
    void UserSteppingAction(const G4Step*) override;

private:
    WorkerEventData* fEventData;
    const SimConfig* fConfig;
    const sphmirrDetectorConstruction* fDetector;

    std::vector<PixelNormalCache> pixelCache;
    void InitializePixelCache();

    // Cached pointers for O(1) comparisons (set once per worker)
    G4ParticleDefinition* fOpticalPhoton{nullptr};
    const G4LogicalVolume* fPmtLog{nullptr};
    const G4LogicalVolume* fMirrorLog{nullptr};
    const G4LogicalVolume* fMosaicLog{nullptr};
    const G4LogicalVolume* fCollectorLog{nullptr};
    const G4LogicalVolume* fHoodLog{nullptr};
    const G4LogicalVolume* fHoodNLog{nullptr};
    const G4LogicalVolume* fWorldLog{nullptr};
    void InitializeVolumeCache();
};

#endif
