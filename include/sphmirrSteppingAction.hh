#ifndef SteppingAction_h
#define SteppingAction_h 1

#include "G4UserSteppingAction.hh"
#include "globals.hh"
#include "G4ThreeVector.hh"
#include <vector>

class sphmirrDetectorConstruction;
struct WorkerEventData;
struct SimConfig;

struct PixelNormalCache {
    G4double inv_r;
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
};

#endif
