#include "sphmirrPhysicsListMessenger.hh"
#include "sphmirrPhysicsList.hh"
#include "G4UIdirectory.hh"
#include "G4UIcmdWithAnInteger.hh"

sphmirrPhysicsListMessenger::sphmirrPhysicsListMessenger(sphmirrPhysicsList *pPhys)
        : pPhysicsList(pPhys) {
    sphmirrDir = new G4UIdirectory("/sphmirr/");
    sphmirrDir->SetGuidance("UI commands of this example");

    physDir = new G4UIdirectory("/sphmirr/phys/");
    physDir->SetGuidance("PhysicsList control");

    verboseCmd = new G4UIcmdWithAnInteger("/sphmirr/phys/verbose", this);
    verboseCmd->SetGuidance("set verbose for physics processes");
    verboseCmd->SetParameterName("verbose", true);
    verboseCmd->SetDefaultValue(1);
    verboseCmd->SetRange("verbose>=0");
    verboseCmd->AvailableForStates(G4State_PreInit, G4State_Idle);
}
sphmirrPhysicsListMessenger::~sphmirrPhysicsListMessenger() {
    delete verboseCmd;
    delete physDir;
    delete sphmirrDir;
}
void sphmirrPhysicsListMessenger::SetNewValue(G4UIcommand *command,
                                              G4String newValue) {
    if (command == verboseCmd) { pPhysicsList->SetVerbose(G4UIcmdWithAnInteger::GetNewIntValue(newValue)); }
}