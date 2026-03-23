#ifndef sphmirrPrimaryGeneratorAction_h
#define sphmirrPrimaryGeneratorAction_h 1

#include "G4VUserPrimaryGeneratorAction.hh"
#include "globals.hh"
#include "PhelReader.hh"

class G4ParticleGun;
class G4Event;
class FileQueue;
struct SimConfig;
struct WorkerEventData;
class sphmirrDetectorConstruction;

class sphmirrPrimaryGeneratorAction final : public G4VUserPrimaryGeneratorAction {
public:
    sphmirrPrimaryGeneratorAction(FileQueue* fileQueue,
                                   WorkerEventData* eventData,
                                   const SimConfig* config,
                                   const sphmirrDetectorConstruction* detector);
    ~sphmirrPrimaryGeneratorAction() override;
    void GeneratePrimaries(G4Event*) override;

private:
    G4ParticleGun* fParticleGun;
    FileQueue* fFileQueue;
    WorkerEventData* fEventData;
    const SimConfig* fConfig;
    const sphmirrDetectorConstruction* fDetector;

    // Cached trigonometry for detector orientation (per-worker, set once)
    G4double fCosTheta{-1}, fSinTheta{0}, fCosPhi{1}, fSinPhi{0};

    void SetOptPhotonPolar() const;
    void SetOptPhotonPolar(G4double angle) const;

    /// Build output file suffix from input filename.
    /// Strips .phel.zst, extracts from Q marker, inserts height.
    std::string BuildSuffix(const std::string& filename, const std::string& height);

    PhelEvent fCurrentEvent;
};

#endif /*sphmirrPrimaryGeneratorAction_h*/
