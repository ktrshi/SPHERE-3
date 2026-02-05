#ifndef sphmirrActionInitialization_hh
#define sphmirrActionInitialization_hh 1

#include "G4VUserActionInitialization.hh"

class FileQueue;
struct SimConfig;
class sphmirrDetectorConstruction;

class sphmirrActionInitialization final : public G4VUserActionInitialization {
public:
    sphmirrActionInitialization(FileQueue* fileQueue,
                                 const SimConfig* config,
                                 const sphmirrDetectorConstruction* detector);
    ~sphmirrActionInitialization() override = default;

    void Build() const override;
    void BuildForMaster() const override;

private:
    FileQueue* fFileQueue;
    const SimConfig* fConfig;
    const sphmirrDetectorConstruction* fDetector;
};

#endif
