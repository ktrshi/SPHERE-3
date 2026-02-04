#include "globals.hh"
#include "sphmirrPhysicsList.hh"
#include "sphmirrPhysicsListMessenger.hh"
#include "G4ParticleDefinition.hh"
#include "G4ParticleTypes.hh"
#include "G4ProcessManager.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpAbsorption.hh"
#include "G4OpRayleigh.hh"
#include "G4Decay.hh"
#include "G4ComptonScattering.hh"
#include "G4GammaConversion.hh"
#include "G4PhotoElectricEffect.hh"
#include "G4eMultipleScattering.hh"
#include "G4MuMultipleScattering.hh"
#include "G4hMultipleScattering.hh"
#include "G4eIonisation.hh"
#include "G4eBremsstrahlung.hh"
#include "G4eplusAnnihilation.hh"
#include "G4MuIonisation.hh"
#include "G4MuBremsstrahlung.hh"
#include "G4MuPairProduction.hh"
#include "G4hIonisation.hh"

sphmirrPhysicsList::sphmirrPhysicsList() : G4VUserPhysicsList() {
    theBoundaryProcess = nullptr;
    theDecayProcess = new G4Decay();
    pMessenger = new sphmirrPhysicsListMessenger(this);
    SetVerboseLevel(0);
}
sphmirrPhysicsList::~sphmirrPhysicsList() {
    delete pMessenger;
    delete theDecayProcess;
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
    ConstructGeneral();
    ConstructEM();
    ConstructOp();
}
void sphmirrPhysicsList::ConstructGeneral() const {
    const auto particleIterator = GetParticleIterator();
    particleIterator->reset();
    while ((*particleIterator)()) {
        const G4ParticleDefinition *particle = particleIterator->value();
        G4ProcessManager *pmanager = particle->GetProcessManager();
        if (theDecayProcess->IsApplicable(*particle)) {
            pmanager->AddProcess(theDecayProcess);
            pmanager->SetProcessOrdering(theDecayProcess, idxPostStep);
            pmanager->SetProcessOrdering(theDecayProcess, idxAtRest);
        }
    }
}
void sphmirrPhysicsList::ConstructEM() const {
    const auto particleIterator = GetParticleIterator();
    particleIterator->reset();
    while ((*particleIterator)()) {
        const G4ParticleDefinition *particle = particleIterator->value();
        G4ProcessManager *pmanager = particle->GetProcessManager();
        if (G4String particleName = particle->GetParticleName(); particleName == "gamma") {
            pmanager->AddDiscreteProcess(new G4GammaConversion());
            pmanager->AddDiscreteProcess(new G4ComptonScattering());
            pmanager->AddDiscreteProcess(new G4PhotoElectricEffect());
        } else if (particleName == "e-") {
            pmanager->AddProcess(new G4eMultipleScattering(), -1, 1, 1);
            pmanager->AddProcess(new G4eIonisation(), -1, 2, 2);
            pmanager->AddProcess(new G4eBremsstrahlung(), -1, 3, 3);

        } else if (particleName == "e+") {
            pmanager->AddProcess(new G4eMultipleScattering(), -1, 1, 1);
            pmanager->AddProcess(new G4eIonisation(), -1, 2, 2);
            pmanager->AddProcess(new G4eBremsstrahlung(), -1, 3, 3);
            pmanager->AddProcess(new G4eplusAnnihilation(), 0, -1, 4);
        } else if (particleName == "mu+" ||
                   particleName == "mu-") {
            pmanager->AddProcess(new G4MuMultipleScattering(), -1, 1, 1);
            pmanager->AddProcess(new G4MuIonisation(), -1, 2, 2);
            pmanager->AddProcess(new G4MuBremsstrahlung(), -1, 3, 3);
            pmanager->AddProcess(new G4MuPairProduction(), -1, 4, 4);
        } else {
            if ((particle->GetPDGCharge() != 0.0) &&
                (particle->GetParticleName() != "chargedgeantino")) {
                pmanager->AddProcess(new G4hMultipleScattering(), -1, 1, 1);
                pmanager->AddProcess(new G4hIonisation(), -1, 2, 2);
            }
        }
    }
}
void sphmirrPhysicsList::ConstructOp() {
    theBoundaryProcess = new G4OpBoundaryProcess();
    SetVerbose(1);
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
