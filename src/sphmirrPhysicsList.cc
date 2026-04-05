#include "globals.hh"
#include "sphmirrPhysicsList.hh"
#include "sphmirrPhysicsListMessenger.hh"
#include "G4ParticleDefinition.hh"
#include "G4ParticleTypes.hh"
#include "G4ProcessManager.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpAbsorption.hh"
#include "G4OpRayleigh.hh"

sphmirrPhysicsList::sphmirrPhysicsList() : G4VUserPhysicsList() {
    theBoundaryProcess = nullptr;
    pMessenger = new sphmirrPhysicsListMessenger(this);
    SetVerboseLevel(0);
}
sphmirrPhysicsList::~sphmirrPhysicsList() {
    delete pMessenger;
}
void sphmirrPhysicsList::ConstructParticle() {
    ConstructBosons();
}
void sphmirrPhysicsList::ConstructBosons() {
    G4Geantino::GeantinoDefinition();
    G4OpticalPhoton::OpticalPhotonDefinition();
}
void sphmirrPhysicsList::ConstructProcess() {
    AddTransportation();
    ConstructOp();
}
void sphmirrPhysicsList::ConstructOp() {
    theBoundaryProcess = new G4OpBoundaryProcess();
    SetVerbose(0);
    const auto particleIterator = GetParticleIterator();
    particleIterator->reset();
    while ((*particleIterator)()) {
        const G4ParticleDefinition *particle = particleIterator->value();
        G4ProcessManager *pmanager = particle->GetProcessManager();
        if (G4String particleName = particle->GetParticleName(); particleName == "opticalphoton") {
            G4cout << " AddDiscreteProcess to OpticalPhoton " << G4endl;
            pmanager->AddDiscreteProcess(theBoundaryProcess);
            pmanager->AddDiscreteProcess(new G4OpAbsorption());
            pmanager->AddDiscreteProcess(new G4OpRayleigh());
        }
    }
}
void sphmirrPhysicsList::SetVerbose(const G4int verbose) const {
    theBoundaryProcess->SetVerboseLevel(verbose);
}
void sphmirrPhysicsList::SetCuts() {
    SetCutsWithDefault();
    if (verboseLevel > 0) DumpCutValuesTable();
}
