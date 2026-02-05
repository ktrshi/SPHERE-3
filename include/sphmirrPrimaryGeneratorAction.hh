#ifndef sphmirrPrimaryGeneratorAction_h
#define sphmirrPrimaryGeneratorAction_h 1

#include "G4VUserPrimaryGeneratorAction.hh"
#include "globals.hh"

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

    /// Parse input file header line, fill fEventData fields (zz, xsh, ysh, height).
    /// Returns false if header is invalid.
    bool ParseHeader(const std::string& headerLine);

    /// Build output file suffix from input filename.
    /// "phels_to_trace_Q0_atm01_..." -> "Q0_atm01_..."
    std::string BuildSuffix(const std::string& inputFilename);
};

#endif /*sphmirrPrimaryGeneratorAction_h*/
