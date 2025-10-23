#ifndef sphmirrPrimaryGeneratorMessenger_h
#define sphmirrPrimaryGeneratorMessenger_h 1
#include "G4UImessenger.hh"
#include "globals.hh"
class sphmirrPrimaryGeneratorAction;
class G4UIdirectory;
class G4UIcmdWithADoubleAndUnit;
class G4UIcmdWithAnInteger;
class sphmirrPrimaryGeneratorMessenger final : public G4UImessenger
{
    public:
        explicit sphmirrPrimaryGeneratorMessenger(sphmirrPrimaryGeneratorAction*);
        ~sphmirrPrimaryGeneratorMessenger() override;
        void SetNewValue(G4UIcommand*, G4String) override;
    private:
        sphmirrPrimaryGeneratorAction* sphmirrAction;
        G4UIdirectory* gunDir;
        G4UIcmdWithADoubleAndUnit* polarCmd;
        G4UIcmdWithADoubleAndUnit* dirThetaCmd;
        G4UIcmdWithADoubleAndUnit* dirPhiCmd;
        G4UIcmdWithAnInteger* dirGrpCmd;
        G4UIcmdWithADoubleAndUnit* dirRadCmd;
        G4UIcmdWithADoubleAndUnit* dirX0Cmd;
        G4UIcmdWithADoubleAndUnit* dirY0Cmd;
};
#endif

