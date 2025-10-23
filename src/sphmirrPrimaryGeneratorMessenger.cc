#include "sphmirrPrimaryGeneratorMessenger.hh"
#include "sphmirrPrimaryGeneratorAction.hh"
#include "G4UIdirectory.hh"
#include "G4UIcmdWithADoubleAndUnit.hh"
#include "G4UIcmdWithAnInteger.hh"
#include "G4SystemOfUnits.hh"
sphmirrPrimaryGeneratorMessenger::sphmirrPrimaryGeneratorMessenger(
        sphmirrPrimaryGeneratorAction *sphmirrGun)
        : sphmirrAction(sphmirrGun) {
    gunDir = new G4UIdirectory("/sphmirr/gun/");
    gunDir->SetGuidance("PrimaryGenerator control");
    polarCmd = new G4UIcmdWithADoubleAndUnit("/sphmirr/gun/optPhotonPolar", this);
    polarCmd->SetGuidance("Set linear polarization");
    polarCmd->SetGuidance("  angle w.r.t. (k,n) plane");
    polarCmd->SetParameterName("angle", true);
    polarCmd->SetUnitCategory("Angle");
    polarCmd->SetDefaultValue(-360.0);
    polarCmd->SetDefaultUnit("deg");
    polarCmd->AvailableForStates(G4State_Idle);
    dirThetaCmd = new G4UIcmdWithADoubleAndUnit("/sphmirr/gun/optPhotonTheta", this);
    dirThetaCmd->SetGuidance("Set photon direction polar angle");
    dirThetaCmd->SetParameterName("dirTheta", true);
    dirThetaCmd->SetUnitCategory("Angle");
    dirThetaCmd->SetDefaultValue(0.0);
    dirThetaCmd->SetDefaultUnit("deg");
    dirThetaCmd->AvailableForStates(G4State_Idle);
    dirPhiCmd = new G4UIcmdWithADoubleAndUnit("/sphmirr/gun/optPhotonPhi", this);
    dirPhiCmd->SetGuidance("Set photon direction azimuthal angle");
    dirPhiCmd->SetParameterName("dirPhi", true);
    dirPhiCmd->SetUnitCategory("Angle");
    dirPhiCmd->SetDefaultValue(0.0);
    dirPhiCmd->SetDefaultUnit("deg");
    dirPhiCmd->AvailableForStates(G4State_Idle);
    dirGrpCmd = new G4UIcmdWithAnInteger("/sphmirr/gun/optPhotonGrp", this);
    dirGrpCmd->SetGuidance("Set photon direction group, increase the group number");
    dirGrpCmd->SetGuidance("  by 1 for every consecutive direction angle set");
    dirGrpCmd->SetParameterName("dirGrp", true);
    dirGrpCmd->SetDefaultValue(0);
    dirGrpCmd->SetRange("dirGrp >= 0");
    dirGrpCmd->AvailableForStates(G4State_Idle);
    dirRadCmd = new G4UIcmdWithADoubleAndUnit("/sphmirr/gun/optPhotonRad", this);
    dirRadCmd->SetGuidance("Set photon beam transverse radius");
    dirRadCmd->SetParameterName("dirRad", true);
    dirRadCmd->SetUnitCategory("Length");
    dirRadCmd->SetDefaultValue(0.0);
    dirRadCmd->SetRange("dirRad >= 0.0");
    dirRadCmd->SetDefaultUnit("cm");
    dirRadCmd->AvailableForStates(G4State_Idle);
    dirX0Cmd = new G4UIcmdWithADoubleAndUnit("/sphmirr/gun/optPhotonX0", this);
    dirX0Cmd->SetGuidance("Set photon beam center X0");
    dirX0Cmd->SetParameterName("dirX0", true);
    dirX0Cmd->SetUnitCategory("Length");
    dirX0Cmd->SetDefaultValue(0.0);
    dirX0Cmd->SetDefaultUnit("cm");
    dirX0Cmd->AvailableForStates(G4State_Idle);
    dirY0Cmd = new G4UIcmdWithADoubleAndUnit("/sphmirr/gun/optPhotonY0", this);
    dirY0Cmd->SetGuidance("Set photon beam center Y0");
    dirY0Cmd->SetParameterName("dirY0", true);
    dirY0Cmd->SetUnitCategory("Length");
    dirY0Cmd->SetDefaultValue(0.0);
    dirY0Cmd->SetDefaultUnit("cm");
    dirY0Cmd->AvailableForStates(G4State_Idle);
}
sphmirrPrimaryGeneratorMessenger::~sphmirrPrimaryGeneratorMessenger() {
    delete polarCmd;
    delete dirThetaCmd;
    delete dirPhiCmd;
    delete dirGrpCmd;
    delete dirRadCmd;
    delete dirX0Cmd;
    delete dirY0Cmd;
    delete gunDir;
}
void sphmirrPrimaryGeneratorMessenger::SetNewValue(
        G4UIcommand *command, G4String newValue) {
    if (command == polarCmd) {
        if (const G4double angle = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(newValue); angle == -360.0 * deg) {
            sphmirrAction->SetOptPhotonPolar();
        } else {
            sphmirrAction->SetOptPhotonPolar(angle);
        }
    }
    if (command == dirThetaCmd) {
        if (const G4double angle = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(newValue);
            (angle >= 0.0 * deg) && (angle <= 30.0 * deg))
            sphmirrAction->SetOptPhotonDirTheta(angle);
        else
            sphmirrAction->SetOptPhotonDirTheta(0.0);
    }
    if (command == dirPhiCmd) {
        if (const G4double angle = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(newValue);
            (angle >= 0.0 * deg) && (angle <= 360.0 * deg))
            sphmirrAction->SetOptPhotonDirPhi(angle);
        else
            sphmirrAction->SetOptPhotonDirPhi(0.0);
    }
    if (command == dirGrpCmd) {
        if (const G4int grp = G4UIcmdWithAnInteger::GetNewIntValue(newValue); (grp > 0) && (grp <= 10))
            sphmirrAction->SetOptPhotonDirGrp(grp);
        else
            sphmirrAction->SetOptPhotonDirGrp(0);
    }
    if (command == dirRadCmd) {
        if (const G4double radius = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(newValue);
            (radius >= 0.0 * cm) && (radius <= 300.0 * cm))
            sphmirrAction->SetOptPhotonBeamRad(radius);
        else
            sphmirrAction->SetOptPhotonBeamRad(0.0 * cm);
    }
    if (command == dirX0Cmd) {
        if (const G4double X0 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(newValue);
            (X0 >= -300.0 * cm) && (X0 <= 300.0 * cm))
            sphmirrAction->SetOptPhotonBeamX0(X0);
        else
            sphmirrAction->SetOptPhotonBeamX0(0.0 * cm);
    }
    if (command == dirY0Cmd) {
        if (const G4double Y0 = G4UIcmdWithADoubleAndUnit::GetNewDoubleValue(newValue);
            (Y0 >= -300.0 * cm) && (Y0 <= 300.0 * cm))
            sphmirrAction->SetOptPhotonBeamY0(Y0);
        else
            sphmirrAction->SetOptPhotonBeamY0(0.0 * cm);
    }
}