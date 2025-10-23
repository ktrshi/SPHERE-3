#ifndef SteppingAction_h
#define SteppingAction_h 1
#include "sphmirrDetectorConstruction.hh"
#include "sphmirrEventAction.hh"
#include "G4UserSteppingAction.hh"
#include "globals.hh"
#include <unordered_map>
#include "G4ThreeVector.hh"

class sphmirrDetectorConstruction_alt;
class EventAction;
class G4OpBoundaryProcess;

// Precomputed normalized position vector for each PMT pixel
struct PixelNormalCache {
    G4double inv_r;  // 1/sqrt(x^2 + y^2 + z^2)
    G4double u, v, w; // normalized vector components
};

class sphmirrSteppingAction final: public G4UserSteppingAction
{
    public:
        sphmirrSteppingAction(sphmirrDetectorConstruction*, EventAction*);
        ~sphmirrSteppingAction() override;
        void UserSteppingAction(const G4Step*) override;
        void InitializePixelCache(); // Initialize precomputed values
    private:
        // The starting position of the photon
        [[maybe_unused]] G4double initZ{};
        // initial gamma of the photon
        [[maybe_unused]] G4double initGamma{};
        // initial theta of the photon
        [[maybe_unused]] G4double initTheta{};
        [[maybe_unused]] G4OpBoundaryProcess* opProcess{};
        [[maybe_unused]] sphmirrDetectorConstruction* detector;
        [[maybe_unused]] EventAction* eventaction;

        // Cache for precomputed normalized vectors for each pixel
        std::unordered_map<G4int, PixelNormalCache> pixelCache;
};
#endif