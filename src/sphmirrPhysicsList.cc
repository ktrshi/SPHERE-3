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
    ConstructLeptons();
    ConstructMesons();
    ConstructBaryons();
}
void sphmirrPhysicsList::ConstructBosons() {
    G4Geantino::GeantinoDefinition();
    G4ChargedGeantino::ChargedGeantinoDefinition();
    G4Gamma::GammaDefinition();
    G4OpticalPhoton::OpticalPhotonDefinition();
}
void sphmirrPhysicsList::ConstructLeptons() {
    G4Electron::ElectronDefinition();
    G4Positron::PositronDefinition();
    G4MuonPlus::MuonPlusDefinition();
    G4MuonMinus::MuonMinusDefinition();
    G4NeutrinoE::NeutrinoEDefinition();
    G4AntiNeutrinoE::AntiNeutrinoEDefinition();
    G4NeutrinoMu::NeutrinoMuDefinition();
    G4AntiNeutrinoMu::AntiNeutrinoMuDefinition();
}
void sphmirrPhysicsList::ConstructMesons() {
    G4PionPlus::PionPlusDefinition();
    G4PionMinus::PionMinusDefinition();
    G4PionZero::PionZeroDefinition();
}
void sphmirrPhysicsList::ConstructBaryons() {
    G4Proton::ProtonDefinition();
    G4AntiProton::AntiProtonDefinition();
    G4Neutron::NeutronDefinition();
    G4AntiNeutron::AntiNeutronDefinition();
}
void sphmirrPhysicsList::ConstructProcess() {
    AddTransportation();
    ConstructOp();
}
// Kept as empty stubs (declarations remain in header for ABI safety)
void sphmirrPhysicsList::ConstructGeneral() const {}
void sphmirrPhysicsList::ConstructEM() const {}
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
